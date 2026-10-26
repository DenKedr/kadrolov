#!/bin/sh
set -eu
build_dir=${1:-build}
archive=${2:-kadrolov-macos-arm64.zip}
app="$build_dir/kadrolov.app"
qt_dir=${QT_BASE_DIR:-$(brew --prefix qtbase)}
"$qt_dir/bin/macdeployqt" "$app" -always-overwrite
mkdir -p "$app/Contents/Resources/licenses"
cp LICENSE THIRD_PARTY.md "$app/Contents/Resources/licenses/"
cp licenses/*.txt "$app/Contents/Resources/licenses/"
codesign --force --deep --sign "${KADROLOV_SIGN_IDENTITY:--}" "$app"
codesign --verify --deep --strict "$app"
ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
shasum -a 256 "$archive"
