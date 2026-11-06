#!/usr/bin/env python3
"""Compare native world math to unmodified retail x86 functions in Unicorn."""
import argparse
from pathlib import Path
import random
import struct
import subprocess

from dosre.image import LeModule
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
                               UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_FPCW)

ROOT = Path(__file__).resolve().parents[1]
THIS, STACK, STOP = 0x300000, 0x800000, 0x900000


class Original:
    def __init__(self):
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(0, 0x1000000)
        image = LeModule("CM_HI.EXE", (ROOT / "orig/retail/CM_HI.EXE").read_bytes())
        for obj in image.objects:
            self.cpu.mem_write(obj.base, image.read(obj.base, obj.virtual_size))
        self.cpu.reg_write(UC_X86_REG_FPCW, 0x37F)

    def call(self, addr, args):
        regs = (UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ECX)
        for reg, value in zip(regs, args):
            self.cpu.reg_write(reg, value & 0xFFFFFFFF)
        extra = args[4:]
        self.cpu.mem_write(STACK, struct.pack("<" + "I" * (1 + len(extra)), STOP,
                                            *(v & 0xFFFFFFFF for v in extra)))
        self.cpu.reg_write(UC_X86_REG_ESP, STACK)
        self.cpu.emu_start(addr, STOP, count=10_000_000)
        if self.cpu.reg_read(UC_X86_REG_ESP) != STACK + 4 + len(extra) * 4:
            raise ValueError(f"original 0x{addr:x}: unexpected stack cleanup")
        return self.cpu.reg_read(UC_X86_REG_EAX)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    args = parser.parse_args()
    native = args.native.resolve()
    original = Original()
    original.call(0x58F88, [THIS])
    expected = [struct.unpack("<2048i", original.cpu.mem_read(THIS + off, 8192))
                for off in (0xB427C, 0xB627C, 0xB827C)]
    lines = subprocess.check_output([str(native), "trig"], text=True).splitlines()
    actual = [tuple(map(int, line.split())) for line in lines]
    mismatches = [(i, tuple(t[i] for t in expected), got) for i, got in enumerate(actual)
                  if got != tuple(t[i] for t in expected)]
    if len(actual) != 2048 or mismatches:
        raise ValueError(f"trig mismatches {len(mismatches)}: {mismatches[:8]}")
    rng = random.Random(0xC1995)
    vectors = [(0,0,0,0),(0,0,65536,0),(0,0,0,65536),
               (0,0,-65536,0),(0,0,0,-65536),(1,2,4,6)]
    vectors += [tuple(rng.randint(-0x200000, 0x200000) for _ in range(4)) for _ in range(100)]
    for vector in vectors:
        angle = original.call(0x598E4, [THIS, *vector]) & 0xFFFF
        distance = original.call(0x5F9A0, [THIS, *vector])
        got = tuple(map(int, subprocess.check_output([str(native), "math", *map(str,vector)], text=True).split()))
        if got != (angle, distance):
            raise ValueError(f"math {vector}: native={got} original={(angle,distance)}")
    print(f"original x86: trig 6144 values and {len(vectors)} atan64k/Distance vectors matched")


if __name__ == "__main__":
    main()
