# 2026-11-07 · Claude (cloud) · Incremental clean-up passes after a libQEx fallback
- Repos/commits: core 87ceb37 on perf/incremental-cleanup, built on perf/extraction-cleanup c4d4b9a
- Did: rewrote `cleanupTriangles`, `mergeSharedFiveEdgeFaces` and `collapseThreeValenceDiagonals` so a
  round no longer rebuilds the adjacency and rewrites every face. Faces keep their index while the rounds
  run, an ordered candidate set gives the same pick a full rescan would, and a change re-checks only the
  faces or edges whose answer can depend on it.
- Found: each of the three passes cost O(rounds x faces): a full adjacency rebuild, a full candidate scan
  and a full face rewrite per collapse, so a libQEx fallback (many triangles and pentagons) made them
  super-linear in the target.
- Measured: output byte-identical (ab.py lane light: public 770/770, private 44/44; external_thin 3k/10k
  24/24; pass-level fuzz 400 soups x 4 modes, disprover 3,493 own inputs x 4 modes). Head, `--extractor
  trace`, 5 interleaved rounds: the three passes 12.7-17.5 s -> 0.88-1.11 s at 50k, 56-69 s -> 2.1-2.3 s
  at 100k; wall 70-84 s -> 58-67 s and 213-232 s -> 152-175 s. Head 400k default (libQEx falls back),
  both builds with the #175 AMD fix, one run each: the passes 535 s -> 11 s, wall 2,071 s -> 1,525 s.
- Next: PR after the 0.2.1 tag and the merge queue, rebased on master once perf/extraction-cleanup merges.
