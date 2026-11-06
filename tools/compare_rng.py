#!/usr/bin/env python3
"""Diff RNG calls by operation, bound and pre-call words; report both callers."""
import argparse
import csv
from pathlib import Path


def load(path, original):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    if original:
        rows = [r for r in rows if not (r['operation'] == 'RANDOM' and r['caller'].lower() == '67329')]
    return rows


def key(row):
    return (row['operation'],) + tuple(int(row[n]) for n in ('bound', 'rng0', 'rng1', 'rng2'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('original', type=Path)
    p.add_argument('native', type=Path)
    args = p.parse_args()
    original, native = load(args.original, True), load(args.native, False)
    for index, (want, got) in enumerate(zip(original, native)):
        if key(want) == key(got):
            continue
        print(f'First mismatch at logical call {index}')
        print(f'original frame={want["frame"]} caller={want["caller"]} {key(want)}')
        print(f'native   frame={got["frame"]} caller={got["caller"]} {key(got)}')
        for extra in range(1, 9):
            if index + extra < len(original) and key(original[index + extra]) == key(got):
                print('Original calls preceding matching native call: ' +
                      ', '.join(f'{r["caller"]}(bound={r["bound"]})' for r in original[index:index+extra]))
                break
        return 1
    if len(original) != len(native):
        print(f'Common prefix matches {min(len(original),len(native))} calls; lengths original={len(original)} native={len(native)}')
        return 1
    print(f'MATCH: {len(original)} RNG calls (operation, bound, all three pre-call words)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
