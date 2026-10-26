package ru.kadrolov.app

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.camera2.*
import android.opengl.*
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.view.Surface
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/** Screen crop and optional camera are composited directly into encoder EGL surface. */
class VideoCompositor(
  private val context: Context,
  private val encoderSurface: Surface,
  private val sourceWidth: Int,
  private val sourceHeight: Int,
  private val outWidth: Int,
  private val outHeight: Int,
  private val options: CaptureOptions,
  private val onError: (String) -> Unit,
) {
  private val thread = HandlerThread("Kadrolov-GL").apply { start() }
  private val handler = Handler(thread.looper)
  private var display = EGL14.EGL_NO_DISPLAY
  private var eglContext = EGL14.EGL_NO_CONTEXT
  private var window = EGL14.EGL_NO_SURFACE
  private var program = 0
  private var screenTexture = 0
  private var cameraTexture = 0
  private lateinit var screen: SurfaceTexture
  private var camera: SurfaceTexture? = null
  private var cameraSurface: Surface? = null
  private var device: CameraDevice? = null
  private var session: CameraCaptureSession? = null
  private var cameraReady = false
  private var screenReady = false
  private val frames = FrameSchedule(options.fps.coerceIn(15, 60))
  private val ticker =
    object : Runnable {
      override fun run() {
        if (closed) return
        val timestamp = frames.next(SystemClock.elapsedRealtimeNanos(), screenReady, suspended)
        if (timestamp >= 0) render(timestamp)
        val delay =
          if (suspended || !screenReady) 1_000_000_000L / options.fps.coerceIn(15, 60)
          else frames.delayNanos(SystemClock.elapsedRealtimeNanos())
        handler.postDelayed(this, maxOf(1L, (delay + 999_999L) / 1_000_000L))
      }
    }
  private var cameraRotation = 0
  @Volatile private var closed = false
  @Volatile private var suspended = false

  fun suspendFrames() {
    suspended = true
  }

  fun resumeFrames() {
    suspended = false
  }

  fun awaitIdle() {
    val latch = CountDownLatch(1)
    handler.post { latch.countDown() }
    check(latch.await(3, TimeUnit.SECONDS)) { "Графический захват не завершил кадр" }
  }

  lateinit var inputSurface: Surface
    private set

  fun finishFrame(timestampNanos: Long) {
    val done = CountDownLatch(1)
    handler.post {
      try {
        val timestamp = frames.finish(timestampNanos, screenReady, false)
        if (!closed && timestamp >= 0) render(timestamp)
      } finally {
        done.countDown()
      }
    }
    check(done.await(3, TimeUnit.SECONDS)) { "Не удалось завершить последний кадр" }
  }

  private val vertices =
    ByteBuffer.allocateDirect(8 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply {
      put(floatArrayOf(-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f))
      position(0)
    }

  fun start() {
    val ready = CountDownLatch(1)
    var failure: Throwable? = null
    handler.post {
      try {
        initialize()
        if (options.camera) openCamera()
      } catch (e: Throwable) {
        failure = e
      } finally {
        ready.countDown()
      }
    }
    check(ready.await(10, TimeUnit.SECONDS)) { "Не удалось подготовить графический захват" }
    failure?.let { throw IllegalStateException("Графический захват: ${it.message}", it) }
  }

  private fun initialize() {
    display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
    check(display != EGL14.EGL_NO_DISPLAY)
    val versions = IntArray(2)
    check(EGL14.eglInitialize(display, versions, 0, versions, 1))
    val configs = arrayOfNulls<EGLConfig>(1)
    val count = IntArray(1)
    val attrs =
      intArrayOf(
        EGL14.EGL_RED_SIZE,
        8,
        EGL14.EGL_GREEN_SIZE,
        8,
        EGL14.EGL_BLUE_SIZE,
        8,
        EGL14.EGL_ALPHA_SIZE,
        8,
        EGL14.EGL_RENDERABLE_TYPE,
        EGL14.EGL_OPENGL_ES2_BIT,
        0x3142,
        1,
        EGL14.EGL_NONE,
      )
    check(EGL14.eglChooseConfig(display, attrs, 0, configs, 0, 1, count, 0) && count[0] > 0)
    eglContext =
      EGL14.eglCreateContext(
        display,
        configs[0],
        EGL14.EGL_NO_CONTEXT,
        intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE),
        0,
      )
    window =
      EGL14.eglCreateWindowSurface(
        display,
        configs[0],
        encoderSurface,
        intArrayOf(EGL14.EGL_NONE),
        0,
      )
    check(EGL14.eglMakeCurrent(display, window, window, eglContext))
    val vertex =
      "attribute vec4 position; attribute vec2 textureCoordinate; uniform mat4 textureMatrix; varying vec2 uv; varying vec2 localUv; void main(){gl_Position=position;localUv=textureCoordinate;uv=(textureMatrix*vec4(textureCoordinate,0.0,1.0)).xy;}"
    val fragment =
      "#extension GL_OES_EGL_image_external : require\nprecision mediump float; uniform samplerExternalOES image; uniform float roundMask; varying vec2 uv; varying vec2 localUv; void main(){if(roundMask>0.5 && distance(localUv,vec2(0.5))>0.5)discard;gl_FragColor=texture2D(image,uv);}"
    fun shader(type: Int, text: String): Int {
      val id = GLES20.glCreateShader(type)
      GLES20.glShaderSource(id, text)
      GLES20.glCompileShader(id)
      val ok = IntArray(1)
      GLES20.glGetShaderiv(id, GLES20.GL_COMPILE_STATUS, ok, 0)
      check(ok[0] != 0) { GLES20.glGetShaderInfoLog(id) }
      return id
    }
    val v = shader(GLES20.GL_VERTEX_SHADER, vertex)
    val f = shader(GLES20.GL_FRAGMENT_SHADER, fragment)
    program = GLES20.glCreateProgram()
    GLES20.glAttachShader(program, v)
    GLES20.glAttachShader(program, f)
    GLES20.glLinkProgram(program)
    val linked = IntArray(1)
    GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, linked, 0)
    check(linked[0] != 0) { GLES20.glGetProgramInfoLog(program) }
    GLES20.glDeleteShader(v)
    GLES20.glDeleteShader(f)
    screenTexture = texture()
    screen =
      SurfaceTexture(screenTexture).apply {
        setDefaultBufferSize(sourceWidth, sourceHeight)
        setOnFrameAvailableListener(
          {
            if (!closed) {
              try {
                updateTexImage()
                screenReady = true
              } catch (e: Exception) {
                onError(e.message ?: "Не удалось получить кадр")
              }
            }
          },
          handler,
        )
      }
    inputSurface = Surface(screen)
    if (options.camera) {
      cameraTexture = texture()
      camera =
        SurfaceTexture(cameraTexture).apply {
          setDefaultBufferSize(640, 480)
          setOnFrameAvailableListener(
            {
              if (!closed) {
                runCatching {
                  updateTexImage()
                  cameraReady = true
                }
              }
            },
            handler,
          )
        }
      cameraSurface = Surface(camera)
    }
    handler.post(ticker)
  }

  private fun texture(): Int {
    val ids = IntArray(1)
    GLES20.glGenTextures(1, ids, 0)
    GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, ids[0])
    GLES20.glTexParameteri(
      GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
      GLES20.GL_TEXTURE_MIN_FILTER,
      GLES20.GL_LINEAR,
    )
    GLES20.glTexParameteri(
      GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
      GLES20.GL_TEXTURE_MAG_FILTER,
      GLES20.GL_LINEAR,
    )
    GLES20.glTexParameteri(
      GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
      GLES20.GL_TEXTURE_WRAP_S,
      GLES20.GL_CLAMP_TO_EDGE,
    )
    GLES20.glTexParameteri(
      GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
      GLES20.GL_TEXTURE_WRAP_T,
      GLES20.GL_CLAMP_TO_EDGE,
    )
    return ids[0]
  }

  private fun draw(
    texture: Int,
    surfaceTexture: SurfaceTexture,
    x: Float,
    y: Float,
    w: Float,
    h: Float,
  ) {
    GLES20.glUseProgram(program)
    GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
    GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texture)
    val matrix = FloatArray(16)
    surfaceTexture.getTransformMatrix(matrix)
    if (texture == cameraTexture) {
      val rotation = FloatArray(16)
      Matrix.setIdentityM(rotation, 0)
      Matrix.translateM(rotation, 0, 0.5f, 0.5f, 0f)
      Matrix.rotateM(rotation, 0, cameraRotation.toFloat(), 0f, 0f, 1f)
      Matrix.translateM(rotation, 0, -0.5f, -0.5f, 0f)
      val transformed = FloatArray(16)
      Matrix.multiplyMM(transformed, 0, matrix, 0, rotation, 0)
      System.arraycopy(transformed, 0, matrix, 0, 16)
    }
    val coords =
      ByteBuffer.allocateDirect(8 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply {
        put(floatArrayOf(x, 1f - y - h, x + w, 1f - y - h, x, 1f - y, x + w, 1f - y))
        position(0)
      }
    val position = GLES20.glGetAttribLocation(program, "position")
    val uv = GLES20.glGetAttribLocation(program, "textureCoordinate")
    GLES20.glEnableVertexAttribArray(position)
    GLES20.glVertexAttribPointer(position, 2, GLES20.GL_FLOAT, false, 0, vertices)
    GLES20.glEnableVertexAttribArray(uv)
    GLES20.glVertexAttribPointer(uv, 2, GLES20.GL_FLOAT, false, 0, coords)
    GLES20.glUniformMatrix4fv(
      GLES20.glGetUniformLocation(program, "textureMatrix"),
      1,
      false,
      matrix,
      0,
    )
    GLES20.glUniform1i(GLES20.glGetUniformLocation(program, "image"), 0)
    GLES20.glUniform1f(
      GLES20.glGetUniformLocation(program, "roundMask"),
      if (texture == cameraTexture && options.cameraShape == "circle") 1f else 0f,
    )
    GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
  }

  private fun render(timestamp: Long) {
    if (closed) return
    try {
      GLES20.glViewport(0, 0, outWidth, outHeight)
      GLES20.glClearColor(0f, 0f, 0f, 1f)
      GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
      draw(
        screenTexture,
        screen,
        options.cropX,
        options.cropY,
        options.cropWidth,
        options.cropHeight,
      )
      camera?.let {
        if (cameraReady) {
          val w = minOf((outWidth * options.cameraScale.coerceIn(.1f, .5f)).toInt(), outHeight / 2)
          val h =
            (if (options.cameraShape == "circle") w else w * 4 / 3).coerceAtMost(outHeight / 2)
          val x = if (options.cameraPosition.endsWith("Left")) 16 else outWidth - w - 16
          val y = if (options.cameraPosition.startsWith("top")) outHeight - h - 16 else 16
          GLES20.glViewport(x, y, w, h)
          draw(cameraTexture, it, 0f, 0f, 1f, 1f)
        }
      }
      EGLExt.eglPresentationTimeANDROID(display, window, timestamp)
      check(EGL14.eglSwapBuffers(display, window)) { "Не удалось передать кадр кодировщику" }
    } catch (e: Exception) {
      if (!closed) onError(e.message ?: "Ошибка графического захвата")
    }
  }

  @SuppressLint("MissingPermission")
  private fun openCamera() {
    val manager = context.getSystemService(CameraManager::class.java)
    val id =
      manager.cameraIdList.firstOrNull {
        manager.getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING) ==
          (if (options.cameraFacing == "back") CameraCharacteristics.LENS_FACING_BACK
          else CameraCharacteristics.LENS_FACING_FRONT)
      } ?: error("Выбранная камера не найдена. Измените выбор перед записью")
    cameraRotation =
      (manager.getCameraCharacteristics(id).get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 0) +
        (if (sourceWidth > sourceHeight) 90 else 0)
    manager.openCamera(
      id,
      object : CameraDevice.StateCallback() {
        override fun onOpened(c: CameraDevice) {
          if (closed) {
            c.close()
            return
          }
          device = c
          @Suppress("DEPRECATION")
          c.createCaptureSession(
            listOf(cameraSurface!!),
            object : CameraCaptureSession.StateCallback() {
              override fun onConfigured(s: CameraCaptureSession) {
                if (closed) {
                  s.close()
                  return
                }
                session = s
                val request =
                  c.createCaptureRequest(CameraDevice.TEMPLATE_RECORD)
                    .apply {
                      addTarget(cameraSurface!!)
                      set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO)
                    }
                    .build()
                s.setRepeatingRequest(request, null, handler)
              }

              override fun onConfigureFailed(s: CameraCaptureSession) {
                onError("Не удалось подготовить камеру")
              }
            },
            handler,
          )
        }

        override fun onDisconnected(c: CameraDevice) {
          c.close()
          if (!closed) onError("Камера отключена")
        }

        override fun onError(c: CameraDevice, error: Int) {
          c.close()
          if (!closed) onError("Камера недоступна ($error)")
        }
      },
      handler,
    )
  }

  fun close() {
    if (closed) return
    closed = true
    handler.removeCallbacks(ticker)
    val done = CountDownLatch(1)
    handler.post {
      try {
        runCatching { session?.close() }
        runCatching { device?.close() }
        cameraSurface?.release()
        camera?.release()
        if (::inputSurface.isInitialized) inputSurface.release()
        if (::screen.isInitialized) screen.release()
        if (display != EGL14.EGL_NO_DISPLAY) {
          EGL14.eglMakeCurrent(
            display,
            EGL14.EGL_NO_SURFACE,
            EGL14.EGL_NO_SURFACE,
            EGL14.EGL_NO_CONTEXT,
          )
          EGL14.eglDestroySurface(display, window)
          EGL14.eglDestroyContext(display, eglContext)
          EGL14.eglTerminate(display)
        }
      } finally {
        done.countDown()
        thread.quitSafely()
      }
    }
    done.await(3, TimeUnit.SECONDS)
  }
}
