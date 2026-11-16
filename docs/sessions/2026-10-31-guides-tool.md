# 2026-10-31 · cloud thread · Build guides tool for edge loops
- Repos/commits: core 57c19f9 (master) → branch feat/guide-loops
- Did: Draw Guide tool in the add-on (`guide_tool.py`, `guides.py`): clicks or a drag on the surface joined by
  shortest edge paths, closed on the first point, stored in the edge attribute `quadami_guide`, always sent to the
  worker as hard marks; Mark/Clear Guide in Edit Mode, Clear Guides, overlay, panel count, report line. Core: hard
  lines on smooth surfaces slide and Taubin-straighten in the isotropic remesh; quad vertices on T's hard edges are
  pinned through the extractor smoothing and the surface relax. Bench: `make_guides.py` (guides set),
  `guides_report.py` (closed edge loop on each guide loop).
- Found: hard marks alone gave no closed loop on any guide (zigzag edge paths put cones on the line; the smoothing
  after extraction slid the loop off the guide). On a real head sculpt the loops now follow the guides but 1–6 poles
  stay on each.
- Measured: guides set (5 meshes × 1k/3k × 6 replicates) closed guide loops 0/84 → 36/84, 6 of 10 cases better in
  every replicate, none worse; private head with 3 guide loops (3k/10k × 6) 0/36 → 0/36, cones on guides 17 → 10
  (3k) and 8.5 → 2 (10k, medians). Lane and marked set: see the PR.
- Next: zero translation holonomy across each closed guide (tube-ring prototype's constraint), a guide-specific
  fallback when a marked part folds; hand-off in the shared research folder (handoff-guides-tool-2026-10-31.md).
