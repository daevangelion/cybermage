#!/usr/bin/env python3
"""Two-NPC lost-target melee regression against retail UpdateObjects x86.

Only visibility/movement/tracking/device boundaries are replaced identically
in the native fixture. Original target restoration, call-local distance,
attack decisions, DamagePerson and all RNG instructions execute unchanged.
"""
from pathlib import Path
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP
from world_original import Original, THIS

PERSON, MONSTER = 0x610000, 0x600000


def run(prior):
    original = Original()
    cpu = original.cpu

    def u32(address, value):
        cpu.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def u16(address, value):
        cpu.mem_write(address, struct.pack('<H', value & 0xffff))

    for offset, value in [(0x130d04, 1), (0x130d08, 0), (0x130d24, 3),
                          (0x130cec, 1), (0x14480c, 5), (0x13181c, MONSTER)]:
        u32(THIS+offset, value)
    for address, value in [(0x207473, 10), (0x1e3944, 1), (0x1e391c, 0),
                           (0x1e3924, 0), (0x925e8, 1)]:
        u32(address, value)
    cpu.mem_write(0x9747f, struct.pack('<3H', 17871, 21011, 26122))
    cpu.mem_write(MONSTER+0x38, bytes([100, 100, 9]))
    u32(MONSTER+0x30, 0x40000)
    u32(MONSTER+0x5c, 49152); u32(MONSTER+0x60, 30720)
    u16(MONSTER+0x66, 12); u16(MONSTER+0x6a, 1)
    cpu.mem_write(MONSTER+0x6e, struct.pack('<BBh', 1, 5, 1))
    for person in range(3):
        address, obj = PERSON+person*0xc0, person+1
        u32(THIS+0x13141c+person*4, address)
        u16(address, obj); u32(address+8, 0)
        u32(address+0x24, 100); u32(address+0x38, (40 << 16) | 40)
        u32(address+0x4c, -1); u32(address+0x50, -1)
        for offset, value in [(0x61418, 0x80000+person*0x1000),
                              (0x64080, 0x80000), (0x66ce8, 0x1000),
                              (0x6c5b8, 0x8000)]:
            u32(THIS+offset+obj*4, value)
        cpu.mem_write(THIS+0x7db42+obj, b'\1')
        cpu.mem_write(THIS+0x7d028+obj, b'\1')
        u16(THIS+0x7b9f4+obj*2, person)
    u16(PERSON+0xc0+0x1c, 1); u32(PERSON+0xc0+0xc, prior)
    u16(PERSON+0xc0+0x1e, 1)
    attack = PERSON+2*0xc0
    u16(attack+0x1c, 4); u32(attack+0xc, 15517)
    for offset in (0x18, 0x1a, 0x34):
        u16(attack+offset, 1)
    u16(attack+0x1e, 10); u16(attack+0x3c, 16)

    def boundary(c, address, size, context):
        del size, context
        if address not in (0x34488, 0x2a9b0, 0x344e0, 0x33ed0, 0x28680):
            return
        if address == 0x2a9b0 and c.reg_read(UC_X86_REG_EDX) == 2:
            u32(attack+8, -1)
        stack = c.reg_read(UC_X86_REG_ESP)
        target = struct.unpack('<I', c.mem_read(stack, 4))[0]
        c.reg_write(UC_X86_REG_EAX, 0)
        c.reg_write(UC_X86_REG_ESP, stack+4)
        c.reg_write(UC_X86_REG_EIP, target)

    cpu.hook_add(UC_HOOK_CODE, boundary)
    original.call(0x348c4, [THIS, 1])
    hp = struct.unpack('<h', cpu.mem_read(PERSON+0x38, 2))[0]
    rng = struct.unpack('<3H', cpu.mem_read(0x9747f, 6))
    target = struct.unpack('<i', cpu.mem_read(attack+8, 4))[0]
    state, cooldown = [struct.unpack('<h', cpu.mem_read(attack+offset, 2))[0] for offset in (0x1c, 0x1e)]
    stance = struct.unpack('<h', cpu.mem_read(attack+0x3c, 2))[0]
    player_stance = struct.unpack('<h', cpu.mem_read(PERSON+0x3c, 2))[0]
    return hp, *rng, target, state, cooldown, stance, player_stance


def main():
    native = Path(sys.argv[1]).resolve()
    for prior in (60000, 30720, 30719, 10000):
        wanted = run(prior)
        actual = tuple(map(int, subprocess.check_output([str(native), str(prior)], text=True).split()))
        if wanted != actual:
            raise SystemExit(f'prior distance={prior}, stale own=15517: original={wanted}, native={actual}')
    missing = subprocess.run([str(native), '60000', '--no-prior-target'],
                             capture_output=True, text=True)
    if missing.returncode or missing.stdout.strip() != 'undefined original melee distance rejected':
        raise SystemExit(f'missing prior target must fail closed: {missing.stdout} {missing.stderr}')
    print('Melee lost-target distance: 4 unmodified retail x86 comparisons matched')
    print('Undefined first-distance read: rejected without player damage')


if __name__ == '__main__':
    main()
