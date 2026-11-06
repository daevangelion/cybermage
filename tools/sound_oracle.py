#!/usr/bin/env python3
"""Run original PanVolDIGI on scene1 snapshot inputs; compare its SOS calls.

The C fixture supplies original scene coordinates as INPUT, not expected output.
Unicorn executes the retail x86 helper and original x87 distance/atan code.
Only SampleDone and the final CMsosPlaySample device boundary are intercepted.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX
from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP

ROOT = Path(__file__).resolve().parents[1]
THIS = 0x300000
STACK = 0x780000
STOP = 0x7ff000


def load_le(path):
    spec = importlib.util.spec_from_file_location('cm_reference_le', ROOT / 'ref/rust/tools/le.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module.LE(str(path))


def original(image, snapshot, request):
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    cpu.mem_map(0x10000, 0x800000)
    cpu.mem_write(image.lo, bytes(image.mem)); cpu.mem_write(THIS, snapshot)
    player = struct.unpack_from('<I', snapshot, 0x130d04)[0]
    cpu.mem_write(THIS + 0x7e65c + player*2, struct.pack('<H', request['heading']))
    cpu.mem_write(0x1e38c8, struct.pack('<I', 0))  # enable original game helper
    cpu.mem_write(0x1e3d40, struct.pack('<I', 32767))
    cpu.mem_write(0x95c34 + request['sample'], b'\1')  # known loaded sample
    cpu.mem_write(0x1e3d60 + request['sample']*4, struct.pack('<I', 0x600000))
    cpu.mem_write(0x7fe000, b'\xdb\xe3')  # FNINIT, then enter original code
    cpu.emu_start(0x7fe000, 0x7fe002)
    cpu.mem_write(STACK, struct.pack('<III', STOP, request['maximum'], request['minimum']))
    for register, value in ((UC_X86_REG_EAX, THIS), (UC_X86_REG_EDX, request['sample']),
                            (UC_X86_REG_EBX, request['object']), (UC_X86_REG_ECX, request['range']),
                            (UC_X86_REG_ESP, STACK)):
        cpu.reg_write(register, value)
    calls = []

    def hook(uc, address, size, user):
        del size, user
        if address == 0x66d20:  # SampleDone -> first slot is free
            sp = uc.reg_read(UC_X86_REG_ESP)
            target = struct.unpack('<I', uc.mem_read(sp, 4))[0]
            uc.reg_write(UC_X86_REG_EAX, 1); uc.reg_write(UC_X86_REG_ESP, sp + 4)
            uc.reg_write(UC_X86_REG_EIP, target)
        elif address == 0x66c3c:
            sp = uc.reg_read(UC_X86_REG_ESP)
            target, channels, bits, volume, loops, pan = struct.unpack('<6I', uc.mem_read(sp, 24))
            slot = uc.reg_read(UC_X86_REG_EDX)
            sample = struct.unpack('<I', uc.mem_read(0x2072e0 + slot*4, 4))[0]
            calls.append({'sample': sample, 'slot': slot, 'volume': volume,
                          'loops': loops if loops < 0x80000000 else loops-0x100000000,
                          'pan': pan, 'channels': channels, 'bits': bits,
                          'length': uc.reg_read(UC_X86_REG_EBX)})
            uc.reg_write(UC_X86_REG_EAX, slot); uc.reg_write(UC_X86_REG_ESP, sp + 24)
            uc.reg_write(UC_X86_REG_EIP, target)
    cpu.hook_add(UC_HOOK_CODE, hook)
    cpu.emu_start(0x27f2c, STOP, count=200000)
    if cpu.reg_read(UC_X86_REG_EIP) != STOP:
        raise RuntimeError(f'original helper did not return: EIP={cpu.reg_read(UC_X86_REG_EIP):x}')
    if len(calls) != 1:
        raise AssertionError(f'expected one original SOS call, got {calls}')
    return calls[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=ROOT / 'orig/retail')
    parser.add_argument('--snapshot', type=Path, default=ROOT / 'captures/newgame/scene-start.xox')
    parser.add_argument('--fixture', type=Path, default=ROOT / 'port/build/audio/test_sound')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/audio/game-sound-oracle.json')
    args = parser.parse_args()
    image = load_le(args.data / 'CM_HI.EXE'); snapshot = args.snapshot.read_bytes()
    result = subprocess.run([str(args.fixture), str(args.data), str(args.snapshot)],
                            text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f'native sound fixture failed ({result.returncode}): {result.stderr}')
    requests = [json.loads(line) for line in result.stdout.splitlines()]
    comparisons = []
    for request in requests:
        call = original(image, snapshot, request)
        for field in ('sample', 'slot', 'volume', 'pan', 'loops'):
            if call[field] != request[field]:
                raise AssertionError(f'{field}: native {request} != original {call}')
        if call['channels'] != 1 or call['bits'] != 8 or not call['length']:
            raise AssertionError(f'invalid original sample format: {call}')
        comparisons.append({'request': request, 'original': call})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'method': 'CM_HI.EXE:027F2C ->066C3C',
                                     'snapshot': str(args.snapshot), 'comparisons': comparisons}, indent=2) + '\n')
    print(f'original sound oracle: {len(comparisons)} scene1-state requests match sample/slot/pan/volume/loops')
    print(result.stderr.strip())


if __name__ == '__main__':
    main()
