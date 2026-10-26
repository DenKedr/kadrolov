#!/usr/bin/env python3
"""Real capture regression: keep a small region static across a long pause/resume.

No UI is driven. Test media is local temporary data and is removed after verification.
"""
import argparse
import json
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time

p = argparse.ArgumentParser()
p.add_argument('helper', type=Path)
p.add_argument('--rect', default='10,40,300,200')
p.add_argument('--exclude-pid', default='')
p.add_argument('--window', default='')
a = p.parse_args()
with tempfile.TemporaryDirectory(prefix='kadrolov-sparse-pause-') as tmp:
    file = Path(tmp)/'static.mp4'
    command = [str(a.helper.resolve()), '--output', str(file), '--rect', a.rect, '--width', '600', '--height', '400',
        '--fps', '30', '--audio', 'none', '--cursor', '0', '--clicks', '0']
    if a.window:
        command += ['--window', a.window]
    if a.exclude_pid:
        command += ['--exclude-pid', a.exclude_pid]
    proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
    events = []
    def get_event(wanted, timeout=20):
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            ready,_,_ = select.select([proc.stdout],[],[], max(0,deadline-time.monotonic()))
            if not ready:
                break
            line = proc.stdout.readline()
            if not line:
                raise RuntimeError(f'helper exited {proc.poll()}: {proc.stderr.read()}')
            event = json.loads(line)
            events.append(event)
            if event['event'] == 'error':
                raise RuntimeError(json.dumps(event, ensure_ascii=False))
            if event['event'] == wanted:
                return event
        raise TimeoutError(wanted)
    def send(name):
        proc.stdin.write(json.dumps({'command':name})+'\n');proc.stdin.flush()
    try:
        get_event('started')
        time.sleep(6)
        send('pause');get_event('paused')
        time.sleep(2)
        send('resume');get_event('resumed')
        time.sleep(5)
        send('stop');finished=get_event('finished')
        assert proc.wait(timeout=10)==0
        probe=json.loads(subprocess.check_output([shutil.which('ffprobe'),'-v','error','-show_frames','-show_streams','-show_format','-of','json',str(file)]))
        pts=[float(v['best_effort_timestamp_time']) for v in probe['frames'] if v.get('media_type') == 'video']
        assert all(b>a for a,b in zip(pts,pts[1:])), f'Non-monotonic video PTS: {pts}'
        duration=float(probe['format']['duration'])
        assert 10.0 < duration < 12.0, (duration, finished)
        subprocess.run([shutil.which('ffmpeg'),'-v','error','-i',str(file),'-f','null','-'],check=True,capture_output=True)
        print(json.dumps({'passed':True,'duration':duration,'frames':finished['frames'],'monotonicVideoPTS':True,
            'bFrames':probe['streams'][0].get('has_b_frames'),'events':[v['event'] for v in events],'recordingKept':False}))
    finally:
        if proc.poll() is None:
            proc.kill();proc.wait()
