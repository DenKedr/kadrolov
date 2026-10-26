#!/usr/bin/env python3
"""Local Wayland ScreenCast/Screenshot portal helper. No network, no privileged input hooks."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import sys
import threading
import uuid
from urllib.parse import unquote, urlparse


class CaptureError(Exception):
    pass


def emit(event: str, **values) -> None:
    print(json.dumps({'event': event, **values}, ensure_ascii=False), flush=True)


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--screenshot', type=Path)
    parser.add_argument('--capabilities', action='store_true')
    parser.add_argument('--rect', help='global logical x,y,width,height; selected portal stream must contain it')
    parser.add_argument('--width', type=int, default=0)
    parser.add_argument('--height', type=int, default=0)
    parser.add_argument('--fps', type=int, default=30)
    parser.add_argument('--quality', type=int, default=23)
    parser.add_argument('--audio', choices=['none', 'system', 'mic', 'both'], default='none')
    parser.add_argument('--cursor', choices=['0', '1'], default='1')
    parser.add_argument('--clicks', choices=['0', '1'], default='0')
    parser.add_argument('--camera', choices=['0', '1'], default='0')
    parser.add_argument('--camera-device', default='/dev/video0')
    parser.add_argument('--microphone', default='')
    parser.add_argument('--system-device', default='')
    args = parser.parse_args(argv)
    if not args.capabilities and not args.output and not args.screenshot:
        parser.error('--output or --screenshot is required')
    if not 1 <= args.fps <= 60 or not 0 <= args.quality <= 51:
        parser.error('FPS must be 1...60 and quality 0...51')
    if not 0 <= args.width <= 16384 or not 0 <= args.height <= 16384 or bool(args.width) != bool(args.height):
        parser.error('Width and height must be specified together, between 2 and 16384')
    if args.width and (args.width < 2 or args.height < 2):
        parser.error('Video dimensions must be at least 2')
    if args.rect:
        try:
            parts = [int(n) for n in args.rect.split(',')]
            if len(parts) != 4 or parts[2] < 2 or parts[3] < 2:
                raise ValueError()
            args.rect = tuple(parts)
        except ValueError:
            parser.error('Region must be x,y,width,height with dimensions at least 2')
    target = args.screenshot or args.output
    if target and target.exists():
        parser.error('Output file already exists; it will not be overwritten')
    if target and not target.parent.is_dir():
        parser.error('Output directory does not exist')
    return args


def crop_geometry(region, stream_position, stream_size):
    """Reject a mismatched source rather than widening a user's chosen capture."""
    width, height = map(int, stream_size)
    if width < 2 or height < 2:
        raise CaptureError('Портал не сообщил подходящий размер источника.')
    if region is None:
        return (0, 0, width, height)
    x, y, w, h = region
    left, top = x - int(stream_position[0]), y - int(stream_position[1])
    if left < 0 or top < 0 or left + w > width or top + h > height:
        raise CaptureError('Выбранный в системном диалоге источник не содержит указанную область. Выберите тот же монитор или задайте область заново.')
    return (left, top, w, h)


def validated_camera_path(value):
    import re
    resolved = str(Path(value).resolve())
    if not re.fullmatch(r'/dev/video[0-9]+', resolved):
        raise CaptureError('Выберите локальное V4L2-устройство /dev/videoN или ссылку на него.')
    return resolved


def load_runtime():
    try:
        import dbus
        import dbus.mainloop.glib
        import gi
        gi.require_version('Gst', '1.0')
        from gi.repository import Gst, GLib
    except (ImportError, ValueError) as exc:
        raise CaptureError('Для Wayland нужны python3-dbus, python3-gi, GStreamer/PipeWire и xdg-desktop-portal. Подробности — native/linux/README.md.') from exc
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    Gst.init(None)
    return dbus, Gst, GLib


class PortalCapture:
    def __init__(self, args, runtime):
        self.args = args
        self.dbus, self.Gst, self.GLib = runtime
        self.bus = self.dbus.SessionBus()
        self.portal = self.bus.get_object('org.freedesktop.portal.Desktop', '/org/freedesktop/portal/desktop')
        self.loop = self.GLib.MainLoop()
        self.session = None
        self.request_handle = None
        self.receivers = []
        self.pipewire_fd = None
        self.pipeline = None
        self.state = 'starting'
        self.failed = False
        self.start_announced = False
        self.stopping = False
        self.stop_timeout = None
        self.stream_size = None
        self.output_dimensions = None
        self.active_audio = 0

    def request(self, interface, name, positional, options, callback):
        token = 'kadrolov_' + uuid.uuid4().hex
        sender = self.bus.get_unique_name().lstrip(':').replace('.', '_')
        expected = '/org/freedesktop/portal/desktop/request/' + sender + '/' + token
        options = {**options, 'handle_token': self.dbus.String(token)}
        holder = {}
        def response(code, data):
            receiver = holder.get('receiver')
            if receiver:
                receiver.remove()
            self.request_handle = None
            if self.stopping:
                return
            if int(code) != 0:
                self.fail('Системный выбор захвата отменён.' if int(code) == 1 else 'Портал не разрешил захват экрана.')
                return
            try:
                callback(data)
            except Exception as exc:
                self.fail(str(exc))
        receiver = self.bus.add_signal_receiver(response, signal_name='Response',
            dbus_interface='org.freedesktop.portal.Request', bus_name='org.freedesktop.portal.Desktop', path=expected)
        holder['receiver'] = receiver
        self.receivers.append(receiver)
        method = self.portal.get_dbus_method(name, interface)
        self.request_handle = str(method(*positional, self.dbus.Dictionary(options, signature='sv')))
        if self.request_handle != expected:
            receiver.remove()
            receiver = self.bus.add_signal_receiver(response, signal_name='Response',
                dbus_interface='org.freedesktop.portal.Request', bus_name='org.freedesktop.portal.Desktop', path=self.request_handle)
            holder['receiver'] = receiver
            self.receivers.append(receiver)

    def start(self):
        if self.args.clicks == '1':
            raise CaptureError('Wayland ScreenCast portal не предоставляет глобальные события кликов. Выключите подсветку кликов.')
        if self.args.screenshot:
            self.request('org.freedesktop.portal.Screenshot', 'Screenshot', [''],
                {'interactive': self.dbus.Boolean(True)}, self.screenshot_ready)
        else:
            for required in ['pipewiresrc', 'videoconvert', 'videoscale', 'videorate', 'queue', 'filesink']:
                if not self.Gst.ElementFactory.find(required):
                    raise CaptureError('GStreamer: не установлен модуль ' + required)
            self.request('org.freedesktop.portal.ScreenCast', 'CreateSession', [],
                {'session_handle_token': self.dbus.String('kadrolov_session_' + uuid.uuid4().hex)}, self.session_created)
        thread = threading.Thread(target=self.read_commands, daemon=True)
        thread.start()
        self.GLib.unix_signal_add(self.GLib.PRIORITY_DEFAULT, signal.SIGTERM, self.stop)
        self.GLib.unix_signal_add(self.GLib.PRIORITY_DEFAULT, signal.SIGINT, self.stop)
        self.loop.run()
        return 1 if self.failed else 0

    def session_created(self, data):
        self.session = str(data['session_handle'])
        self.bus.add_signal_receiver(lambda *_: self.fail('Источник захвата отозван системой.') if not self.stopping else None,
            signal_name='Closed', dbus_interface='org.freedesktop.portal.Session', path=self.session,
            bus_name='org.freedesktop.portal.Desktop')
        cursor = 2 if self.args.cursor == '1' else 1
        properties = self.dbus.Interface(self.portal, 'org.freedesktop.DBus.Properties')
        available = int(properties.Get('org.freedesktop.portal.ScreenCast', 'AvailableCursorModes'))
        if not available & cursor:
            raise CaptureError('Системный портал не поддерживает выбранный режим курсора.')
        self.request('org.freedesktop.portal.ScreenCast', 'SelectSources', [self.session],
            {'types': self.dbus.UInt32(3), 'multiple': self.dbus.Boolean(False),
             'cursor_mode': self.dbus.UInt32(cursor)}, self.sources_selected)

    def sources_selected(self, _data):
        self.request('org.freedesktop.portal.ScreenCast', 'Start', [self.session, ''], {}, self.source_ready)

    def source_ready(self, data):
        streams = data.get('streams', [])
        if len(streams) != 1:
            raise CaptureError('Нужен ровно один экран или окно.')
        node, details = streams[0]
        if 'size' not in details:
            raise CaptureError('Портал не сообщил размер захватываемого источника.')
        position = tuple(details.get('position', (0, 0)))
        size = tuple(details['size'])
        if self.args.rect and 'position' not in details:
            raise CaptureError('Портал не сообщил положение источника для выбранной области. Выберите захват экрана целиком.')
        crop = crop_geometry(self.args.rect, position, size)
        interface = self.dbus.Interface(self.portal, 'org.freedesktop.portal.ScreenCast')
        self.pipewire_fd = interface.OpenPipeWireRemote(self.session, self.dbus.Dictionary({}, signature='sv')).take()
        left, top, width, height = crop
        out_w = max(2, (self.args.width or width) // 2 * 2)
        out_h = max(2, (self.args.height or height) // 2 * 2)
        self.stream_size = size
        self.output_dimensions = (out_w, out_h)
        extension = self.args.output.suffix.lower()
        bitrate = max(500, min(120000, int(out_w * out_h * self.args.fps * 0.10 * 2 ** ((23 - self.args.quality)/6) / 1000)))
        if extension == '.webm':
            video_encoder = f'vp8enc deadline=1 cpu-used=8 target-bitrate={bitrate*1000}'
            muxer, audio_encoder = 'webmmux name=mux', 'opusenc bitrate=128000'
            required = ['vp8enc', 'webmmux', 'opusenc']
        elif extension in ('.mp4', '.mkv'):
            video_encoder = f'x264enc tune=zerolatency speed-preset=veryfast bitrate={bitrate} key-int-max={self.args.fps*2} ! h264parse'
            muxer = 'mp4mux name=mux fragment-duration=2000' if extension == '.mp4' else 'matroskamux name=mux'
            aac = next((name for name in ['avenc_aac', 'voaacenc', 'fdkaacenc'] if self.Gst.ElementFactory.find(name)), None)
            if self.args.audio != 'none' and aac is None:
                raise CaptureError('Не найден AAC-кодировщик GStreamer для выбранного формата.')
            audio_encoder = aac or 'identity'
            required = ['x264enc', 'h264parse', 'mp4mux' if extension == '.mp4' else 'matroskamux']
        else:
            raise CaptureError('Wayland-модуль сохраняет MP4, MKV или WebM.')
        if self.args.audio != 'none':
            required += ['pulsesrc', 'audioconvert', 'audioresample', 'audiomixer']
        if self.args.rect:
            required.append('videocrop')
        if self.args.camera == '1':
            required += ['v4l2src', 'compositor']
            if out_w < 160 or out_h < 120:
                raise CaptureError('Для записи с камерой выберите область не меньше 160 × 120.')
        for name in required:
            if not self.Gst.ElementFactory.find(name):
                raise CaptureError('GStreamer: не установлен модуль ' + name)
        # Normalize to portal logical size before crop: Qt's region coordinates are logical pixels.
        # Without a crop, retain the source until the explicit output-size conversion below.
        crop_chain = ''
        if self.args.rect:
            crop_chain = (f'videoscale ! video/x-raw,width={size[0]},height={size[1]} ! '
                f'videocrop left={left} top={top} right={size[0]-left-width} bottom={size[1]-top-height} ! ')
        overlay = ''
        if self.args.camera == '1':
            camera_w = min(out_w-16, max(64, int(out_w * 0.23) // 2 * 2))
            camera_h = max(2, camera_w * 3 // 4 // 2 * 2)
            margin = max(4, int(out_w * 0.015))
            overlay = (f'queue ! scene.sink_0 compositor name=scene background=black '
                f'sink_0::zorder=0 sink_1::zorder=1 sink_1::xpos={out_w-camera_w-margin} '
                f'sink_1::ypos={max(0,out_h-camera_h-margin)} ! '
                f'video/x-raw,width={out_w},height={out_h},framerate={self.args.fps}/1 ! videoconvert ! ')
        graph = (f'pipewiresrc name=screen fd={self.pipewire_fd} path={int(node)} do-timestamp=true ! '
            f'queue max-size-buffers=3 leaky=downstream ! videoconvert ! {crop_chain}'
            f'videoscale ! videorate ! video/x-raw,width={out_w},height={out_h},framerate={self.args.fps}/1 ! '
            f'{overlay}identity name=first_frame ! {video_encoder} ! queue ! mux. '
            f'{muxer} ! filesink name=output_file sync=false ')
        if self.args.camera == '1':
            graph += (f'v4l2src name=camera_source do-timestamp=true ! queue max-size-buffers=2 leaky=downstream ! '
                f'videoconvert ! videoscale ! videorate ! video/x-raw,width={camera_w},height={camera_h},framerate={self.args.fps}/1 ! scene.sink_1 ')
        if self.args.audio != 'none':
            graph += f'audiomixer name=mix ! audioconvert ! audioresample ! audio/x-raw,rate=48000,channels=2 ! {audio_encoder} ! queue ! mux. '
            if self.args.audio in ('system', 'both'):
                graph += 'pulsesrc name=system_source do-timestamp=true ! queue ! audioconvert ! audioresample ! mix. '
            if self.args.audio in ('mic', 'both'):
                graph += 'pulsesrc name=microphone_source do-timestamp=true ! queue ! audioconvert ! audioresample ! mix. '
            self.active_audio = 1
        self.pipeline = self.Gst.parse_launch(graph)
        self.pipeline.get_by_name('output_file').set_property('location', str(self.args.output.resolve()))
        if self.args.camera == '1':
            camera_path = validated_camera_path(self.args.camera_device)
            self.pipeline.get_by_name('camera_source').set_property('device', camera_path)
        system = self.pipeline.get_by_name('system_source')
        if system:
            device = self.args.system_device or self.default_monitor()
            system.set_property('device', device)
        microphone = self.pipeline.get_by_name('microphone_source')
        if microphone and self.args.microphone:
            microphone.set_property('device', self.args.microphone)
        self.pipeline.get_by_name('first_frame').get_static_pad('src').add_probe(self.Gst.PadProbeType.BUFFER, self.first_frame)
        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect('message', self.bus_message)
        if self.pipeline.set_state(self.Gst.State.PLAYING) == self.Gst.StateChangeReturn.FAILURE:
            raise CaptureError('Не удалось запустить GStreamer запись.')

    def default_monitor(self):
        import subprocess
        try:
            sink = subprocess.check_output(['pactl', 'get-default-sink'], text=True, timeout=5).strip()
        except (OSError, subprocess.SubprocessError) as exc:
            raise CaptureError('Не удалось определить источник системного звука. Установите pactl или задайте --system-device.') from exc
        if not sink:
            raise CaptureError('Системный звуковой выход не найден.')
        return sink + '.monitor'

    def first_frame(self, _pad, _probe):
        if not self.start_announced:
            self.start_announced = True
            self.GLib.idle_add(self.announce_started)
        return self.Gst.PadProbeReturn.REMOVE

    def announce_started(self):
        if not self.stopping:
            self.state = 'recording'
            emit('started', path=str(self.args.output), width=self.output_dimensions[0],
                height=self.output_dimensions[1], audioTracks=self.active_audio, cursor=self.args.cursor == '1', clicks=False, camera=self.args.camera == '1')
        return False

    def screenshot_ready(self, data):
        import shutil
        parsed = urlparse(str(data.get('uri', '')))
        if parsed.scheme != 'file' or parsed.netloc not in ('', 'localhost'):
            raise CaptureError('Портал вернул недопустимый локальный путь снимка.')
        source = Path(unquote(parsed.path))
        if not source.is_file():
            raise CaptureError('Файл снимка от портала не найден.')
        with source.open('rb') as image:
            if image.read(8) != b'\x89PNG\r\n\x1a\n':
                raise CaptureError('Портал вернул снимок в неподдерживаемом формате; ожидался PNG.')
        shutil.copyfile(source, self.args.screenshot)
        emit('finished', path=str(self.args.screenshot))
        self.cleanup()

    def bus_message(self, _bus, message):
        if message.type == self.Gst.MessageType.ERROR:
            error, _debug = message.parse_error()
            self.fail('Ошибка захвата/сохранения: ' + error.message)
        elif message.type == self.Gst.MessageType.EOS:
            emit('finished', path=str(self.args.output), audioTracks=self.active_audio)
            self.cleanup()

    def command(self, name):
        try:
            if name == 'stop':
                self.stop()
            elif name == 'pause' and self.state == 'recording':
                if self.pipeline.set_state(self.Gst.State.PAUSED) == self.Gst.StateChangeReturn.FAILURE:
                    raise CaptureError('Не удалось поставить запись на паузу.')
                self.state = 'paused'
                emit('paused')
            elif name == 'resume' and self.state == 'paused':
                if self.pipeline.set_state(self.Gst.State.PLAYING) == self.Gst.StateChangeReturn.FAILURE:
                    raise CaptureError('Не удалось продолжить запись.')
                self.state = 'recording'
                emit('resumed')
        except Exception as exc:
            self.fail(str(exc))
        return False

    def read_commands(self):
        for line in sys.stdin:
            try:
                name = json.loads(line).get('command')
                if name not in ('pause', 'resume', 'stop'):
                    raise ValueError()
                self.GLib.idle_add(self.command, name)
            except (ValueError, AttributeError):
                emit('warning', message='Ожидалась JSON-команда pause, resume или stop.')
        self.GLib.idle_add(self.stop)

    def stop(self):
        if self.stopping:
            return False
        self.stopping = True
        if self.pipeline and self.start_announced:
            # EOS must travel in PLAYING even if the recording was paused.
            self.pipeline.set_state(self.Gst.State.PLAYING)
            self.pipeline.send_event(self.Gst.Event.new_eos())
            self.stop_timeout = self.GLib.timeout_add_seconds(20, lambda: self.fail('Не удалось завершить файл за 20 секунд. Частичная запись сохранена.'))
        else:
            if self.request_handle:
                try:
                    self.bus.get_object('org.freedesktop.portal.Desktop', self.request_handle).get_dbus_method('Close', 'org.freedesktop.portal.Request')()
                except Exception:
                    pass
            self.cleanup()
        return False

    def fail(self, message):
        if self.failed:
            return False
        self.failed = True
        emit('error', message=message, path=str(self.args.output or self.args.screenshot or ''))
        self.cleanup()
        return False

    def cleanup(self):
        self.stopping = True
        if self.stop_timeout:
            self.GLib.source_remove(self.stop_timeout)
            self.stop_timeout = None
        if self.pipeline:
            self.pipeline.set_state(self.Gst.State.NULL)
        if self.pipewire_fd is not None:
            os.close(self.pipewire_fd)
            self.pipewire_fd = None
        if self.session:
            try:
                self.bus.get_object('org.freedesktop.portal.Desktop', self.session).get_dbus_method('Close', 'org.freedesktop.portal.Session')()
            except Exception:
                pass
            self.session = None
        self.loop.quit()


def main(argv=None):
    args = arguments(argv)
    try:
        runtime = load_runtime()
        if args.capabilities:
            _, Gst, _ = runtime
            emit('capabilities', available=True, clicks=False, camera=bool(Gst.ElementFactory.find('v4l2src') and Gst.ElementFactory.find('compositor')),
                videoEncoder=bool(Gst.ElementFactory.find('x264enc')), pipewire=bool(Gst.ElementFactory.find('pipewiresrc')))
            return 0
        capture = PortalCapture(args, runtime)
        return capture.start()
    except Exception as exc:
        emit('error', message=str(exc))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
