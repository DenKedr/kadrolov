# Sparse screen frames and pause regression

27 October 2026, macOS 15.6.1 ARM64. Investigation did not modify UI, TCC, signing, the packaged app, or the user's unfinished recording.

## Observed GUI failure

A 300 × 200 point region (600 × 400 encoded pixels), no audio, failed after pause/resume. Unified logs showed AVFoundationErrorDomain `-11800`, underlying OSStatus `-16341`, originating from the format writer. The saved partial MP4 had only three sparse video packets. Those packets alone were not sufficient to diagnose invalid presentation order: compressed B-frame packet PTS need not be monotonic.

## Reproducible failing case

The test captured an existing static window belonging to Kadrolov, at 600 × 400, recorded six seconds, paused two, resumed five and stopped. The old helper returned `finished`, one captured source frame and duration ~11.017 seconds, but ffprobe reported an actual movie duration **22.033 seconds**. Dynamic-region tests had not exposed the defect.

Disabling frame reordering alone did **not** fix the regression: actual duration remained ~22.027 seconds.

## Fix and successful case

ScreenCaptureKit raw samples are no longer copied with their source sample timing/attachments for video. The same CVPixelBuffer and format are wrapped in a fresh CMSampleBuffer with the mapped presentation time, invalid decode time and an explicit duration of 1/FPS. The final static-screen padding sample uses the same fresh-frame path. Audio retiming is unchanged. The H.264 encoder also disables frame reordering for this real-time capture use case.

With that change the exact same static-window test returned an actual duration **11.048 seconds**, decoded presentation timestamps were monotonic, and the entire MP4 decoded without error. This establishes the raw-video timing/copy contract as the defect; it does not identify one particular undocumented SCK attachment as the sole cause.

## Regression command

Choose a non-animated window ID from `--list-windows`, preferably the application's own idle window:

```sh
python3 native/macos/tests/test_sparse_pause.py build/native/kadrolov-capture --window WINDOW_ID
```

This test records real local pixels but drives no UI. Temporary test media is removed after verification. The user's original unfinished fragments are never touched.

Errors now include nested NSError domain/code/description and stage in JSON. Optional `KADROLOV_CAPTURE_TRACE_TIMING=1` emits numeric source/mapped timing to stderr for diagnosis; it is disabled by default and contains no pixels or window titles.
