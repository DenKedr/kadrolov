#!/bin/sh
# Sign an existing unsigned release APK with the owner's persistent app identity.
set -eu

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 unsigned-release.apk signed-release.apk" >&2
  exit 2
fi

: "${KADROLOV_ANDROID_KEYSTORE:?Set the path to the persistent release keystore}"
: "${KADROLOV_ANDROID_PASSWORD_FILE:?Set the path to its password file}"
sdk_dir=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$sdk_dir" ]; then
  echo 'Set ANDROID_HOME or ANDROID_SDK_ROOT to the Android SDK directory.' >&2
  exit 2
fi
tools_dir="$sdk_dir/build-tools/${KADROLOV_ANDROID_BUILD_TOOLS_VERSION:-35.0.0}"
zipalign="$tools_dir/zipalign"
apksigner="$tools_dir/apksigner"
for tool in "$zipalign" "$apksigner"; do
  if [ ! -x "$tool" ]; then
    echo "Android build tool is missing: $tool" >&2
    exit 2
  fi
done
if [ ! -f "$1" ] || [ ! -r "$1" ]; then
  echo 'The unsigned input APK is not readable.' >&2
  exit 2
fi
if [ ! -f "$KADROLOV_ANDROID_KEYSTORE" ] || [ ! -r "$KADROLOV_ANDROID_KEYSTORE" ]; then
  echo 'The release keystore is not readable.' >&2
  exit 2
fi
if [ ! -f "$KADROLOV_ANDROID_PASSWORD_FILE" ] || [ ! -s "$KADROLOV_ANDROID_PASSWORD_FILE" ] || [ ! -r "$KADROLOV_ANDROID_PASSWORD_FILE" ]; then
  echo 'The release password file is missing, empty or unreadable.' >&2
  exit 2
fi
if [ -e "$2" ] || [ -L "$2" ]; then
  echo 'The output already exists; choose a new output filename.' >&2
  exit 2
fi
output_dir=$(dirname -- "$2")
if [ ! -d "$output_dir" ]; then
  echo 'The output directory does not exist.' >&2
  exit 2
fi
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)
output_path="$output_dir/$(basename -- "$2")"

# Keep temporary APKs on the output filesystem; publish only after both checks pass.
scratch=$(mktemp -d "$output_dir/.kadrolov-android-package.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
"$zipalign" -P 16 -f 4 "$1" "$scratch/aligned.apk"
"$apksigner" sign --ks "$KADROLOV_ANDROID_KEYSTORE" --ks-key-alias kadrolov \
  --ks-pass "file:$KADROLOV_ANDROID_PASSWORD_FILE" \
  --out "$scratch/signed.apk" "$scratch/aligned.apk" < /dev/null
"$apksigner" verify --verbose "$scratch/signed.apk"
"$zipalign" -c -P 16 4 "$scratch/signed.apk"
mv -n "$scratch/signed.apk" "$output_path"
if [ -f "$scratch/signed.apk" ]; then
  echo 'The output appeared during signing; it was not replaced.' >&2
  exit 1
fi
echo "Signed and verified APK: $2"
