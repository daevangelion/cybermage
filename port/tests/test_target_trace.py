"""Target trace format/order controls; synthetic inputs are not gameplay evidence."""
import csv
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[2] / 'tools' / 'compare_targets.py'
spec = importlib.util.spec_from_file_location('compare_targets', TOOL)
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class TargetTrace(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.trace = self.root / 'frames.csv'
        self.states = self.root / 'native'
        self.states.mkdir()
        with self.trace.open('w', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(['CMTRACE1', 1])
            writer.writerow(['range', 'x', '4b00c', 324])
            writer.writerow(['frame', 'ranges...'])
            for frame, targets in enumerate([[], [19, 7], [7]]):
                data = bytearray(324)
                struct.pack_into('<i', data, 0, len(targets))
                for index, value in enumerate(targets):
                    struct.pack_into('<H', data, 4 + index * 2, value)
                writer.writerow([frame, data.hex()])
        for frame, targets in enumerate([[19, 7], [7], [123]]):
            (self.states / f'frame{frame:06d}.state').write_text(
                f'targets count={len(targets)} objects=' + ','.join(map(str, targets)) + '\n')

    def tearDown(self):
        self.temp.cleanup()

    def test_post_frame_alignment(self):
        result = compare.compare(compare.original(self.trace), compare.native(self.states))
        self.assertEqual(result['compared'], 2)
        self.assertEqual(result['nonempty'], 2)
        self.assertEqual(result['mismatches'], 0)
        self.assertIsNone(result['first'])

    def test_order_change_fails(self):
        (self.states / 'frame000000.state').write_text('targets count=2 objects=7,19\n')
        result = compare.compare(compare.original(self.trace), compare.native(self.states))
        self.assertEqual(result['mismatches'], 1)
        self.assertEqual(result['first']['native_frame'], 0)

    def test_missing_native_frame_fails(self):
        (self.states / 'frame000001.state').unlink()
        with self.assertRaisesRegex(ValueError, 'contiguous'):
            compare.native(self.states)

    def test_missing_target_fields_fails(self):
        (self.states / 'frame000000.state').write_text('unrelated=0\n')
        with self.assertRaisesRegex(ValueError, 'target'):
            compare.native(self.states)

    def test_invalid_count_fails(self):
        text = self.trace.read_text()
        self.trace.write_text(text.replace('00000000', 'a1000000', 1))
        with self.assertRaisesRegex(ValueError, 'count'):
            compare.original(self.trace)

    def test_truncated_span_fails(self):
        with self.trace.open(newline='') as stream:
            rows = list(csv.reader(stream))
        rows[-1][-1] = rows[-1][-1][:-2]
        with self.trace.open('w', newline='') as stream:
            csv.writer(stream).writerows(rows)
        with self.assertRaisesRegex(ValueError, 'length'):
            compare.original(self.trace)

    def test_empty_capture_not_nonempty_evidence(self):
        result = compare.compare({0: [], 1: []}, {0: [], 1: []})
        self.assertEqual(result['nonempty'], 0)
        self.assertEqual(result['mismatches'], 0)


if __name__ == '__main__':
    unittest.main()
