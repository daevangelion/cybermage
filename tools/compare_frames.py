#!/usr/bin/env python3
"""Compare RGB frames (PNG/PPM/BMP). Nonzero differences return status 1."""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops


def compare(left, right, diff, tolerance, rect=None):
    with Image.open(left) as source:
        a = source.convert('RGB')
    with Image.open(right) as source:
        b = source.convert('RGB')
    if a.size != b.size:
        raise ValueError(f'dimensions differ: {a.size} != {b.size}')
    if rect is not None:
        x, y, w, h = rect
        if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > a.width or y + h > a.height:
            raise ValueError('--rect lies outside frame bounds')
        a = a.crop((x, y, x + w, y + h))
        b = b.crop((x, y, x + w, y + h))
    delta = ImageChops.difference(a, b)
    raw = delta.tobytes()
    count = sum(max(raw[i:i+3]) > tolerance for i in range(0, len(raw), 3))
    diff.parent.mkdir(parents=True, exist_ok=True)
    delta.save(diff)
    print(f'{left} vs {right}: {count}/{a.width*a.height} mismatched pixels; diff={diff}')
    return count


def frame_files(path):
    if path.is_file():
        return [path]
    if not path.is_dir():
        raise ValueError(f'not a file or directory: {path}')
    files = sorted(p for p in path.iterdir() if p.suffix.lower() in ('.png', '.ppm', '.bmp'))
    if not files:
        raise ValueError(f'no frames in {path}')
    if len({p.stem for p in files}) != len(files):
        raise ValueError(f'ambiguous duplicate frame stems in {path}')
    return files


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('port', type=Path)
    ap.add_argument('oracle', type=Path)
    ap.add_argument('--diff', type=Path, default=Path('out/diff'))
    ap.add_argument('--tolerance', type=int, default=0)
    ap.add_argument('--rect', help='compare only x,y,width,height')
    args = ap.parse_args(argv)
    if not 0 <= args.tolerance <= 255:
        ap.error('--tolerance must be 0..255')
    try:
        rect = None
        if args.rect is not None:
            rect = tuple(int(n) for n in args.rect.split(','))
            if len(rect) != 4:
                raise ValueError('--rect requires x,y,width,height')
        left, right = frame_files(args.port), frame_files(args.oracle)
        if len(left) != len(right):
            raise ValueError(f'frame counts differ: {len(left)} != {len(right)}')
        if len(left) > 1 and [p.stem for p in left] != [p.stem for p in right]:
            raise ValueError('frame stems differ; use explicit files for differing capture names')
        count = 0
        for a, b in zip(left, right):
            dst = args.diff if args.diff.suffix.lower() == '.png' else args.diff / (a.stem + '.png')
            if len(left) > 1 and dst == args.diff:
                raise ValueError('--diff must be a directory for multiple frames')
            count += compare(a, b, dst, args.tolerance, rect)
        print(f'total: {count} mismatched pixels across {len(left)} frame(s)')
        return int(bool(count))
    except (OSError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
