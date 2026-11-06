#!/usr/bin/env python3
"""Re-run four existing original scenarios after the BIOS BDA keyboard fix."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
CASES = ("title", "newgame", "movement", "events-door")


def image_hash(path):
    with Image.open(path) as image:
        return hashlib.sha256(image.convert("RGB").tobytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", default="bios-bda",
                        help="separate capture/receipt label for this hardware correction")
    args = parser.parse_args()
    if not args.label or args.label in (".", "..") or Path(args.label).name != args.label:
        raise ValueError("label must be a single directory name")
    receipts = {}
    recipes = ROOT / "captures" / args.label
    recipes.mkdir(parents=True, exist_ok=True)
    for case in CASES:
        before = ROOT / "captures" / case
        metadata = json.loads((before / "run.json").read_text())
        images = sorted(before.glob("frame*.png"))
        if not images:
            raise ValueError(f"{case}: no retained baseline image")
        expected = {p.name: image_hash(p) for p in images}
        trace = before / "frames.csv"
        trace_hash = hashlib.sha256(trace.read_bytes()).hexdigest() if trace.is_file() else None
        scenario = recipes / f"{args.label}-{case}.keys"
        scenario.write_text(metadata["scenario"])
        command = [str(ROOT / "tools/oracle.sh"), str(scenario)]
        if metadata.get("frame_trace"):
            command.append("--frame-trace")
        for span in metadata.get("trace_ranges", []):
            command += ["--trace-range", span]
        process = subprocess.run(command, cwd=ROOT)
        if process.returncode:
            receipts[case] = {"unchanged": False, "capture_exit": process.returncode,
                              "baseline_producer": metadata["binary_sha256"]}
            (recipes / "comparison.json").write_text(json.dumps(receipts, indent=2) + "\n")
            print(f"{case}: CAPTURE FAILED ({process.returncode})", flush=True)
            continue
        after = ROOT / "captures" / scenario.stem
        actual = {name: image_hash(after / name) for name in expected}
        actual_trace = hashlib.sha256((after / "frames.csv").read_bytes()).hexdigest() if trace_hash else None
        new_meta = json.loads((after / "run.json").read_text())
        result = {
            "baseline_producer": metadata["binary_sha256"],
            "new_producer": new_meta["binary_sha256"],
            "new_patch": new_meta["patch_sha256"],
            "pixels_before": expected, "pixels_after": actual,
            "trace_before": trace_hash, "trace_after": actual_trace,
            "unchanged": expected == actual and trace_hash == actual_trace,
        }
        receipts[case] = result
        (recipes / "comparison.json").write_text(json.dumps(receipts, indent=2) + "\n")
        print(f"{case}: {'UNCHANGED' if result['unchanged'] else 'CHANGED'}", flush=True)
        if result["unchanged"]:
            for name in expected:
                (after / name).unlink()
    if not all(result["unchanged"] for result in receipts.values()):
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
