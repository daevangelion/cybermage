#!/usr/bin/env python3
"""Compare loaded scene fields against an original first-HandleNextFrame Xox dump."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("--persons", type=Path)
    parser.add_argument("--scene", type=int, default=1)
    args = parser.parse_args()
    data = args.snapshot.read_bytes()
    if len(data) != 0x144B80:
        raise ValueError("expected full original Xox snapshot (0x144B80 bytes)")
    def value(offset, fmt):
        return struct.unpack_from("<" + fmt,data,offset)[0]
    output = subprocess.check_output([str(args.native.resolve()),"state",str(ROOT/"orig/retail"),str(args.scene)],text=True)
    stats = defaultdict(lambda: {"matched":0,"mismatched":0,"examples":[]})
    def compare(field, index, actual, expected):
        s=stats[field]
        s["matched" if actual == expected else "mismatched"] += 1
        if actual != expected and len(s["examples"]) < 4:
            s["examples"].append({"index":index,"native":actual,"original":expected})
    fields = [("flags",0x5E7B0,"I",4),("x",0x61418,"i",4),("y",0x64080,"i",4),
              ("z",0x66CE8,"i",4),("floor_z",0x78D8C,"i",4),("radius",0x69950,"i",4),
              ("height",0x6C5B8,"i",4),("unk_6F220",0x6F220,"i",4),("unk_71E88",0x71E88,"i",4),
              ("heading",0x7E65C,"H",2),("texture",0x81DDE,"H",2),("person",0x7B9F4,"h",2),
              ("active",0x7DB42,"B",1),("targetable",0x7D028,"B",1),("rotations",0x83412,"b",1)]
    maps = [("walls",0xBE074,"I",4),("floor",0xC308C,"B",1),
            ("ceiling",0xC2088,"B",1),("unk_C5090",0xC4090,"B",1),
            ("region",0xC5094,"B",1),("light",0xCA09C,"B",1),
            ("inertia_mask",0xC6098,"H",2),("path_mask",0xC8098,"H",2)]
    persons = {}
    if args.persons:
        raw = args.persons.read_bytes()
        if raw[:8] != b"CMPERSON":
            raise ValueError("invalid original PERSON snapshot")
        count = struct.unpack_from("<I",raw,8)[0]
        if len(raw) != 12 + count * 200:
            raise ValueError("truncated original PERSON snapshot")
        for at in range(12,len(raw),200):
            index = struct.unpack_from("<I",raw,at)[0]
            persons[index] = raw[at+8:at+200]
    counts = None
    observed = defaultdict(int)
    extra = {
        "map_id":[("flags",0x5DFA4,"I",4),("texture",0x5E5A4,"H",2)],
        "light":[("parameter",0xA48F4,"h",2),("type",0xA4934,"H",2)],
        "curve":[("base",0xA497C,"i",4),("shift",0xA49BC,"i",4),("scale",0xA49FC,"i",4)],
        "texture":[("width",0xA623C,"i",4),("height",0xA7A3C,"i",4),
                   ("half_w",0xA923C,"i",4),("base",0xAAA3C,"i",4),
                   ("depth",0xAC23C,"i",4),("frame_height",0xADA3C,"i",4)],
        "omap":[("next",0xCB0A0,"H",4),("object",0xCB0A2,"H",4)],
        "flatmap":[("next",0xD7244,"H",4),("object",0xD7246,"H",4)]}
    for line in output.splitlines():
        name,*numbers=line.split(); numbers=list(map(int,numbers))
        if name == "counts":
            counts={"native_objects":numbers[0],"native_persons":numbers[1],
                    "original_objects":value(0x130D48,"I"),"original_persons":value(0x130D24,"I")}
        elif name == "texture_count":
            if counts is None: raise ValueError("texture count before world counts")
            counts["native_textures"] = numbers[0]
            counts["original_textures"] = value(0x130D4C,"H")
        elif name in ("map","object") or name in extra:
            index,*numbers=numbers
            schema=maps if name == "map" else fields if name == "object" else extra[name]
            if len(numbers) != len(schema): raise ValueError(f"short native {name} row")
            observed[name] += 1
            for (field,base,fmt,stride),actual in zip(schema,numbers):
                compare(name+"."+field,index,actual,value(base+index*stride,fmt))
        elif name == "person":
            index,*numbers=numbers
            if index not in persons:
                continue
            for (field,offset,fmt),actual in zip(
                [("object",0,"h"),("original_type",2,"h"),("monster_index",4,"h"),
                 ("state",0x1C,"h"),("status",0x36,"h"),
                 ("animation",0xBC,"B"),("cursor",0xBD,"B"),("unk_00BE",0xBE,"h")],numbers):
                compare("person."+field,index,actual,struct.unpack_from("<"+fmt,persons[index],offset)[0])
        else:
            raise ValueError(f"unexpected native state row {name}")
    report={"checkpoint":"original first HandleNextFrame entry after scene initialization",
            "scene":args.scene,"counts":counts,"fields":dict(stats)}
    if counts is None or observed["map"] != 4096 or observed["object"] != counts["native_objects"]:
        raise ValueError("incomplete native scene state")
    for name,n in [("map_id",256),("light",32),("curve",16),
                   ("texture",counts["native_textures"]),("omap",12392),("flatmap",12392)]:
        if observed[name] != n: raise ValueError(f"incomplete native {name} table")
    destination=ROOT/"captures"/f"world-scene{args.scene}-comparison.json"
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(report,indent=2)+"\n")
    for field,s in stats.items():
        print(f"{field}: matched={s['matched']} mismatched={s['mismatched']}")
    print('counts',counts)
    print(destination)
    if any(s["mismatched"] for s in stats.values()):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
