#!/usr/bin/env python3
"""Check saved TextMessage completion against unmodified CM_HI.EXE instructions.

Both implementations receive numeric timer/cursor inputs. The original runs
017F88 until its real page-draw or completion branch; no guest code, timer
read, branch or output is replaced. Rendering is outside this gate comparison.
"""
import argparse
from pathlib import Path
import struct
import subprocess

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP
from world_original import Original, THIS, STACK, STOP


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    args = parser.parse_args()
    clocks = [(0, 0), (1, 0), (2538, 2539), (2539, 2539), (2540, 2539),
              (0x7fffffff, 0x80000000), (0x80000000, 0x7fffffff),
              (0xffffffff, 0), (0, 0xffffffff)]
    cursors = [(1, 1, 1), (0, 0, 1), (0, 1, 0), (0, 0, 0), (1, 0, 0)]
    cases = [(*clock, *cursor) for clock in clocks for cursor in cursors]
    run = subprocess.run([str(args.native.resolve()), "--matrix"],
                         input="".join(" ".join(map(str, case))+"\n" for case in cases),
                         capture_output=True, text=True)
    if run.returncode:
        raise ValueError(f"native caption fixture: {run.stdout}\n{run.stderr}")
    actual = [tuple(map(int, line.split())) for line in run.stdout.splitlines()]
    if len(actual) != len(cases) or any(len(row) != 5 for row in actual):
        raise ValueError("truncated native text gate output")
    original = Original()
    cpu = original.cpu
    branch = []

    def observe(machine, address, size, context):
        del size, context
        if address in (0x180c4, 0x1814a):
            branch.append(address)
            machine.emu_stop()

    cpu.hook_add(UC_HOOK_CODE, observe)
    complete = pending = 0
    for case, got in zip(cases, actual):
        ticks, deadline, line, add, end = case
        cpu.mem_write(0x207473, struct.pack("<I", ticks))
        cpu.mem_write(0x206664, struct.pack("<4I", deadline, line, add, end))
        cpu.mem_write(0x20664b, struct.pack("<H", 1))
        cpu.mem_write(STACK, struct.pack("<I", STOP))
        cpu.reg_write(UC_X86_REG_EAX, THIS)
        cpu.reg_write(UC_X86_REG_ESP, STACK)
        branch.clear()
        cpu.emu_start(0x17f88, STOP, count=100)
        if len(branch) != 1:
            raise ValueError(f"original text branch not reached: {case}")
        done = int(branch[0] == 0x1814a)
        if got[0] != done:
            raise ValueError(f"text completion {case}: native={got[0]}, original={done}")
        if done or end:
            wanted = struct.unpack("<4I", cpu.mem_read(0x206664, 16))
            if got[1:] != wanted:
                raise ValueError(f"text cursor {case}: native={got[1:]}, original={wanted}")
        complete += done
        pending += not done
    if not complete or not pending:
        raise ValueError("missing completion/pending positive control")
    print(f"Text load: {len(cases)} original-x86 timer/cursor cases match "
          f"({complete} complete, {pending} pending)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
