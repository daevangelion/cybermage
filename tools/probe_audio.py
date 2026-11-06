#!/usr/bin/env python3
"""Inspect original CyberMage sound headers and RAW.RSC audio spans."""
import argparse
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('data', type=Path)
    args = parser.parse_args()
    root = args.data
    for name in ('SOUNDSYS.CNF', 'SB16/SOUNDSYS.CNF', 'SC55/SOUNDSYS.CNF'):
        print(name, (root / name).read_text().strip())
    for name in ('MELODIC.BNK', 'DRUM.BNK', 'SPEECH.ENG', 'TEXT.MSG', 'TESTSONG.HMI', 'ECM.TGV'):
        with (root / name).open('rb') as f:
            header = f.read(80)
        print(name, (root / name).stat().st_size, header.hex(' '))
    raw = (root / 'RAW.RSC').read_bytes()
    print('RAW header', raw[:8].hex(' '))
    for i in range(256):
        off = struct.unpack_from('<I', raw, 8 + 4*i)[0]
        length = struct.unpack_from('<I', raw, 8 + 4*256 + 4*i)[0]
        if length and (i >= 192 or raw[off:off+3] == b'HMI'):
            print('RAW', i, hex(off), length, repr(raw[off:off+24]))


if __name__ == '__main__':
    main()
