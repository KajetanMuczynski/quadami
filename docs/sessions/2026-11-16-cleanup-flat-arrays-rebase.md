# 2026-11-16 · cloud thread "Rebase extraction cleanup perf branch" · clean-up passes on flat arrays onto master
- Repos/commits: branch `perf/cleanup-flat-arrays` from master 2906a31: the two commits of
  `perf/extraction-cleanup` (fb47a54, c4d4b9a) cherry-picked; PR #188 (TODO 8.9).
- Did: resolved two conflicts in `quadextractor.cpp`/`.h` against master's guide pinning (TODO 5.7) and
  conical tips (TODO 2.23): the rewritten `smoothAndProject` locks pinned guide vertices and tip vertices
  before it collects the moving patch, as master locked them before smoothing. The tip guard in the double
  shared edge merge carried over unchanged.
- Measured: `ab.py lane 2906a31 HEAD`: light, public 790/790 runs byte-identical (all sets with default
  targets, incl. `guides` and `primitives`); private sculpts 44/44 and head guides 4/4 byte-identical. Core
  tests 28/28. Clean-up step sums (stage diagnostics, steps after "Extracting quads", 3 interleaved rounds,
  10k Quality): head ×0.51, cube ×0.52, sword ×0.54. Wall time in this container was too noisy to update
  the 2026-11-05 numbers (5 rounds, round-to-round spread up to 25 %).
- Next: `perf/untangle-projection` and `perf/incremental-cleanup` rebase onto this after it merges (0.4).
