#!/bin/sh
# Build an installable Debian package from an existing native CMake build.
# Usage: scripts/package-linux.sh BUILD_DIR amd64|arm64|i386|armhf OUTPUT_DIR 5|6
set -eu
build_dir=${1:-build}
architecture=${2:-$(dpkg --print-architecture)}
output_dir=${3:-dist}
qt_major=${4:-6}
version=${KADROLOV_VERSION:-0.1.1}
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "$architecture" in amd64|arm64|i386|armhf) ;; *) echo "Unsupported Debian architecture: $architecture" >&2; exit 2 ;; esac
case "$qt_major" in 5|6) ;; *) echo "Qt major version must be 5 or 6" >&2; exit 2 ;; esac
for command in cmake dpkg dpkg-deb dpkg-shlibdeps md5sum; do
  command -v "$command" >/dev/null || { echo "Missing build tool: $command (install build-essential/dpkg-dev)" >&2; exit 2; }
done
actual_architecture=$(dpkg --print-architecture)
if [ "$actual_architecture" != "$architecture" ]; then
  echo "Package architecture $architecture differs from native build environment $actual_architecture" >&2
  exit 2
fi
dpkg --validate-version "$version"
build_dir=$(CDPATH= cd -- "$build_dir" && pwd)
mkdir -p "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)
archive="$output_dir/kadrolov_${version}_${architecture}.deb"
if [ -e "$archive" ]; then
  echo "Output already exists: $archive" >&2
  exit 2
fi
scratch=$(mktemp -d "${TMPDIR:-/tmp}/kadrolov-deb.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
stage="$scratch/package"
mkdir -p "$stage/DEBIAN" "$scratch/metadata/debian"
DESTDIR="$stage" cmake --install "$build_dir" --prefix /usr
for file in usr/bin/kadrolov usr/bin/portal_capture.py usr/share/applications/kadrolov.desktop usr/share/icons/hicolor/256x256/apps/kadrolov.png usr/share/doc/kadrolov/LICENSE usr/share/doc/kadrolov/THIRD_PARTY.md usr/share/doc/kadrolov/licenses/LGPL-3.0.txt; do
  test -f "$stage/$file" || { echo "Required package file is missing: $file" >&2; exit 1; }
done
# Let Debian's shlibs/symbols metadata determine actual Qt, glibc and C++ ABI requirements.
# This avoids labelling an Ubuntu-built ELF as installable against older Debian libraries.
cat > "$scratch/metadata/debian/control" <<EOF_CONTROL
Source: kadrolov
Section: graphics
Priority: optional
Maintainer: Kadrolov contributors <195293092+DenKedr@users.noreply.github.com>

Package: kadrolov
Architecture: $architecture
Description: Local screenshots and screen recording
EOF_CONTROL
shared_dependencies=$(cd "$scratch/metadata" && dpkg-shlibdeps -O -e "$stage/usr/bin/kadrolov")
shared_dependencies=$(printf '%s\n' "$shared_dependencies" | sed -n 's/^shlibs:Depends=//p')
if [ -z "$shared_dependencies" ]; then
  echo "dpkg-shlibdeps did not produce dependency metadata" >&2
  exit 1
fi
if [ "$qt_major" = 6 ]; then
  image_plugins="qt6-image-formats-plugins, qt6-qpa-plugins"
else
  image_plugins="qt5-image-formats-plugins"
fi
installed_size=$(du -sk "$stage/usr" | awk '{print $1}')
cat > "$stage/DEBIAN/control" <<EOF_CONTROL
Package: kadrolov
Version: $version
Section: graphics
Priority: optional
Architecture: $architecture
Maintainer: Kadrolov contributors <195293092+DenKedr@users.noreply.github.com>
Installed-Size: $installed_size
Depends: $shared_dependencies, $image_plugins, ffmpeg, python3
Recommends: tesseract-ocr, tesseract-ocr-rus, tesseract-ocr-eng, python3-dbus, python3-gi, gir1.2-gstreamer-1.0, gstreamer1.0-pipewire, gstreamer1.0-plugins-good, gstreamer1.0-plugins-bad, gstreamer1.0-libav, xdg-desktop-portal
Homepage: https://github.com/DenKedr/kadrolov
Description: Local screenshots and screen recording with a Russian interface
 Select a screen area, annotate images and save screenshots or videos locally.
 Supports configurable shortcuts, a recording control panel and optional audio.
 No account, server or cloud service is required.
EOF_CONTROL
cat "$repo_dir/LICENSE" "$repo_dir/THIRD_PARTY.md" > "$stage/usr/share/doc/kadrolov/copyright"
chmod 0755 "$stage/DEBIAN"
chmod 0644 "$stage/DEBIAN/control"
(cd "$stage" && find usr -type f -print0 | LC_ALL=C sort -z | xargs -0 md5sum > DEBIAN/md5sums)
dpkg-deb --root-owner-group --build "$stage" "$archive"
dpkg-deb --info "$archive"
printf 'Created %s\n' "$archive"
