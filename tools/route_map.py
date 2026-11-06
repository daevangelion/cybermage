#!/usr/bin/env python3
"""Draw read-only retail map walls/switches to plan real-key oracle routes.

SCENE section layout: docs/re/formats.md, CM_HI.EXE LoadMAP 027960.
This is a navigation aid, not a passability oracle or evidence of a scene clear.
"""
import argparse
from pathlib import Path
import struct

from PIL import Image, ImageDraw
from rsc import parse

SIDE, LEVELS, SCALE, MARGIN = 32, 4, 18, 28
PANEL = SIDE * SCALE + MARGIN * 2


def draw(archive, scene, output, player=None, persons=()):
    data = archive.read_bytes()
    entries = parse(data, archive)
    def section(index):
        at, size = entries[index * 12 + scene]
        if not at:
            raise ValueError(f"scene {scene}: missing section {index}")
        return memoryview(data)[at:at + size]
    grid = section(0)
    if len(grid) != 54790 or struct.unpack_from("<3H", grid) != (SIDE, SIDE, LEVELS):
        raise ValueError("unsupported map dimensions/length")
    flags = struct.unpack_from("<256I", grid, 518)
    image = Image.new("RGB", (PANEL * 2, PANEL * 2), "white")
    ink = ImageDraw.Draw(image)
    for level in range(LEVELS):
        ox = (level % 2) * PANEL + MARGIN
        oy = (level // 2) * PANEL + MARGIN
        ink.text((ox, oy - 24), f"scene {scene}, floor_z level {level}", fill="black")
        for y in range(SIDE):
            ink.text((ox - 22, oy + y * SCALE), str(y), fill="black")
            for x in range(SIDE):
                if not y:
                    ink.text((ox + x * SCALE, oy - 12), str(x), fill="black")
                tile = y * SIDE + x
                cell = level * SIDE * SIDE + tile
                wall = struct.unpack_from("<I", grid, 1542 + cell * 4)[0]
                sentinel = struct.unpack_from("<I", grid, 1542 + tile * 4)[0]
                px, py = ox + x * SCALE, oy + y * SCALE
                floor = grid[17926 + cell * 5]
                ink.rectangle((px, py, px + SCALE, py + SCALE),
                              fill="#333333" if sentinel == 0xffffffff else
                              "#e8e8e8" if floor else "#b9c8d8", outline="#c0c0c0")
                # RaySight 05D6D4: sides are east, south, west, north.
                sides = ((px + SCALE, py, px + SCALE, py + SCALE),
                         (px, py + SCALE, px + SCALE, py + SCALE),
                         (px, py, px, py + SCALE),
                         (px, py, px + SCALE, py))
                for side in range(4):
                    panel = (wall >> (side * 8)) & 255
                    if panel:
                        ink.line(sides[side], fill="#c08000" if flags[panel] & 0x40000000
                                 else "black", width=2)
    switches = section(5)
    switch_count = struct.unpack_from("<I", switches)[0]
    if switch_count >= 128 or len(switches) < (switch_count + 1) * 16:
        raise ValueError("invalid switch records")
    for index in range(1, switch_count + 1):
        tile = struct.unpack_from("<I", switches, index * 16)[0]
        if tile >= SIDE * SIDE * LEVELS:
            raise ValueError("invalid switch tile")
        level, tile = divmod(tile, SIDE * SIDE)
        y, x = divmod(tile, SIDE)
        px = (level % 2) * PANEL + MARGIN + x * SCALE
        py = (level // 2) * PANEL + MARGIN + y * SCALE
        ink.text((px, py), str(index - 1), fill="#b00000")
    # LoadPERSONS 022294: 351-byte records; active type+345, XY+319/+323,
    # floor_z+347. Runtime person indices omit nonpositive placement types.
    placements = section(3)
    person_count = struct.unpack_from("<I", placements)[0]
    if person_count > 2842 or len(placements) < (person_count + 1) * 351:
        raise ValueError("invalid person records")
    runtime = 0
    for index in range(person_count):
        at = (index + 1) * 351
        kind = struct.unpack_from("<h", placements, at + 345)[0]
        if kind <= 0:
            continue
        if runtime in persons:
            x, y = struct.unpack_from("<ii", placements, at + 319)
            z = struct.unpack_from("<i", placements, at + 347)[0]
            level = z // 65536
            print(f"person={runtime} type={kind} x={x} y={y} floor_z={z}")
            if 0 <= level < LEVELS:
                px = (level % 2) * PANEL + MARGIN + x * SCALE / 65536
                py = (level // 2) * PANEL + MARGIN + y * SCALE / 65536
                ink.ellipse((px - 4, py - 4, px + 4, py + 4), fill="green")
                ink.text((px + 5, py - 5), f"P{runtime}", fill="green")
        runtime += 1
    if player:
        x, y, z = player
        level = z // 65536
        if not 0 <= level < LEVELS:
            raise ValueError("player height outside map")
        px = (level % 2) * PANEL + MARGIN + x * SCALE / 65536
        py = (level // 2) * PANEL + MARGIN + y * SCALE / 65536
        ink.ellipse((px - 5, py - 5, px + 5, py + 5), fill="blue")
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)
    print(f"scene={scene} switches={switch_count} persons={runtime} map={output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("scene", type=int, choices=range(12))
    parser.add_argument("output", type=Path)
    parser.add_argument("--player", type=int, nargs=3, metavar=("X", "Y", "Z"))
    parser.add_argument("--person", type=int, action="append", default=[],
                        help="mark original runtime person index")
    args = parser.parse_args()
    draw(args.archive, args.scene, args.output, args.player, args.person)


if __name__ == "__main__":
    main()
