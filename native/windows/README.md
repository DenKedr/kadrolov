# Native Windows capture helper

`capture.cpp` is an original Windows 10+ C++17 implementation using GDI, WASAPI,
and Media Foundation. It requires no FFmpeg or downloaded codec package. Windows
N/KN editions require Microsoft's Media Feature Pack. H.264/AAC encoder availability
is checked by creating the writer; missing features produce an explicit error.

Build from an MSVC developer prompt for the desired x86, x64 or ARM64 target:

```bat
cl /nologo /std:c++17 /EHsc /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 capture.cpp /Fe:kadrolov-capture.exe /link mfplat.lib mfreadwrite.lib mfuuid.lib mf.lib ole32.lib uuid.lib gdi32.lib user32.lib
```

CMake target: `WIN32` executable is **not** appropriate (this is a console helper
with a `wmain` entry point). Link `mfplat mfreadwrite mfuuid mf ole32 uuid gdi32 user32`.
Static MSVC runtime (`/MT`) avoids a separate VC runtime installation. The Qt parent
should hide this child console using its process creation flags.

```text
kadrolov-capture.exe --output C:\Videos\screen.mp4 --rect -1920,0,1920,1080 --fps 30 --width 1920 --height 1080 --quality 23 --audio both --cursor 1 --clicks 1 --camera 0
```

All options take a separate value. Output is a Unicode filesystem path. Rectangle
coordinates are physical virtual-desktop pixels (negative coordinates are valid).
FPS range is 1–60. Output dimensions must be even, 2–8192; omitted dimensions default
to the rectangle rounded down to even pixels. Quality range 0–51 maps to a bounded
H.264 bitrate (smaller is better); it is not the FFmpeg CRF algorithm. Audio accepts
`none`, `system`, `mic`, or `both`. Both inputs are averaged into **one stereo AAC
track** at 48 kHz / 192 kbit/s. WASAPI's shared-mode sample-rate/channel converter
normalizes device input into stereo signed 16-bit PCM. Device failure aborts with an
error instead of dropping sound silently. The default endpoints are selected once;
device replacement requires a new recording.

Stdin accepts exactly these JSON objects, whitespace allowed, one per line:

```json
{"command":"pause"}
{"command":"resume"}
{"command":"stop"}
```

Keep stdin open during recording. EOF also requests stop. Unknown lines are ignored.
Allow each command's event to arrive before sending the next command. Pause excludes
wall-clock time and captured audio; resume continues the same MP4 timeline. `stop`
finalizes the MP4 before `finished`. Do not terminate the child to stop a recording:
forced termination can leave an unfinalized file. Start failures and runtime failures
emit `error` and return exit code 1. A file left after an error is not a successful
recording. The parent must only expose a result after `finished` and exit code 0.

Stdout contains JSON lines with `event` (`started`, `paused`, `resumed`, `finished`,
`error`), `path`, `audioTracks` (0 or 1), and `message`. No diagnostic text contaminates
stdout. With `--camera 1`, a real camera frame is composited at the bottom right, preserving
its aspect ratio, at about 25% of output width and at most one third of output height.
`--camera-device` optionally selects an exact Media Foundation symbolic link or
friendly name; otherwise the first enumerated camera is used. The helper waits for
the first camera frame before `started` and reports missing, denied, disconnected or
stalled cameras explicitly. Windows Settings → Privacy & security → Camera must
allow desktop applications. The source reader converts camera input to RGB32 in a
dedicated COM worker using asynchronous reads; stop cancels the outstanding read
instead of waiting for a synchronous driver capture call. Camera frames continue
updating while recording is paused. Cursor capture and held-left-click outlines
are composited in GDI. Protected content, secure desktop and hardware overlays may be
black, as with other GDI capture tools. Windows capture permission and endpoint privacy
settings still apply.

Audio uses WASAPI QPC timestamps, zero fills absent samples (including silent loopback)
and reserves 100 ms for packet delivery. It drains capture during pause and drops
those packets. Mixing averages both sources to avoid clipping. There is no acoustic
echo cancellation: microphone speaker feedback depends on the user's device setup.
The desktop is sampled on one encoder thread; a slow encoder drops video frames
while preserving wall-clock timestamps. This backend currently needs Windows CI compilation and physical
x86/x64/ARM64 recording tests; it has not been executed on the development Mac.

Official API references used:

- https://learn.microsoft.com/en-us/windows/win32/medfound/using-the-sink-writer
- https://learn.microsoft.com/en-us/windows/win32/coreaudio/audclnt-streamflags-xxx-constants
- https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer

Camera API references:

- https://learn.microsoft.com/en-us/windows/win32/medfound/enumerating-video-capture-devices
- https://learn.microsoft.com/en-us/windows/win32/medfound/using-the-source-reader-in-asynchronous-mode
- https://learn.microsoft.com/en-us/windows/win32/medfound/mf-source-reader-enable-video-processing
