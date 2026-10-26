# 2026-10-26 · Claude (cloud thread) · CI fixes before the v0.1.0 tag
- Repos/commits: core a0fd599 (master)
- Did: the addon job installs numpy and pefile from `ci/requirements.txt` pinned with hashes (it ran
  an unpinned `pip install --upgrade pip` and unpinned numpy/pefile in the job that builds the
  shipped zips); qt-app drops `qttools5-dev-tools` and `libqt5svg5-dev` (autoremesher.pro uses
  only core, widgets, opengl); apt installs go through `ci/apt_install.sh` (download capped at 300 s
  per attempt, 3 attempts, resumes partial downloads); the docs-only check is `ci/docs_only.sh` with
  `git diff --no-renames`, tested in `ci/tests/test_docs_only.py`.
- Found: with rename detection a move from `addon/quadami/` into `docs/` listed only the new path, so
  the PR counted as documentation only and skipped macOS (checked on a throwaway repo; the new test
  fails with the old flag).
- Measured: nothing on mesh output (CI files only).
- Next: the release job (`v*` tag) cannot run before a tag; it uses no pip packages from this change.
