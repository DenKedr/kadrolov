#!/bin/sh
set -eu
native_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$native_dir/../.." && pwd)
output=${1:-"$repo_dir/build/native/kadrolov-capture"}
mkdir -p "$(dirname -- "$output")"
compile() {
  xcrun swiftc -swift-version 5 -O -parse-as-library -target "$1" \
    "$native_dir/CaptureCore.swift" "$native_dir/CaptureMain.swift" "$native_dir/NativeScreenshot.swift" "$native_dir/NativeAudioMixer.swift" \
    -framework AppKit -framework ScreenCaptureKit -framework AVFoundation -framework CoreMedia -framework AudioToolbox -framework Vision -framework ImageIO -framework CoreImage \
    -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker "$native_dir/Info.plist" \
    -o "$2"
}
if [ -n "${KADROLOV_SWIFT_TARGET:-}" ]; then
  compile "$KADROLOV_SWIFT_TARGET" "$output"
elif [ "${KADROLOV_SWIFT_ARCHS:-}" = "arm64 x86_64" ] || [ "${KADROLOV_SWIFT_ARCHS:-}" = "x86_64 arm64" ]; then
  scratch=$(mktemp -d "${TMPDIR:-/tmp}/kadrolov-swift-build.XXXXXX")
  trap 'rm -rf "$scratch"' EXIT
  compile arm64-apple-macos13.0 "$scratch/arm64"
  compile x86_64-apple-macos13.0 "$scratch/x86_64"
  xcrun lipo -create "$scratch/arm64" "$scratch/x86_64" -output "$output"
else
  arch=${KADROLOV_SWIFT_ARCHS:-$(uname -m)}
  case "$arch" in arm64|x86_64) ;; *) echo "Unsupported macOS architecture: $arch" >&2; exit 1 ;; esac
  compile "$arch-apple-macos13.0" "$output"
fi
printf '%s\n' "$output"
