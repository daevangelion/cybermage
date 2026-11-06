"""Synthetic actor trace controls, not retail combat evidence."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[2] / 'tools' / 'compare_actors.py'
spec = importlib.util.spec_from_file_location('compare_actors', TOOL)
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class ActorTrace(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.original = {}
        for frame in range(3):
            for person in range(2):
                row = dict.fromkeys(compare.FIELDS, 0)
                row.update(frame=frame, person=person, ticks=300 + frame * 2,
                           rng0=frame + 1, rng1=2, rng2=3,
                           object=person + 5, x=frame * 100, hp=40 - frame)
                self.original[frame, person] = row
        for frame in range(3):
            expected = [self.original[min(frame + 1, 2), person] for person in range(2)]
            text = f'frame={frame} ticks={300 + frame * 2} rng0={min(frame + 1, 2) + 1} rng1=2 rng2=3\n'
            text += 'targets count=0 objects=\n'
            for person, row in enumerate(expected):
                text += f'actor={person} ' + ' '.join(f'actor_{name}={row[name]}' for name in compare.FIELDS) + '\n'
            (self.root / f'frame{frame:06d}.state').write_text(text)

    def tearDown(self):
        self.temp.cleanup()

    def aligned(self):
        return compare.align_postframes(self.original, compare.load_state_directory(self.root))

    def test_post_frame_alignment(self):
        want, got = self.aligned()
        self.assertEqual(len(want), 4)
        self.assertEqual(want.keys(), got.keys())
        self.assertIsNone(compare.compare(want, got)['first'])
        self.assertIsNone(compare.compare(want, got)['first_rng'])

    def test_changed_health_is_not_masked(self):
        path = self.root / 'frame000000.state'
        path.write_text(path.read_text().replace('actor_hp=39', 'actor_hp=38', 1))
        result = compare.compare(*self.aligned())
        self.assertEqual(result['first'], ((1, 0), 'hp', 39, 38))

    def test_changed_rng_is_not_masked(self):
        path = self.root / 'frame000000.state'
        path.write_text(path.read_text().replace('rng0=2', 'rng0=123', 1))
        result = compare.compare(*self.aligned())
        self.assertEqual(result['first_rng'], (1, (2, 2, 3), (123, 2, 3)))

    def test_original_undefined_context_never_masks_damage(self):
        path = self.root / 'frame000000.state'
        path.write_text(path.read_text().replace('actor_hp=39', 'actor_hp=38', 1) +
                        'original_undefined_person=1 original_undefined_distance=15517\n')
        result = compare.compare(*self.aligned())
        self.assertEqual(result['first'], ((1, 0), 'hp', 39, 38))
        self.assertEqual(result['original_undefined'], [(1, 1, 15517)])

    def test_missing_actor_fails(self):
        path = self.root / 'frame000001.state'
        path.write_text('\n'.join(line for line in path.read_text().splitlines() if not line.startswith('actor=1 ')) + '\n')
        with self.assertRaisesRegex(ValueError, 'actor|checkpoint'):
            self.aligned()

    def test_missing_frame_fails(self):
        (self.root / 'frame000001.state').unlink()
        with self.assertRaisesRegex(ValueError, 'frame|checkpoint'):
            self.aligned()

    def test_duplicate_actor_fails(self):
        path = self.root / 'frame000000.state'
        text = path.read_text()
        path.write_text(text + next(line for line in text.splitlines() if line.startswith('actor=0 ')) + '\n')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.aligned()

    def test_missing_actor_field_fails(self):
        path = self.root / 'frame000000.state'
        path.write_text(path.read_text().replace(' actor_target=0', '', 1))
        with self.assertRaisesRegex(ValueError, 'field'):
            self.aligned()

    def test_extra_frame_fails(self):
        path = self.root / 'frame000003.state'
        path.write_text((self.root / 'frame000002.state').read_text().replace('frame=2 ', 'frame=3 ', 1))
        with self.assertRaisesRegex(ValueError, 'frame|checkpoint'):
            self.aligned()

    def test_actorless_trace_fails(self):
        for path in self.root.glob('*.state'):
            path.write_text('\n'.join(line for line in path.read_text().splitlines() if not line.startswith('actor=')) + '\n')
        with self.assertRaisesRegex(ValueError, 'actor'):
            self.aligned()


if __name__ == '__main__':
    unittest.main()
