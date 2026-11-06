#!/usr/bin/env python3
"""Compare prepared fire/action and its cooldown with retail UpdateObjects.

Visibility, movement, tracking, PC presentation and missile launch boundaries
are intercepted identically; target-kind filtering, attack decisions, numeric
field writes and all RNG instructions execute unchanged in the original.
"""
from pathlib import Path
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EIP
from world_original import Original, THIS

PERSON, MONSTER = 0x610000, 0x600000
original = Original()
cpu = original.cpu


def u32(address, value):
    cpu.mem_write(address, struct.pack('<I', value & 0xffffffff))


def u16(address, value):
    cpu.mem_write(address, struct.pack('<H', value & 0xffff))


def boundary(c, address, size, context):
    del size, context
    if address not in (0x34488, 0x2a9b0, 0x344e0, 0x33ed0, 0x2dfd0):
        return
    stack = c.reg_read(UC_X86_REG_ESP)
    target = struct.unpack('<I', c.mem_read(stack, 4))[0]
    c.reg_write(UC_X86_REG_EAX, 0)
    c.reg_write(UC_X86_REG_ESP, stack+4)
    c.reg_write(UC_X86_REG_EIP, target)


cpu.hook_add(UC_HOOK_CODE, boundary)


def run(actor_kind, target_kind, alternate, seed):
    cpu.mem_write(THIS, bytes(0x144b80))
    cpu.mem_write(PERSON, bytes(0x180))
    cpu.mem_write(MONSTER, bytes(0x400))
    for offset, value in [(0x130d04, 1), (0x130d08, 0), (0x130d24, 2),
                          (0x130cec, 1), (0x14480c, 7 if target_kind == 17 else 5),
                          (0x13181c, MONSTER), (0x131820, MONSTER+0x200)]:
        u32(THIS+offset, value)
    for address, value in [(0x207473, 10), (0x1e3944, 1), (0x1e391c, 0),
                           (0x1e3924, 0), (0x206674, 0)]:
        u32(address, value)
    cpu.mem_write(0x9747f, struct.pack('<3H', 3000, seed, 29999))
    cpu.mem_write(MONSTER+0x19, bytes([1, 1]))
    cpu.mem_write(MONSTER+0x1d, bytes([alternate]))
    cpu.mem_write(MONSTER+0x38, bytes([100, 0, actor_kind]))
    cpu.mem_write(MONSTER+0x200+0x3a, bytes([target_kind]))
    u32(MONSTER+0x30, 0x40000)
    u16(MONSTER+0x7c, 24); u16(MONSTER+0x7e, 1)
    for person in range(2):
        address, obj = PERSON+person*0xc0, person+1
        u32(THIS+0x13141c+person*4, address)
        u16(address, obj); u16(address+4, 1 if person == 0 else 0)
        u32(address+0x24, 100); u32(address+0x38, 40)
        u32(address+0x4c, -1); u32(address+0x50, -1)
        u32(THIS+0x61418+obj*4, 0x88000+person*0x1000)
        u32(THIS+0x64080+obj*4, 0x88000+person*0x1000)
        cpu.mem_write(THIS+0x7db42+obj, b'\1')
        u16(THIS+0x7b9f4+obj*2, person)
    actor = PERSON+0xc0
    u16(actor+0x1c, 5); u32(actor+0xc, 0x20000)

    def checkpoint():
        words = [struct.unpack('<h', cpu.mem_read(actor+offset, 2))[0]
                 for offset in (0x1c, 0x28)]
        words += [cpu.mem_read(actor+0xbc, 1)[0]]
        words += [struct.unpack('<h', cpu.mem_read(actor+offset, 2))[0]
                  for offset in (0x34, 0x1e, 0x1a)]
        return (*words, *struct.unpack('<3H', cpu.mem_read(0x9747f, 6)))

    original.call(0x348c4, [THIS, 1])
    first = checkpoint()
    u16(actor+0x18, 1); u32(0x207473, 20)
    original.call(0x348c4, [THIS, 1])
    return first, checkpoint()


def main():
    native = Path(sys.argv[1]).resolve()
    count = 0; actions = set()
    for actor_kind in (9, 6):
        for target_kind in (0, 5, 6, 17):
            for alternate in (0, 1):
                for seed in range(1, 5):
                    args = (actor_kind, target_kind, alternate, seed)
                    wanted = run(*args)
                    result = subprocess.check_output([str(native), *map(str, args)], text=True)
                    actual = tuple(tuple(map(int, line.split())) for line in result.splitlines())
                    if wanted != actual:
                        raise ValueError(f'actor/target/alternate/seed={args}: original={wanted}, native={actual}')
                    actions.add(wanted[0][1]); count += 1
    if actions != {0, 3, 4}:
        raise ValueError(f'preparation branch coverage incomplete: {actions}')
    print(f'Attack preparation: {count} original-x86 target-filtered action/cooldown cases matched')


if __name__ == '__main__':
    main()
