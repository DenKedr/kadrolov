package ru.kadrolov.app

import android.app.*
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.*
import android.provider.Settings
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.*
import java.io.File
import kotlin.concurrent.thread

object CaptureState {
  @Volatile var state = "idle"
  @Volatile var elapsedMs = 0L
  @Volatile var message = ""
}

class CaptureService : Service() {
  private val handler = Handler(Looper.getMainLooper())
  private var projection: MediaProjection? = null
  private var recorder: ScreenRecorder? = null
  private var reader: ImageReader? = null
  private var virtualDisplay: VirtualDisplay? = null
  private var options = CaptureOptions()
  private var overlay: View? = null
  private var overlayLabel: TextView? = null
  private var pauseButton: Button? = null
  private var captured = false
  private var capturedSize: Pair<Int, Int>? = null
  @Volatile private var closed = false
  private val ticker =
    object : Runnable {
      override fun run() {
        CaptureState.elapsedMs = recorder?.elapsedMs() ?: 0
        overlayLabel?.text =
          "${if(CaptureState.state=="paused")"Пауза" else "Запись"} ${time(CaptureState.elapsedMs)}"
        if (CaptureState.state == "recording" || CaptureState.state == "paused") {
          getSystemService(NotificationManager::class.java).notify(1, notification())
          handler.postDelayed(this, 1000)
        }
      }
    }

  override fun onBind(intent: Intent?) = null

  override fun onCreate() {
    super.onCreate()
    getSystemService(NotificationManager::class.java)
      .createNotificationChannel(
        NotificationChannel("capture", "Запись экрана", NotificationManager.IMPORTANCE_LOW)
      )
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    when (intent?.action) {
      "pause" -> {
        if (recorder == null) {
          stopSelf()
          return START_NOT_STICKY
        }
        setPaused(true)
        return START_NOT_STICKY
      }
      "resume" -> {
        if (recorder == null) {
          stopSelf()
          return START_NOT_STICKY
        }
        setPaused(false)
        return START_NOT_STICKY
      }
      "stop" -> {
        stopCapture()
        return START_NOT_STICKY
      }
    }
    if (intent == null) {
      stopSelf()
      return START_NOT_STICKY
    }
    if (projection != null) return START_NOT_STICKY
    options = CaptureOptions.from(intent)
    try {
      val microphone = options.audio == "mic" || options.audio == "both"
      val types =
        ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION or
          (if (Build.VERSION.SDK_INT >= 30 && microphone)
            ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
          else 0) or
          (if (Build.VERSION.SDK_INT >= 30 && options.camera)
            ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA
          else 0)
      startForeground(1, notification(), types)
      @Suppress("DEPRECATION")
      val data =
        if (Build.VERSION.SDK_INT >= 33)
          intent.getParcelableExtra("projectionData", Intent::class.java)
        else intent.getParcelableExtra("projectionData")
      checkNotNull(data) { "Нет разрешения захвата. Запустите новую сессию" }
      projection =
        getSystemService(MediaProjectionManager::class.java)
          .getMediaProjection(Activity.RESULT_OK, data)
      projection!!.registerCallback(
        object : MediaProjection.Callback() {
          override fun onStop() {
            if (!closed) {
              CaptureState.message = "Системное разрешение захвата отозвано"
              stopCapture()
            }
          }

          override fun onCapturedContentResize(width: Int, height: Int) {
            // Existing virtual display is deliberately not recreated with the one-use token.
            if (!options.screenshot) {
              val size = width to height
              val before = capturedSize
              capturedSize = size
              if (before != null && before != size && recorder != null) {
                CaptureState.message =
                  "Размер источника изменился. Запись сохранена; начните новую сессию"
                stopCapture()
              }
            }
          }
        },
        handler,
      )
      CaptureState.state = "starting"
      CaptureState.message = ""
      handler.postDelayed(
        {
          if (!closed) {
            if (options.screenshot) captureScreenshot() else startRecording()
          }
        },
        if (options.screenshot) 700 else 3000,
      )
    } catch (e: Exception) {
      failure(e.message ?: "Не удалось начать захват")
    }
    return START_NOT_STICKY
  }

  private fun screenSize(): Pair<Int, Int> {
    val wm = getSystemService(WindowManager::class.java)
    if (Build.VERSION.SDK_INT >= 30) {
      val b = wm.maximumWindowMetrics.bounds
      return b.width() to b.height()
    }
    @Suppress("DEPRECATION")
    val metrics = android.util.DisplayMetrics().also { wm.defaultDisplay.getRealMetrics(it) }
    return metrics.widthPixels to metrics.heightPixels
  }

  private fun startRecording() {
    try {
      val (w, h) = screenSize()
      val cw = w * options.cropWidth
      val ch = h * options.cropHeight
      val scale = minOf(1.0, options.maxHeight.toDouble() / maxOf(cw, ch))
      val width = maxOf(2, (cw * scale).toInt() / 2 * 2)
      val height = maxOf(2, (ch * scale).toInt() / 2 * 2)
      recorder =
        ScreenRecorder(
          this,
          projection!!,
          options,
          width,
          height,
          resources.displayMetrics.densityDpi,
          w,
          h,
          { m -> handler.post { failure(m) } },
          { file -> handler.post { if (!closed) saveVideo(file) } },
          { warning -> handler.post { if (!closed) CaptureState.message = warning } },
        )
      recorder!!.start()
      CaptureState.state = "recording"
      if (options.overlay && Settings.canDrawOverlays(this)) showOverlay()
      handler.post(ticker)
    } catch (e: Exception) {
      failure("Не удалось начать запись: ${e.message}. Попробуйте 720p, 30 кадров/с или MP4")
    }
  }

  private fun captureScreenshot() {
    try {
      val (w, h) = screenSize()
      reader = ImageReader.newInstance(w, h, PixelFormat.RGBA_8888, 2)
      reader!!.setOnImageAvailableListener(
        { r ->
          val image = r.acquireLatestImage() ?: return@setOnImageAvailableListener
          if (captured) {
            image.close()
            return@setOnImageAvailableListener
          }
          captured = true
          try {
            val plane = image.planes[0]
            val stride = plane.pixelStride
            val padded = image.width + (plane.rowStride - stride * image.width) / stride
            val paddedBitmap = Bitmap.createBitmap(padded, image.height, Bitmap.Config.ARGB_8888)
            paddedBitmap.copyPixelsFromBuffer(plane.buffer)
            val bitmap = Bitmap.createBitmap(paddedBitmap, 0, 0, image.width, image.height)
            if (bitmap !== paddedBitmap) paddedBitmap.recycle()
            val temp = File(cacheDir, "capture-${System.currentTimeMillis()}.png")
            temp.outputStream().use { check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            bitmap.recycle()
            CaptureState.message = "Снимок готов — выберите область и добавьте пометки"
            val edit =
              Intent(this, EditorActivity::class.java)
                .putExtra("path", temp.absolutePath)
                .putExtra("tree", options.destinationTree)
                .putExtra("imageFormat", options.imageFormat)
                .putExtra("imageQuality", options.imageQuality)
                .putExtra("regionOnly", options.selectVideoRegion)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            val pending =
              PendingIntent.getActivity(
                this,
                200,
                edit,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
              )
            getSystemService(NotificationManager::class.java)
              .notify(
                2,
                Notification.Builder(this, "capture")
                  .setSmallIcon(android.R.drawable.ic_menu_camera)
                  .setContentTitle("Снимок готов")
                  .setContentText("Открыть редактор Кадролова")
                  .setContentIntent(pending)
                  .setAutoCancel(true)
                  .build(),
              )
            runCatching { startActivity(edit) }
            cleanup()
          } catch (e: Exception) {
            failure(e.message ?: "Не удалось сделать снимок")
          } finally {
            image.close()
          }
        },
        handler,
      )
      virtualDisplay =
        projection!!.createVirtualDisplay(
          "Кадролов — снимок",
          w,
          h,
          resources.displayMetrics.densityDpi,
          DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
          reader!!.surface,
          null,
          handler,
        )
      handler.postDelayed(
        { if (!captured) failure("Не получены кадры. Источник может запрещать захват") },
        7000,
      )
    } catch (e: Exception) {
      failure(e.message ?: "Ошибка снимка")
    }
  }

  private fun saveVideo(file: File) {
    CaptureState.state = "saving"
    handler.post {
      removeOverlay()
      update()
    }
    thread(name = "Kadrolov-save") {
      try {
        check(file.length() > 0) { "Запись пуста" }
        val mime = if (options.videoFormat == "webm") "video/webm" else "video/mp4"
        val uri =
          MediaFiles.save(
            this,
            file,
            mime,
            MediaFiles.name(options.videoFormat),
            options.destinationTree,
          )
        file.delete()
        handler.post {
          CaptureState.message =
            if (CaptureState.message.isBlank()) "Видео сохранено"
            else CaptureState.message + ". Видео сохранено"
          val open =
            Intent(Intent.ACTION_VIEW)
              .setDataAndType(uri, mime)
              .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
          val pi =
            PendingIntent.getActivity(
              this,
              201,
              open,
              PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
            )
          getSystemService(NotificationManager::class.java)
            .notify(
              2,
              Notification.Builder(this, "capture")
                .setSmallIcon(android.R.drawable.ic_media_play)
                .setContentTitle("Видео сохранено")
                .setContentText("Нажмите, чтобы открыть")
                .setContentIntent(pi)
                .setAutoCancel(true)
                .build(),
            )
          cleanup()
        }
      } catch (e: Exception) {
        handler.post {
          failure("Не удалось сохранить: ${e.message}. Временная запись сохранена в приложении")
        }
      }
    }
  }

  private fun stopCapture() {
    if (CaptureState.state == "saving") return
    if (recorder != null) {
      CaptureState.state = "saving"
      removeOverlay()
      update()
      recorder?.stop()
    } else cleanup()
  }

  private fun update() {
    getSystemService(NotificationManager::class.java).notify(1, notification())
  }

  private fun notification(): Notification {
    val open =
      PendingIntent.getActivity(
        this,
        0,
        Intent(this, MainActivity::class.java),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
      )
    val b =
      Notification.Builder(this, "capture")
        .setSmallIcon(android.R.drawable.presence_video_online)
        .setContentTitle("Кадролов · ${time(CaptureState.elapsedMs)}")
        .setContentText(
          when (CaptureState.state) {
            "paused" -> "Пауза"
            "saving" -> "Сохранение видео…"
            else -> "Захват экрана"
          }
        )
        .setContentIntent(open)
        .setOngoing(true)
    if (!options.screenshot && CaptureState.state != "saving") {
      val pause = CaptureState.state != "paused"
      b.addAction(
        Notification.Action.Builder(
            null,
            if (pause) "Пауза" else "Продолжить",
            action(if (pause) "pause" else "resume"),
          )
          .build()
      )
    }
    b.addAction(Notification.Action.Builder(null, "Стоп", action("stop")).build())
    return b.build()
  }

  private fun action(a: String) =
    PendingIntent.getService(
      this,
      a.hashCode(),
      Intent(this, CaptureService::class.java).setAction(a),
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

  private fun showOverlay() {
    val wm = getSystemService(WindowManager::class.java)
    val bar =
      LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(16, 8, 16, 8)
        setBackgroundColor(0xEEFFF7ED.toInt())
      }
    overlayLabel =
      TextView(this).apply {
        text = "Запись 00:00"
        setTextColor(0xFF111827.toInt())
        textSize = 14f
      }
    bar.addView(overlayLabel)
    val row = LinearLayout(this)
    pauseButton =
      Button(this).apply {
        text = "Пауза"
        setOnClickListener { setPaused(CaptureState.state != "paused") }
      }
    row.addView(pauseButton)
    row.addView(
      Button(this).apply {
        text = "Стоп"
        setOnClickListener { stopCapture() }
      }
    )
    bar.addView(row)
    val params =
      WindowManager.LayoutParams(
          WindowManager.LayoutParams.WRAP_CONTENT,
          WindowManager.LayoutParams.WRAP_CONTENT,
          WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
          WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
          PixelFormat.TRANSLUCENT,
        )
        .apply {
          gravity = Gravity.TOP or Gravity.END
          x = 8
          y = 80
        }
    var x = 0f
    var y = 0f
    var px = 0
    var py = 0
    overlayLabel!!.setOnTouchListener { _, e ->
      when (e.action) {
        MotionEvent.ACTION_DOWN -> {
          x = e.rawX
          y = e.rawY
          px = params.x
          py = params.y
          true
        }
        MotionEvent.ACTION_MOVE -> {
          params.x = px - (e.rawX - x).toInt()
          params.y = py + (e.rawY - y).toInt()
          wm.updateViewLayout(bar, params)
          true
        }
        MotionEvent.ACTION_UP -> {
          overlayLabel?.performClick()
          true
        }
        else -> false
      }
    }
    wm.addView(bar, params)
    overlay = bar
  }

  private fun removeOverlay() {
    overlay?.let { runCatching { getSystemService(WindowManager::class.java).removeView(it) } }
    overlay = null
    overlayLabel = null
    pauseButton = null
  }

  private fun setPaused(pause: Boolean) {
    if (recorder == null || CaptureState.state !in listOf("recording", "paused")) return
    try {
      if (pause) {
        recorder?.pause()
        CaptureState.state = "paused"
        pauseButton?.text = "Продолжить"
      } else {
        recorder?.resume()
        CaptureState.state = "recording"
        pauseButton?.text = "Пауза"
      }
      update()
    } catch (e: Exception) {
      failure(
        "Не удалось изменить паузу: ${e.message}. Доступные данные оставлены во временной записи"
      )
    }
  }

  private fun failure(m: String) {
    if (closed) return
    recorder?.stop()
    CaptureState.message = m
    Toast.makeText(this, m, Toast.LENGTH_LONG).show()
    cleanup()
  }

  private fun cleanup() {
    if (closed) return
    closed = true
    handler.removeCallbacksAndMessages(null)
    removeOverlay()
    virtualDisplay?.release()
    virtualDisplay = null
    reader?.close()
    reader = null
    projection?.stop()
    projection = null
    recorder = null
    CaptureState.state = "idle"
    stopForeground(STOP_FOREGROUND_REMOVE)
    stopSelf()
  }

  override fun onDestroy() {
    recorder?.stop()
    if (!closed) cleanup()
    super.onDestroy()
  }

  companion object {
    fun time(ms: Long): String {
      val s = ms / 1000
      return "%02d:%02d".format(s / 60, s % 60)
    }
  }
}
