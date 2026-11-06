"""Synthetic format/alignment controls, not retail gameplay evidence."""
import csv
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[2] / 'tools' / 'compare_projectiles.py'
spec = importlib.util.spec_from_file_location('compare_projectiles', TOOL)
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class ProjectileTrace(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.trace = self.root / 'frames.csv'
        self.states = self.root / 'native'
        self.states.mkdir()
        self.write_trace(True)
        expected = compare.original(self.trace)[1]
        text = '\n'.join(f'm{slot}_{field}={value[field]}'
                         for slot, value in enumerate(expected)
                         for field in compare.COMPARE_FIELDS)
        for frame in range(2):
            (self.states / f'frame{frame:06d}.state').write_text(text)

    def tearDown(self):
        self.temp.cleanup()

    def write_trace(self, active):
        ranges = [(0x135ecc, 576), (0x62518, 48), (0x65180, 48),
                  (0x79e8c, 48), (0x7eedc, 24)]
        with self.trace.open('w', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(['CMTRACE1', len(ranges)])
            writer.writerows(['range', 'x', f'{address:x}', length] for address, length in ranges)
            writer.writerow(['frame', 'ranges...'])
            for frame in range(2):
                records = bytearray(576)
                for slot in range(12):
                    struct.pack_into('<12i', records, slot*48,
                                     int(active and frame == 1 and slot in (0, 11)),
                                     1088+slot, -321, 456, 9, -1,
                                     0, 0, -1, 12345, 0, 0)
                spans = [records]
                for value in (40, 50, 60):
                    spans.append(struct.pack('<12i', *range(value, value+12)))
                spans.append(struct.pack('<12H', *range(10000, 10012)))
                writer.writerow([frame] + [data.hex() for data in spans])

    def run_compare(self):
        return subprocess.run([sys.executable, str(TOOL), str(self.trace), str(self.states)],
                              capture_output=True, text=True)

    def test_post_frame_alignment_and_signed_fields(self):
        result = self.run_compare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Compared 12 checkpoints (2 active)', result.stdout)
        self.assertIn('First divergence: None', result.stdout)

    def test_all_slots_are_checked(self):
        missiles = compare.original(self.trace)[1]
        self.assertEqual(len(missiles), 12)
        self.assertEqual(missiles[11]['active'], 1)
        self.assertEqual(len(compare.native(self.states)[0]), 12)

    def test_one_changed_active_field_fails(self):
        path = self.states / 'frame000000.state'
        path.write_text(path.read_text().replace('m0_vertical=-321', 'm0_vertical=-320'))
        result = self.run_compare()
        self.assertEqual(result.returncode, 1)
        self.assertIn("'field': 'vertical'", result.stdout)

    def test_changed_last_slot_fails(self):
        path = self.states / 'frame000000.state'
        path.write_text(path.read_text().replace('m11_vertical=-321', 'm11_vertical=-320'))
        result = self.run_compare()
        self.assertEqual(result.returncode, 1)
        self.assertIn("'slot': 11", result.stdout)
        self.assertIn("'field': 'vertical'", result.stdout)

    def test_missing_last_slot_fails(self):
        path = self.states / 'frame000000.state'
        path.write_text('\n'.join(line for line in path.read_text().splitlines()
                                  if not line.startswith('m11_')))
        result = self.run_compare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing m11_active', result.stderr)

    def test_inactive_slots_have_no_live_payload(self):
        path = self.states / 'frame000000.state'
        path.write_text(path.read_text().replace('m1_type=9', 'm1_type=999'))
        result = self.run_compare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_no_original_activity_fails(self):
        self.write_trace(False)
        result = self.run_compare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no original projectile activity', result.stderr)

    def test_missing_native_checkpoint_fails(self):
        (self.states / 'frame000000.state').unlink()
        result = self.run_compare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('contiguous from frame0', result.stderr)

    def test_truncated_original_span_fails(self):
        with self.trace.open(newline='') as stream:
            rows = list(csv.reader(stream))
        rows[-1][-1] = rows[-1][-1][:-2]
        with self.trace.open('w', newline='') as stream:
            csv.writer(stream).writerows(rows)
        with self.assertRaisesRegex(ValueError, 'span length mismatch'):
            compare.original(self.trace)


if __name__ == '__main__':
    unittest.main()
