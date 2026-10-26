#!/usr/bin/env python3
"""Static APK integrity evidence; explicitly does not claim Android runtime verification."""
import argparse
import hashlib
import json
from pathlib import Path
from zipfile import ZipFile
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--apk', type=Path, default=root / 'app/build/outputs/apk/debug/app-debug.apk')
parser.add_argument('--output', type=Path, default=root / 'tests/verification.json')
args = parser.parse_args()
apk = args.apk.resolve()
provenance = json.loads((root / 'licenses/dependency-provenance.json').read_text())
with ZipFile(apk) as z:
    names = z.namelist()
    abis = sorted({n.split('/')[1] for n in names if n.startswith('lib/') and n.endswith('.so')})
    assert set(abis) == {'arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64'}, abis
    for lang in ('rus', 'eng'):
        data = z.read(f'assets/tessdata/{lang}.traineddata')
        assert hashlib.sha256(data).hexdigest() == provenance['tessdata'][lang]['sha256']
    assert 'assets/licenses/Kadrolov-MIT.txt' in names
    assert len([n for n in names if n.startswith('assets/licenses/') and n.endswith('.txt')]) >= 7
    assert z.testzip() is None
lint_file = root / 'app/build/reports/lint-results-debug.xml'
lint = ET.parse(lint_file).getroot()
counts = {level: sum(x.attrib.get('severity') == level for x in lint.findall('issue'))
          for level in ('Error', 'Warning', 'Fatal')}
assert counts['Error'] == 0 and counts['Fatal'] == 0, counts
tests = []
for file in sorted((root / 'app/build/test-results/testDebugUnitTest').glob('TEST-*.xml')):
    r = ET.parse(file).getroot()
    tests.append({k: r.attrib[k] for k in ('name', 'tests', 'failures', 'errors')})
assert sum(int(t['tests']) for t in tests) >= 6
assert all(t['failures'] == '0' and t['errors'] == '0' for t in tests)
report = {
    'status': 'built_and_static_checked',
    'runtime_device_tested': False,
    'apk': 'android/app/build/outputs/apk/debug/app-debug.apk',
    'apk_bytes': apk.stat().st_size,
    'apk_sha256': hashlib.sha256(apk.read_bytes()).hexdigest(),
    'abis': abis,
    'lint': counts,
    'unit_tests': tests,
    'standalone_smoke_assertions': 12,
    'notes': [
        'No emulator or physical device was used in this task',
        'MediaProjection/audio/camera/EGL/overlay still require device testing',
        'APK dependencies include all four native OCR ABIs and verified rus/eng models',
        'Boot receiver only prepares an optional shortcut notification; no automatic capture',
    ],
}
args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
print(json.dumps(report, indent=2, ensure_ascii=False))
