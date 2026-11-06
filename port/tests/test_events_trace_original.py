#!/usr/bin/env python3
"""Negative controls against a supplied, genuine saved-start Events capture."""
import csv
import io
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def csv_rows(path):
    with path.open(newline="") as stream:
        return list(csv.reader(stream))


def put_csv(path, rows):
    with path.open("w", newline="") as stream:
        csv.writer(stream, lineterminator="\n").writerows(rows)


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: test_events_trace_original.py BINARY DATA CAPTURE ORIGINAL_SAVES")
    binary, data, capture, saves = map(lambda value: Path(value).resolve(), sys.argv[1:])
    lines = (capture / "frames.csv").read_text().splitlines()
    count = int(lines[0].split(",")[1])
    header = lines[:count + 2]
    first = next(csv.reader([lines[count + 2]]))
    assert first[0] == "0", "controls require the original first frame, not a shifted checkpoint"
    ranges = [line.split(",") for line in lines[1:count + 1]]
    inputs = {
        "phase": csv_rows(capture / "events-input.csv"),
        "items": csv_rows(capture / "items-input.csv"),
        "actors": csv_rows(capture / "actors.csv"),
        "selector": csv_rows(capture / "selectors-input.csv"),
    }
    assert len(inputs["items"]) > 1, "zero observed calls only prove inventory stability"
    controls = 0
    with tempfile.TemporaryDirectory(prefix="cybermage-events-controls-") as temporary:
        work = Path(temporary)
        user = work / "user"
        user.mkdir()
        for name in ("SAVE000.DAT", "SAVE010.DAT", "GAMENAME.USR"):
            shutil.copyfile(saves / name, user / name)
        if (saves / "CMDATA.CNF").exists():
            shutil.copyfile(saves / "CMDATA.CNF", user / "CMDATA.CNF")
        for key, rows in inputs.items():
            put_csv(work / f"{key}.csv", [rows[0]] + [row for row in rows[1:] if row[0] == "0"])
        frame = work / "frames.csv"

        def write_frame(row, headers=header):
            stream = io.StringIO(newline="")
            csv.writer(stream, lineterminator="\n").writerow(row)
            frame.write_text("\n".join(headers) + "\n" + stream.getvalue())

        def command(trace=frame, phase=work / "phase.csv", item=work / "items.csv",
                    actors=work / "actors.csv", selector=work / "selector.csv"):
            return [str(binary), str(data), str(trace), str(phase),
                    "--replay-load-slot", "0", "--user", str(user),
                    "--item-input", str(item), "--actors", str(actors),
                    "--selector-input", str(selector)]

        def check(args, expected, message=None):
            nonlocal controls
            result = subprocess.run(args, capture_output=True, text=True)
            output = result.stdout + result.stderr
            if expected == 0:
                assert result.returncode == 0, output
            else:
                assert result.returncode != 0, "corrupted observations were accepted\n" + output
                if expected == 1:
                    assert result.returncode == 1, output
            if message:
                assert message in output, output
            controls += 1
            return output

        full = check(command(capture / "frames.csv", capture / "events-input.csv",
                             capture / "items-input.csv", capture / "actors.csv",
                             capture / "selectors-input.csv"), 0)
        print(full.strip())
        assert re.search(r"inventory verifier: [1-9]\d* external CollItem calls;", full), full
        assert re.search(r"selector observations: [1-9]\d* ProcessSelector pointer inputs;", full), full
        # Immutable frame data may be shared, but sidecars resolve beside the
        # supplied path. These controls must not fall back to frame timestamps.
        clocks = csv_rows(capture / "draw-clocks.csv")
        assert clocks[0] == ["frame", "draw", "phase", "pc", "ticks"]
        no_clock = work / "clock-controls"
        no_clock.mkdir()
        (no_clock / "frames.csv").symlink_to(capture / "frames.csv")
        for name in ("seed.csv", "ui-actions.csv", "mouse-input.csv", "actor-clocks.csv"):
            if (capture / name).exists():
                (no_clock / name).symlink_to(capture / name)
        full_args = lambda: command(no_clock / "frames.csv", capture / "events-input.csv",
                                    capture / "items-input.csv", capture / "actors.csv",
                                    capture / "selectors-input.csv")
        check(full_args(), 1, "missing measured draw clock file")
        changed_clocks = [list(row) for row in clocks]
        changed_clocks[1][3] = "193a4"
        put_csv(no_clock / "draw-clocks.csv", changed_clocks)
        check(full_args(), 1,
              "wanted draw=0 phase=0 pc=16391; observed frame=0 draw=0 phase=0 pc=193a4")
        changed_clocks = [list(row) for row in clocks]
        deadline = next(row for row in changed_clocks[1:] if row[0] == "0" and row[3] == "193a4")
        deadline[4] = str((int(deadline[4]) + 1) & 0xFFFFFFFF)
        put_csv(no_clock / "draw-clocks.csv", changed_clocks)
        check(full_args(), 1, "event divergence frame=1 link:206834")
        write_frame(first)
        check(command(), 0)
        paused = list(first)
        offset = 0x19 * 2
        paused[24] = paused[24][:offset] + "02" + paused[24][offset + 2:]
        write_frame(paused)
        check(command(), -1, "dedicated P/pause deadline adjustments")
        for address in (0x130D78, 0x1313A4, 0x131414, 0x144974):
            changed = list(first)
            for index, spec in enumerate(ranges):
                start, length = int(spec[2], 16), int(spec[3])
                if spec[1] == "x" and start <= address < start + length:
                    column = 25 + index
                    offset = (address - start) * 2
                    value = (int(changed[column][offset:offset + 2], 16) + 1) & 255
                    changed[column] = (changed[column][:offset] + f"{value:02x}" +
                                       changed[column][offset + 2:])
                    break
            else:
                raise AssertionError(f"capture lacks required address {address:x}")
            write_frame(changed)
            check(command(), 1, f"event divergence frame=0 Xox+{address:x}")
        write_frame(first)
        actors = [list(row) for row in csv_rows(work / "actors.csv")]
        assert actors[1][5] == "0"
        actors[1][13] = str(int(actors[1][13]) + 1)
        put_csv(work / "bad-hp.csv", actors)
        check(command(actors=work / "bad-hp.csv"), 1, "actor0 HP divergence frame=0")
        partial = list(header)
        changed = list(first)
        for index, spec in enumerate(ranges):
            if spec[1:3] == ["x", "130d64"]:
                partial[index + 1] = "range,x,130d64,1599"
                changed[25 + index] = changed[25 + index][:-2]
                break
        else:
            raise AssertionError("missing exact inventory range")
        write_frame(changed, partial)
        check(command(), -1, "complete inventory/equipment/selector ranges")
        write_frame(first)
        item_rows = [list(row) for row in inputs["items"]]
        item_rows[1][0] = "-1"
        put_csv(work / "bad-item.csv", item_rows)
        check(command(item=work / "bad-item.csv"), -1, "negative item input word")
        item_rows = [list(row) for row in inputs["items"]]
        move_ticks = {int(row[0]): int(row[1]) for row in inputs["phase"][1:]}
        item_frame = int(item_rows[1][0])
        assert item_frame in move_ticks, "positive callback lacks a completed MoveWorld phase"
        item_rows[1][1] = str((move_ticks[item_frame] + 1) & 0xFFFFFFFF)
        put_csv(work / "late-item.csv", item_rows)
        check(command(capture / "frames.csv", capture / "events-input.csv",
                      work / "late-item.csv", capture / "actors.csv",
                      capture / "selectors-input.csv"), -1, "outside the pre-MoveWorld phase")
        selector_rows = [list(row) for row in inputs["selector"]]
        for index, row in enumerate(selector_rows[1:], 1):
            if row[2] == "0":
                del selector_rows[index]
                break
        else:
            raise AssertionError("capture has no ProcessSelector inputs")
        put_csv(work / "missing-selector.csv", selector_rows)
        check(command(capture / "frames.csv", capture / "events-input.csv",
                      capture / "items-input.csv", capture / "actors.csv",
                      work / "missing-selector.csv"), -1, "missing ProcessSelector input frame")
        selector_rows = [list(row) for row in inputs["selector"]]
        hover_indices = [index for index, row in enumerate(selector_rows[1:], 1) if row[2] == "0"]
        assert len(hover_indices) >= 2
        # Counterfactual input, not a golden-output replacement. The genuine
        # route has no hardware pointer; enable one and point at the left cell.
        changed = selector_rows[hover_indices[-2]]
        changed[3], changed[5], changed[6] = "1", "-156", "-96"
        put_csv(work / "changed-pointer.csv", selector_rows)
        check(command(capture / "frames.csv", capture / "events-input.csv",
                      capture / "items-input.csv", capture / "actors.csv",
                      work / "changed-pointer.csv"), 1, "selector device disagrees")
        # Counterfactual *input setting*, not a genuine-route replacement:
        # selecting Mouse in CMDATA is required before hardware can route it.
        original_config = (user / "CMDATA.CNF").read_bytes()
        mouse_config = bytearray(original_config)
        mouse_config[4:8] = (1).to_bytes(4, "little")
        (user / "CMDATA.CNF").write_bytes(mouse_config)
        check(command(capture / "frames.csv", capture / "events-input.csv",
                      capture / "items-input.csv", capture / "actors.csv",
                      work / "changed-pointer.csv"), 1, "Xox+144978")
        (user / "CMDATA.CNF").write_bytes(original_config)
    print(f"original saved event verifier: {controls} positive/negative controls passed")


if __name__ == "__main__":
    main()
