# macOS: нативный захват Кадролова

Модуль Swift использует ScreenCaptureKit и AVAssetWriter, работает локально и ничего не отправляет в сеть. Минимальная версия — macOS 13. Системный звук поддерживается с macOS 13; нативные круги кликов и выбор конкретного микрофона — с macOS 15. На 13/14 микрофон записывается AVAudioEngine через системное устройство по умолчанию. Курсор и круги независимы; круги оформляет сама macOS.

## Сборка

```sh
native/macos/build.sh
# По умолчанию архитектура текущего Mac, macOS deployment target 13.0.
KADROLOV_SWIFT_ARCHS='arm64 x86_64' native/macos/build.sh build/native/kadrolov-capture-universal
```

Нужны Xcode Command Line Tools и SDK с ScreenCaptureKit macOS 15 API. Никаких скачиваний моделей или библиотек при запуске. Usage descriptions камеры/микрофона встроены в Mach-O helper; основное `.app` должно иметь собственные соответствующие поля Info.plist. При упаковке helper нужно подписать вместе с приложением. Разрешения TCC могут зависеть от запускающего приложения; разрешение из Terminal/Codex не доказывает разрешение `.app`.

## Протокол

```sh
kadrolov-capture --check-permission
kadrolov-capture --list-displays
kadrolov-capture --list-windows
kadrolov-capture --screenshot /path/screenshot.png --cursor 0
kadrolov-capture --ocr /path/screenshot.png
kadrolov-capture --output /path/recording.mp4 --rect 100,100,800,600 \
  --width 1280 --height 960 --fps 30 --quality 23 \
  --audio both --cursor 1 --clicks 1 --exclude-pid 1234
```

`--display` принимает CGDirectDisplayID из `--list-displays`. Без него монитор подбирается по области, а если область не задана — основной монитор. `--window` выбирает SCWindow ID из `--list-windows`, а не расширяет захват на весь экран. `--exclude-pid` исключает окна основного приложения, включая мини-панель; иначе она могла бы попасть в запись.

`--rect x,y,w,h` задаётся **в глобальных логических точках Qt/macOS**, начало координат в левом верхнем углу основного экрана, отрицательные координаты разрешены. Модуль вычитает CGDisplayBounds(display).origin и передаёт локальный прямоугольник в SCStreamConfiguration.sourceRect. Область должна целиком помещаться в одном выбранном мониторе; несовпадение отклоняется, а не подменяется другим монитором. Исходный размер переводится в пиксели с масштабом выбранного дисплея. Ширина/высота видео округляются вниз до чётных; снимок сохраняет точный размер.

Режимы `--audio`: `none`, `system`, `mic`, `both`. `none` создаёт файл без аудиодорожки. `both` создаёт **две отдельные AAC-дорожки**, которые основной Recorder смешивает нативной командой `--mix-audio RAW.mp4 --output FINAL.mp4` перед выдачей конечного файла. Это обязательно: многие проигрыватели выбирают только одну из двух дорожек. NativeAudioMixer использует AVAssetReaderAudioMixOutput → AAC, сохраняя compressed H.264 packets без повторного кодирования; FFmpeg для этого и для обычной записи MP4 не требуется. Если выбран другой контейнер/кодек, преобразование остаётся задачей основного Recorder. `--microphone DEVICE_ID` — AVCaptureDevice.uniqueID (macOS 15+). Устройство не заменяется незаметно.

`--camera 1` добавляет камеру в правый нижний угол, `--camera-device DEVICE_ID` выбирает конкретную AVCaptureDevice. Нужны разрешение и доступная камера. Камера запрашивается только после явного включения. Наложение CoreImage записывается в сами видеокадры; камера выключается при остановке.

В stdin идут строки JSON:

```json
{"command":"pause"}
{"command":"resume"}
{"command":"stop"}
```

В stdout идут только строки JSON: `started`, `paused`, `resumed`, `finished`, `error` (и `warning` для неверной команды). `started` содержит размеры, фактические параметры и плановое число аудиодорожек. `finished` содержит `path`, `duration`, `frames`, фактическое `audioTracks` и числа буферов звука. Отсутствие запрошенных аудиобуферов считается ошибкой, а не успешной записью со звуком. EOF stdin и SIGTERM завершают захват, чтобы при падении контроллера не осталось невидимой записи.

Пауза вычитается из временной шкалы всех дорожек. Опоздавшие буферы из паузы и немонотонные метки отбрасываются. Статический экран дополняется последним кадром до времени остановки. MP4 пишется фрагментами по 2 секунды для возможности восстановления при прерывании; штатное завершение всё равно обязательно. При ошибке файл не объявляется готовым и может потребовать восстановления.

`--screenshot` сохраняет PNG через SCScreenshotManager на macOS 14+, на 13 — первый полноценный SCStream кадр. `--ocr` выполняет offline Vision распознавание ru-RU/en-US и возвращает `{event:ocr,text,languages}`. Пользовательский текст не включается в журналы основной программы и не уходит провайдерам.

## Проверки

```sh
native/macos/tests/run.sh
python3 native/macos/tests/integration.py build/native/kadrolov-capture --audio none
python3 native/macos/tests/integration.py build/native/kadrolov-capture --audio system
python3 native/macos/tests/integration.py build/native/kadrolov-capture --audio mic
python3 native/macos/tests/integration.py build/native/kadrolov-capture --audio both
python3 native/macos/tests/test_mix.py build/native/kadrolov-capture
```

На macOS 15.6.1 ARM64 прошли unit-проверки паузы, поздних/переставленных буферов, геометрии отрицательного монитора и CLI. Реальные записи во всех четырёх режимах прошли `ffprobe` и полное декодирование FFmpeg; пауза 1.4 с исключена из длительности. Тестовые записи остаются только во временном локальном каталоге и удаляются после проверки. На синтетическом двухтональном MP4 дополнительно проверены native mix: одна AAC-дорожка, обе частоты 440/880 Гц присутствуют, compressed video packets идентичны исходным; FFmpeg используется только для создания тестового файла/проверки. Проверены screenshot с видимым содержимым окна и Vision OCR искусственного русско-английского изображения. Цвет/позиция native-кругов отдельно визуально не проверялись; камера, macOS 13/14, Intel и несколько мониторов на устройствах не проверялись.

### Opt-in диагностика разрешения

`--check-permission` остаётся пассивной проверкой без SCK-запроса. Комбинация `--check-permission --probe-content` дополнительно вызывает SCShareableContent и может показать системный запрос; запускайте её только по явному диагностическому действию. В JSON возвращаются количества экранов/окон либо NSError domain/code, preflight до/после и PID/parentPID. Снимки и названия окон не собираются. Проверку нужно выполнять из реального GUI → QProcess → helper, поскольку успешный запуск из Codex/Terminal не доказывает разрешение самостоятельного приложения.
