#!/usr/bin/env python3
"""Compare computed post-frame water/player/HP/RNG state with original N+1.

Requires --actor-trace and ranges x:144b34:36,l:1e391c:12,l:9747f:6.
The terminal native frame has no next original checkpoint and is excluded.
"""
import argparse
import csv
from pathlib import Path
import re
import struct
from compare_targets import contiguous

FIELDS = ('x', 'y', 'z', 'swim', 'b34', 'swim_deadline', 'hp', 'rng0', 'rng1', 'rng2')


def original(path):
    hp = {}
    with (path.parent / 'actors.csv').open(newline='') as stream:
        for row in csv.DictReader(stream):
            if int(row['person']) == 0:
                frame = int(row['frame'])
                if frame in hp:
                    raise ValueError('duplicate original player HP frame')
                hp[frame] = int(row['hp'])
    result = {}
    with path.open(newline='') as stream:
        rows = csv.reader(stream)
        magic, count = next(rows)
        if magic != 'CMTRACE1':
            raise ValueError('not CMTRACE1')
        ranges = []
        for _ in range(int(count)):
            tag, kind, address, length = next(rows)
            if tag != 'range' or kind not in ('x', 'l'):
                raise ValueError('invalid range')
            ranges.append((kind, int(address, 16), int(length)))
        header = next(rows)
        first = header.index('ranges...')
        for row in rows:
            if len(row) != first + len(ranges):
                raise ValueError('row/range count mismatch')
            frame = int(row[0])
            if frame in result or frame not in hp:
                raise ValueError('duplicate frame or missing HP')
            if int(row[header.index('person')]) != 0:
                raise ValueError('water comparator requires on-foot PERSON0')
            spans = []
            for (kind, address, length), text in zip(ranges, row[first:]):
                data = bytes.fromhex(text)
                if len(data) != length:
                    raise ValueError('truncated trace span')
                spans.append((kind, address, data))

            def read(kind, address, fmt):
                size = struct.calcsize(fmt)
                for span_kind, start, data in spans:
                    if kind == span_kind and start <= address and address + size <= start + len(data):
                        return struct.unpack_from(fmt, data, address - start)
                raise ValueError(f'missing {kind}:{address:x}:{size}')

            values = {key: int(row[header.index(key)]) for key in ('x', 'y', 'z')}
            values.update(swim=read('x', 0x144b50, '<i')[0], b34=read('x', 0x144b34, '<i')[0],
                          swim_deadline=read('l', 0x1e3920, '<I')[0], hp=hp[frame])
            values.update(zip(('rng0', 'rng1', 'rng2'), read('l', 0x9747f, '<3H')))
            result[frame] = values
    contiguous(result)
    return result


def native(path):
    result = {}
    for file in path.glob('frame*.state'):
        match = re.fullmatch(r'frame([0-9]+)\.state', file.name)
        if not match:
            continue
        frame = int(match[1])
        if frame in result:
            raise ValueError('duplicate native frame')
        pairs = re.findall(r'\b([a-z0-9_]+)=(-?[0-9]+)\b', file.read_text())
        for key in FIELDS:
            if sum(name == key for name, _ in pairs) > 1:
                raise ValueError(f'{file}: duplicate {key}')
        values = dict(pairs)
        if any(key not in values for key in FIELDS):
            raise ValueError(f'{file}: missing water field')
        result[frame] = {key: int(values[key]) for key in FIELDS}
    contiguous(result)
    return result


def compare(want, got):
    contiguous(want)
    contiguous(got)
    if len(want) != len(got):
        raise ValueError(f'checkpoint count mismatch: {len(want)}/{len(got)}')
    wet = mismatches = 0
    first = None
    for frame in range(len(want) - 1):
        expected, actual = want[frame + 1], got[frame]
        wet += bool(expected['swim'])
        differences = {key: (expected[key], actual[key]) for key in FIELDS if expected[key] != actual[key]}
        if differences:
            mismatches += 1
            if first is None:
                first = (frame, differences)
    if not wet:
        raise ValueError('no original wet frames: not a water gate')
    return len(want) - 1, wet, mismatches, first


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('native', type=Path)
    args = parser.parse_args()
    count, wet, mismatches, first = compare(original(args.original), native(args.native))
    print(f'Water: {count} completed frames; {wet} original wet; {mismatches} mismatches; terminal excluded')
    print('First divergence:', first)
    return int(mismatches != 0)


if __name__ == '__main__':
    raise SystemExit(main())
