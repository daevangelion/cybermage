#!/usr/bin/env python3
"""Compare state14 with unmodified retail 036E3C..036F01.

Only LaunchMissile and MoveObject are intercepted, identically to the native
fixture. All timer, burst, status, animation and RNG instructions run unchanged.
"""
import argparse
import itertools
from pathlib import Path
import struct
import subprocess

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX,
                               UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_EDI,
                               UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP)
from world_original import Original, THIS, STACK

PERSON, MONSTER = 0x610000, 0x600000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    args = parser.parse_args()
    cases = [(0, 1, 1, 1, 1, 3, 1, 1, 0)]
    cases += list(itertools.product((0, 2), (0, 1, 2, -1, -32768),
                  (0, 1, 2, 3, -1, -32768), (0, 1), (0, 1), (0, 3),
                  (0, 1), (1, 65535), (0, 4)))
    result = subprocess.run([str(args.native.resolve())],
        input="".join(" ".join(map(str, row))+"\n" for row in cases),
        capture_output=True, text=True)
    if result.returncode:
        raise ValueError(f"native state14 fixture: {result.stdout}\n{result.stderr}")
    actual = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
    if len(actual) != len(cases) or any(len(row) != 12 for row in actual):
        raise ValueError("truncated native state14 checkpoints")
    original = Original()
    cpu = original.cpu
    calls = []
    current_tick = 0

    def write16(address, value):
        cpu.mem_write(address, struct.pack("<H", value & 0xffff))

    def boundary(c, address, size, context):
        del size, context
        if address not in (0x2dfd0, 0x33ed0):
            return
        registers = tuple(c.reg_read(r) for r in
            (UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ECX))
        wanted = (THIS, 2, 3, 20) if address == 0x2dfd0 else (THIS, 1, current_tick, 0)
        if registers != wanted:
            raise ValueError(f"original call {address:x}: {registers} != {wanted}")
        calls.append(address)
        sp = c.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack("<I", c.mem_read(sp, 4))[0]
        c.reg_write(UC_X86_REG_EAX, 0)
        c.reg_write(UC_X86_REG_ESP, sp+4)
        c.reg_write(UC_X86_REG_EIP, ret)

    cpu.hook_add(UC_HOOK_CODE, boundary)
    launches = random_steps = 0
    for case, got in zip(cases, actual):
        status, cooldown, burst, loop, bp, target, current_tick, seed, span = case
        cpu.mem_write(PERSON, bytes(0xc0))
        for offset, value in ((0x1c, 14), (0x1e, cooldown), (0x34, burst),
                               (0x36, status), (0x16, 0x4321), (0x18, loop)):
            write16(PERSON+offset, value)
        cpu.mem_write(PERSON+0xbc, b"\1")
        write16(MONSTER+0x7c, 7); write16(MONSTER+0x7e, span)
        cpu.mem_write(0x1e3944, struct.pack("<I", bp))
        cpu.mem_write(0x9747f, struct.pack("<3H", 3000, seed, 29999))
        # UpdateObjects locals: animation tick, PERSON id, missile type,
        # target object, source object (036EAF..036EB7).
        cpu.mem_write(STACK, struct.pack("<5I", current_tick, 1, 20, target, 2))
        for register, value in ((UC_X86_REG_ESI, PERSON), (UC_X86_REG_EBP, MONSTER),
                                 (UC_X86_REG_EDI, THIS), (UC_X86_REG_ESP, STACK)):
            cpu.reg_write(register, value)
        calls.clear()
        cpu.emu_start(0x36e3c, 0x36f01, count=1000)
        if cpu.reg_read(UC_X86_REG_EIP) != 0x36f01:
            raise ValueError("original state14 did not reach its MoveObject return")
        words = tuple(struct.unpack("<h", cpu.mem_read(PERSON+o, 2))[0]
                      for o in (0x1e, 0x34, 0x16, 0x18))
        rng = struct.unpack("<3H", cpu.mem_read(0x9747f, 6))
        wanted = (14, cpu.mem_read(PERSON+0xbc, 1)[0], *words, *rng,
                  calls.count(0x2dfd0), calls.count(0x33ed0), current_tick)
        if got != wanted:
            raise ValueError(f"state14 {case}: original={wanted}, native={got}")
        launches += calls.count(0x2dfd0)
        random_steps += rng != (3000, seed, 29999)
    if not launches or not random_steps:
        raise ValueError("state14 positive controls did not launch or advance RNG")
    print(f"State14: {len(cases)} original-x86 cases match; "
          f"{launches} launches, {random_steps} RNG-advancing cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
