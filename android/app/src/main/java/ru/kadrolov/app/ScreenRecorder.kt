package ru.kadrolov.app

import android.annotation.SuppressLint
import android.content.Context
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.*
import android.media.projection.MediaProjection
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Surface
import java.io.File
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/** H264 surface video and real playback/microphone PCM -> AAC in one local MP4. */
class ScreenRecorder(
  private val context: Context,
  private val projection: MediaProjection,
  private val options: CaptureOptions,
  private val width: Int,
  private val height: Int,
  private val density: Int,
  private val sourceWidth: Int = width,
  private val sourceHeight: Int = height,
  private val onError: (String) -> Unit,
  private val onFinished: (File) -> Unit,
  private val onWarning: (String) -> Unit = {},
) {
  private val videoMime =
    if (options.videoFormat == "webm") MediaFormat.MIMETYPE_VIDEO_VP8
    else MediaFormat.MIMETYPE_VIDEO_AVC
  private val audioMime =
    if (options.videoFormat == "webm") MediaFormat.MIMETYPE_AUDIO_OPUS
    else MediaFormat.MIMETYPE_AUDIO_AAC
  private val video = MediaCodec.createEncoderByType(videoMime)
  private var compositor: VideoCompositor? = null
  private var captureSurface: Surface? = null
  private var audio: MediaCodec? = null
  private var surface: Surface? = null
  private var display: VirtualDisplay? = null
  private var muxer: MediaMuxer? = null
  private val running = AtomicBoolean(false)
  private val audioQueue = ConcurrentLinkedQueue<ByteArray>()
  private val recorders = mutableListOf<AudioRecord>()
  private val audioManager = context.getSystemService(AudioManager::class.java)
  private var selectedMicrophone: AudioRecord? = null
  private var watchingDevice = false
  @Volatile private var finishWarning: String? = null
  private val deviceHandler = Handler(Looper.getMainLooper())
  private val deviceCallback =
    object : AudioDeviceCallback() {
      override fun onAudioDevicesRemoved(devices: Array<out AudioDeviceInfo>) {
        if (options.microphoneDeviceId > 0 && devices.any { it.id == options.microphoneDeviceId }) {
          finishWarning =
            "Выбранный микрофон отключён. Запись остановлена без переключения на другое устройство"
          stop()
        }
      }
    }
  private val routingListener =
    AudioRouting.OnRoutingChangedListener { routing ->
      val routed = routing.routedDevice
      if (
        running.get() &&
          options.microphoneDeviceId > 0 &&
          routed != null &&
          routed.id != options.microphoneDeviceId
      ) {
        finishWarning =
          "Android изменил маршрут выбранного микрофона. Запись остановлена без подмены устройства"
        stop()
      }
    }
  private var audioThread: Thread? = null
  private var worker: Thread? = null
  private val timeline = RecordingTimeline()
  @Volatile private var paused = false
  @Volatile private var totalPauseUs = 0L
  private var pauseStarted = 0L
  @Volatile private var waitForKeyframe = false
  @Volatile private var stopping = false
  @Volatile private var failureReason: String? = null
  @Volatile private var stopTimestampNanos = 0L
  private val temp =
    File.createTempFile(
      "kadrolov-record-",
      if (options.videoFormat == "webm") ".webm" else ".mp4",
      context.cacheDir,
    )

  fun elapsedMs() = timeline.elapsedUs(SystemClock.elapsedRealtimeNanos()) / 1000

  @Synchronized
  fun pause() {
    if (!paused && running.get()) {
      paused = true
      pauseStarted = SystemClock.elapsedRealtimeNanos()
      timeline.pause(pauseStarted)
      compositor?.suspendFrames()
      display?.surface = null
      audioQueue.clear()
    }
  }

  @Synchronized
  fun resume() {
    if (paused && running.get()) {
      val now = SystemClock.elapsedRealtimeNanos()
      totalPauseUs += (now - pauseStarted) / 1000
      timeline.resume(now)
      waitForKeyframe = true
      video.setParameters(Bundle().apply { putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0) })
      paused = false
      compositor?.resumeFrames()
      display?.surface = captureSurface
    }
  }

  @Synchronized
  fun stop() {
    if (stopping) return
    stopping = true
    stopTimestampNanos = SystemClock.elapsedRealtimeNanos()
    running.set(false)
    compositor?.suspendFrames()
    runCatching { display?.surface = null }
    recorders.forEach { runCatching { it.stop() } }
  }

  @SuppressLint("MissingPermission")
  fun start() {
    try {
      require(!options.camera || (width >= 128 && height >= 128)) {
        "Для камеры выберите область не меньше 128 × 128"
      }
      require(
        options.cropX >= 0f &&
          options.cropY >= 0f &&
          options.cropWidth > 0f &&
          options.cropHeight > 0f &&
          options.cropX + options.cropWidth <= 1.001f &&
          options.cropY + options.cropHeight <= 1.001f
      ) {
        "Область видео недействительна. Выберите её заново"
      }
      val format =
        MediaFormat.createVideoFormat(videoMime, width, height).apply {
          setInteger(
            MediaFormat.KEY_COLOR_FORMAT,
            MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface,
          )
          setInteger(MediaFormat.KEY_BIT_RATE, options.bitrate.coerceIn(1_000_000, 40_000_000))
          setInteger(MediaFormat.KEY_FRAME_RATE, options.fps.coerceIn(15, 60))
          setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
        }
      video.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
      surface = video.createInputSurface()
      video.start()
      if (options.audio != "none") {
        val af =
          MediaFormat.createAudioFormat(audioMime, 48000, 1).apply {
            if (audioMime == MediaFormat.MIMETYPE_AUDIO_AAC)
              setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
            setInteger(MediaFormat.KEY_BIT_RATE, 128000)
            setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384)
          }
        audio =
          MediaCodec.createEncoderByType(audioMime).apply {
            configure(af, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            start()
          }
        val audioFormat =
          AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(48000)
            .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
            .build()
        val size =
          maxOf(
            16384,
            AudioRecord.getMinBufferSize(
              48000,
              AudioFormat.CHANNEL_IN_MONO,
              AudioFormat.ENCODING_PCM_16BIT,
            ) * 2,
          )
        if (options.audio == "system" || options.audio == "both") {
          val config =
            AudioPlaybackCaptureConfiguration.Builder(projection)
              .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
              .addMatchingUsage(AudioAttributes.USAGE_GAME)
              .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
              .build()
          recorders +=
            AudioRecord.Builder()
              .setAudioFormat(audioFormat)
              .setBufferSizeInBytes(size)
              .setAudioPlaybackCaptureConfig(config)
              .build()
        }
        if (options.audio == "mic" || options.audio == "both") {
          val preferred = MicrophoneDevices.selected(context, options.microphoneDeviceId)
          val microphone =
            AudioRecord.Builder()
              .setAudioFormat(audioFormat)
              .setBufferSizeInBytes(size)
              .setAudioSource(MediaRecorder.AudioSource.MIC)
              .build()
          recorders += microphone
          selectedMicrophone = microphone
          if (preferred != null) {
            check(microphone.setPreferredDevice(preferred)) {
              "Android не разрешил использовать выбранный микрофон. Выберите другое устройство"
            }
            microphone.addOnRoutingChangedListener(routingListener, deviceHandler)
            audioManager.registerAudioDeviceCallback(deviceCallback, deviceHandler)
            watchingDevice = true
            MicrophoneDevices.selected(context, options.microphoneDeviceId)
          }
        }
        check(recorders.all { it.state == AudioRecord.STATE_INITIALIZED }) {
          "Звуковое устройство недоступно"
        }
      }
      muxer =
        MediaMuxer(
          temp.absolutePath,
          if (options.videoFormat == "webm") MediaMuxer.OutputFormat.MUXER_OUTPUT_WEBM
          else MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4,
        )
      timeline.start(SystemClock.elapsedRealtimeNanos())
      running.set(true)
      compositor =
        VideoCompositor(context, surface!!, sourceWidth, sourceHeight, width, height, options) { m
            ->
            failureReason = m
            stop()
          }
          .also { it.start() }
      captureSurface = compositor!!.inputSurface
      display =
        projection.createVirtualDisplay(
          "Кадролов",
          sourceWidth,
          sourceHeight,
          density,
          DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
          captureSurface,
          null,
          null,
        )
      if (audio != null) {
        recorders.forEach { it.startRecording() }
        audioThread = thread(name = "Kadrolov-audio") { captureAudio() }
      }
      worker = thread(name = "Kadrolov-mux") { encodeLoop() }
    } catch (e: Exception) {
      release()
      temp.delete()
      throw e
    }
  }

  private fun captureAudio() {
    val buffers = Array(recorders.size) { ShortArray(1024) }
    try {
      while (running.get()) {
        val counts =
          recorders.mapIndexed { idx, r ->
            r.read(buffers[idx], 0, 1024, AudioRecord.READ_BLOCKING)
          }
        if (!running.get()) break
        if (counts.any { it < 0 }) {
          val microphoneIndex = selectedMicrophone?.let { recorders.indexOf(it) } ?: -1
          if (
            options.microphoneDeviceId > 0 && microphoneIndex >= 0 && counts[microphoneIndex] < 0
          ) {
            finishWarning =
              "Выбранный микрофон перестал передавать звук. Запись остановлена без подмены устройства"
            stop()
            break
          }
          error("Аудиоустройство отключено")
        }
        val n = counts.minOrNull() ?: 0
        if (paused || n == 0) continue
        if (options.microphoneDeviceId > 0 && selectedMicrophone != null) {
          val selected = selectedMicrophone!!.routedDevice
          if (selected == null || selected.id != options.microphoneDeviceId) {
            finishWarning =
              "Android не подтвердил маршрут выбранного микрофона. Запись остановлена; выберите доступный микрофон"
            stop()
            break
          }
        }
        val bytes = ByteArray(n * 2)
        for (i in 0 until n) {
          val sample =
            (if (buffers.size == 2) (buffers[0][i].toInt() + buffers[1][i].toInt()) / 2
              else buffers[0][i].toInt())
              .coerceIn(-32768, 32767)
          bytes[i * 2] = sample.toByte()
          bytes[i * 2 + 1] = (sample shr 8).toByte()
        }
        if (audioQueue.size > 64) error("Кодировщик звука не успевает. Уменьшите качество видео")
        audioQueue.add(bytes)
      }
    } catch (e: Exception) {
      if (running.get()) {
        failureReason = e.message ?: "Ошибка звука"
        stop()
      }
    }
  }

  private data class Packet(
    val video: Boolean,
    val bytes: ByteArray,
    val pts: Long,
    val flags: Int,
  )

  private fun encodeLoop() {
    var vt = -1
    var at = -1
    var muxStarted = false
    var videoBase = -1L
    var lastVideo = -1L
    var audioFrames = 0L
    var audioEos = false
    var audioInputEnded = false
    var videoEos = false
    var sentEnd = false
    var stopAt = 0L
    val packets = ArrayList<Packet>()
    fun write(p: Packet) {
      val b = ByteBuffer.wrap(p.bytes)
      val info = MediaCodec.BufferInfo()
      info.set(0, p.bytes.size, p.pts, p.flags)
      muxer!!.writeSampleData(if (p.video) vt else at, b, info)
    }
    try {
      while (!videoEos || (!audioEos && audio != null)) {
        if (!running.get() && !sentEnd) {
          audioThread?.join(1500)
          if (!paused) compositor?.finishFrame(stopTimestampNanos)
          compositor?.awaitIdle()
          video.signalEndOfInputStream()
          sentEnd = true
          stopAt = SystemClock.elapsedRealtime()
        }
        audio?.let { codec ->
          val pcm = audioQueue.peek()
          if (pcm != null || sentEnd && !audioInputEnded) {
            val idx = codec.dequeueInputBuffer(0)
            if (idx >= 0) {
              val b = codec.getInputBuffer(idx)!!
              if (pcm != null) {
                audioQueue.poll()
                b.clear()
                b.put(pcm)
                codec.queueInputBuffer(idx, 0, pcm.size, audioFrames * 1_000_000 / 48000, 0)
                audioFrames += pcm.size / 2
              } else {
                codec.queueInputBuffer(
                  idx,
                  0,
                  0,
                  audioFrames * 1_000_000 / 48000,
                  MediaCodec.BUFFER_FLAG_END_OF_STREAM,
                )
                audioInputEnded = true
              }
            }
          }
        }
        fun drain(codec: MediaCodec, isVideo: Boolean) {
          val info = MediaCodec.BufferInfo()
          while (true) {
            val idx = codec.dequeueOutputBuffer(info, 0)
            if (idx == MediaCodec.INFO_TRY_AGAIN_LATER) break
            if (idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
              if (isVideo) vt = muxer!!.addTrack(codec.outputFormat)
              else at = muxer!!.addTrack(codec.outputFormat)
              if (!muxStarted && vt >= 0 && (audio == null || at >= 0)) {
                muxer!!.start()
                muxStarted = true
                packets.forEach { write(it) }
                packets.clear()
              }
              continue
            }
            if (idx < 0) continue
            val eos = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
            val config = info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0
            val key = info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME != 0
            val keep = (!isVideo || !paused && (!waitForKeyframe || key))
            if (info.size > 0 && !config && keep) {
              if (isVideo && key) waitForKeyframe = false
              val raw = codec.getOutputBuffer(idx)!!
              raw.position(info.offset)
              raw.limit(info.offset + info.size)
              val bytes = ByteArray(info.size)
              raw.get(bytes)
              var pts = info.presentationTimeUs
              if (isVideo) {
                if (videoBase < 0) videoBase = pts
                pts = maxOf(lastVideo + 1, pts - videoBase - totalPauseUs)
                lastVideo = pts
              }
              val packet =
                Packet(
                  isVideo,
                  bytes,
                  pts,
                  info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM.inv(),
                )
              if (muxStarted) write(packet)
              else {
                check(packets.sumOf { it.bytes.size } < 32 * 1024 * 1024) {
                  "Кодировщик не сообщил формат"
                }
                packets += packet
              }
            }
            codec.releaseOutputBuffer(idx, false)
            if (eos) {
              if (isVideo) videoEos = true else audioEos = true
              break
            }
          }
        }
        drain(video, true)
        audio?.let { drain(it, false) }
        if (sentEnd && SystemClock.elapsedRealtime() - stopAt > 5000) break
        if (context.cacheDir.usableSpace < 16L * 1024 * 1024 && running.get()) {
          failureReason =
            "Запись остановлена: мало свободного места. Доступная часть сохранена во временных записях"
          stop()
        }
        Thread.sleep(4)
      }
      check(muxStarted && lastVideo >= 0) {
        "Не получены кадры экрана. Проверьте разрешение захвата"
      }
      check(videoEos && (audio == null || audioEos)) { "Кодировщик не завершил запись вовремя" }
      muxer?.stop()
      muxer?.release()
      muxer = null
      release()
      if (failureReason != null) onError(failureReason!!)
      else {
        finishWarning?.let { onWarning(it) }
        onFinished(temp)
      }
    } catch (e: Exception) {
      release()
      if (temp.length() == 0L) temp.delete()
      onError(e.message ?: "Ошибка кодирования")
    }
  }

  private fun release() {
    running.set(false)
    if (watchingDevice) {
      runCatching { audioManager.unregisterAudioDeviceCallback(deviceCallback) }
      watchingDevice = false
    }
    selectedMicrophone?.let { runCatching { it.removeOnRoutingChangedListener(routingListener) } }
    selectedMicrophone = null
    display?.release()
    display = null
    compositor?.close()
    compositor = null
    captureSurface = null
    recorders.forEach {
      runCatching { it.stop() }
      runCatching { it.release() }
    }
    recorders.clear()
    runCatching { video.stop() }
    runCatching { video.release() }
    runCatching { audio?.stop() }
    runCatching { audio?.release() }
    audio = null
    surface?.release()
    surface = null
    runCatching { muxer?.release() }
    muxer = null
  }
}
