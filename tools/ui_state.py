#!/usr/bin/env python3
"""Inspect original UI trace words, strings, and widget package dimensions."""
import argparse
import csv
import struct
from pathlib import Path

from rsc import parse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=Path('orig/retail'))
    parser.add_argument('--trace', type=Path)
    parser.add_argument('--strings', action='store_true')
    parser.add_argument('--banks', action='store_true')
    args = parser.parse_args()
    if args.strings:
        for index, value in enumerate((args.data/'STRINGS.MSG').read_bytes().split(b'\0')):
            print(index, repr(value.decode('cp437')))
    if args.banks:
        path = args.data/'HIPAK.RSC'
        data = path.read_bytes()
        entries = parse(data, str(path))
        for bank in (19, 21, 22, 23, 24):
            at, size = entries[bank]
            count, capacity = struct.unpack_from('<HH', data, at)
            print('bank', bank, 'bytes', size, 'count', count, 'capacity', capacity)
            for i in range(count):
                print(i, struct.unpack_from('<H', data, at+4+i*2)[0],
                      struct.unpack_from('<H', data, at+44+i*2)[0])
    if args.trace:
        with args.trace.open(newline='') as source:
            header = source.readline().strip().split(',')
            if header != ['CMTRACE1', '1']:
                raise ValueError('requires exactly one CMTRACE range')
            spec = source.readline().strip().split(',')
            base = int(spec[2], 16)
            rows = list(csv.reader(source))[1:]
        for row in (rows[0], rows[1], rows[-2], rows[-1]):
            values = bytes.fromhex(row[25])
            print('frame', row[0], 'ticks', row[1])
            for i in range(0, len(values)-3, 4):
                print(f'{base+i:06x}', struct.unpack_from('<i', values, i)[0])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
