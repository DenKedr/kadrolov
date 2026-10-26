# Wayland: локальный системный портал

`portal_capture.py` использует официальные XDG ScreenCast/Screenshot portals, PipeWire и GStreamer. Он не применяет X11-захват к Wayland и не получает root-доступ. Источник выбирает пользователь в системном диалоге окружения. Разрешения или закрытие сессии не обходятся.

На Debian/Ubuntu нужны системные пакеты (точные названия зависят от дистрибутива):

```sh
sudo apt install python3-dbus python3-gi gir1.2-gstreamer-1.0 \
  gstreamer1.0-pipewire gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav \
  xdg-desktop-portal pulseaudio-utils
```

Также должен быть установлен portal backend для вашего окружения (GNOME/KDE/wlr и т. п.), а PipeWire — запущен в пользовательской сессии. Программа сама не меняет compositor, firewall, системные разрешения или desktop session. Устанавливать все backend одновременно не требуется.

Пример:

```sh
/usr/bin/python3 native/linux/portal_capture.py --capabilities
/usr/bin/python3 native/linux/portal_capture.py --screenshot /path/screenshot.png
/usr/bin/python3 native/linux/portal_capture.py --output /path/video.mp4 \
  --width 1280 --height 720 --fps 30 --audio both --cursor 1
```

Форматы: MP4/MKV (H.264/AAC) и WebM (VP8/Opus). Наличие кодировщика проверяется перед началом записи; silent fallback на другой формат отсутствует. Запрошенный системный звук записывается из monitor-source PulseAudio/PipeWire-Pulse; default sink определяется `pactl get-default-sink`, можно задать `--system-device SOURCE`. Для микрофона `--microphone SOURCE` выбирает Pulse source. Два аудиоисточника смешиваются до кодирования в одну дорожку. Режим `none` не создаёт аудиодорожку.

Камера включается через `--camera 1`, устройство выбирается `--camera-device /dev/video0` (или локальная ссылка на V4L2-устройство). Нужны GStreamer `v4l2src` и `compositor`; камера добавляется в правый нижний угол. Устройство передаётся как свойство GStreamer, без shell-интерполяции. Ошибка камеры останавливает запись и не заменяет её другой камерой. Эта ветка runtime не проверялась.

Курсор скрывается или включается самим порталом (`cursor_mode` hidden/embedded). **Глобальные круги кликов этим Wayland-helper не реализованы**: включение подсветки отвергается с понятной ошибкой до записи. Нельзя утверждать, что ScreenCast portal предоставляет события глобальных кликов. Современные платформы могут ограничивать другие источники/размеры.

`--rect x,y,w,h` — глобальная область в логических координатах desktop. Она применяется только после выбора содержащего её monitor-stream и проверки portal metadata position/size. Если compositor не сообщает положение, область отвергается; не происходит захвата другого монитора. При обрезке PipeWire изображение сначала приводится к логическому размеру, поэтому для HiDPI сохранение нативной плотности в выбранной области пока не реализовано; явно выбранные width/height работают после обрезки.

stdin принимает отдельные строки JSON `{"command":"pause"}`, `resume`, `stop`. stdout содержит JSON-события `started`, `paused`, `resumed`, `finished`, `error`. GStreamer PAUSED/PLAYING сохраняет running time; на остановке отправляется EOS и ожидается закрытие контейнера. EOF контроллера закрывает сессию. MP4 создаётся фрагментами по две секунды; незавершённый файл не объявляется готовым. Звуковые устройства не заменяются незаметно.

Screenshot portal открывает собственный выбор снимка и возвращает локальный PNG. Приложение копирует его в выбранную папку; состояние выбора курсора/прямоугольника для скриншота определяется самим portal backend. Режим окна/экрана/области и отображение курсора в его системном диалоге могут отличаться по окружениям.

## Проверки и фактические ограничения

На Mac выполнены `py_compile` и восемь unit-тестов безопасности геометрии и выбора камеры (отрицательный монитор, границы области, неподходящий источник). Linux D-Bus/PipeWire/GStreamer runtime здесь отсутствует, поэтому **реальный Wayland-захват, звук, пауза и экспорт не проверялись на устройстве**. Наличие исходников и CI-проверки синтаксиса не считается подтверждением runtime.

```sh
python3 -m unittest discover -s native/linux/tests -v
python3 -m py_compile native/linux/portal_capture.py
```

Выпуск Wayland-сборки требует реальной проверки в поддерживаемом окружении и соответствующей отметки в общей матрице возможностей.
