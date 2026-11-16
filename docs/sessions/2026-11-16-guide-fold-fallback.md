# 2026-11-16 · Claude (cloud) · Keep guides through the marked-edge fold fallback
- Repos/commits: core on fix/guide-fold-fallback from master a569948.
- Did: the marked-edge fallback (run again without marks when the marked run has more than 2 more
  folded, degenerate or non-quad faces) now counts those faces per chain of marks (a guide, a mark
  line) within 3 quad widths of it. A chain with more than 2 extra faces next to it fails. A run that
  keeps marks may have at most max(2, 1 per 1000 planned quads) more bad faces in all. No failing
  chain: keep the marked run (within that budget); all failing: the plain run as before; otherwise a
  third run with the passing chains, kept if faithful, within the budget and its chains pass again
  (`marked_edges_dropped` 2, new warning "Some marked edges were not followed ..."). New island
  diagnostics `marked_chains`, `marked_chains_dropped`; `badFaceFlags()` in measuremesh.h.
- Found: on the private head with three guide rings the fallback dropped all guides in 7 of 24 runs
  (3k/5k/7k/10k, plain + 5 float32 replicates each) on master. At 10k the marked run's bad faces sat
  3-16 quads off the guides (66 of 69 at 3 or more), where the plain runs fold too (47 of 49). At 3k
  28 of 29 sat within 3 quads of a guide. A disprover found the extra far folds gather at one spot
  ~10 quads off and the guided runs have ~35 % more cones, so they may still come from the guides:
  hence the budget. Without it one 7k run went 1 -> 10 folds.
- Measured (ab.py, realguides set, both sides): guides dropped entirely 7/24 -> 2/24, partly
  (1 or 2 of 3 rings kept) 0 -> 3, 10k 2/6 -> 0/6. Folded faces on the 5 changed runs 16 -> 25
  (10k u0 6 -> 10, u4 6 -> 11). Guide rings followed by a closed loop (guides_report.py): 0/36 both
  sides, so this alone closes no loop. Public lane: full (only marked_suzanne 5k moved, 2 of 790 runs).
- Next: zero-holonomy ring rows on the kept guides (reuse of feat/tube-ring-soft), separate PR.
