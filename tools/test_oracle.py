#!/usr/bin/env python3
"""Exercise oracle source trust checks against a real disposable Git repository."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import hashlib
from oracle import verified_sources, copy_saves, scenario


class HardwareMouseScenarios(unittest.TestCase):
    def parse(self, text):
        with tempfile.TemporaryDirectory(prefix="cm-oracle-input-") as directory:
            path = Path(directory) / "mouse.keys"
            path.write_text(text)
            return scenario(path)

    def test_raw_mouse_events_use_driver_coordinates_and_button_masks(self):
        ips, events, captures, states = self.parse(
            "key 1 1c\nmouse 1.25 639 239 0x3\nmouse 2 320 120 0\ncapture 3\n")
        self.assertEqual(ips, 60_000_000)
        self.assertEqual(events, ["--key", "1.0:1c",
                                 "--raw-mouse", "1.25:639:239:3",
                                 "--raw-mouse", "2.0:320:120:0"])
        self.assertEqual(captures, [3.0])
        self.assertEqual(states, [])

    def test_raw_mouse_rejects_out_of_driver_range_and_nonfinite_time(self):
        for event in ("-1 0 0 0", "nan 0 0 0", "inf 0 0 0",
                      "1 -1 0 0", "1 640 0 0", "1 0 -1 0",
                      "1 0 240 0", "1 0 0 4", "1 0 0 -1"):
            with self.subTest(event=event), self.assertRaises(ValueError):
                self.parse(f"mouse {event}\ncapture 3\n")

    def test_keyboard_only_scenario_transport_is_unchanged(self):
        self.assertEqual(self.parse("key 8 9c\nhold 9 10 e48\ncapture 12\n"),
                         (60_000_000, ["--key", "8.0:9c", "--hold", "9.0:10.0:e48"],
                          [12.0], []))


class SavePersistence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cm-oracle-saves-")
        self.source = Path(self.temp.name) / "source"
        self.destination = Path(self.temp.name) / "destination"
        self.source.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def test_copies_only_original_save_names_and_records_exact_hashes(self):
        files = {
            "SAVE010.DAT": b"original autosave bytes",
            "GAMENAME.USR": b"original menu names",
            "SAVEGAME/SAVE000.DAT": b"original manual save bytes",
        }
        (self.source / "SAVEGAME").mkdir()
        for name, data in files.items():
            (self.source / name).write_bytes(data)
        (self.source / "CMDATA.CNF").write_bytes(b"not a save")
        (self.source / "CM_HI.EXE").write_bytes(b"not executable input")
        (self.source / "SAVEGAME/other.dat").write_bytes(b"not a save")
        receipt = copy_saves(self.source, self.destination)
        self.assertEqual(receipt, {name: hashlib.sha256(data).hexdigest()
                                   for name, data in files.items()})
        for name, data in files.items():
            self.assertEqual((self.destination / name).read_bytes(), data)
        self.assertFalse((self.destination / "CMDATA.CNF").exists())
        self.assertFalse((self.destination / "CM_HI.EXE").exists())
        self.assertFalse((self.destination / "SAVEGAME/other.dat").exists())
        replay = Path(self.temp.name) / "replay"
        self.assertEqual(copy_saves(self.destination, replay), receipt)

    def test_symlink_save_and_symlink_save_directory_are_rejected(self):
        outside = Path(self.temp.name) / "outside"
        outside.write_bytes(b"not an original run file")
        (self.source / "SAVE000.DAT").symlink_to(outside)
        with self.assertRaises(ValueError):
            copy_saves(self.source, self.destination)
        (self.source / "SAVE000.DAT").unlink()
        (self.source / "SAVEGAME").symlink_to(outside.parent, target_is_directory=True)
        with self.assertRaises(ValueError):
            copy_saves(self.source, self.destination)

    def test_missing_empty_and_same_directory_fail_closed(self):
        with self.assertRaises(ValueError):
            copy_saves(self.source / "absent", self.destination)
        with self.assertRaises(ValueError):
            copy_saves(self.source, self.destination)
        (self.source / "SAVE000.DAT").write_bytes(b"save")
        with self.assertRaises(ValueError):
            copy_saves(self.source, self.source)

    def test_original_configuration_is_copied_separately_with_its_hash(self):
        from oracle import copy_config
        config = b"\x00\x01original CMDATA bytes\x00\xff"
        (self.source / "CMDATA.CNF").write_bytes(config)
        (self.source / "SAVE000.DAT").write_bytes(b"separate save")
        self.assertEqual(copy_config(self.source, self.destination),
                         hashlib.sha256(config).hexdigest())
        self.assertEqual((self.destination / "CMDATA.CNF").read_bytes(), config)
        self.assertFalse((self.destination / "SAVE000.DAT").exists())

    def test_configuration_missing_symlink_duplicate_and_self_copy_are_rejected(self):
        from oracle import copy_config
        with self.assertRaises(ValueError):
            copy_config(self.source, self.destination)
        outside = Path(self.temp.name) / "outside.cnf"
        outside.write_bytes(b"not an original run config")
        path = self.source / "CMDATA.CNF"
        path.symlink_to(outside)
        with self.assertRaises(ValueError):
            copy_config(self.source, self.destination)
        path.unlink()
        path.write_bytes(b"original config")
        with self.assertRaises(ValueError):
            copy_config(self.source, self.source)
        (self.source / "cmdata.cnf").write_bytes(b"ambiguous config")
        with self.assertRaises(ValueError):
            copy_config(self.source, self.destination)


class HardwareBaselines(unittest.TestCase):
    def test_corrected_hardware_receipt_is_the_baseline_not_the_old_trace(self):
        from check_oracle_bda import expected_outputs
        pixels = {"frame000000.png": "corrected pixels"}
        receipt = {"movement": {"new_producer": "corrected producer",
                                "pixels_after": pixels, "trace_after": "corrected flags"}}
        self.assertEqual(expected_outputs(Path("/unused"), {"binary_sha256": "old producer"},
                                          receipt, "movement"),
                         ("corrected producer", pixels, "corrected flags"))

    def test_incomplete_hardware_receipt_is_rejected(self):
        from check_oracle_bda import expected_outputs
        with self.assertRaises(ValueError):
            expected_outputs(Path("/unused"), {}, {}, "movement")
        receipt = {"movement": {"new_producer": "source", "trace_after": None,
                                "pixels_after": {"../frame000000.png": "hash"}}}
        with self.assertRaises(ValueError):
            expected_outputs(Path("/unused"), {}, receipt, "movement")


class SourceTrust(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cm-oracle-test-")
        root = Path(self.temp.name)
        self.repo = root / "repo"
        self.repo.mkdir()
        self.git("init", "-q")
        (self.repo / "main.rs").write_text("old\n")
        self.git("add", "main.rs")
        self.git("-c", "user.name=Oracle Test", "-c", "user.email=test@invalid",
                 "commit", "-qm", "base")
        self.patch = root / "oracle.patch"
        self.patch.write_text("diff --git a/main.rs b/main.rs\n--- a/main.rs\n+++ b/main.rs\n@@ -1 +1 @@\n-old\n+original\n"
                              "diff --git a/trace.rs b/trace.rs\nnew file mode 100644\n--- /dev/null\n+++ b/trace.rs\n@@ -0,0 +1 @@\n+observer\n")

    def tearDown(self):
        self.temp.cleanup()

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.repo)

    def test_clean_checkout_then_exact_applied_patch_is_idempotent(self):
        verified_sources(self.repo, self.patch)
        self.assertEqual((self.repo / "main.rs").read_text(), "original\n")
        self.assertEqual((self.repo / "trace.rs").read_text(), "observer\n")
        verified_sources(self.repo, self.patch)

    def test_untracked_unrelated_source_is_rejected_before_apply(self):
        (self.repo / "extra.rs").write_text("mutation\n")
        with self.assertRaises(ValueError):
            verified_sources(self.repo, self.patch)
        self.assertEqual((self.repo / "main.rs").read_text(), "old\n")

    def test_extra_hunk_in_allowed_source_is_rejected(self):
        verified_sources(self.repo, self.patch)
        with (self.repo / "main.rs").open("a") as out:
            out.write("mutation\n")
        with self.assertRaises(ValueError):
            verified_sources(self.repo, self.patch)

    def test_modified_observer_and_staged_change_are_rejected(self):
        verified_sources(self.repo, self.patch)
        (self.repo / "trace.rs").write_text("not observer\n")
        with self.assertRaises(ValueError):
            verified_sources(self.repo, self.patch)
        (self.repo / "trace.rs").write_text("observer\n")
        self.git("add", "trace.rs")
        with self.assertRaises(ValueError):
            verified_sources(self.repo, self.patch)


if __name__ == "__main__":
    unittest.main()
