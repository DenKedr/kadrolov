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

## Follow-up after stable signing

The main agent subsequently signed both binaries with the existing Apple Development identity. Read-only inspection confirmed certificate-backed designated requirements and the same TeamIdentifier for parent/helper; no personal certificate subject is recorded here. The app's new CDHash was `8368f414dc524233185a4e02263143db0edeb8e4` and helper's `88bf3d3426c3137438a3f19725a5164772250c48`.

Nevertheless, unified-log events at 00:52:07 still attribute the real helper request to parent `ru.kadrolov.desktop` and compare against the **same old ad-hoc `3f8b03...` requirement**, returning -67050. Re-adding one identically labelled UI row did not establish that the saved bundle-ID principal's verifier was replaced. Duplicate display labels can correspond to different bundle/path principals. A targeted, system-supported reset of only `ScreenCapture` for `ru.kadrolov.desktop`, followed by the normal user's grant, is therefore a narrower next action for the main agent than resetting all services; it was not performed by this investigator.

An explicit diagnostic was added:

```sh
kadrolov-capture --check-permission --probe-content
```

Without `--probe-content`, passive preflight retains its previous non-prompting behavior. With the flag, the helper additionally attempts actual SCShareableContent enumeration and returns only display/window **counts**, preflight before/after, PID/parentPID, or NSError domain/code/description. This can trigger a native OS consent prompt and must only be invoked by an explicit diagnostic action. No pixels or window titles are collected. Enumeration success is not labelled successful screenshot permission.

A separate binary was compiled at `build/native/kadrolov-capture-diagnostic`; the signed packaged application was not modified. From the Codex launch context it reported preflight true, actual enumeration successful, one display and 51 windows. The GUI-parent probe is still required to test the failing context. Production screenshot/video guards remain unchanged.
