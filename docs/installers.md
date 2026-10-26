# Установочные пакеты

Исходники упаковки не заменяют готовые пакеты: результаты сборки и испытания установщиков указываются в общем [отчёте](verification.md). Скрипты не загружают пользовательские файлы и не включают автозахват.

## Windows: EXE

Установщик Inno Setup устанавливает полную папку приложения с Qt и нативными модулями в `%LOCALAPPDATA%\Programs\Kadrolov`. Права администратора не нужны. Создаётся пункт меню «Пуск», значок рабочего стола предлагается отдельно. Автозапуск по умолчанию выключен; его переключатель находится в приложении.

Удаление доступно в системном списке приложений. Оно сохраняет настройки, историю, снимки и записи. Команда автозапуска удаляется только если указывает на удаляемую установленную копию; другую portable-копию установщик не меняет.

Для сборки нужны готовый `stage` после `windeployqt` и Inno Setup 6.3 или новее. Пример из корня репозитория:

```powershell
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" `
  "/DStageDir=$((Resolve-Path stage).Path)" `
  "/DOutputDir=$PWD\dist" `
  /DTargetArch=x64 packaging/windows/kadrolov.iss
```

`TargetArch`: `x64`, `x86` или `arm64`. Получается `kadrolov-windows-ARCH-setup.exe`. ARM64-пакет требует Windows 11, x64/x86 — Windows 10 1809 или новее. Инсталлятор проверяет подходящую архитектуру. На отдельном CI-компьютере возможны автоматическая установка `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-` и удаление через `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`. Это не запускает запись.

Архитектурные идентификаторы и установка без повышения прав соответствуют официальной документации [Inno Setup](https://jrsoftware.org/ishelp/topic_setup_architecturesallowed.htm), [режима установки](https://jrsoftware.org/ishelp/topic_setup_architecturesinstallin64bitmode.htm) и [PrivilegesRequired](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm).

## Linux: DEB

```sh
sudo apt install ./kadrolov_0.1.0_amd64.deb
```

Система устанавливает обязательные зависимости. Рекомендуемые пакеты добавляют локальный OCR и компоненты Wayland. Файлы приложения попадают в `/usr/bin`, значок — в системное меню. Qt и FFmpeg остаются заменяемыми системными библиотеками/программами.

`amd64` и `arm64` CI собираются на Ubuntu 24.04 с Qt6. `i386` и `armhf` — на Debian 12 с Qt5. Требования к ABI определяются реальным ELF через `dpkg-shlibdeps`: пакет, собранный на новой Ubuntu, не объявляется совместимым со старыми библиотеками Debian. Проверка на других дистрибутивах проводится отдельно.

```sh
# Нужны CMake, dpkg-dev, binutils и уже собранная нативная программа.
scripts/package-linux.sh build amd64 dist 6
# В Debian i386/ARMv7 окружении:
scripts/package-linux.sh build-legacy i386 dist 5
scripts/package-linux.sh build-legacy armhf dist 5
```

Архитектура параметра должна совпадать с `dpkg --print-architecture` окружения сборки. Скрипт создаёт новый `kadrolov_VERSION_ARCH.deb`, добавляет README/лицензии, зависимости, контрольные суммы и root-владение. Существующий выходной файл не перезаписывается. `KADROLOV_VERSION` позволяет задать версию; по умолчанию `0.1.0`.

Проверка пакета без установки:

```sh
dpkg-deb --info dist/kadrolov_0.1.0_amd64.deb
dpkg-deb --contents dist/kadrolov_0.1.0_amd64.deb
```

Удаление `sudo apt remove kadrolov` не удаляет домашние настройки и медиа. После удаления можно отключить оставшуюся пользовательскую запись автозапуска в настройках окружения; установочный пакет не перебирает домашние папки пользователей.
