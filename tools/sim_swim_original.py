#!/usr/bin/env python3
"""Compare the unmodified retail swim block for 160 reduced scene1 water frames.

Uses real scene1 water/dry panel numbers and coordinates, not a boot/playthrough
capture. Only external contact, motion, jump/flight/vehicle inputs are supplied;
the x86 and C reducers independently compute swim state, HP, deadlines and RNG.
"""
from pathlib import Path
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_ESI, UC_X86_REG_ESP
from world_original import Original, THIS, STACK

original = Original()
c = original.cpu
scene = int(sys.argv[2]) if len(sys.argv) == 3 else 1
if scene not in (1, 7):
    raise SystemExit('supported reduced scenes: 1, 7')
panel, art = (30, 29) if scene == 7 else (45, 44)

def word(address, value):
    c.mem_write(address, struct.pack('<I', value & 0xffffffff))

def field(offset, value):
    word(THIS + offset, value)

def read(offset):
    return struct.unpack('<i', c.mem_read(THIS + offset, 4))[0]

floors, flats, person = 0x600000, 0x610000, 0x620000
for offset, value in [(0xc408c, floors), (0xe33e4, flats), (0x130d04, 1),
                      (0x130d08, 0), (0x13141c, person), (0x5e7a4, 3),
                      (0x64084, 18 * 65536 + 32768), (0x78d90, 2 * 65536),
                      (0x66cec, 24576), (0x144b54, -1)]:
    field(offset, value)
c.mem_write(floors + 2 * 1024 + 18 * 32 + 22, bytes([panel]))
c.mem_write(floors + 2 * 1024 + 18 * 32 + 26, bytes([81]))
word(person + 0x38, 40 * 65536 + (30 if scene == 7 else 40))
field(0x5e7a8, int(scene == 7))
word(0x1e38c8, 1)  # Original oracle's -x/noSoundFlag input.
word(0x1e3924, 1); word(0x1e391c, -1 if scene == 7 else 0)
word(0x9260c, panel); word(0x92610, art)
c.mem_write(0x9747f, struct.pack('<3H', 3000, 44, 29999))
rows = subprocess.check_output([str(Path(sys.argv[1]).resolve()), str(scene)], text=True).splitlines()
if len(rows) != 160:
    raise SystemExit(f'expected 160 native frames, got {len(rows)}')
wet = entries = 0
previous = 0
for frame, row in enumerate(rows):
    field(0x6141c, (22 if frame % 40 < 30 else 26) * 65536 + 32768)
    field(0x144b1c, int(frame == 55)); field(0x144b40, int(frame == 90))
    field(0x14480c, 7 if frame == 125 else 5)
    field(0x144aec, 8 if frame % 3 else 0)
    word(0x207473, 100 + frame * 2)
    c.reg_write(UC_X86_REG_ESI, THIS)
    c.reg_write(UC_X86_REG_ESP, STACK)
    c.emu_start(0x1cac0, 0x1cd80, count=100000)
    if c.reg_read(UC_X86_REG_ESP) != STACK:
        raise SystemExit('swim block did not balance its original stack')
    swim = read(0x144b50)
    expected = (frame, swim, read(0x144b34), read(0x144b3c), read(0x144afc),
                read(0x130d1c), struct.unpack('<I', c.mem_read(0x1e3920, 4))[0],
                struct.unpack('<H', c.mem_read(person + 0x38, 2))[0],
                *struct.unpack('<3H', c.mem_read(0x9747f, 6)))
    got = tuple(map(int, row.split()))
    if expected != got:
        raise SystemExit(f'swim frame={frame}: original={expected}, native={got}')
    wet += bool(swim); entries += bool(swim and not previous); previous = swim
scope = 'HP stays40, damage0' if scene == 1 else 'healing HP/RND10/deadline RND60 exact'
print(f'Swim scene{scene}: {len(rows)} original-x86/C frames MATCH; {wet} wet; {entries} entries; state/entry RNG/deadline exact; {scope}; sound off')
