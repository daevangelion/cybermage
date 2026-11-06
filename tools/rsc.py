#!/usr/bin/env python3
"""List/extract CyberMage RSC archives, including sparse and reordered entries.

Directory: u32 capacity, u32 count, u32 offsets[capacity], u32 sizes[capacity].
CM_HI.EXE OpenGLOBALFILES 0x21328 (HIPAK reads at 0x2178f..0x21805).
SCENE.RSC/DEMOSCEN.RSC have nine section-major tables of capacity entries.
"""
import argparse
import mmap
import struct
import sys
from pathlib import Path


class FormatError(ValueError):
    pass


def parse(data, name):
    def fail(at, why):
        raise FormatError(f'{name}:0x{at:x}: {why}')

    if len(data) < 8:
        fail(0, 'truncated RSC header')
    capacity, count = struct.unpack_from('<II', data)
    if count > capacity:
        fail(4, 'record count exceeds capacity')
    if Path(name).name.upper() in ('SCENE.RSC', 'DEMOSCEN.RSC'):
        capacity *= 9  # LoadScene 0x11E3D reads 108 offsets and 108 sizes.
    if capacity > (len(data) - 8) // 8:
        fail(0, 'truncated RSC directory')
    end = 8 + capacity * 8
    entries = []
    for i in range(capacity):
        at = 8 + 4 * i
        offset = struct.unpack_from('<I', data, at)[0]
        length = struct.unpack_from('<I', data, at + 4 * capacity)[0]
        if offset == 0 and length == 0:
            entries.append((0, 0))
            continue
        if offset < end or offset > len(data):
            fail(at, f'entry {i}: offset outside payload')
        if length > len(data) - offset:
            fail(at + 4 * capacity, f'entry {i}: size exceeds file')
        entries.append((offset, length))
    return entries


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    commands = ap.add_subparsers(dest='command', required=True)
    listing = commands.add_parser('list')
    listing.add_argument('path', type=Path)
    extract = commands.add_parser('extract')
    extract.add_argument('path', type=Path)
    extract.add_argument('index', type=int)
    extract.add_argument('output', type=Path)
    args = ap.parse_args(argv)
    try:
        with args.path.open('rb') as src:
            if src.seek(0, 2) < 8:
                raise FormatError(f'{args.path}:0x0: truncated RSC header')
            with mmap.mmap(src.fileno(), 0, access=mmap.ACCESS_READ) as data:
                entries = parse(data, args.path)
                if args.command == 'list':
                    capacity, count = struct.unpack_from('<II', data)
                    print(f'{args.path}: capacity={capacity} count={count} entries={len(entries)} bytes={len(data)}')
                    print('index offset size')
                    for i, (offset, length) in enumerate(entries):
                        print(f'{i:5d} 0x{offset:08x} {length}')
                else:
                    if not 0 <= args.index < len(entries):
                        raise FormatError(f'{args.path}:0x0: entry index out of range')
                    offset, length = entries[args.index]
                    if offset == 0:
                        raise FormatError(f'{args.path}:0x{8+4*args.index:x}: empty entry')
                    if args.output.resolve() == args.path.resolve():
                        raise FormatError('output must not overwrite the archive')
                    with args.output.open('wb') as dst:
                        view = memoryview(data)[offset:offset+length]
                        try:
                            dst.write(view)
                        finally:
                            view.release()
        return 0
    except (OSError, FormatError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
