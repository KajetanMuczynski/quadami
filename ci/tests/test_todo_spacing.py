# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""docs/TODO.md keeps a blank line before every top-level item.

Most PRs edit an item there. Git merges edits to two neighbouring items cleanly
only when an unchanged line separates them; without it, parallel PRs conflict.
"""
import pathlib
import re
import unittest

TODO = pathlib.Path(__file__).resolve().parents[2] / "docs" / "TODO.md"


def unspaced_items(lines):
    """1-based numbers of top-level items whose previous line is not blank."""
    found, fence = [], False
    for i, line in enumerate(lines):
        if line.startswith("```"):
            fence = not fence
        elif not fence and re.match(r"- \[", line) and i > 0 and lines[i - 1].strip():
            found.append(i + 1)
    return found


class TodoSpacing(unittest.TestCase):
    def test_detector(self):
        self.assertEqual(unspaced_items(["- [ ] a", "- [ ] b", "", "- [x] c", "  - sub"]), [2])
        self.assertEqual(unspaced_items(["```", "- [ ] a", "- [ ] b", "```"]), [])

    def test_todo_items_are_spaced(self):
        lines = TODO.read_text(encoding="utf-8").split("\n")
        bad = unspaced_items(lines)
        self.assertEqual(bad, [], "docs/TODO.md: add a blank line before the item on line(s) %s" % bad)


if __name__ == "__main__":
    unittest.main()
