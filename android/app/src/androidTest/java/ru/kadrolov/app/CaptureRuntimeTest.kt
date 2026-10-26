package ru.kadrolov.app

import android.Manifest
import android.content.Intent
import android.graphics.BitmapFactory
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.By
import androidx.test.uiautomator.BySelector
import androidx.test.uiautomator.Condition
import androidx.test.uiautomator.UiDevice
import androidx.test.uiautomator.UiObject2
import androidx.test.uiautomator.Until
import java.io.File
import java.nio.ByteBuffer
import java.util.Locale
import java.util.regex.Pattern
import org.junit.After
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TestWatcher
import org.junit.runner.Description
import org.junit.runner.RunWith

/** Intentionally restricted to disposable API 35 English emulators. No projection mocks. */
@RunWith(AndroidJUnit4::class)
class CaptureRuntimeTest {
  private val instrumentation = InstrumentationRegistry.getInstrumentation()
  private val context = instrumentation.targetContext
  private val device = UiDevice.getInstance(instrumentation)
  private var originalUris = emptySet<String>()
  private var initialized = false

  @get:Rule val failureArtifacts = object : TestWatcher() {
    override fun failed(error: Throwable, description: Description) {
      val dir = File(context.getExternalFilesDir(null), "runtime-artifacts/${description.methodName}")
      dir.mkdirs()
      runCatching { device.takeScreenshot(File(dir, "screen.png")) }
      runCatching { device.dumpWindowHierarchy(File(dir, "window.xml")) }
      runCatching { File(dir, "logcat.txt").writeText(device.executeShellCommand("logcat -d -t 1500")) }
      try {
        File(dir, "failure.txt").writeText(error.stackTraceToString())
      } finally {
        // UTP uninstalls the APK after instrumentation, deleting its external files.
        // Preserve failure evidence under shell-owned emulator storage before uninstall.
        val durable = "/data/local/tmp/kadrolov-runtime/${description.methodName}"
        runCatching { device.executeShellCommand("mkdir -p $durable") }
        runCatching { device.executeShellCommand("cp -R ${dir.absolutePath}/. $durable/") }
        runCatching { device.executeShellCommand("screencap -p $durable/shell-screen.png") }
        runCatching { device.executeShellCommand("logcat -d -t 2000 -f $durable/shell-logcat.txt") }
      }
    }
  }

  @Before fun setup() {
    assumeTrue("Disposable emulator only", Build.FINGERPRINT.contains("generic") ||
      Build.MODEL.contains("sdk_gphone") || Build.HARDWARE in listOf("ranchu", "goldfish"))
    assumeTrue("Projection selectors target API 35", Build.VERSION.SDK_INT == 35)
    assumeTrue("System consent must be English", Locale.getDefault().language == "en")
    device.wakeUp()
    device.executeShellCommand("wm dismiss-keyguard")
    device.pressHome()
    originalUris = historyUris()
    initialized = true
    // Configure owned emulator state through the application API; all capture actions use the UI.
    context.getSharedPreferences("settings", 0).edit().clear()
      .putInt("audio", 0).putInt("size", 1).putInt("videoFormat", 0)
      .putBoolean("camera", false).putBoolean("overlay", false).commit()
    instrumentation.uiAutomation.grantRuntimePermission(context.packageName, Manifest.permission.POST_NOTIFICATIONS)
    context.startActivity(Intent(context, MainActivity::class.java)
      .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
    visible(label("Снимок"))
  }

  @After fun cleanup() {
    if (!initialized) return
    // Stop any failed recording and remove only this test's newly created MediaStore entries.
    context.startService(Intent(context, CaptureService::class.java).setAction("stop"))
    await("Capture service finishes", 30_000) { CaptureState.state == "idle" }
    for (uri in historyUris() - originalUris) {
      context.contentResolver.delete(Uri.parse(uri), null, null)
      MediaFiles.removeHistory(context, uri)
    }
  }

  @Test fun screenshotOpensEditorAndSavesDecodableImage() {
    visible(label("Снимок")).click()
    approveWholeScreen()
    await("Screenshot frame captured", 20_000) { CaptureState.message.startsWith("Снимок готов") }
    if (!device.hasObject(By.desc("Изображение для пометок и выбора области"))) {
      device.openNotification()
      visible(label("Снимок готов")).click()
    }
    visible(By.desc("Изображение для пометок и выбора области"))
    visible(label("Сохранить").clazz("android.widget.Button")).click()
    visible(label("Снимок сохранён"))
    val uri = newMedia("image/")
    val bitmap = context.contentResolver.openInputStream(uri).use { BitmapFactory.decodeStream(it) }
    assertNotNull("Saved screenshot must decode", bitmap)
    assertTrue("Screenshot has meaningful dimensions", bitmap!!.width > 100 && bitmap.height > 100)
    // The captured app contains text and controls; a blank/solid frame must fail.
    val colors = mutableSetOf<Int>()
    for (y in 0 until bitmap.height step maxOf(1, bitmap.height / 40))
      for (x in 0 until bitmap.width step maxOf(1, bitmap.width / 40)) colors += bitmap.getPixel(x, y)
    bitmap.recycle()
    assertTrue("Screenshot must contain rendered content", colors.size > 8)
  }

  @Test fun silentRecordingPausesResumesAndProducesPlayableMp4() {
    visible(label("Запись")).click()
    visible(label("Выбрать источник")).click()
    approveWholeScreen()
    await("Recording service starts", 20_000) { CaptureState.state == "recording" }
    // Production backgrounds its task when projection begins. Reopen its UI.
    context.startActivity(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    visible(By.text(Pattern.compile("Идёт запись · .*")))
    await("At least two seconds of encoded activity", 20_000) { CaptureState.elapsedMs >= 2_000 }
    visible(label("Пауза").clazz("android.widget.Button")).click()
    visible(By.text(Pattern.compile("Пауза · .*")))
    visible(label("Продолжить").clazz("android.widget.Button")).click()
    visible(By.text(Pattern.compile("Идёт запись · .*")))
    await("Recording progresses after resume", 20_000) { CaptureState.elapsedMs >= 4_000 }
    visible(label("Стоп").clazz("android.widget.Button")).click()
    visible(label("Видео сохранено"))
    val uri = newMedia("video/")
    val extractor = MediaExtractor()
    try {
      extractor.setDataSource(context, uri, null)
      var videoTrack = -1
      var audioTracks = 0
      for (i in 0 until extractor.trackCount) {
        val format = extractor.getTrackFormat(i)
        val mime = format.getString(MediaFormat.KEY_MIME).orEmpty()
        if (mime.startsWith("video/")) {
          videoTrack = i
          assertTrue(format.getInteger(MediaFormat.KEY_WIDTH) > 0)
          assertTrue(format.getInteger(MediaFormat.KEY_HEIGHT) > 0)
        }
        if (mime.startsWith("audio/")) audioTracks++
      }
      assertEquals("Default silent recording has no audio track", 0, audioTracks)
      assertTrue("MP4 contains a video track", videoTrack >= 0)
      extractor.selectTrack(videoTrack)
      val buffer = ByteBuffer.allocate(8 * 1024 * 1024)
      var samples = 0
      var previous = -1L
      while (extractor.readSampleData(buffer, 0) >= 0) {
        assertTrue("Video timestamps must increase", extractor.sampleTime > previous)
        previous = extractor.sampleTime
        samples++
        if (!extractor.advance()) break
        buffer.clear()
      }
      assertTrue("Several encoded frames exist", samples > 10)
      assertTrue("Video contains both active recording intervals", previous >= 3_000_000)
    } finally { extractor.release() }
    val retriever = MediaMetadataRetriever()
    try {
      retriever.setDataSource(context, uri)
      val frame = retriever.getFrameAtTime(1_000_000, MediaMetadataRetriever.OPTION_CLOSEST_SYNC)
      assertNotNull("Platform decoder can play a saved frame", frame)
      frame?.recycle()
    } finally { retriever.release() }
  }

  private fun approveWholeScreen() {
    // Android 15 defaults to a single app; explicitly select the full display via system UI.
    val spinner = visible(By.res("com.android.systemui", "screen_share_mode_spinner"))
    spinner.click()
    visible(label("Entire screen")).click()
    visible(By.text(Pattern.compile("Start (now|recording|sharing)", Pattern.CASE_INSENSITIVE or Pattern.UNICODE_CASE))).click()
    assertTrue("System consent closes", device.wait(Until.gone(By.res("com.android.systemui", "screen_share_mode_spinner")), 10_000))
  }

  // Material Button textAllCaps changes accessibility text; preserve exact labels but ignore case.
  private fun label(text: String): BySelector = By.text(Pattern.compile(
    Pattern.quote(text), Pattern.CASE_INSENSITIVE or Pattern.UNICODE_CASE))

  private fun visible(selector: BySelector): UiObject2 =
    requireNotNull(device.wait(Until.findObject(selector), 30_000)) { "Visible UI missing: $selector" }

  private fun await(message: String, timeout: Long = 15_000, predicate: () -> Boolean) {
    assertTrue(message, device.wait(object : Condition<UiDevice, Boolean> {
      override fun apply(device: UiDevice): Boolean = predicate()
    }, timeout) == true)
  }

  private fun historyUris(): Set<String> {
    val history = MediaFiles.history(context)
    return (0 until history.length()).map { history.getJSONObject(it).getString("uri") }.toSet()
  }

  private fun newMedia(mime: String): Uri {
    var result: Uri? = null
    await("New $mime output appears in local history") {
      val history = MediaFiles.history(context)
      result = (0 until history.length()).map { history.getJSONObject(it) }
        .firstOrNull { it.getString("uri") !in originalUris && it.getString("mime").startsWith(mime) }
        ?.getString("uri")?.let(Uri::parse)
      result != null
    }
    return requireNotNull(result)
  }
}
