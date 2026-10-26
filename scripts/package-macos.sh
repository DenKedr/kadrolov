#!/bin/sh
set -eu
build_dir=${1:-build}
archive=${2:-kadrolov-macos-arm64.zip}
disk_image=${3:-}
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
if [ -n "$disk_image" ]; then
  dmg_stage=$(mktemp -d "${TMPDIR:-/tmp}/kadrolov-dmg.XXXXXX")
  trap 'rm -rf "$dmg_stage"' EXIT HUP INT TERM
  ditto "$app" "$dmg_stage/kadrolov.app"
  ln -s /Applications "$dmg_stage/Applications"
  hdiutil create -volname "Кадролов" -srcfolder "$dmg_stage" -ov -format UDZO "$disk_image"
  hdiutil verify "$disk_image"
  shasum -a 256 "$disk_image"
fi
