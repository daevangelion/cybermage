#!/usr/bin/env python3
"""Export Watcom v3 globals as dosre labels (LE link addresses).

Format reference: ref/rust/tools/wdbg.py; symbols themselves come from the EXE.
"""
import argparse
import csv
import re
import struct
from collections import Counter
from pathlib import Path


def labels(path):
    data = path.read_bytes()
    if len(data) < 14:
        raise ValueError("truncated debug trailer")
    sig, major, minor, _, _, langs, segs, size = struct.unpack_from("<HBBBBHHI", data, len(data) - 14)
    if sig != 0x8386 or major != 3 or size > len(data):
        raise ValueError("expected Watcom v3 debug trailer")
    section = len(data) - size + langs + segs
    mods, globals_, addresses, end, _ = struct.unpack_from("<IIIIH", data, section)
    if not 18 <= mods <= globals_ <= addresses <= end <= len(data) - section:
        raise ValueError("invalid debug section ranges")

    def name(at):
        n = data[at]
        return data[at + 1:at + 1 + n].decode("latin1"), at + 1 + n

    modules = []
    at = section + mods
    while at < section + globals_:
        module, at = name(at + 20)
        modules.append(module)
    by_addr = {}
    at = section + globals_
    while at < section + addresses:
        off, obj, module, kind = struct.unpack_from("<IHHB", data, at)
        mangled, at = name(at + 9)
        if obj not in (1, 2) or module >= len(modules):
            raise ValueError("invalid global object/module")
        addr = {1: 0x10000, 2: 0x90000}[obj] + off
        match = re.match(r"W\?([^$]+)\$", mangled)
        if match:
            readable = match[1]
            cls = re.search(r"\$:([^$]+)\$", mangled)
            if cls:
                readable = cls[1] + "::" + readable
        else:
            readable = mangled.rstrip("_") if not mangled.startswith("_") else mangled
        comment = f"{modules[module]} | kind={kind} | {mangled}"
        if addr in by_addr:
            by_addr[addr][2] += f" | alias: {readable} ({comment})"
        else:
            by_addr[addr] = [addr, readable, comment]
    rows = list(by_addr.values())
    counts = Counter(row[1] for row in rows)
    for row in rows:
        if counts[row[1]] > 1:
            row[1] += f"__{row[0]:06X}"
    return sorted(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    rows = labels(args.exe)
    with args.output.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["module", "addr", "name", "comment"])
        writer.writerows(("CM_HI.EXE", f"0x{addr:06X}", name, comment) for addr, name, comment in rows)
    print(f"wrote {len(rows)} unique addresses; aliases retained in comments")


if __name__ == "__main__":
    main()
