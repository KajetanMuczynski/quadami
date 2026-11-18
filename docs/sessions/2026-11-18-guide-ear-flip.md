# 2026-11-18 · cloud thread · Guide ear flip
- Repos/commits: core ba5273f on master 2906a31 (PR #191), master merged after the A/B.
- Did: ported only the ear flip of the guide-cones experiment (exp/guide-cones-master, arm B) to
  `thirdparty/isotropicremesher`: `flipChainEars()` flips the third side of a triangle with two hard
  sides on a smooth surface, and a normal flip may no longer make such a triangle. Experiment switches,
  counter and guide straightening dropped. CRLF of the vendored files kept.
- Found: `ab.py lane 2906a31 ba5273f`: full; 691 runs byte-identical, 23 differ, all in `marked` and `guides`
  (the only public sets with hard marks).
- Measured (`ab.py run --replicates 5`, both sides, every public set; private head via `AR_CORPUS`):
  - closed, corpus, suzanne, sculpt, brush, external, cad, primitives, usage: no metric moved in any run.
  - guides: loops followed 48/84 -> 60/84 (`guides_report.py`), better on 2 of 10 cases in every replicate,
    worse on none; degenerate map triangles 16 -> 1; UV flips 0 -> 5 on guides_icosphere_ring 3k (every
    replicate; sj_le_0 and folds stay 0).
  - marked: neutral (no fixed/broken, flips x1.015, hausdorff 1 run lower / 2 higher separated).
  - head_guides 3k-10k: loops followed 1/72 -> 5/72; degenerate 166 -> 106; folds, holes, handles unchanged
    within noise (no separated run).
  - Same loop counts as arm B of 2026-11-16 (independent build of the experiment branch).
- Next: where output loops leave cone-free guides (handoff-guide-cones-master-2026-11-16.md).
