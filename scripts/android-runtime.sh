#!/bin/bash
set +e
cd "$(dirname "$0")/../android" || exit 2
./gradlew --no-daemon --max-workers=2 connectedDebugAndroidTest
result=$?
mkdir -p runtime-artifacts
adb pull /sdcard/Android/data/ru.kadrolov.app/files/runtime-artifacts/ runtime-artifacts/ || true
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
