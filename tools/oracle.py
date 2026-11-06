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


def build():
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REFERENCE, text=True).strip()
    if revision != REVISION:
        raise ValueError(f"reference revision mismatch: need {REVISION}, found {revision}")
    patch = ROOT / "tools/oracle-original.patch"
    # Idempotent, fail closed if the pinned reference has unrelated edits at these hunks.
    reverse = subprocess.run(["git", "apply", "--recount", "--reverse", "--check", str(patch)],
                             cwd=REFERENCE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if reverse.returncode:
        subprocess.run(["git", "apply", "--recount", str(patch)], cwd=REFERENCE, check=True)
    subprocess.run(["cargo", "build", "--release", "--locked"], cwd=REFERENCE, check=True)
    return REFERENCE / "target/release/cybermage"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", help="built-in scenario name, or explicit scenario path")
    args = parser.parse_args()
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
                "exe_sha256": hashlib.sha256((assets / "CM_HI.EXE").read_bytes()).hexdigest(),
                "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "patch_sha256": hashlib.sha256((ROOT / "tools/oracle-original.patch").read_bytes()).hexdigest()}
    (destination / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
