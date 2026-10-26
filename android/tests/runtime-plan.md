# Android runtime verification

## Current evidence

The source includes two independent `CaptureRuntimeTest` instrumentation journeys. They have not been executed on an emulator or physical device yet. `compileDebugAndroidTestKotlin assembleDebugAndroidTest` completed successfully on the Mac with JDK 21 / SDK 35 (13 seconds). The resulting test APK is `app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk`. A successful compilation is only build verification; it does not verify Android MediaProjection, codecs, permissions, camera or playback at runtime.

## Required CI environment

Use a fresh, disposable **API 35** Google APIs emulator, English (`en-US`), portrait, e.g. Pixel 2, hardware keyboard enabled, no snapshot reuse. The app supports API 29+, but these system-consent selectors deliberately target API 35. Tests skip other APIs/locales and non-emulator devices before changing app settings or requesting capture. CI must inspect the JUnit report and require **two executed, zero skipped** tests; a skipped suite is not a pass for this gate.

Run from `android/` with JDK 17 or 21 and Android SDK 35:

```sh
./gradlew --no-daemon --max-workers=2 connectedDebugAndroidTest
```

In an emulator runner, set locale through emulator options (`-prop persist.sys.locale=en-US`) and disable animations. Do not pre-grant MediaProjection or mock the projection token: both tests click through the real system consent window, explicitly choose **Entire screen**, and accept **Start now / Start recording / Start sharing**. The notification permission is granted by the test fixture; permission-denial handling is outside these two journeys.

For stability qualification, run the command ten consecutive times on disposable CI emulator state, require all 20 executions green with zero retries/skips, and record the run URLs. No ten-run or CI-pass claim is made until that evidence exists.

## Journeys and assertions

1. **Screenshot → system consent → editor → save:** launch UI, capture the whole display, wait for the accessible editor canvas, click Save, require success status and new image history entry. Decode the actual saved image, check dimensions and multiple rendered colors (a blank frame fails).
2. **Silent MP4 → system consent → recording → pause → resume → stop:** use isolated app preferences (720p, no audio/camera/overlay); require UI recording and pause states. Wait on actual elapsed recording time, with no hard sleeps, then stop through the app button. Require saved status and a new video history entry; MediaExtractor must read multiple video samples with strictly increasing timestamps, positive dimensions, at least three seconds of media and no audio track. MediaMetadataRetriever must decode a saved frame. This is a container/sample/decode smoke test, not proof of smooth playback on every device.

Each test removes only media URIs newly added since its own baseline. Settings are reset only after an emulator/API/locale guard. These tests are for ephemeral CI devices and must not be forced onto a user's device. No uploads or network calls are made by the tests.

## Artifacts

Always retain Gradle's reports/results:

- `app/build/reports/androidTests/connected/`
- `app/build/outputs/androidTest-results/connected/`

On failure, a JUnit watcher writes screenshot, window hierarchy, last 1500 logcat lines and exception stack to the target app's external files directory:

`/sdcard/Android/data/ru.kadrolov.app/files/runtime-artifacts/`

Pull this directory **before the emulator shuts down** (in the emulator runner script using a shell EXIT trap). Failure to collect an artifact must not replace the original Gradle exit code. Logcat may include emulator/system information; use only disposable CI accounts/data. The fixture uses semantic text/content descriptions and the Android SystemUI resource ID for the sharing-mode spinner; any OS prompt change should fail with screenshot/hierarchy evidence instead of automatically clicking unrelated dialogs.

## Remaining runtime coverage

Not verified by these tests: microphone/system/both audio, audio policy restrictions, camera overlay and camera shapes/position, region video crop, screenshot drag/crop/icon popup/annotations, OCR accuracy, boot shortcut, SAF folder permissions, WebM support, notification/overlay controls, rotation/background behavior and vendor-specific codecs. A physical Android device remains required for real camera/microphone/system audio behavior and performance. None is claimed device-verified here.
