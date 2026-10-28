# 2026-10-27 · cloud thread · Audit typical remesher uses we never tested
- Repos/commits: master `fca96dc`; sanitizer PR #112 head `fc85aa6` as a second route.
- Did: coverage matrix of what users do with remeshers (vendor docs, forum threads) against every
  set, gate and note (`docs/USAGE-COVERAGE.md`); ≈50 checks of the add-on's code path in `bpy` 5.2.2;
  new `usage` set (`tools/bench/make_usage.py`, `corpus.json`) run with `ab.py` at 1k/5k, 6 replicates,
  plus Smooth Normal 45–180°, Target Quads 6–200, Edge Scaling 4 and setting pairs; USER-GUIDE §7 rows
  (Smooth Normal advice, tiny/huge models, converted text, glTF imports); TODO 2.25, 2.26, G16, G13
  evidence.
- Found: glTF imports and converted text reach the core split (cracks with no warning; flat-shaded
  files and files without shared vertices fall apart), and the bench welds glTF before running, so
  the `external` set never saw this; #112's seam weld fixes every case in the set; Smooth Normal
  above 54.7° destroys the default cube, above 45° damages the cylinder, and the guide recommended
  it; very low counts give wrong shapes silently; the panel says nothing in Sculpt Mode, with several
  objects selected, or after edits on a result; constraints and animation are not carried over.
  `ab.py`'s peak memory reads the harness's own size for small runs (passed to the measurement audit).
- Measured: USAGE-COVERAGE.md §4 (every [M] row 6 runs). A disprover worker corrected five claims
  (uncovered area on shattered outputs, helmet file, cube warning rate, Suzanne sockets at every
  low target, Hausdorff divided twice by my table script); all fixed before the PR.
- Next: re-run `usage` when #112 merges; 2.25 (dihedral test), 2.26 (effective target, open-island
  boundary loss), G16 (panel hints, constraints, local view check by hand).
