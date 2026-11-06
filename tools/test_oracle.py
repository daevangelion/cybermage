#!/usr/bin/env python3
"""Exercise oracle source trust checks against a real disposable Git repository."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import hashlib
from oracle import verified_sources, copy_saves


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
