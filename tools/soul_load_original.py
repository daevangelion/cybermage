#!/usr/bin/env python3
"""Compare native Soul PROP mirrors with unmodified LoadGame05158B..0515B9.

The optional genuine save is decoded by the ordinary native save codec. Its
numeric Soul records are the input to the original post-read fixup, not extra
original runtime state. No guest instruction or output is replaced.
"""
import argparse
from pathlib import Path
import struct
import subprocess
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EIP
from world_original import Original, THIS, ROOT


def original_mirror(records, initial):
    if len(records) != 70 or len(initial) != 10:
        raise ValueError('expected ten seven-word Soul records')
    original = Original()
    cpu = original.cpu
    descriptors = 0x600000
    cpu.mem_write(THIS+0x13610c, struct.pack('<70I', *records))
    addresses = set()
    for i in range(10):
        prop = records[i*7+2]
        if prop >= 2048:
            raise ValueError('Soul PROP index outside original pool')
        address = descriptors+i*12
        cpu.mem_write(THIS+0x131bcc+prop*4, struct.pack('<I', address))
        cpu.mem_write(address+6, struct.pack('<H', initial[i] & 0xffff))
        addresses.add(address+6)
    writes = []

    def observe(c, access, address, size, value, context):
        del c, access, value, context
        if address in addresses:
            if size != 2:
                raise ValueError('original Soul mirror write is not a word')
            writes.append(address)

    cpu.hook_add(UC_HOOK_MEM_WRITE, observe)
    cpu.reg_write(UC_X86_REG_EBP, THIS)
    cpu.reg_write(UC_X86_REG_ESI, THIS+0x118)
    cpu.emu_start(0x5158b, 0x515b9, count=2000)
    count = sum(0 < records[i*7] < 0x80000000 for i in range(10))
    if cpu.reg_read(UC_X86_REG_EIP) != 0x515b9 or len(writes) != count:
        raise ValueError('original fixup did not execute the complete signed-active loop')
    return tuple(struct.unpack('<h', cpu.mem_read(descriptors+i*12+6, 2))[0]
                 for i in range(10)), count


def decoded(native, args):
    result = subprocess.run([str(native), *args], capture_output=True, text=True)
    if result.returncode:
        raise ValueError(f'native save fixture failed: {result.stdout} {result.stderr}')
    lines = result.stdout.splitlines()
    if len(lines) != 2:
        raise ValueError('native save fixture must print records and PROP mirrors')
    records = tuple(map(int, lines[0].split()))
    actual = tuple(map(int, lines[1].split()))
    if len(records) != 70 or len(actual) != 10:
        raise ValueError('truncated native Soul record or PROP output')
    return records, actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native', type=Path)
    parser.add_argument('--save', type=Path)
    parser.add_argument('--data', type=Path, default=ROOT/'orig/retail')
    args = parser.parse_args()
    native = args.native.resolve()
    records, actual = decoded(native, ['--matrix'])
    expected, count = original_mirror(records, list(range(200, 210)))
    if actual != expected:
        raise ValueError(f'Soul PROP mirror: original={expected}, native={actual}')
    print(f'Soul load: ten signed-active/low-word cases match original ({count} word writes)')
    collision = subprocess.run([str(native), '--collision'], capture_output=True, text=True)
    if collision.returncode:
        raise ValueError(f'collision/validation fixture failed: {collision.stdout} {collision.stderr}')
    print(collision.stdout.strip())
    if args.save:
        records, actual = decoded(native, ['--save', str(args.save.resolve()),
                                           '--data', str(args.data.resolve())])
        expected, count = original_mirror(records, [0]*10)
        if not count:
            raise ValueError('supplied save has no active Souls: not a loaded-Soul verification')
        if actual != expected:
            raise ValueError(f'loaded save Soul mirror: original={expected}, native={actual}')
        print(f'Genuine save: {count} active Soul mirrors match original LoadGame fixup; items={actual}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
