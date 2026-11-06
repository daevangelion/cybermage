#!/usr/bin/env python3
"""Compare actor states/RNG, from matching CSVs or full-game post-frame dumps."""
import argparse
import csv
from pathlib import Path
import re

FIELDS = ('object', 'x', 'y', 'z', 'heading', 'state', 'status', 'hp',
          'target', 'animation', 'cursor', 'active')


def load(path):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError(f'{path}: empty actor trace')
    result = {}
    for row in rows:
        value = {name: int(row[name]) for name in ('frame', 'person', 'ticks',
                 'rng0', 'rng1', 'rng2') + FIELDS}
        for name in ('original_undefined_person', 'original_undefined_distance'):
            value[name] = int(row.get(name, 0))
        key = value['frame'], value['person']
        if key in result:
            raise ValueError(f'{path}: duplicate actor checkpoint {key}')
        result[key] = value
    return result


def checkpoint_frames(rows):
    frames = {}
    for frame, person in rows:
        frames.setdefault(frame, set()).add(person)
    if not frames or sorted(frames) != list(range(len(frames))):
        raise ValueError('actor checkpoint frames must be contiguous from frame0')
    for people in frames.values():
        if not people or people != set(range(len(people))):
            raise ValueError('actor checkpoint IDs must be contiguous from person0')
    return frames


def load_state_directory(path):
    result = {}
    roots = ('frame', 'ticks', 'rng0', 'rng1', 'rng2')
    pattern = re.compile(r'(?:^|\s)(frame|ticks|rng0|rng1|rng2|original_undefined_person|original_undefined_distance)=(-?[0-9]+)(?=\s|$)')
    for file in sorted(path.glob('frame*.state')):
        match = re.fullmatch(r'frame([0-9]+)\.state', file.name)
        if not match:
            continue
        frame = int(match[1])
        text = file.read_text()
        pairs = pattern.findall(text)
        root = {name: int(value) for name, value in pairs}
        if len(pairs) != len(root) or not set(roots) <= set(root):
            raise ValueError(f'{file}: missing/duplicate actor clock or RNG fields')
        if root['frame'] != frame:
            raise ValueError(f'{file}: actor frame/header mismatch')
        found = False
        for line in text.splitlines():
            if not line.startswith('actor='):
                continue
            values = {}
            for token in line.split():
                name, separator, value = token.partition('=')
                if not separator or name in values:
                    raise ValueError(f'{file}: malformed/duplicate actor field')
                values[name] = int(value)
            if 'actor' not in values or any(f'actor_{name}' not in values for name in FIELDS):
                raise ValueError(f'{file}: missing actor field')
            person = values['actor']
            key = frame, person
            if key in result:
                raise ValueError(f'{file}: duplicate actor checkpoint {key}')
            result[key] = dict(root, person=person,
                               **{name: values[f'actor_{name}'] for name in FIELDS})
            found = True
        if not found:
            raise ValueError(f'{file}: missing actor rows')
    checkpoint_frames(result)
    return result


def align_postframes(expected, actual):
    expected_frames, actual_frames = checkpoint_frames(expected), checkpoint_frames(actual)
    if len(expected_frames) < 2 or expected_frames.keys() != actual_frames.keys():
        raise ValueError('original/native actor frame checkpoint count mismatch')
    last = max(actual_frames)
    wanted = {key: value for key, value in expected.items() if key[0] != 0}
    got = {(frame + 1, person): dict(value, frame=frame + 1)
           for (frame, person), value in actual.items() if frame != last}
    if wanted.keys() != got.keys():
        raise ValueError('original/native actor checkpoint IDs mismatch')
    return wanted, got


def compare(expected, actual):
    if expected.keys() != actual.keys():
        missing = sorted(expected.keys() - actual.keys())
        extra = sorted(actual.keys() - expected.keys())
        raise ValueError(f'CHECKPOINT MISMATCH missing={missing[:1]} extra={extra[:1]}')
    first, first_npc, first_rng = None, None, None
    changed = set()
    for key in sorted(expected):
        want, got = expected[key], actual[key]
        for field in FIELDS:
            if want[field] == got[field]:
                continue
            changed.add(field)
            divergence = (key, field, want[field], got[field])
            if first is None:
                first = divergence
            if key[1] != 0 and first_npc is None:
                first_npc = divergence
        if key[1] == 0 and first_rng is None:
            want_rng = tuple(want[f'rng{i}'] for i in range(3))
            got_rng = tuple(got[f'rng{i}'] for i in range(3))
            if want_rng != got_rng:
                first_rng = (key[0], want_rng, got_rng)
    undefined = sorted((key[0], value.get('original_undefined_person', 0),
                        value.get('original_undefined_distance', 0))
                       for key, value in actual.items()
                       if key[1] == 0 and value.get('original_undefined_person', 0))
    return {'first': first, 'first_npc': first_npc, 'first_rng': first_rng,
            'changed': changed, 'original_undefined': undefined}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('native', type=Path)
    parser.add_argument('--native-post-frame', action='store_true',
                        help='native is a --state-dump directory; compare post-N to original pre-N+1')
    args = parser.parse_args()
    try:
        expected = load(args.original)
        if args.native_post_frame:
            expected, actual = align_postframes(expected, load_state_directory(args.native))
            print('Post-frame alignment: excluding original initial state and unmatched native terminal state')
        else:
            actual = load(args.native)
        result = compare(expected, actual)
    except (OSError, ValueError, KeyError) as error:
        print(error)
        return 1
    first, first_npc, first_rng, changed = (result[name] for name in
        ('first', 'first_npc', 'first_rng', 'changed'))
    print(f'Compared {len(expected)} actor checkpoints across '
          f'{len({key[0] for key in expected})} frames')
    print(f'First divergence: {first}')
    print(f'First NPC divergence: {first_npc}')
    print(f'First RNG divergence: {first_rng}')
    print(f'Differing fields: {",".join(sorted(changed)) or "none"}')
    if result['original_undefined']:
        print(f'Original undefined distance input observed at {len(result["original_undefined"])} checkpoints')
        first_bad = min(([first[0][0]] if first else []) + ([first_rng[0]] if first_rng else []),
                        default=None)
        prior = [event for event in result['original_undefined']
                 if first_bad is not None and event[0] <= first_bad]
        if prior:
            frame, person, distance = prior[-1]
            print(f'Original undefined context: checkpoint={frame} PERSON={person} '
                  f'cached_distance={distance}; causal relation not inferred, no fields masked')
    for name, rows in (('Original', expected), ('Native', actual)):
        first_frame = min(key[0] for key in rows)
        initial = {key[1]: value for key, value in rows.items() if key[0] == first_frame}
        active, moved, hurt = set(), set(), set()
        for key, value in rows.items():
            person = key[1]
            if person == 0:
                continue
            if value['active'] and value['status'] != 2:
                active.add(person)
            start = initial[person]
            if any(value[field] != start[field] for field in ('x', 'y', 'z')):
                moved.add(person)
            if value['hp'] < start['hp']:
                hurt.add(person)
        print(f'{name}: active NPCs={len(active)}, moved NPCs={len(moved)}, '
              f'NPCs losing HP={len(hurt)}')
    return int(first is not None or first_rng is not None)


if __name__ == '__main__':
    raise SystemExit(main())
