#!/usr/bin/env python3
"""Compare Image1 target candidates, in order, at completed frame boundaries.

Capture --trace-range x:4b00c:324. Original pre-frame N+1 corresponds to
native post-frame N. The terminal native frame is explicitly excluded.
An all-empty match is reported, but is not nonempty auto-aim evidence.
"""
import argparse
import csv
from pathlib import Path
import re
import struct

TARGET_COUNT = 0x4b00c
TARGET_LIST = 0x4b010
TARGET_MAX = 160
OBJECT_MAX = 2842


def contiguous(rows):
    if len(rows) < 2 or sorted(rows) != list(range(len(rows))):
        raise ValueError('target frames must be contiguous from frame0, at least two')


def original(path):
    result = {}
    with path.open(newline='') as stream:
        rows = csv.reader(stream)
        magic = next(rows)
        if len(magic) != 2 or magic[0] != 'CMTRACE1':
            raise ValueError(f'{path}: not a CMTRACE1 trace')
        ranges = []
        for _ in range(int(magic[1])):
            tag, kind, address, length = next(rows)
            if tag != 'range' or kind not in ('x', 'l'):
                raise ValueError('invalid trace range')
            ranges.append((kind, int(address, 16), int(length)))
        first_range = next(rows).index('ranges...')
        for row in rows:
            if len(row) != first_range + len(ranges):
                raise ValueError('trace row/range count mismatch')
            frame = int(row[0])
            if frame in result:
                raise ValueError('duplicate original target frame')
            spans = []
            for (kind, address, length), text in zip(ranges, row[first_range:]):
                data = bytes.fromhex(text)
                if len(data) != length:
                    raise ValueError('trace span length mismatch')
                if kind == 'x':
                    spans.append((address, data))

            def read(address, length):
                for start, data in spans:
                    if start <= address and address + length <= start + len(data):
                        return data[address-start:address-start+length]
                raise ValueError(f'missing original target span at {address:x}, {length} bytes')

            count, = struct.unpack('<i', read(TARGET_COUNT, 4))
            if count < 0 or count > TARGET_MAX:
                raise ValueError('original target count out of range')
            values = list(struct.unpack(f'<{count}H', read(TARGET_LIST, count * 2)))
            if any(value >= OBJECT_MAX for value in values):
                raise ValueError('original target object out of range')
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
            raise ValueError('duplicate native target frame')
        line = re.search(r'^targets count=([0-9]+) objects=([0-9,]*)$', file.read_text(), re.MULTILINE)
        if line is None:
            raise ValueError(f'{file}: missing target list')
        count = int(line[1])
        values = [int(value) for value in line[2].split(',')] if line[2] else []
        if count != len(values) or count > OBJECT_MAX or any(value >= OBJECT_MAX for value in values):
            raise ValueError(f'{file}: invalid native target count/object')
        result[frame] = values
    contiguous(result)
    return result


def compare(want, got):
    contiguous(want)
    contiguous(got)
    if len(want) != len(got):
        raise ValueError(f'checkpoint count mismatch original={len(want)} native={len(got)}')
    result = {'compared': len(want)-1, 'nonempty': 0, 'mismatches': 0, 'first': None}
    for frame in range(len(want)-1):
        expected, actual = want[frame+1], got[frame]
        result['nonempty'] += bool(expected)
        if expected != actual:
            result['mismatches'] += 1
            if result['first'] is None:
                result['first'] = {'native_frame': frame, 'original_frame': frame+1,
                                   'expected': expected, 'actual': actual}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('native', type=Path)
    args = parser.parse_args()
    result = compare(original(args.original), native(args.native))
    print(f"Compared {result['compared']} completed frames; {result['nonempty']} original nonempty; "
          f"{result['mismatches']} mismatches; terminal native frame excluded")
    print('First divergence:', result['first'])
    if not result['nonempty']:
        print('All-empty capture: no nonempty auto-aim evidence')
    return int(result['mismatches'] != 0)


if __name__ == '__main__':
    raise SystemExit(main())
