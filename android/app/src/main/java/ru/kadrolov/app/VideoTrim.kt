package ru.kadrolov.app

import android.content.Context
import android.media.*
import android.net.Uri
import java.io.File
import java.nio.ByteBuffer

object VideoTrim {
  /** Lossless local trim; start snaps to preceding video keyframe (shown in UI). */
  fun trim(c: Context, uri: Uri, startMs: Long, endMs: Long, webm: Boolean, tree: String?): Uri {
    require(startMs >= 0 && endMs > startMs) { "Конец должен быть позже начала" }
    val extractor = MediaExtractor()
    val temp = File.createTempFile("trim-", if (webm) ".webm" else ".mp4", c.cacheDir)
    var muxer: MediaMuxer? = null
    var started = false
    try {
      extractor.setDataSource(c, uri, null)
      muxer =
        MediaMuxer(
          temp.absolutePath,
          if (webm) MediaMuxer.OutputFormat.MUXER_OUTPUT_WEBM
          else MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4,
        )
      val tracks = mutableMapOf<Int, Int>()
      var bufferSize = 1024 * 1024
      for (i in 0 until extractor.trackCount) {
        val format = extractor.getTrackFormat(i)
        val mime = format.getString(MediaFormat.KEY_MIME).orEmpty()
        if (mime.startsWith("video/") || mime.startsWith("audio/")) {
          extractor.selectTrack(i)
          tracks[i] = muxer.addTrack(format)
          if (format.containsKey(MediaFormat.KEY_MAX_INPUT_SIZE))
            bufferSize = maxOf(bufferSize, format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE))
        }
      }
      check(tracks.isNotEmpty()) { "В файле нет видео или звука" }
      muxer.start()
      started = true
      extractor.seekTo(startMs * 1000, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
      val base = extractor.sampleTime.coerceAtLeast(0)
      val buffer = ByteBuffer.allocate(bufferSize.coerceAtMost(32 * 1024 * 1024))
      val info = MediaCodec.BufferInfo()
      var samples = 0
      while (true) {
        val track = extractor.sampleTrackIndex
        if (track < 0) break
        val at = extractor.sampleTime
        if (at > endMs * 1000) break
        buffer.clear()
        val n = extractor.readSampleData(buffer, 0)
        if (n < 0) break
        check(n <= buffer.capacity()) { "Слишком большой видеокадр" }
        info.set(
          0,
          n,
          (at - base).coerceAtLeast(0),
          if (extractor.sampleFlags and MediaExtractor.SAMPLE_FLAG_SYNC != 0)
            MediaCodec.BUFFER_FLAG_KEY_FRAME
          else 0,
        )
        muxer.writeSampleData(tracks.getValue(track), buffer, info)
        samples++
        if (!extractor.advance()) break
      }
      check(samples > 0) { "В выбранном отрезке нет кадров" }
      muxer.stop()
      started = false
      muxer.release()
      muxer = null
      return MediaFiles.save(
        c,
        temp,
        if (webm) "video/webm" else "video/mp4",
        MediaFiles.name(if (webm) "webm" else "mp4"),
        tree,
      )
    } finally {
      runCatching { extractor.release() }
      if (started) runCatching { muxer?.stop() }
      runCatching { muxer?.release() }
      temp.delete()
    }
  }
}
