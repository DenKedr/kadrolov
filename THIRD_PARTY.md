# Сторонние компоненты

Лицензия MIT в корне относится к собственному коду Кадролова. Сторонние компоненты сохраняют свои лицензии.

- **Qt Widgets/Gui/Core/PrintSupport**: dynamic linking; LGPL-3.0/GPL-3.0/commercial terms depending on distribution. This project uses the open-source LGPL distribution. Qt source and notices: https://code.qt.io/cgit/qt/qtbase.git/ and https://www.qt.io/licensing/open-source-lgpl-obligations . Users may replace compatible shared Qt libraries; no hardware restriction or signature check in Kadrolov prevents replacement.
- **Qt Image Formats**: open-source Qt distribution; https://code.qt.io/cgit/qt/qtimageformats.git/ . Individual image codec libraries retain their licenses.
- **FFmpeg**: optional external program, not bundled by this repository's package script. GPL/LGPL depends on the user's build. Sources and build configuration: https://ffmpeg.org/download.html . Used for X11 recording, Windows/macOS video conversion and trimming. No FFmpeg code is copied into Kadrolov.
- **GStreamer/PipeWire/D-Bus**: Linux system dependencies, with their respective LGPL/MIT licenses; not vendored. https://gstreamer.freedesktop.org/ and https://pipewire.org/ .
- **Tesseract4Android and tessdata_fast**: Apache-2.0; Android dependency identities, full license texts and model hashes are in android/licenses/. OCR models are bundled for offline use.
- **Gradle wrapper**: Apache-2.0; https://github.com/gradle/gradle . Android SDK and platform libraries are build tools supplied separately by Google.
- macOS ScreenCaptureKit, AVFoundation, Vision and Windows WASAPI are system APIs.

The reference product's executable, graphics, page scripts, private APIs and user media are not distributed with Kadrolov. Public behavior and documentation informed the requirements; implementation is independent.
