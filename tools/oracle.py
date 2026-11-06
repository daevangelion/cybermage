#!/usr/bin/env python3
"""Run an original-x86 scenario in an isolated, empty writable overlay."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "ref/rust"
REVISION = "6c99942342913bb18cf53df7240bc5725d4719a4"
MARKER = "ORACLE original-x86: native::install bypassed; hooks=0; asset patches=off; raw VGA"


def scenario(path):
    keys, holds, mice, captures, states = [], [], [], [], []
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
            elif command == "mouse" and len(args) == 4:
                time = float(args[0])
                x, y, buttons = int(args[1]), int(args[2]), int(args[3], 0)
                assert math.isfinite(time) and time >= 0
                assert 0 <= x < 640 and 0 <= y < 240 and 0 <= buttons <= 3
                mice += ["--raw-mouse", f"{time}:{x}:{y}:{buttons}"]
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
    return ips, keys + holds + mice, sorted(captures), states


def copy_saves(source, destination):
    """Copy only save files, preserving their DOS-relative location and bytes."""
    source, destination = Path(source), Path(destination)
    if not source.is_dir() or source.resolve() == destination.resolve():
        raise ValueError("save source must exist and differ from the destination")
    directories = [source]
    nested = source / "SAVEGAME"
    if nested.is_symlink():
        raise ValueError(f"save directory is a symlink: {nested}")
    if nested.exists():
        if not nested.is_dir():
            raise ValueError(f"save directory is not a directory: {nested}")
        directories.append(nested)
    files = {}
    for directory in directories:
        for path in sorted(directory.iterdir()):
            name = path.name.upper()
            if name != "GAMENAME.USR" and not (name.startswith("SAVE") and name.endswith(".DAT")):
                continue
            if path.is_symlink() or not path.is_file():
                raise ValueError(f"save is not a regular file: {path}")
            relative = path.relative_to(source).as_posix().upper()
            if relative in files:
                raise ValueError(f"duplicate DOS save name: {relative}")
            files[relative] = path
    if not files:
        raise ValueError(f"no SAVE*.DAT or GAMENAME.USR files in {source}")
    receipt = {}
    for relative, path in files.items():
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        with target.open("rb") as stream:
            receipt[relative] = hashlib.file_digest(stream, "sha256").hexdigest()
    return receipt


def copy_config(source, destination):
    """Preserve original CMDATA.CNF bytes; never synthesize device settings."""
    source, destination = Path(source), Path(destination)
    if not source.is_dir() or source.resolve() == destination.resolve():
        raise ValueError("configuration source must be a separate directory")
    candidates = [path for path in source.iterdir()
                  if path.name.upper() == "CMDATA.CNF"]
    if len(candidates) != 1:
        raise ValueError(f"need one original CMDATA.CNF in {source}")
    path = candidates[0]
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"configuration is not a regular file: {path}")
    destination.mkdir(parents=True, exist_ok=True)
    target = destination / "CMDATA.CNF"
    if target.is_symlink():
        raise ValueError(f"configuration destination is a symlink: {target}")
    shutil.copyfile(path, target)
    with target.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


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
                        help="record original actor state/RNG and separate IRQ0 animation clock inputs")
    parser.add_argument("--rng-trace", action="store_true",
                        help="record every original RND/RANDOM call site and pre-call RNG words")
    parser.add_argument("--event-trace", action="store_true",
                        help="record read-only MoveWorld entry inputs keyed by frame")
    parser.add_argument("--item-trace", action="store_true",
                        help="record read-only root CollItem inputs from Sim contacts")
    parser.add_argument("--selector-trace", action="store_true",
                        help="record selector device inputs and completed UI save/load actions")
    parser.add_argument("--watch", action="append", default=[], metavar="LINK_HEX",
                        help="read-only byte write-watch; heap addresses use runtime minus 0x400000")
    parser.add_argument("--steering-trace", action="store_true",
                        help="record original NewMONBound decision inputs")
    parser.add_argument("--collision-object", type=int,
                        help="record CollObject input and candidate chains for one object")
    parser.add_argument("--import-saves", type=Path, metavar="DIR",
                        help="seed each run with saves exported by a previous original run")
    parser.add_argument("--export-saves", type=Path, metavar="DIR",
                        help="copy original save files out of the final checkpoint")
    parser.add_argument("--import-config", type=Path, metavar="DIR",
                        help="seed original CMDATA.CNF captured from a previous run")
    args = parser.parse_args()
    if args.collision_object is not None and not 0 <= args.collision_object < 2842:
        raise ValueError("collision object outside original object pool")
    watches = [int(value, 16) for value in args.watch]
    if any(value < 0 or value > 0xFFBFFFFF for value in watches):
        raise ValueError("watch link address overflows runtime relocation")
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
    if (args.trace_range or args.snapshot or args.actor_trace or args.rng_trace or args.steering_trace or args.event_trace or args.item_trace or args.selector_trace or args.collision_object is not None) and not args.frame_trace:
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
    if watches:
        env["CM_WATCH"] = ",".join(f"{value:x}" for value in watches)
    if states:
        env["CM_DUMP"] = f"{states[0][0]:x}:{states[0][1]}"
    imported_saves, exported_saves, imported_config = {}, {}, {}
    config_sha256 = None
    for index, time in enumerate(captures):
        if args.frame_trace and index == len(captures) - 1:
            env["CM_FRAME_TRACE"] = str(destination / "frames.csv")
            env["CM_FRAME_RANGES"] = ",".join(args.trace_range)
            env["CM_MOUSE_TRACE"] = str(destination / "mouse-input.csv")
            if args.snapshot:
                env["CM_FRAME_SNAPSHOT"] = str(destination / "scene-start")
            if args.actor_trace:
                env["CM_ACTOR_TRACE"] = str(destination / "actors.csv")
                env["CM_ACTOR_CLOCK_TRACE"] = str(destination / "actor-clocks.csv")
            if args.rng_trace:
                env["CM_RNG_TRACE"] = str(destination / "rng.csv")
            if args.event_trace:
                env["CM_EVENT_TRACE"] = str(destination / "events-input.csv")
            if args.item_trace:
                env["CM_ITEM_TRACE"] = str(destination / "items-input.csv")
            if args.selector_trace:
                env["CM_SELECTOR_TRACE"] = str(destination / "selectors-input.csv")
            if args.steering_trace:
                env["CM_STEERING_TRACE"] = str(destination / "steering.csv")
            if args.collision_object is not None:
                env["CM_COLLISION_TRACE"] = str(destination / "collision.csv")
                env["CM_COLLISION_OBJECT"] = str(args.collision_object)
        # Each checkpoint boots with only the explicitly requested earned saves.
        with tempfile.TemporaryDirectory(prefix="oracle-", dir=destination) as temp:
            root = Path(temp)
            (root / "iso").mkdir()
            (root / "iso/CYBERMAG").symlink_to(assets, target_is_directory=True)
            if args.import_saves:
                imported_saves[f"frame{index:06d}"] = copy_saves(args.import_saves, root / "run")
            if args.import_config:
                imported_config[f"frame{index:06d}"] = copy_config(args.import_config, root / "run")
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
            if args.export_saves and index == len(captures) - 1:
                exported_saves = copy_saves(root / "run", args.export_saves)
            if index == len(captures) - 1:
                run = root / "run"
                if run.is_dir() and any(p.name.upper() == "CMDATA.CNF" for p in run.iterdir()):
                    config_sha256 = copy_config(run, destination)
                else:
                    (destination / "CMDATA.CNF").unlink(missing_ok=True)
            print(f"{path.stem} t={time:g}: {destination / f'frame{index:06d}.png'}")
    metadata = {"mode": "original x86; DOS/DPMI/BIOS/VGA HLE; no native game hooks",
                "reference": REVISION, "ips": ips, "capture_seconds": captures,
                "scenario": path.read_text(),
                "import_saves_directory": str(args.import_saves.resolve()) if args.import_saves else None,
                "export_saves_directory": str(args.export_saves.resolve()) if args.export_saves else None,
                "imported_saves": imported_saves,
                "exported_saves": exported_saves,
                "import_config_directory": str(args.import_config.resolve()) if args.import_config else None,
                "imported_config": imported_config,
                "config_file": "CMDATA.CNF" if config_sha256 else None,
                "config_sha256": config_sha256,
                "frame_trace": "frames.csv" if args.frame_trace else None,
                "draw_clock_trace": "draw-clocks.csv" if args.frame_trace else None,
                "ui_action_trace": "ui-actions.csv" if args.frame_trace else None,
                "mouse_trace": "mouse-input.csv" if args.frame_trace else None,
                "trace_ranges": args.trace_range,
                "actor_trace": "actors.csv" if args.actor_trace else None,
                "actor_clock_trace": "actor-clocks.csv" if args.actor_trace else None,
                "seed_trace": "seed.csv" if args.frame_trace else None,
                "rng_trace": "rng.csv" if args.rng_trace else None,
                "event_trace": "events-input.csv" if args.event_trace else None,
                "item_trace": "items-input.csv" if args.item_trace else None,
                "selector_trace": "selectors-input.csv" if args.selector_trace else None,
                "watch_links": watches,
                "steering_trace": "steering.csv" if args.steering_trace else None,
                "collision_object": args.collision_object,
                "exe_sha256": hashlib.sha256((assets / "CM_HI.EXE").read_bytes()).hexdigest(),
                "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "patch_sha256": hashlib.sha256((ROOT / "tools/oracle-original.patch").read_bytes()).hexdigest()}
    (destination / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
