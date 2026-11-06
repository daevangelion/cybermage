#!/usr/bin/env python3
"""Compare calculated PC hand/weapon state and missiles at completed frame boundaries.

Capture Xox1444F4:1524,1313A8:112,135ECC:576 and projectile XY/Z/heading
spans with --frame-trace. Compare original entryN+1 with native post-frameN.
No recorded state is supplied to the port: its replay consumes clock and keys.
"""
import argparse
from collections import Counter
import csv
from pathlib import Path
import re
import struct

from compare_projectiles import original as projectile_original, native as projectile_native, COMPARE_FIELDS

PC_FIELDS = {
    'glow_frame': (0x1444F4, 'i'), 'glow_set': (0x1444F8, 'i'),
    'pc_mode': (0x14480C, 'i'), 'hand_type': (0x144A40, 'i'),
    'pc_a48': (0x144A48, 'i'), 'sword_frame': (0x144A4C, 'i'),
    'muzzle_frame': (0x144A5C, 'i'), 'muzzle_mode': (0x144A60, 'i'),
    'muzzle_count': (0x144A64, 'i'),
    'pc_hx': (0x144AA8, 'i'), 'pc_hy': (0x144AAC, 'i'),
    'pc_ab0': (0x144AB0, 'i'), 'pc_ab4': (0x144AB4, 'i'),
    'pc_ab8': (0x144AB8, 'i'), 'pc_ax': (0x144ABC, 'i'),
    'pc_ay': (0x144AC0, 'i'), 'render_x': (0x144AD8, 'i'),
    'render_y': (0x144ADC, 'i'), 'selected_spell': (0x1313A8, 'i'),
    'selected_melee': (0x1313AC, 'i'), 'selected_weapon': (0x1313B0, 'i'),
    'energy': (0x131410, 'h'),
}


def pc_original(path):
    result = {}
    with path.open(newline='') as stream:
        rows = csv.reader(stream)
        magic = next(rows)
        if len(magic) != 2 or magic[0] != 'CMTRACE1':
            raise ValueError('not a CMTRACE1 trace')
        ranges = []
        for _ in range(int(magic[1])):
            tag, kind, address, length = next(rows)
            if tag != 'range' or kind != 'x':
                raise ValueError('PC comparison requires Xox-relative spans')
            ranges.append((int(address, 16), int(length)))
        first = next(rows).index('ranges...')
        for row in rows:
            if len(row) != first + len(ranges):
                raise ValueError('trace range count mismatch')
            spans = [(address, bytes.fromhex(text)) for (address, _), text in zip(ranges, row[first:])]
            if any(len(data) != length for (_, data), (_, length) in zip(spans, ranges)):
                raise ValueError('trace range length mismatch')
            frame = int(row[0])
            if frame in result:
                raise ValueError(f'duplicate original frame {frame}')
            fields = {}
            for name, (address, kind) in PC_FIELDS.items():
                width = struct.calcsize('<' + kind)
                for start, data in spans:
                    if start <= address and address + width <= start + len(data):
                        fields[name] = struct.unpack_from('<' + kind, data, address - start)[0]
                        break
                else:
                    raise ValueError(f'missing original {name} at Xox{address:x}')
            result[frame] = fields
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('native', type=Path)
    args = parser.parse_args()
    want_missiles = projectile_original(args.original)
    got_missiles = projectile_native(args.native)
    want = pc_original(args.original)
    if len(got_missiles) != len(want_missiles) or len(want) != len(want_missiles):
        raise ValueError('native/original checkpoint count mismatch')
    if not any(p['pc_ab0'] for p in want.values()):
        raise ValueError('original has no fire input: not a firing/hand verification')
    original_active = sum(bool(m['active']) for row in want_missiles.values() for m in row)
    if not original_active and len({p['sword_frame'] for p in want.values()}) < 2:
        raise ValueError('original has neither projectile activity nor a changing sword pose')
    differences = Counter()
    first = None
    active = 0
    for frame in range(len(want)-1):
        state = args.native / f'frame{frame:06d}.state'
        actual = {key: int(value) for key, value in re.findall(r'\b([A-Za-z0-9_]+)=(-?[0-9]+)', state.read_text())}
        pairs = []
        for name, expected in want[frame+1].items():
            if name not in actual:
                raise ValueError(f'{state}: missing {name}')
            pairs.append((name, expected, actual[name]))
        for slot, (expected, got) in enumerate(zip(want_missiles[frame+1], got_missiles[frame])):
            fields = COMPARE_FIELDS if expected['active'] or got['active'] else ('active',)
            active += bool(expected['active'] or got['active'])
            pairs.extend((f'm{slot}_{name}', expected[name], got[name]) for name in fields)
        for name, expected, got in pairs:
            if expected != got:
                differences[name] += 1
                if first is None:
                    first = {'native_frame': frame, 'original_frame': frame+1,
                             'field': name, 'expected': expected, 'actual': got}
    print(f'PC fire: {len(want)-1} completed frames, {len(PC_FIELDS)} PC fields/frame, '
          f'{4*(len(want)-1)} missile checkpoints ({active} active); terminal native frame excluded')
    print('Field differences:', dict(differences))
    print('First divergence:', first)
    return int(first is not None)


if __name__ == '__main__':
    raise SystemExit(main())
