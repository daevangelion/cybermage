#!/usr/bin/env python3
"""Compare original pre-frame missile spans against native post-frame state.

Original observer ranges: Xox135ECC:576 and the12 projectile objects' XY,
floor-Z and heading spans. Native: --replay ORIGINAL --state-dump DIRECTORY.
Original rowN+1 is compared with native fileN; the last native file has no
completed next original checkpoint and is explicitly excluded.
"""
import argparse
import csv
from pathlib import Path
import re
import struct

POSITIONS = {'x': 0x61418, 'y': 0x64080, 'z': 0x78d8c, 'heading': 0x7e65c}
MISSILE_COUNT = 12  # Original 576-byte pool; matches AI_MISSILE_MAX.
RECORD_FIELDS = ('active', 'object', 'vertical', 'ticks', 'type', 'target',
                 'source', 'frame', 'sound', 'speed', 'variant', 'radius')
COMPARE_FIELDS = ('active', 'type', 'frame', 'ticks', 'vertical', 'speed',
                  'target', 'x', 'y', 'z', 'heading')


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
            if tag != 'range' or kind != 'x':
                raise ValueError('projectile comparison requires Xox-relative spans')
            ranges.append((int(address, 16), int(length)))
        header = next(rows); first_range = header.index('ranges...')
        for row in rows:
            if len(row) != first_range + len(ranges):
                raise ValueError('trace row/range count mismatch')
            frame = int(row[0])
            if frame in result:
                raise ValueError(f'duplicate original frame {frame}')
            spans = [(address, bytes.fromhex(text)) for (address, length), text in zip(ranges, row[first_range:])]
            for (_, length), (_, data) in zip(ranges, spans):
                if len(data) != length:
                    raise ValueError('trace span length mismatch')

            def read(address, count):
                for start, data in spans:
                    if start <= address and address + count <= start + len(data):
                        return data[address-start:address-start+count]
                raise ValueError(f'missing original Xox span at {address:x}, {count} bytes')

            missiles = []
            for slot in range(MISSILE_COUNT):
                values = struct.unpack('<12i', read(0x135ecc + slot*48, 48))
                value = dict(zip(RECORD_FIELDS, values))
                if value['object'] < 0 or value['object'] >= 2842:
                    raise ValueError('original missile object outside object pool')
                for name, base in POSITIONS.items():
                    width = 2 if name == 'heading' else 4
                    value[name] = struct.unpack('<H' if width == 2 else '<i', read(base + value['object']*width, width))[0]
                missiles.append(value)
            result[frame] = missiles
    if sorted(result) != list(range(len(result))) or len(result) < 2:
        raise ValueError('original trace frames must be contiguous and nonempty')
    return result


def native(path):
    result = {}
    for file in path.glob('frame*.state'):
        match = re.fullmatch(r'frame([0-9]+)\.state', file.name)
        if not match:
            continue
        frame = int(match[1]); fields = dict(re.findall(r'\b([A-Za-z0-9_]+)=(-?[0-9]+)', file.read_text()))
        if frame in result:
            raise ValueError(f'duplicate native frame {frame}')
        missiles = []
        for slot in range(MISSILE_COUNT):
            value = {}
            for field in COMPARE_FIELDS:
                key = f'm{slot}_{field}'
                if key not in fields:
                    raise ValueError(f'{file}: missing {key}')
                value[field] = int(fields[key])
            missiles.append(value)
        result[frame] = missiles
    if not result or sorted(result) != list(range(len(result))):
        raise ValueError('native state files must be contiguous from frame0')
    return result


def summary(rows):
    active = [(frame, slot, missile) for frame, missiles in rows.items()
              for slot, missile in enumerate(missiles) if missile['active']]
    return {'frames': len(rows), 'active_checkpoints': len(active),
            'types': sorted({missile['type'] for _, _, missile in active}),
            'first_active': (active[0][0], active[0][1]) if active else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('native', type=Path, nargs='?')
    args = parser.parse_args()
    want = original(args.original)
    print('Original:', summary(want))
    if not summary(want)['active_checkpoints']:
        raise ValueError('no original projectile activity: this is not a projectile verification')
    if args.native is None:
        return 0
    got = native(args.native)
    print('Native:', summary(got))
    if len(got) != len(want):
        raise ValueError(f'checkpoint count mismatch original={len(want)} native={len(got)}')
    first = None; compared = active = 0
    for frame in range(len(want)-1):
        for slot in range(MISSILE_COUNT):
            expected, actual = want[frame+1][slot], got[frame][slot]
            compared += 1
            fields = COMPARE_FIELDS if expected['active'] or actual['active'] else ('active',)
            active += bool(expected['active'] or actual['active'])
            for field in fields:
                if expected[field] != actual[field] and first is None:
                    first = {'native_frame': frame, 'original_frame': frame+1,
                             'slot': slot, 'field': field, 'expected': expected[field], 'actual': actual[field]}
    print(f'Compared {compared} checkpoints ({active} active); terminal native frame excluded')
    print('First divergence:', first)
    return int(first is not None)


if __name__ == '__main__':
    raise SystemExit(main())
