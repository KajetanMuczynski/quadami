# 2026-11-25 · Claude (cloud thread) · Closed guide loops: row bound, follow, padding (research)
- Repos/commits: core on exp/guide-ring-pad from master 76a81ac (experiment, no PR). Report: the doc
  "Closed guide loops in Quadami: what breaks them and what closes them" (Kajetan's artifacts).
- Did: measured why the loop on a guide ring ends at a pole, and built three switches (default output
  unchanged): `--experiment guide_separation=1` (each cone within 8 faces of a constrained guide edge gets an
  integer for the rows of quads between it and the guide, bounded below by 1; `MixedIntegerLeastSquares::
  setVariableLowerBound`, rounding fixes a bounded integer at the nearest value not below its bound),
  `--experiment guide_follow=1` (at a guide vertex the fan's rotations on each side must sum to the guide's
  quarter turns, else one inner edge is re-rounded and the cone moves off the guide), `--experiment
  guide_rings=pad` (a ring that carries a pole gets one row of quads per side, `guiderings.cpp`). Always
  recorded: stage `guide_rings` and the separations in `cover_result`. Bench: `ring_probe.py`,
  `make_ring_stress.py` (head stand-in with eye, mouth and neck rings; one ring of falling size). Tests:
  `bounded_rounding_test`, `guide_rings_test`.
- Found: on the guides set every pole on a ring is a cone whose rounded separation is 0 rows (18 of 18), and
  the continuous map already has it below half a row (0.14, -0.24, 0.38 on replicate 0), so rounding is not
  at fault. The extractor merges that cone into a ring vertex, which then sits off the guide. On the head
  stand-in cones also sit on guide vertices (90 over 48 master runs), where the guide turns within one fan of
  T, and at 1k the rings have fewer than 8 edges and T does not carry them (38 % of the guide length without
  an isoline constraint). A ring needs about 8 quad edges around to close; below about 18 its inside must be
  denser than its outside [derived, matches the sweep in order of size].
- Measured (ab.py, plain + 5 float32 replicates, both sides; closes = every ring vertex has one edge leaving
  on each side, counted in the core):
  - guides set, 84 loops: closes 66 -> 84 with row bound (+ follow, which changes nothing there), 84 with
    padding alone; guides_report.py followed 60 -> 72, the other 12 close and miss only its h/2 test against
    the zigzag of the drawn path (84 at 1 h). All replicates agree. UV flips 8 -> 5, no non-quads or folds
    either side, energy ratio 1.124 -> 1.132, time x1.01.
  - head stand-in, 192 loops (1k-12k, smooth and noisy): closes 83 -> 123 (row bound + follow) -> 128 (+
    padding); at 3k-12k 75 -> 113 -> 118 of 144. Cones on guide vertices 90 -> 32. Cost of row bound + follow:
    folded faces 3.5 -> 5.0 summed over 8 cases, flips x1.11, SJ p1 0.817 -> 0.791, time x1.22; no case
    separated in every replicate.
  - ring size sweep, 144 runs: closes 11 -> 84 -> 96.
  - Default (no switch) byte-identical to master: 60 of 60 guides runs, 48 of 48 sweep runs.
  - 1,728 of 1,728 separations kept as kernel variables; none rounded below 1 with the bound.
- Not done: no disprover, no private sets (head), no lane on marked/corpus/closed (no bpy or downloads
  here), no macOS/Windows build. The stand-in is not Kajetan's head.
- Next: run the three switches on the private head (`realguides`); PR row bound + follow as an option; make
  the marked-edge fallback weigh a closed loop (it drops 5-11 rings the steps would keep); guide sizing (at
  least 8 edges around, edge no longer than the guide's radius of curvature) on the 5.8 carrier; push cones
  apart before the solve where two straddle a guide; padding as the last fallback with a report line.
