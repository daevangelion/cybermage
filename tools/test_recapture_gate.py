#!/usr/bin/env python3
from pathlib import Path
import tempfile
import unittest

from recapture_gate import CORE, frame_rows, fresh_directory, manifest_routes, paired, selected_indices


class PairingTests(unittest.TestCase):
    def test_explicit_retry_subset_does_not_change_manifest_scope(self):
        self.assertEqual(selected_indices(list(CORE), []), list(range(12)))
        self.assertEqual(selected_indices(list(CORE), ['movement']), [2])
        for names in (['unknown'], ['movement', 'movement']):
            with self.assertRaises(ValueError):
                selected_indices(list(CORE), names)

    def test_manifest_rejects_duplicate_escape_and_missing_baseline(self):
        baseline = {'routes': [{'route': name} for name in CORE + ('extra',)]}
        valid = {'routes': [{'route': name} for name in CORE + ('extra',)]}
        self.assertEqual(manifest_routes(valid, baseline), list(CORE) + ['extra'])
        for names in (CORE + ('extra', 'extra'), CORE + ('../extra',), CORE):
            with self.assertRaises(ValueError):
                manifest_routes({'routes': [{'route': name} for name in names]}, baseline)
        with self.assertRaises(ValueError):
            manifest_routes({'routes': list(reversed(valid['routes']))}, baseline)

    def test_existing_evidence_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            sentinel = path / 'receipt.json'
            sentinel.write_text('original evidence')
            with self.assertRaises(FileExistsError):
                fresh_directory(path)
            self.assertEqual(sentinel.read_text(), 'original evidence')

    def test_frame_header_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'frames.csv'
            path.write_text('CMTRACE1,0\n')
            with self.assertRaisesRegex(ValueError, 'Missing frame header'):
                frame_rows(path)
            path.write_text('CMTRACE1,0\nframe,x\n')
            with self.assertRaisesRegex(ValueError, 'No gameplay records'):
                frame_rows(path)

    def test_common_columns_and_external_clocks_are_exact(self):
        with tempfile.TemporaryDirectory() as directory:
            old, new = (Path(directory) / name for name in ('old', 'new'))
            old.mkdir(); new.mkdir()
            (old / 'frames.csv').write_text('CMTRACE1,0\nframe,x\n0,3\n1,4\n')
            (new / 'frames.csv').write_text('CMTRACE1,0\nframe,x,extra\n0,3,9\n1,4,8\n')
            for path in (old, new):
                (path / 'actor-clocks.csv').write_text('frame,fast_ticks,normal_ticks\n0,2,3\n')
            result = paired(old, new)
            self.assertTrue(result['common_frame_fields_identical'])
            self.assertTrue(result['paired_files_identical'])
            (new / 'frames.csv').write_text('CMTRACE1,0\nframe,x\n0,3\n1,5\n')
            result = paired(old, new)
            self.assertFalse(result['common_frame_fields_identical'])
            self.assertEqual(result['frame_changes']['x'], {'count': 1, 'first': ['1', '4', '5']})
            (new / 'actor-clocks.csv').write_text('frame,fast_ticks,normal_ticks\n0,3,3\n')
            self.assertFalse(paired(old, new)['paired_files_identical'])

    def test_missing_rows_are_not_accepted_as_a_prefix_match(self):
        with tempfile.TemporaryDirectory() as directory:
            old, new = (Path(directory) / name for name in ('old', 'new'))
            old.mkdir(); new.mkdir()
            (old / 'frames.csv').write_text('CMTRACE1,0\nframe,x\n0,3\n1,4\n')
            (new / 'frames.csv').write_text('CMTRACE1,0\nframe,x\n0,3\n')
            result = paired(old, new)
            self.assertFalse(result['common_frame_fields_identical'])
            self.assertEqual(result['frame_changes']['record_count'], [2, 1])

    def test_extended_observation_compares_every_original_byte(self):
        with tempfile.TemporaryDirectory() as directory:
            old, new = (Path(directory) / name for name in ('old', 'new'))
            old.mkdir(); new.mkdir()
            (old / 'frames.csv').write_text('CMTRACE1,2\nrange,l,206820,2\nrange,x,20,1\nframe,ranges...\n0,0102,03\n')
            (new / 'frames.csv').write_text('CMTRACE1,2\nrange,l,206820,4\nrange,x,20,1\nframe,ranges...\n0,01020405,03\n')
            self.assertTrue(paired(old, new)['common_frame_fields_identical'])
            (new / 'frames.csv').write_text('CMTRACE1,2\nrange,l,206820,4\nrange,x,20,1\nframe,ranges...\n0,01020405,ff\n')
            result = paired(old, new)
            self.assertFalse(result['common_frame_fields_identical'])
            self.assertIn('range1', result['frame_changes'])


if __name__ == '__main__':
    unittest.main()
