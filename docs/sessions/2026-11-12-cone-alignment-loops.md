# 2026-11-12 · Claude (cloud) · Closed edge loops: aligning near-miss cone pairs (research, no PR)
- Repos/commits: branch exp/cone-align from master abe3d9e.
- Did: `tools/bench/separatrix_report.py` (pole separatrices: where they end, near misses at 1-2 rows).
  Prototype `--align-cones ratio,maxOffset` (+ `--align-soft w`, env AUTOREMESHER_ALIGN_DRY/PRINT):
  probe continuous solve, trace each separatrix to the first cone it passes within maxOffset, row
  "that cone's transverse coordinate, carried along the trace, equals the start cone's" as a hard
  constraint or soft energy.
- Found: head 10k has 0/192 closed loops, so every edge lies on a pole-to-pole chain; about half the
  separatrices pass another pole 1 row off. Hard alignment breaks the rounded map (thousands of flips)
  even for pairs already < 0.5 rows apart in the continuous map; soft alignment stops at about half
  the pairs at any weight (rounded energy x3-x85). The pair set is jointly infeasible: picking a
  consistent subset is T-mesh quantization. Not worth continuing ad hoc.
- Measured: single runs only (unconfirmed), research/closed-loops-2026-11-12/runs-single.txt.
- Next: see research/handoff-cone-align-2026-11-12.md.
