#!/bin/bash
set +e
cd "$(dirname "$0")/../android" || exit 2

collect_runtime_artifacts() {
  # UTP can uninstall the app before Gradle returns; test watcher preserves its evidence here.
  # Preserve the original Gradle/report status even if a device has already disconnected.
  local result=$?
  trap - EXIT
  mkdir -p runtime-artifacts
  adb pull /data/local/tmp/kadrolov-runtime/ runtime-artifacts/ || true
  adb pull /sdcard/Android/data/ru.kadrolov.app/files/runtime-artifacts/ runtime-artifacts/app-external/ || true
  adb shell screencap -p /data/local/tmp/kadrolov-final-screen.png || true
  adb pull /data/local/tmp/kadrolov-final-screen.png runtime-artifacts/final-screen.png || true
  adb shell uiautomator dump /data/local/tmp/kadrolov-final-window.xml || true
  adb pull /data/local/tmp/kadrolov-final-window.xml runtime-artifacts/final-window.xml || true
  adb logcat -d -t 3000 > runtime-artifacts/final-logcat.txt || true
  adb shell dumpsys activity activities > runtime-artifacts/final-activities.txt || true
  adb shell dumpsys window > runtime-artifacts/final-windows.txt || true
  exit "$result"
}
trap collect_runtime_artifacts EXIT
./gradlew --no-daemon --max-workers=2 connectedDebugAndroidTest
result=$?
if [ "$result" -eq 0 ]; then
python3 - <<'PY'
from pathlib import Path
import xml.etree.ElementTree as E
roots=[E.parse(p).getroot() for p in Path('app/build/outputs/androidTest-results/connected').rglob('TEST-*.xml')]
tests=sum(int(r.attrib.get('tests','0')) for r in roots)
skips=sum(int(r.attrib.get('skipped','0')) for r in roots)
assert tests>=2 and skips==0, f'Runtime checks not executed: tests={tests}, skipped={skips}'
print(f'Runtime tests actually executed: {tests}; skipped: {skips}')
PY
result=$?
fi
exit "$result"
