#!/usr/bin/env python3
"""Compare difficulty-scaled attack delays with unmodified SetMonAtkIgnore."""
from pathlib import Path
import struct
import subprocess
import sys

from world_original import Original, THIS

MONSTER = 0x600000
FIELDS = (0x66, 0x7c, 0x94)


def main():
    native = Path(sys.argv[1]).resolve()
    original = Original()
    cpu = original.cpu
    cpu.mem_write(THIS + 0x130cf0, struct.pack('<i', 1))
    cpu.mem_write(THIS + 0x130d28, struct.pack('<I', 3))
    for index in range(3):
        cpu.mem_write(THIS + 0x13181c + index * 4,
                      struct.pack('<I', MONSTER + index * 0x100))
    count = 0
    values = ((0, 0, 0), (0, 24, 1), (1, 2, 3), (-1, -2, -3),
              (-32768, 32767, 24), (32767, -32768, -1),
              (37, 25, 13), (16383, -16385, 32766))
    for previous in range(3):
        for difficulty in range(3):
            for selected in (-1, 0, 1, 2):
                for delays in values:
                    for index in range(3):
                        for offset, value in zip(FIELDS, delays):
                            cpu.mem_write(MONSTER + index * 0x100 + offset,
                                          struct.pack('<h', value))
                    cpu.mem_write(0x925e8, struct.pack('<I', difficulty))
                    original.call(0x53d04, [THIS, selected, previous])
                    expected = tuple(
                        struct.unpack('<h', cpu.mem_read(
                            MONSTER + index * 0x100 + offset, 2))[0]
                        for index in range(3) for offset in FIELDS)
                    arguments = (previous, difficulty, selected, *delays)
                    actual = tuple(map(int, subprocess.check_output(
                        [str(native), *map(str, arguments)], text=True).split()))
                    if actual != expected:
                        raise ValueError(
                            f'previous/difficulty/monster/delays={arguments}: '
                            f'original={expected}, native={actual}')
                    count += 1
    print(f'Attack-ignore difficulty: {count} original-x86 cases matched')


if __name__ == '__main__':
    main()
