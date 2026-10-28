# 2026-10-28 · cloud thread · Hash-pin the remaining CI pip installs
- Repos/commits: master ebab7db; follows 2026-10-27-audit-metrics-ab-ci.md (upgrade 4)
- Did: the four pip installs that were pinned by version only (format job, bench tests, release-check
  meshes and check jobs) now install from hash-pinned files with `--require-hashes --no-deps`, like
  `ci/requirements.txt`: `ci/requirements-format.txt`, `tools/bench/requirements.txt` and
  `tools/bench/requirements-draco.txt` (CPython 3.12 wheels for Linux x86-64, Windows x64 and macOS
  arm64), `tools/bench/requirements-meshes.txt` (Linux, with scikit-image's dependencies). Versions unchanged; the dependencies pinned at what pip resolves
  today, every wheel at least 7 days old. release-check's PR smoke run now also triggers on the two
  bench files.
- Found: release-check's Windows `check` job hung before ab.py's build in 4 of 4 runs while it
  installed DracoPy too (it did not before; 2 runs with ab.py under a faulthandler wrapper passed, so
  the call was never caught). DracoPy moved to `tools/bench/requirements-draco.txt`, installed only by
  the bench tests in ci.yml; the check job installs the same packages as before. Cause not found
  [inferred: DracoPy's install or the wrapper's changed start-up]. The install step now imports the
  packages under a 10-minute limit.
- Measured: each file installs in a fresh Python 3.12 venv; bench tests pass in the bench venv;
  make_synthetic.py in the meshes venv writes 25/25 meshes byte-identical to the current ones.
- Next: runner image pins (C-8); Windows test link time (needs a decision); output-gate mutation cases.
