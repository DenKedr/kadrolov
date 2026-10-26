#!/usr/bin/env python3
"""Opt-in real macOS capture smoke. Keeps all recorded user pixels in a temporary local directory."""
import argparse
import json
import select
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('helper', type=Path)
parser.add_argument('--audio', default='none', choices=['none', 'system', 'mic', 'both'])
args = parser.parse_args()
ffprobe = shutil.which('ffprobe')
if not ffprobe:
    raise SystemExit('ffprobe required for integration validation')
helper = args.helper.resolve()
with tempfile.TemporaryDirectory(prefix='kadrolov-capture-smoke-') as temp:
    output = Path(temp) / 'recording.mp4'
    proc = subprocess.Popen([str(helper), '--output', str(output), '--rect', '10,10,800,600',
        '--width', '640', '--height', '480', '--fps', '15', '--audio', args.audio,
        '--cursor', '0', '--clicks', '1'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, bufsize=1)
    events = []
    def event(wanted):
        limit = time.monotonic() + 20
        while time.monotonic() < limit:
            ready, _, _ = select.select([proc.stdout], [], [], max(0, limit-time.monotonic()))
            if not ready:
                break
            line = proc.stdout.readline()
            if not line:
                raise RuntimeError(f'helper exited {proc.poll()}: {proc.stderr.read()}')
            value = json.loads(line)
            events.append(value)
            if value['event'] == 'error':
                raise RuntimeError(value['message'])
            if value['event'] == wanted:
                return value
        raise TimeoutError(f'waiting for {wanted}')
    def command(name):
        proc.stdin.write(json.dumps({'command': name}) + '\n')
        proc.stdin.flush()
    try:
        event('started')
        time.sleep(1.2)
        command('pause')
        event('paused')
        time.sleep(1.4)
        command('resume')
        event('resumed')
        time.sleep(1.2)
        command('stop')
        result = event('finished')
        assert proc.wait(timeout=10) == 0
        probe = json.loads(subprocess.check_output([ffprobe, '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(output)]))
        video = [s for s in probe['streams'] if s['codec_type'] == 'video']
        audio = [s for s in probe['streams'] if s['codec_type'] == 'audio']
        assert len(video) == 1 and video[0]['width'] == 640 and video[0]['height'] == 480
        duration = float(probe['format']['duration'])
        assert 1.7 < duration < 3.3, f'pause not excluded: {duration}'
        assert len(audio) == {'none': 0, 'system': 1, 'mic': 1, 'both': 2}[args.audio], (audio, result)
        ffmpeg = shutil.which('ffmpeg')
        if ffmpeg:
            subprocess.run([ffmpeg, '-v', 'error', '-i', str(output), '-f', 'null', '-'], check=True, capture_output=True)
        print(json.dumps({'passed': True, 'audio': args.audio, 'duration': duration,
            'videoFrames': result['frames'], 'audioTracks': len(audio), 'events': [e['event'] for e in events],
            'recordingKept': False}, ensure_ascii=False))
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
