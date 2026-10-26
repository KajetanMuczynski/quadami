# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""ci/docs_only.sh (the "changes" job in ci.yml) on small throwaway git repositories."""
import os
import subprocess
import tempfile
import unittest

SCRIPT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs_only.sh")


def git(repo, *args):
    subprocess.run(["git", "-C", repo, "-c", "user.name=t", "-c", "user.email=t@t", *args],
                   check=True, capture_output=True)


def write(repo, path, text):
    full = os.path.join(repo, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w", encoding="utf-8") as f:
        f.write(text)


class DocsOnly(unittest.TestCase):
    def decide(self, change):
        """Runs `change(repo)` on top of a base commit; returns the macos= value."""
        with tempfile.TemporaryDirectory() as repo:
            git(repo, "init", "-q")
            write(repo, "addon/quadami/worker.py", "".join(f"line {i}\n" for i in range(50)))
            write(repo, "docs/TODO.md", "todo\n")
            write(repo, "README.md", "readme\n")
            git(repo, "add", "-A")
            git(repo, "commit", "-q", "-m", "base")
            change(repo)
            git(repo, "add", "-A")
            git(repo, "commit", "-q", "--allow-empty", "-m", "change")
            env = dict(os.environ)
            env.pop("GITHUB_OUTPUT", None)
            out = subprocess.run(["bash", SCRIPT, "HEAD^", "HEAD"], cwd=repo, env=env,
                                 check=True, capture_output=True, text=True).stdout
        return out.strip().splitlines()[-1]

    def test_docs_and_top_level_markdown_skip_macos(self):
        def change(repo):
            write(repo, "docs/TODO.md", "todo 2\n")
            write(repo, "README.md", "readme 2\n")
            write(repo, "addon/README.md", "addon readme\n")
        self.assertEqual(self.decide(change), "macos=false")

    def test_code_runs_macos(self):
        self.assertEqual(self.decide(lambda r: write(r, "addon/quadami/worker.py", "x\n")),
                         "macos=true")

    def test_markdown_inside_the_addon_package_runs_macos(self):
        self.assertEqual(self.decide(lambda r: write(r, "addon/quadami/NOTES.md", "x\n")),
                         "macos=true")

    def test_move_from_addon_into_docs_runs_macos(self):
        # With rename detection only docs/worker.py would be listed: "documentation only".
        self.assertEqual(self.decide(lambda r: git(r, "mv", "addon/quadami/worker.py",
                                                   "docs/worker.py")),
                         "macos=true")

    def test_no_change_runs_macos(self):
        self.assertEqual(self.decide(lambda r: None), "macos=true")


if __name__ == "__main__":
    unittest.main()
