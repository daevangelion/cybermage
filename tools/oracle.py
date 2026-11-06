#!/usr/bin/env python3
"""Run an original-x86 scenario in an isolated, empty writable overlay."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "ref/rust"
REVISION = "6c99942342913bb18cf53df7240bc5725d4719a4"
MARKER = "ORACLE original-x86: native::install bypassed; hooks=0; asset patches=off; raw VGA"


def scenario(path):
    keys, holds, captures, states = [], [], [], []
    ips = 60_000_000
    for number, line in enumerate(path.read_text().splitlines(), 1):
        fields = line.split("#", 1)[0].split()
        if not fields:
            continue
        try:
            command, *args = fields
            if command == "key" and len(args) == 2:
                time = float(args[0]); sc = int(args[1], 16)
                assert math.isfinite(time) and time >= 0 and 0 <= sc <= 255
                keys += ["--key", f"{time}:{sc:02x}"]
            elif command == "hold" and len(args) == 3:
                start, end = map(float, args[:2]); sc = args[2]
                assert math.isfinite(start) and math.isfinite(end) and 0 <= start < end
                assert 0 <= int(sc.removeprefix("e"), 16) < 128
                holds += ["--hold", f"{start}:{end}:{sc}"]
            elif command == "capture" and len(args) == 1:
                time = float(args[0])
                assert math.isfinite(time) and time > 0
                captures.append(time)
            elif command == "state" and len(args) == 2:
                addr, length = int(args[0], 16), int(args[1], 0)
                assert 0 <= addr and 0 < length <= 65536 and addr + length <= 0x20AB80
                states.append((addr, length))
            elif command == "ips" and len(args) == 1:
                ips = int(args[0]); assert 1_000_000 <= ips <= 200_000_000
            else:
                raise ValueError("unknown directive or wrong field count")
        except (ValueError, AssertionError) as e:
            raise ValueError(f"{path}:{number}: invalid scenario: {line}") from e
    if not captures or len(set(captures)) != len(captures):
        raise ValueError(f"{path}: need distinct capture times")
    if len(states) > 1:
        raise ValueError(f"{path}: this backend supports one final memory span per capture")
    return ips, keys + holds, sorted(captures), states


def verified_sources(reference, patch):
    """Allow only pristine HEAD or exactly HEAD plus the complete oracle patch."""
    def git(*args, env=None):
        return subprocess.check_output(["git", *args], cwd=reference, env=env)

    with tempfile.TemporaryDirectory(prefix="oracle-index-") as directory:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(directory) / "index"))
        subprocess.run(["git", "read-tree", "HEAD"], cwd=reference, env=env, check=True)
        subprocess.run(["git", "apply", "--cached", "--recount", str(patch)],
                       cwd=reference, env=env, check=True)
        tree = git("write-tree", env=env).decode().strip()
        changed = set(git("diff", "--cached", "--name-only", "-z", "HEAD",
                          env=env).split(b"\0")) - {b""}
        status = git("status", "--porcelain=v1", "-z", "--untracked-files=all")
        if not status:
            # Only a genuinely pristine checkout may receive the patch.
            subprocess.run(["git", "apply", "--recount", str(patch)],
                           cwd=reference, check=True)
        status = git("status", "--porcelain=v1", "-z", "--untracked-files=all")
        entries = [entry for entry in status.split(b"\0") if entry]
        if {entry[3:] for entry in entries} != changed:
            raise ValueError("reference has missing or unrelated edits/untracked files")
        for entry in entries:
            if entry[:3] not in (b" M ", b" D ", b"?? "):
                raise ValueError("reference has staged, renamed or unsupported changes")
            name = os.fsdecode(entry[3:])
            path = reference / name
            listing = git("ls-tree", "-z", tree, "--", name)
            if not listing:
                if path.exists() or path.is_symlink():
                    raise ValueError(f"reference deletion differs from patch: {name}")
                continue
            mode = listing.split(b" ", 1)[0]
            if mode not in (b"100644", b"100755") or path.is_symlink() or not path.is_file():
                raise ValueError(f"reference file type differs from patch: {name}")
            expected = git("show", f"{tree}:{name}")
            if path.read_bytes() != expected or bool(path.stat().st_mode & 0o111) != (mode == b"100755"):
                raise ValueError(f"reference source differs from patch: {name}")


def build():
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REFERENCE, text=True).strip()
    if revision != REVISION:
        raise ValueError(f"reference revision mismatch: need {REVISION}, found {revision}")
    patch = ROOT / "tools/oracle-original.patch"
    verified_sources(REFERENCE, patch)
    subprocess.run(["cargo", "build", "--release", "--locked"], cwd=REFERENCE, check=True)
    return REFERENCE / "target/release/cybermage"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", help="built-in scenario name, or explicit scenario path")
    parser.add_argument("--frame-trace", action="store_true",
                        help="record original HandleNextFrame entry state to frames.csv")
    parser.add_argument("--trace-range", action="append", default=[], metavar="x|l:HEX:DECIMAL",
                        help="extra Xox-relative (x) or link-address (l) memory span")
    parser.add_argument("--snapshot", action="store_true",
                        help="dump initialized Xox and PERSON records at first gameplay frame")
    parser.add_argument("--actor-trace", action="store_true",
                        help="record original object/PERSON state and RNG words each frame")
    parser.add_argument("--rng-trace", action="store_true",
                        help="record every original RND/RANDOM call site and pre-call RNG words")
    args = parser.parse_args()
    total = 0
    for item in args.trace_range:
        fields = item.split(":")
        if len(fields) != 3 or fields[0] not in ("x", "l"):
            raise ValueError("trace range must be x|l:HEX:DECIMAL")
        address, length = int(fields[1], 16), int(fields[2], 10)
        if address < 0 or length <= 0 or address + length > (
                0x144B74 if fields[0] == "x" else 0x20AB80):
            raise ValueError("trace range outside game memory")
        total += length
    if len(args.trace_range) > 8 or total > 65536:
        raise ValueError("frame traces allow eight ranges / 65536 bytes total")
    if (args.trace_range or args.snapshot or args.actor_trace or args.rng_trace) and not args.frame_trace:
        raise ValueError("--trace-range/--snapshot/--actor-trace/--rng-trace require --frame-trace")
    path = Path(args.scenario)
    if not path.is_file():
        path = ROOT / "tools/scenarios" / f"{args.scenario}.txt"
    ips, events, captures, states = scenario(path)
    assets = ROOT / "orig/retail"
    if not (assets / "CM_HI.EXE").is_file():
        raise ValueError("orig/retail/CM_HI.EXE and retail game assets are required")
    binary = build()
    destination = ROOT / "captures" / path.stem
    destination.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("CM_")}
    env["CM_ORIGINAL"] = "1"
    if states:
        env["CM_DUMP"] = f"{states[0][0]:x}:{states[0][1]}"
    for index, time in enumerate(captures):
        if args.frame_trace and index == len(captures) - 1:
            env["CM_FRAME_TRACE"] = str(destination / "frames.csv")
            env["CM_FRAME_RANGES"] = ",".join(args.trace_range)
            if args.snapshot:
                env["CM_FRAME_SNAPSHOT"] = str(destination / "scene-start")
            if args.actor_trace:
                env["CM_ACTOR_TRACE"] = str(destination / "actors.csv")
            if args.rng_trace:
                env["CM_RNG_TRACE"] = str(destination / "rng.csv")
        # Each checkpoint replays from boot: no saved user configuration or stale saves.
        with tempfile.TemporaryDirectory(prefix="oracle-", dir=destination) as temp:
            root = Path(temp)
            (root / "iso").mkdir()
            (root / "iso/CYBERMAG").symlink_to(assets, target_is_directory=True)
            command = [str(binary), "--root", str(root), "--headless", "--seconds", str(time),
                       "--ips", str(ips), *events]
            logpath = destination / f"frame{index:06d}.log"
            with logpath.open("w") as log:
                subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            log = logpath.read_text()
            if MARKER not in log or "FAULT " in log or "program exited with code" in log:
                raise ValueError(f"original run failed; see {logpath}")
            with Image.open(root / "out/final.bmp") as image:
                if image.size != (640, 480):
                    raise ValueError(f"expected 640x480 original VGA, got {image.size}; see {logpath}")
                image.convert("RGB").save(destination / f"frame{index:06d}.png")
            print(f"{path.stem} t={time:g}: {destination / f'frame{index:06d}.png'}")
    metadata = {"mode": "original x86; DOS/DPMI/BIOS/VGA HLE; no native game hooks",
                "reference": REVISION, "ips": ips, "capture_seconds": captures,
                "scenario": path.read_text(),
                "frame_trace": "frames.csv" if args.frame_trace else None,
                "trace_ranges": args.trace_range,
                "actor_trace": "actors.csv" if args.actor_trace else None,
                "seed_trace": "seed.csv" if args.frame_trace else None,
                "rng_trace": "rng.csv" if args.rng_trace else None,
                "exe_sha256": hashlib.sha256((assets / "CM_HI.EXE").read_bytes()).hexdigest(),
                "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "patch_sha256": hashlib.sha256((ROOT / "tools/oracle-original.patch").read_bytes()).hexdigest()}
    (destination / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
