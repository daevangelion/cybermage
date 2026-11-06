#!/usr/bin/env python3
"""Compare CollFire with unmodified retail x86 for cardinal/diagonal sweeps."""
from pathlib import Path
import struct
import subprocess
import sys
from world_original import Original, THIS

native = Path(sys.argv[1]).resolve()
original = Original()
original.call(0x58f88, [THIS])
c = original.cpu

def word(address, value):
    c.mem_write(address, struct.pack('<I', value & 0xffffffff))

def field(offset, value):
    word(THIS + offset, value)

def signed(n):
    return n if n < 0x80000000 else n - 0x100000000

floors, ceilings, walls, chain = 0x600000, 0x610000, 0x620000, 0x630000
c.mem_write(floors, bytes([1]) * 1024 + bytes(3072))
c.mem_write(ceilings, bytes([1]) * 1024 + bytes(3072))
c.mem_write(walls, bytes(4 * 4096))
c.mem_write(chain, bytes(4 * 12392))
for offset, value in [(0xc408c,floors),(0xc3088,ceilings),(0xc2074,walls),(0xe33e4,chain),
                      (0x5e7a4,3),(0x130d20,0x3ffff),(0x130d48,3),(0x144b6c,2),
                      (0x136224,1),(0x136228,2)]:
    field(offset,value)
count = 0
angles = sorted(set(range(0, 65536, 2048)) | {1,4095,8191,16383,16385,32767,49151,65535})
for panel in (0,1,2):
    for angle in angles:
        cos = struct.unpack('<i',c.mem_read(THIS+0xb827c+(angle>>5)*4,4))[0]
        sin = struct.unpack('<i',c.mem_read(THIS+0xb627c+(angle>>5)*4,4))[0]
        for obj,x,y,z in [(1,0x58000,0x58000,0x100),(2,0x58000+cos,0x58000+sin,0x10000)]:
            for off,value in [(0x61418,x),(0x64080,y),(0x66ce8,z),(0x78d8c,0x8000 if obj==1 else 0),
                              (0x69950,0x8000 if obj==2 else 0),(0x6f220,0),
                              (0x71e88,0x10000 if obj==2 else 0),
                              (0x5e7b0,0x08000000 if panel and obj==2 else 0)]:
                field(off+obj*4,value)
            heading = (angle + (8192 if panel == 2 and obj == 2 else 0)) & 65535
            c.mem_write(THIS+0x7e65c+obj*2,struct.pack('<H',heading))
        c.mem_write(THIS+0x135ecc,struct.pack('<12i',1,1,0,0,2,0,0,0,0,0x20000,0,0))
        result = signed(original.call(0x40050,[THIS,0]))
        wanted = (result,*[signed(struct.unpack('<I',c.mem_read(THIS+off+4,4))[0])
                           for off in (0x61418,0x64080,0x78d8c)],
                  struct.unpack('<H',c.mem_read(THIS+0x7e65c+2,2))[0],
                  signed(struct.unpack('<I',c.mem_read(THIS+0x135ee0,4))[0]),
                  signed(struct.unpack('<I',c.mem_read(THIS+0x135ed4,4))[0]),
                  signed(struct.unpack('<I',c.mem_read(THIS+0x135ed8,4))[0]))
        got = tuple(map(int,subprocess.check_output([str(native),str(angle),str(panel)],text=True).split()))
        if got != wanted:
            raise SystemExit(f'CollFire angle={angle:#x} panel={panel}: native={got}, original={wanted}')
        count += 1
print(f'CollFire: {count} unmodified original x86 sweeps matched')
