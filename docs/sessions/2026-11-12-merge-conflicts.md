# 2026-11-12 · Claude (cloud) · Reduce recurring merge conflicts
- Repos/commits: #179 (merged 2026-11-16), this PR
- Did: replayed every merge since 2026-10-15 with `git merge-tree` to find the conflict hot spots;
  #179 made `tests/CMakeLists.txt` a glob (special tests bring `tests/<name>_test.cmake`), put a blank
  line between `docs/TODO.md` items (checked by `ci/tests/test_todo_spacing.py`) and split the add-on's
  relative imports one per line. This PR adds Kajetan's rule that a PR edits only its own TODO item.
- Found: of 198 merges, `docs/TODO.md` conflicted 15 times, `autoremesher.cpp` 7, `ab.py` 6,
  `tests/CMakeLists.txt` 6, `corpus.json`, bench README and `ENVELOPE.md` 5 each, add-on `__init__.py` 4.
  The blank lines remove 6 of the 15 TODO conflicts on replay; the rest were the same item edited twice
  or two PRs adding the same new number, which the rule addresses. A merge queue would not help (it
  does not resolve conflicts); code conflicts come from long-lived parallel branches.
- Measured: conflict counts above (replay); ctest lists identical before/after #179.
- Next: watch whether `corpus.json` / `ENVELOPE.md` appends keep conflicting before restructuring them.
