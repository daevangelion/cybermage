#!/usr/bin/env python3
"""Compare hand input/stance/AutoSwordSwing/Hand2Hand to unmodified retail x86."""
import argparse
from pathlib import Path
import struct
import subprocess

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP
from world_original import Original, ROOT, STACK, THIS

PERSON = 0xA00000
FIELDS = (0x144AB0, 0x144AB4, 0x144AB8, 0x144AA8, 0x144AAC, 0x144A4C)
DIRECTIONS = (0x48, 0x4D, 0x50, 0x4B, 0x47, 0x49, 0x4F, 0x51)

def rng_observer(cpu):
    calls, pending = [], []

    def signed(value):
        return value if value < 0x80000000 else value - 0x100000000

    def observe(uc, address, size, context):
        if address == 0x6731C:
            stack = uc.reg_read(UC_X86_REG_ESP)
            pending.append((struct.unpack('<I', uc.mem_read(stack, 4))[0],
                            signed(uc.reg_read(UC_X86_REG_EAX))))
        elif pending and address == pending[-1][0]:
            _, bound = pending.pop()
            calls.append((bound, signed(uc.reg_read(UC_X86_REG_EAX))))

    cpu.hook_add(UC_HOOK_CODE, observe)  # Read-only: no register/code replacement.
    return calls, pending


def rng_fields(calls, pending):
    if pending or len(calls) > 2:
        raise ValueError('unexpected/incomplete original RND observation')
    fields = [len(calls)]
    for i in range(2):
        fields.extend(calls[i] if i < len(calls) else (-1, -1))
    return tuple(fields)


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

def compare_breath(native, hp, maximum, hum_slot, hum_sound, seed):
    original = Original()
    cpu = original.cpu
    calls, pending = rng_observer(cpu)
    cpu.mem_write(THIS + 0x13141C, struct.pack('<I', PERSON))
    cpu.mem_write(THIS + 0x130D08, struct.pack('<I', 1))
    cpu.mem_write(PERSON + 0x38, struct.pack('<hh', hp, maximum))
    cpu.mem_write(THIS + 0x144A3C, struct.pack('<i', -1))
    cpu.mem_write(0x9261C, struct.pack('<i', hum_slot))
    cpu.mem_write(0x1E3964, struct.pack('<i', hum_sound))
    cpu.mem_write(0x1E38C8, struct.pack('<I', 1))  # Original -x, not a function hook.
    cpu.mem_write(0x9747F, struct.pack('<3H', *seed))
    cpu.reg_write(UC_X86_REG_EBP, STACK + 0x100)
    cpu.reg_write(UC_X86_REG_ESP, STACK)
    cpu.mem_write(STACK + 0xFC, struct.pack('<I', THIS))
    rows = subprocess.check_output([str(native), str(ROOT / 'orig/retail/CM_HI.EXE'),
                                   'breath', *map(str, (hp, maximum, hum_slot, hum_sound, *seed))],
                                  text=True).splitlines()
    if len(rows) != 80:
        raise ValueError('native respiration fixture did not emit 80 frames')
    for frame, row in enumerate(rows):
        calls.clear()
        cpu.emu_start(0x1928B, 0x19340, count=1024)
        if cpu.reg_read(UC_X86_REG_EIP) != 0x19340:
            raise ValueError('original respiration did not reach its exit')
        wanted = (frame, *struct.unpack('<3H', cpu.mem_read(0x9747F, 6)),
                  struct.unpack('<i', cpu.mem_read(THIS + 0x144A3C, 4))[0],
                  *rng_fields(calls, pending))
        got = tuple(map(int, row.split()))
        if got != wanted:
            raise ValueError(f'breathing hp={hp}/{maximum} hum={hum_slot,hum_sound} '
                             f'seed={seed} frame={frame}: native={got}, original={wanted}')
    return len(rows)

def compare_hum(native, mode, primary, pulse, running, request, vehicle, slot, sample, seed):
    original = Original()
    cpu = original.cpu
    calls, pending = rng_observer(cpu)

    def put(address, value):
        cpu.mem_write(address, struct.pack('<I', value & 0xFFFFFFFF))

    for address, value in ((THIS + 0x13141C, PERSON), (THIS + 0x130D04, 1),
                           (THIS + 0x14480C, mode), (THIS + 0x130D14, vehicle),
                           (THIS + 0x144AB0, 1), (THIS + 0x144B00, 16),
                           (THIS + 0x144A3C, -1), (0x9261C, slot),
                           (0x1E3964, sample), (0x1E38C8, 1)):
        put(address, value)
    put(PERSON + 0x40, 4 | 4 << 16)
    cpu.mem_write(0x9747F, struct.pack('<3H', *seed))
    # Native takes the prepared running gate; Sim's separate fixture proves
    # raw request/primary -> gate/cap. Here the original derives it from request.
    rows = subprocess.check_output([str(native), str(ROOT / 'orig/retail/CM_HI.EXE'),
                                   'hum', *map(str, (mode, primary, pulse, running, vehicle,
                                                    slot, sample, *seed))],
                                  text=True).splitlines()
    if len(rows) != 80:
        raise ValueError('native loop fixture did not emit 80 frames')
    for frame, row in enumerate(rows):
        put(THIS + 0x144B10, primary)
        put(THIS + 0x144B08, request)
        put(0x206848, pulse)
        put(0x207473, 300 + frame * 2)
        calls.clear()
        original.call(0x14754, [THIS])
        wanted = (frame, struct.unpack('<i', cpu.mem_read(THIS + 0x144AB0, 4))[0],
                  struct.unpack('<i', cpu.mem_read(0x9261C, 4))[0],
                  struct.unpack('<i', cpu.mem_read(0x1E3964, 4))[0],
                  struct.unpack('<i', cpu.mem_read(THIS + 0x144A3C, 4))[0],
                  *struct.unpack('<3H', cpu.mem_read(0x9747F, 6)),
                  *rng_fields(calls, pending))
        got = tuple(map(int, row.split()))
        if got != wanted:
            raise ValueError(f'loop mode={mode} primary/pulse={primary,pulse} '
                             f'running/request={running,request} vehicle={vehicle} '
                             f'initial={slot,sample} seed={seed} frame={frame}: '
                             f'native={got}, original={wanted}')
    return len(rows)


def compare_tank(native):
    original = Original()
    cpu = original.cpu
    cases = []
    for vehicle in (0, 1):
        for enabled, automatic in ((0, 0), (0, 1), (1, 0), (1, 1)):
            for hand in (0, 1):
                for aim in (0, 18, 19, 31, 32, 47, 48, 63, 64, 2147483647, -2147483648):
                    for y in (-2147483648, -96, -9, -8, -7, -1, 0, 1, 7, 8, 9, 96, 2147483647):
                        cases.append((vehicle, enabled, automatic, hand, aim, y, 300, 299, 0, 0))
    for now, deadline in ((299, 300), (300, 300), (301, 300), (0xffffffff, 0), (0, 0xffffffff)):
        for latch in (0, 1):
            for cooldown in (0, 1):
                for y in (-96, 96):
                    cases.append((0, 1, 1, 1, 40, y, now, deadline, latch, cooldown))
    rows = subprocess.check_output([str(native), str(ROOT / 'orig/retail/CM_HI.EXE'), 'tank'],
                                   input=''.join(' '.join(map(str, case)) + '\n' for case in cases),
                                   text=True).splitlines()
    if len(rows) != len(cases):
        raise ValueError('tank aim fixture row count mismatch')
    for case, row in zip(cases, rows):
        vehicle, enabled, automatic, hand, aim, y, now, deadline, latch, cooldown = case
        values = ((THIS+0x130D14, vehicle), (THIS+0x4B000, enabled),
                  (0x206860, automatic), (0x206824, hand),
                  (THIS+0x144AC4, aim), (THIS+0x144AAC, y),
                  (0x207473, now), (0x206858, deadline),
                  (THIS+0x144AB8, latch), (THIS+0x144AB4, cooldown),
                  (THIS+0x144A50, 1))
        for address, value in values:
            cpu.mem_write(address, struct.pack('<I', value & 0xffffffff))
        cpu.reg_write(UC_X86_REG_ESI, THIS)
        # One original tank dispatch, stopping at the shared switch re-entry.
        cpu.emu_start(0x1D1D9, 0x1D1C1, count=128)
        if cpu.reg_read(UC_X86_REG_EIP) != 0x1D1C1:
            raise ValueError('original tank aim did not reach its dispatch boundary')
        expected = (struct.unpack('<i', cpu.mem_read(THIS+0x144AC4, 4))[0],
                    struct.unpack('<i', cpu.mem_read(THIS+0x144A50, 4))[0])
        actual = tuple(map(int, row.split()))
        if actual != expected:
            raise ValueError(f'tank aim {case}: native={actual}, original={expected}')
    return len(cases)


def compare_melt(native):
    original = Original()
    cpu = original.cpu
    size = 640 * 480
    background = bytes((i * 29 + i // 640) & 255 for i in range(size))
    count = 0
    for seed in ((1, 2, 3), (3000, 44, 29999), (65535, 32768, 32767)):
        for pattern in range(3):
            image = bytes((i * 13 + (i // 640) * 7) & 255 for i in range(size))
            columns = [7 if pattern == 0 else 7 + (i % 61) * 8 if pattern == 1 else 487
                       for i in range(640)]
            cpu.mem_write(THIS, image)
            cpu.mem_write(THIS + 0xE5920, background)
            cpu.mem_write(STACK, struct.pack('<640H', *columns))
            cpu.mem_write(0x9747F, struct.pack('<3H', *seed))
            cpu.mem_write(0x1E38C8, struct.pack('<I', 1))  # Real original -x option.
            data = subprocess.check_output([str(native), str(ROOT / 'orig/retail/CM_HI.EXE'),
                                            'melt', *map(str, seed), str(pattern)])
            cursor = 0
            for pass_number in range(4):
                end = data.index(b'\n', cursor)
                actual = tuple(map(int, data[cursor:end].split()))
                pixels = data[end+1:end+1+size]
                cursor = end + 1 + size
                cpu.mem_write(STACK + 0x500, struct.pack('<III', THIS+0xE5920, 0, THIS+0x4AD80))
                cpu.reg_write(UC_X86_REG_EBP, THIS)
                cpu.reg_write(UC_X86_REG_ESP, STACK)
                cpu.emu_start(0x1547C, 0x15534, count=10_000_000)
                if cpu.reg_read(UC_X86_REG_EIP) != 0x15534:
                    raise ValueError('original melt did not reach its pass boundary')
                expected = (struct.unpack('<I', cpu.mem_read(STACK+0x504, 4))[0],
                            *struct.unpack('<3H', cpu.mem_read(0x9747F, 6)),
                            *struct.unpack('<640H', cpu.mem_read(STACK, 1280)))
                if actual != expected or pixels != bytes(cpu.mem_read(THIS, size)):
                    raise ValueError(f'death melt seed={seed} pattern={pattern} pass={pass_number}: '
                                     'pixel/column/RNG/completion mismatch')
                count += 1
            if cursor != len(data):
                raise ValueError('extra native melt fixture output')
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native', type=Path)
    parser.add_argument('--tank-only', action='store_true')
    parser.add_argument('--melt-only', action='store_true')
    args = parser.parse_args()
    if args.tank_only:
        print(f'PC tank aim: {compare_tank(args.native.resolve())} unmodified original x86 cases matched')
        return
    if args.melt_only:
        print(f'PC death melt: {compare_melt(args.native.resolve())} unmodified original x86 passes matched byte-exactly')
        return
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
    count = 0
    for hp, maximum in ((0, 40), (1, 40), (19, 40), (20, 40), (39, 40),
                        (-1, 40), (32767, 32767)):
        for hum_slot, hum_sound in ((-1, 1), (0, 1), (0, 32)):
            for seed in ((1, 2, 3), (3000, 44, 29999), (65535, 32768, 32767)):
                count += compare_breath(args.native.resolve(), hp, maximum, hum_slot, hum_sound, seed)
    print(f'PC respiration: {count} unmodified original x86 frames matched '
          '(PERSON0, HP half/death edges, real muted RND/volume path, hum gate, 3 RNG seeds)')
    count = 0
    cases = ((2, 1, 0, 0, 768, 0, -1, 0), (2, 0, 1, 1, 768, 0, -1, 0),
             (2, 0, 0, 1, 768, 0, -1, 0), (2, 0, 1, 0, 0, 0, -1, 0),
             (2, 1, 0, 0, 0, 0, -1, 0), (5, 1, 0, 1, 768, 0, -1, 0),
             (5, 0, 0, 1, 768, 0, -1, 0), (5, 0, 0, 0, 0, 0, 0, 1),
             (5, 0, 0, 0, 0, 0, 0, 32), (7, 0, 0, 0, 0, 0, -1, 0),
             (7, 0, 0, 0, 0, 1, -1, 0), (7, 0, 0, 0, 0, 1, 0, 1),
             (2, 1, 0, 0, 768, 0, 0, 1))
    for case in cases:
        for seed in ((1, 2, 3), (3000, 44, 29999), (65535, 32768, 32767)):
            count += compare_hum(args.native.resolve(), *case, seed)
    print(f'PC loops: {count} unmodified original x86 input frames matched '
          '(held-primary versus swing pulse, running, melee, tank/aircar, stop breath and exact RND calls)')
    print(f'PC tank aim: {compare_tank(args.native.resolve())} unmodified original x86 cases matched')
    print(f'PC death melt: {compare_melt(args.native.resolve())} unmodified original x86 passes matched byte-exactly')


if __name__ == '__main__':
    main()
