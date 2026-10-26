# macOS screen-capture identity diagnosis

Read-only investigation on macOS 15.6.1, 27 October 2026. No TCC database changes, permission bypass, reset, GUI operation or signing change performed by this investigation.

## Evidence

The current packaged app passes `codesign --verify --deep --strict`. Its signing mode is ad hoc, no TeamIdentifier, with designated requirement:

```
cdhash H"f036a5b70e7ec4e0e69a0ca7cbfd9406c8c31dfb"
```

During the actual packaged app's request, `tccd` attributes the responsible code correctly to `ru.kadrolov.desktop`, at the exact `build/kadrolov.app/Contents/MacOS/kadrolov` path. However, for `kTCCServiceScreenCapture`, its log checks the saved requirement:

```
cdhash H"3f8b03df95d8e3dc3a2d29ad3f436c248608b8ef"
SecStaticCodeCheckValidity() ... status: -67050
```

Thus an enabled UI permission entry does not match the current app's ad-hoc code identity. The same helper launched from Codex CLI has a different responsible process, so its successful preflight and actual capture are not evidence that the standalone app has the corresponding grant. This does not establish a false negative in CGPreflightScreenCaptureAccess itself.

The app executable existed before the currently running process was launched, ruling out only the simplest explanation that the running process predates that executable. Ad-hoc signing on rebuild remains inherently unstable; signing updates the designated cdhash requirement.

One valid Apple Development identity was found in the existing local keychain. Its personal subject/fingerprint are deliberately omitted from this report. Its use is a decision for the main agent/user; no identity was created or changed.

## Supported next test

Use a stable existing Apple Development identity for both the helper and packaged application, verify the resulting designated requirements and bundle seal, then obtain the normal system grant for that finalized signed build and restart it. Repeat the screenshot in the actual GUI → QProcess → helper chain. Do not add an identifier-only ad-hoc requirement, disable preflight, edit the private TCC database, or grant a broad interpreter access merely to make a test pass.

If the finalized signed app still fails, add an opt-in diagnostic mode which reports preflight plus the actual SCShareableContent result (display/window counts only, NSError domain/code; no titles or images). Run that diagnostic from the actual Qt parent. A successful direct API result with false preflight would be evidence to revisit the guard; no such evidence exists yet.

Apple DTS confirms that ad-hoc screen capture grants change after rebuilds and recommends an Apple Development identity:
https://developer.apple.com/forums/thread/819406

Apple explains code-signing requirements and stable identities:
https://developer.apple.com/documentation/technotes/tn3127-inside-code-signing-requirements

## Read-only commands used

```sh
codesign -dv --verbose=4 build/kadrolov.app
codesign -dr - build/kadrolov.app
codesign --verify --deep --strict build/kadrolov.app
security find-identity -v -p codesigning
log show --last 8m --style compact \
  --predicate 'process == "tccd" AND eventMessage CONTAINS[c] "kadrolov"' --info --debug
```

The private TCC database was inaccessible during read-only inspection. No effort was made to expand access; the diagnosis uses public signing tools and existing unified-log evidence.
