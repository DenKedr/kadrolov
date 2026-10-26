#!/usr/bin/env python3
"""Two synthetic audio tones verify actual summing, not merely a track count."""
import array
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

helper = str(Path(sys.argv[1]).resolve())
ffmpeg = shutil.which('ffmpeg')
ffprobe = shutil.which('ffprobe')
assert ffmpeg and ffprobe, 'Test fixture/decode requires FFmpeg, production mixer does not.'
with tempfile.TemporaryDirectory(prefix='kadrolov-mix-test-') as temp:
    raw = str(Path(temp) / 'two-tracks.mp4')
    mixed = str(Path(temp) / 'mixed.mp4')
    subprocess.run([ffmpeg, '-v', 'error', '-f', 'lavfi', '-i', 'color=c=orange:s=320x240:r=15',
        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
        '-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000',
        '-map', '0:v', '-map', '1:a', '-map', '2:a', '-t', '2', '-c:v', 'libx264', '-c:a', 'aac', raw], check=True)
    result = subprocess.run([helper, '--mix-audio', raw, '--output', mixed], check=True, capture_output=True, text=True, timeout=30)
    event = json.loads(result.stdout)
    assert event['event'] == 'finished' and event['audioTracks'] == 1 and event['videoPassthrough']
    probe = json.loads(subprocess.check_output([ffprobe, '-v', 'error', '-show_streams', '-show_format', '-of', 'json', mixed]))
    assert len([s for s in probe['streams'] if s['codec_type'] == 'audio']) == 1
    assert abs(float(probe['format']['duration']) - 2.0) < .12
    data = subprocess.check_output([ffmpeg, '-v', 'error', '-i', mixed, '-map', '0:a', '-ac', '1', '-ar', '48000', '-f', 'f32le', '-'])
    pcm = array.array('f')
    pcm.frombytes(data)
    pcm = pcm[12000:60000]
    rms = math.sqrt(sum(x*x for x in pcm)/len(pcm))
    def amplitude(frequency):
        real = sum(x*math.cos(2*math.pi*frequency*n/48000) for n,x in enumerate(pcm))
        imag = sum(x*math.sin(2*math.pi*frequency*n/48000) for n,x in enumerate(pcm))
        return 2*math.hypot(real,imag)/len(pcm)
    tones = [amplitude(440), amplitude(880)]
    assert rms > .03 and min(tones) > .025, (rms, tones)
    # Compressed video packets pass through byte-for-byte; native mixing never re-encodes them.
    hashes = []
    for name in [raw, mixed]:
        hashes.append(subprocess.check_output([ffmpeg, '-v', 'error', '-i', name, '-map', '0:v', '-c', 'copy', '-f', 'hash', '-hash', 'sha256', '-']).decode().strip())
    assert hashes[0] == hashes[1], hashes
    print(json.dumps({'passed':True,'audioTracks':1,'rms':round(rms,5),'toneAmplitudes':[round(v,5) for v in tones],
        'videoPacketsIdentical':True,'productionNeedsFFmpeg':False}))
