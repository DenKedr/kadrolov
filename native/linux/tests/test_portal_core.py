import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('portal_capture', Path(__file__).parents[1] / 'portal_capture.py')
portal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(portal)


class RegionSafety(unittest.TestCase):
    def test_negative_monitor_origin_becomes_source_local(self):
        self.assertEqual(portal.crop_geometry((-1300, 250, 800, 600), (-1440, 200), (1440, 900)), (140, 50, 800, 600))

    def test_wrong_monitor_never_widens_capture(self):
        with self.assertRaises(portal.CaptureError):
            portal.crop_geometry((-1600, 250, 800, 600), (-1440, 200), (1440, 900))

    def test_outside_bottom_rejected(self):
        with self.assertRaises(portal.CaptureError):
            portal.crop_geometry((10, 800, 800, 600), (0, 0), (1440, 900))

    def test_full_source_stays_full_source(self):
        self.assertEqual(portal.crop_geometry(None, (-1440, 200), (1440, 900)), (0, 0, 1440, 900))

    def test_invalid_stream_dimensions_rejected(self):
        with self.assertRaises(portal.CaptureError):
            portal.crop_geometry(None, (0, 0), (0, 0))


class CameraSafety(unittest.TestCase):
    def test_local_v4l2_device(self):
        self.assertEqual(portal.validated_camera_path('/dev/video0'), '/dev/video0')

    def test_pipeline_injection_is_rejected(self):
        with self.assertRaises(portal.CaptureError):
            portal.validated_camera_path('/dev/video0 ! fakesrc')

    def test_arbitrary_file_is_not_a_camera(self):
        with self.assertRaises(portal.CaptureError):
            portal.validated_camera_path('/etc/passwd')


if __name__ == '__main__':
    unittest.main()
