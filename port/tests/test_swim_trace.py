#!/usr/bin/env python3
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from compare_swim import FIELDS, compare


class SwimTrace(unittest.TestCase):
    def setUp(self):
        dry = dict.fromkeys(FIELDS, 0)
        wet = dry | {'swim': 1}
        self.want = {0: dry.copy(), 1: wet.copy()}
        self.got = {0: wet.copy(), 1: dry.copy()}

    def test_post_frame_alignment(self):
        self.assertEqual(compare(self.want, self.got), (1, 1, 0, None))

    def test_changed_rng(self):
        self.got[0]['rng1'] = 1
        self.assertEqual(compare(self.want, self.got)[2], 1)

    def test_changed_swim(self):
        self.got[0]['swim'] = 0
        self.assertEqual(compare(self.want, self.got)[2], 1)

    def test_no_water(self):
        self.want[1]['swim'] = 0
        with self.assertRaises(ValueError):
            compare(self.want, self.got)

    def test_missing_checkpoint(self):
        self.got[2] = self.got.pop(1)
        with self.assertRaises(ValueError):
            compare(self.want, self.got)


if __name__ == '__main__':
    unittest.main()
