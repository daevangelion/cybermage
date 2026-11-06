#!/usr/bin/env python3
"""Synthetic directory regression tests; no retail bytes."""
import struct
import tempfile
import unittest
from pathlib import Path

import rsc


def archive(offset=32, length=3, count=1):
    return struct.pack('<8I', 3, count, offset, 0, 0, length, 0, 0) + b'abc'


class RscTests(unittest.TestCase):
    def test_explicit_length_and_empty_slots(self):
        entries = rsc.parse(archive(), 'fixture')
        self.assertEqual(entries, [(32, 3), (0, 0), (0, 0)])

    def test_out_of_order_entries_are_valid(self):
        data = struct.pack('<6I', 2, 2, 25, 24, 1, 1) + b'ab'
        self.assertEqual(rsc.parse(data, 'fixture'), [(25, 1), (24, 1)])

    def test_rejects_bad_directory(self):
        bad = [b'', b'1234567', archive(count=4), archive(offset=31),
               archive(offset=36), archive(length=4), archive(offset=0),
               struct.pack('<2I', 0xffffffff, 0)]
        for data in bad:
            with self.subTest(data=data):
                with self.assertRaises(rsc.FormatError):
                    rsc.parse(data, 'fixture')

    def test_extract_uses_size_not_next_offset(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, dst = Path(tmp) / 'a.rsc', Path(tmp) / 'entry.bin'
            src.write_bytes(archive())
            self.assertEqual(rsc.main(['extract', str(src), '0', str(dst)]), 0)
            self.assertEqual(dst.read_bytes(), b'abc')
            self.assertNotEqual(rsc.main(['extract', str(src), '3', str(dst)]), 0)

    def test_scene_has_nine_section_tables(self):
        offsets = [872] + [0] * 107
        sizes = [3] + [0] * 107
        data = struct.pack('<2I', 12, 12) + struct.pack('<216I', *(offsets + sizes)) + b'abc'
        entries = rsc.parse(data, 'SCENE.RSC')
        self.assertEqual(len(entries), 108)
        self.assertEqual(entries[0], (872, 3))
        self.assertEqual(entries[-1], (0, 0))
        with self.assertRaises(rsc.FormatError):
            rsc.parse(data[:-1], 'SCENE.RSC')


if __name__ == '__main__':
    unittest.main()
