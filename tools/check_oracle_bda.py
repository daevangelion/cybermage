#!/usr/bin/env python3
"""Compare four original hardware-input baselines without overwriting them."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
CASES = ("title", "newgame", "movement", "events-door")


def image_hash(path):
    with Image.open(path) as image:
        return hashlib.sha256(image.convert("RGB").tobytes()).hexdigest()


def expected_outputs(before, metadata, reference, case):
    if reference is not None:
        try:
            record = reference[case]
            producer, pixels, trace = record["new_producer"], record["pixels_after"], record["trace_after"]
        except (KeyError, TypeError):
            raise ValueError(f"{case}: incomplete hardware baseline receipt") from None
        if not isinstance(pixels, dict) or not pixels or any(
                not isinstance(name, str) or not re.fullmatch(r"frame[0-9]{6,}\.png", name)
                for name in pixels):
            raise ValueError(f"{case}: invalid baseline capture names")
        return producer, pixels, trace
    images = sorted(before.glob("frame*.png"))
    if not images:
        raise ValueError(f"{case}: no retained baseline image")
    pixels = {path.name: image_hash(path) for path in images}
    trace = before / "frames.csv"
    return metadata["binary_sha256"], pixels, (
        hashlib.sha256(trace.read_bytes()).hexdigest() if trace.is_file() else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", default="bios-bda",
                        help="separate capture/receipt label for this hardware correction")
    parser.add_argument("--baseline-receipt", type=Path,
                        help="compare against a prior corrected-hardware comparison.json")
    args = parser.parse_args()
    if not args.label or args.label in (".", "..") or Path(args.label).name != args.label:
        raise ValueError("label must be a single directory name")
    receipts = {}
    recipes = ROOT / "captures" / args.label
    recipes.mkdir(parents=True, exist_ok=True)
    reference = json.loads(args.baseline_receipt.read_text()) if args.baseline_receipt else None
    for case in CASES:
        before = ROOT / "captures" / case
        metadata = json.loads((before / "run.json").read_text())
        baseline_producer, expected, trace_hash = expected_outputs(before, metadata, reference, case)
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
                              "baseline_producer": baseline_producer}
            (recipes / "comparison.json").write_text(json.dumps(receipts, indent=2) + "\n")
            print(f"{case}: CAPTURE FAILED ({process.returncode})", flush=True)
            continue
        after = ROOT / "captures" / scenario.stem
        actual = {name: image_hash(after / name) for name in expected}
        actual_trace = hashlib.sha256((after / "frames.csv").read_bytes()).hexdigest() if trace_hash else None
        new_meta = json.loads((after / "run.json").read_text())
        result = {
            "baseline_producer": baseline_producer,
            "baseline_receipt": str(args.baseline_receipt.resolve()) if args.baseline_receipt else None,
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
