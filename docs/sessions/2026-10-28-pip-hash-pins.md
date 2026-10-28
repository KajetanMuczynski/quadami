# 2026-10-28 · cloud thread · Hash-pin the remaining CI pip installs
- Repos/commits: master ebab7db; follows 2026-10-27-audit-metrics-ab-ci.md (upgrade 4)
- Did: the four pip installs that were pinned by version only (format job, bench tests, release-check
  meshes and check jobs) now install from hash-pinned files with `--require-hashes --no-deps`, like
  `ci/requirements.txt`: `ci/requirements-format.txt`, `tools/bench/requirements.txt` (CPython 3.12
  wheels for Linux x86-64, Windows x64 and macOS arm64), `tools/bench/requirements-meshes.txt` (Linux,
  with scikit-image's dependencies). Versions unchanged; the dependencies pinned at what pip resolves
  today, every wheel at least 7 days old. release-check's PR smoke run now also triggers on the two
  bench files.
- Found: nothing new.
- Measured: each file installs in a fresh Python 3.12 venv; bench tests pass in the bench venv;
  make_synthetic.py in the meshes venv writes 25/25 meshes byte-identical to the current ones.
- Next: runner image pins (C-8); Windows test link time (needs a decision); output-gate mutation cases.
