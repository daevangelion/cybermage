#!/usr/bin/env python3
"""Compare hand input/stance/AutoSwordSwing/Hand2Hand to unmodified retail x86."""
import argparse
from pathlib import Path
import struct
import subprocess

from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP
from world_original import Original, ROOT, STACK, THIS

PERSON = 0xA00000
FIELDS = (0x144AB0, 0x144AB4, 0x144AB8, 0x144AA8, 0x144AAC, 0x144A4C)
DIRECTIONS = (0x48, 0x4D, 0x50, 0x4B, 0x47, 0x49, 0x4F, 0x51)


def compare(native, weapon, position, auto, seed):
    original = Original()
    cpu = original.cpu

    def put(address, value):
        cpu.mem_write(address, struct.pack('<I', value & 0xFFFFFFFF))

    def field(offset, value):
        put(THIS + offset, value)

    def integer(address):
        return struct.unpack('<i', cpu.mem_read(address, 4))[0]

    original.call(0x58F88, [THIS])
    original.call(0x52644, [THIS])
    for offset, value in ((0x14480C, 2), (0x144810, 2), (0x130D04, 1),
                          (0x13141C, PERSON), (0x1313AC, weapon), (0x144AB0, 1),
                          (0x144AA8, (position % 3 - 1) * 64),
                          (0x144AAC, (1 - position // 3) * 64)):
        field(offset, value)
    put(PERSON + 0x40, position | position << 16)
    put(PERSON + 0x38, 40 | 40 << 16)
    cpu.mem_write(0x1E3C8C, struct.pack('<7i', auto, 0, 0, 0, 0, 0, 0))
    cpu.mem_write(0x9747F, struct.pack('<3H', *seed))
    rows = subprocess.check_output([str(native), str(ROOT / 'orig/retail/CM_HI.EXE'),
                                   'hand', str(weapon), str(position), str(auto),
                                   *map(str, seed)], text=True).splitlines()
    if len(rows) != 80:
        raise ValueError(f'native emitted {len(rows)} hand frames, expected 80')
    for frame, row in enumerate(rows):
        put(0x207473, 300 + frame * 2)
        keys = bytearray(128)
        keys[0x39] = int(frame % 24 < 19)
        keys[DIRECTIONS[frame // 2 % 8]] = 1
        keys[0x2A] = frame // 8 % 2
        cpu.mem_write(0x207C54, bytes(keys))
        cpu.mem_write(0x417, bytes([keys[0x2A] * 2]))
        cpu.reg_write(UC_X86_REG_ESI, THIS)
        cpu.emu_start(0x1C244, 0x1C29A, count=64)
        if cpu.reg_read(UC_X86_REG_EIP) != 0x1C29A:
            raise ValueError('original input transient reset did not reach its exit')
        original.call(0x12DD4, [THIS])
        original.call(0x14754, [THIS])
        if integer(THIS + 0x144AB0):
            # Execute the original inline hand-stance basic blocks, stopping
            # before MovePerson/OMapInsert: those belong to the Sim differential.
            cpu.reg_write(UC_X86_REG_ESI, THIS)
            cpu.reg_write(UC_X86_REG_EDI, PERSON)
            cpu.reg_write(UC_X86_REG_ESP, STACK)
            cpu.emu_start(0x1D466, 0x1D5FD, count=1_000_000)
            if cpu.reg_read(UC_X86_REG_EIP) != 0x1D5FD:
                raise ValueError('original hand stance did not reach its exit')
            original.call(0x1A298, [THIS, PERSON])
        else:
            cpu.reg_write(UC_X86_REG_ESI, THIS)
            cpu.emu_start(0x1ECB8, 0x1ECC2, count=1)
            if cpu.reg_read(UC_X86_REG_EIP) != 0x1ECC2:
                raise ValueError('original released-hand reset did not reach its exit')
        wanted = (frame, *(integer(THIS + off) for off in FIELDS),
                  *struct.unpack('<7i', cpu.mem_read(0x1E3C8C, 28)),
                  *struct.unpack('<2i', cpu.mem_read(0x20684C, 8)),
                  *struct.unpack('<4I', cpu.mem_read(PERSON + 0x3C, 16)),
                  *struct.unpack('<3H', cpu.mem_read(0x9747F, 6)))
        got = tuple(map(int, row.split()))
        if got != wanted:
            raise ValueError(f'hand weapon={weapon} position={position} auto={auto} '
                             f'seed={seed} frame={frame}: native={got}, original={wanted}')
    return len(rows)

def compare_regen(native, maximum, energy, deadline):
    original = Original()
    cpu = original.cpu
    cpu.mem_write(THIS + 0x131410, struct.pack('<hh', energy, maximum))
    cpu.mem_write(0x206834, struct.pack('<I', deadline))
    cpu.reg_write(UC_X86_REG_EBP, STACK + 0x100)
    cpu.mem_write(STACK + 0xFC, struct.pack('<I', THIS))
    rows = subprocess.check_output([str(native), str(ROOT / 'orig/retail/CM_HI.EXE'),
                                   'regen', str(maximum), str(energy), str(deadline)],
                                  text=True).splitlines()
    if len(rows) != 80:
        raise ValueError('native recovery fixture did not emit 80 frames')
    for frame, row in enumerate(rows):
        cpu.mem_write(0x207473, struct.pack('<I', 300 + frame * 2 + frame // 5))
        cpu.emu_start(0x19340, 0x193B7, count=128)
        if cpu.reg_read(UC_X86_REG_EIP) != 0x193B7:
            raise ValueError('original energy recovery did not reach its exit')
        wanted = (frame, struct.unpack('<h', cpu.mem_read(THIS + 0x131410, 2))[0],
                  struct.unpack('<I', cpu.mem_read(0x206834, 4))[0])
        got = tuple(map(int, row.split()))
        if got != wanted:
            raise ValueError(f'recovery max={maximum} energy={energy} deadline={deadline} '
                             f'frame={frame}: native={got}, original={wanted}')
    return len(rows)

def compare_bob(native, phase, speed, x, y, moving):
    original = Original()
    cpu = original.cpu

    def field(offset, value):
        cpu.mem_write(THIS + offset, struct.pack('<I', value & 0xFFFFFFFF))

    field(0x144AD4, phase); field(0x144AD8, x); field(0x144ADC, y)
    field(0x144AEC, speed)
    rows = subprocess.check_output([str(native), str(ROOT / 'orig/retail/CM_HI.EXE'),
                                   'bob', *map(str, (phase, speed, x, y, moving))],
                                  text=True).splitlines()
    if len(rows) != 80:
        raise ValueError('native hand-bob fixture did not emit 80 frames')
    for frame, row in enumerate(rows):
        field(0x144AFC, int(frame % 24 < 16 if moving else 4 <= frame % 24 < 20))
        field(0x144B1C, int(frame % 16 == 12))
        cpu.mem_write(0x1E3944, struct.pack('<I', 2 if frame % 3 else 0))
        cpu.reg_write(UC_X86_REG_ESI, THIS)
        cpu.emu_start(0x1C73F, 0x1C87D, count=128)
        if cpu.reg_read(UC_X86_REG_EIP) != 0x1C87D:
            raise ValueError('original hand bob did not reach its exit')
        wanted = (frame, *struct.unpack('<3i', cpu.mem_read(THIS + 0x144AD4, 12)))
        got = tuple(map(int, row.split()))
        if got != wanted:
            raise ValueError(f'bob phase={phase} speed={speed} xy={x,y} moving={moving} '
                             f'frame={frame}: native={got}, original={wanted}')
    return len(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native', type=Path)
    args = parser.parse_args()
    count = 0
    for seed in ((1, 2, 3), (3000, 44, 29999), (65535, 32768, 32767)):
        for weapon in range(4):
            for position in range(9):
                for auto in (0, 1):
                    count += compare(args.native.resolve(), weapon, position, auto, seed)
    print(f'PC hand: {count} unmodified original x86 frames matched '
          '(4 weapons, 9 initial grids, manual/auto, 3 RNG seeds, 8 directions/Shift, press/hold/release)')
    count = 0
    for maximum, energy in ((0, 0), (1, 0), (9, 8), (10, 9), (40, 35),
                           (590, 589), (600, 599), (999, 994),
                           (32767, 32766), (-32768, -32768)):
        for deadline in (0, 300, 361):
            count += compare_regen(args.native.resolve(), maximum, energy, deadline)
    print(f'PC recovery: {count} unmodified original x86 frames matched '
          '(10 signed max/energy pairs, 3 initial deadlines, strict timer edge)')
    count = 0
    for phase in range(14):
        for speed in (-2147483648, -96, 0, 31, 96, 2147483647):
            count += compare_bob(args.native.resolve(), phase, speed, 7, -9, 1)
    for x, y in ((7, -9), (-7, 9), (2147483647, -2147483648), (-2147483648, 2147483647)):
        count += compare_bob(args.native.resolve(), 0, 0, x, y, 0)
    print(f'PC hand bob: {count} unmodified original x86 frames matched '
          '(14 phases, signed speed extremes, idle decay, moving/jump and timer gates)')


if __name__ == '__main__':
    main()
