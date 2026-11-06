#!/usr/bin/env python3
"""Compare original and native actor CSVs, including per-frame RNG words."""
import argparse
import csv
from pathlib import Path

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
        key = value['frame'], value['person']
        if key in result:
            raise ValueError(f'{path}: duplicate actor checkpoint {key}')
        result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('native', type=Path)
    args = parser.parse_args()
    expected, actual = load(args.original), load(args.native)
    if expected.keys() != actual.keys():
        missing = sorted(expected.keys() - actual.keys())
        extra = sorted(actual.keys() - expected.keys())
        print(f'CHECKPOINT MISMATCH missing={missing[:1]} extra={extra[:1]}')
        return 1
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
    print(f'Compared {len(expected)} actor checkpoints across '
          f'{len({key[0] for key in expected})} frames')
    print(f'First divergence: {first}')
    print(f'First NPC divergence: {first_npc}')
    print(f'First RNG divergence: {first_rng}')
    print(f'Differing fields: {",".join(sorted(changed)) or "none"}')
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
