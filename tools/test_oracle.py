#!/usr/bin/env python3
"""Exercise oracle source trust checks against a real disposable Git repository."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from oracle import verified_sources


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
