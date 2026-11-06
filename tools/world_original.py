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
    def wr32(addr, value):
        original.cpu.mem_write(addr, struct.pack("<I", value & 0xFFFFFFFF))
    for seed in [(3000,0,29999),(1,2,3),(0xFFFF,0x8000,0x7FFF)]:
        original.cpu.mem_write(0x9747F,struct.pack("<3H",*seed))
        native_rows = subprocess.check_output([str(native),"rng",*map(str,seed)],text=True).splitlines()
        for row in native_rows:
            result = original.call(0x68AD0,[])
            wanted = (result,*struct.unpack("<3H",original.cpu.mem_read(0x9747F,6)))
            if tuple(map(int,row.split())) != wanted:
                raise ValueError(f"RANDOM mismatch: native={row} original={wanted}")
    original.cpu.mem_write(0x9747F,struct.pack("<3H",1,2,3))
    if original.call(0x6731C,[0]) != 0 or bytes(original.cpu.mem_read(0x9747F,6)) != struct.pack("<3H",1,2,3):
        raise ValueError("original zero-bound RND advanced state")
    wr32(0x46C,0xABCD1234)
    original.call(0x68ABF,[])
    if struct.unpack("<3H",original.cpu.mem_read(0x9747F,6)) != (1,0x1234,3):
        raise ValueError("original BIOS seed overwrote outer RNG words")
    for angle in range(0,65536,4096):
        vector=(0x53001,0x79FFF,0x68000,0x68000,angle)
        wr32(0xA00000,vector[0]); wr32(0xA00004,vector[1])
        original.call(0x596E4,[THIS,0xA00000,0xA00004,*vector[2:]])
        wanted=struct.unpack("<2i",original.cpu.mem_read(0xA00000,8))
        got=tuple(map(int,subprocess.check_output([str(native),"rotate",*map(str,vector)],text=True).split()))
        if got != wanted:
            raise ValueError(f"FixedRotate {vector}: native={got} original={wanted}")
    walls, ceilings, floors = 0x600000, 0x610000, 0x620000
    for field, value in [(0xC2074,walls),(0xC3088,ceilings),(0xC408C,floors),
                         (0x5E7A4,3),(0x61418+4,0x58000),(0x64080+4,0x58000)]:
        wr32(THIS + field,value)
    directions = [(0,0x78000,0x58000),(0x2000,0x78000,0x78000),
                  (0x4000,0x58000,0x78000),(0x6000,0x38000,0x78000),
                  (0x8000,0x38000,0x58000),(0xA000,0x38000,0x38000),
                  (0xC000,0x58000,0x38000),(0xE000,0x78000,0x38000)]
    ray_count = 0
    for angle, tx, ty in directions:
        for flags in [0xFFFFFFFF,0,0x40000000,0x50000000,0x40100000]:
            for level in range(4):
                wr32(walls + (level * 1024 + 5 * 32 + 5) * 4,
                     0 if flags == 0xFFFFFFFF else 0x01010101)
            wr32(THIS+0x5DFA4+4,flags)
            wr32(THIS+0x61418+8,tx); wr32(THIS+0x64080+8,ty)
            z, tz = 0x8000, 0x8000
            distance = original.call(0x5F9A0,[THIS,0x58000,0x58000,tx,ty])
            sight = original.call(0x5D6D4,[THIS,1,2,distance,z,tz,angle])
            pointers = [0xA00000+i*4 for i in range(4)]
            missile = original.call(0x5E5C0,[THIS,0x58000,0x58000,z,tx,ty,tz,angle,distance,*pointers])
            signed = lambda v: v if v < 0x80000000 else v - 0x100000000
            wanted = (signed(sight),signed(missile),
                      *struct.unpack("<4i",original.cpu.mem_read(pointers[0],16)))
            got = tuple(map(int, subprocess.check_output(
                [str(native),"sight",*map(str,(angle,tx,ty,z,tz,flags))],text=True).split()))
            if got != wanted:
                raise ValueError(f"ray angle={angle:#x} flags={flags:#x}: native={got} original={wanted}")
            ray_count += 1
    print(f"original x86: trig 6144 values, {len(vectors)} atan64k/Distance, 60 RANDOM, 16 FixedRotate and {ray_count} sight/missile vectors matched")


if __name__ == "__main__":
    main()
