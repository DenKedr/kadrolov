#!/bin/sh
set -eu
native_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/kadrolov-native-tests.XXXXXX")
trap 'rm -f "$test_binary"' EXIT
xcrun swiftc -swift-version 5 "$native_dir/CaptureCore.swift" "$native_dir/tests/main.swift" -o "$test_binary"
"$test_binary"
