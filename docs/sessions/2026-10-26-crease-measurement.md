# 2026-10-26 · Claude (cloud thread "Design crease preservation in remesher") · replicate re-check and crease measurement tools (TODO 2.5)
- Repos/commits: core `4018750`; probe builds as in the design session (patches outside the repo).
- Did: re-ran the per-stage crease probe and the two headroom interventions with `ab.py --replicates 5`
  on both sides (cad set, corpus boat/wingnut/csg_bracket, brush_box_relief; 228 runs per build, the
  probe build byte-identical to master on all 228); added the measurement tools: the `cad` set
  (`make_synthetic.py`), `metrics.py` `crease_report` (A1θ = coverage of creases ≥ θ − 1° outside strips
  narrower than h/2, quads bent across a crease per 1k), `ab.py` rows `crease_cov_theta` and
  `crease_bent_1k`, and the core stage records `features` (crease length per stage and near-θ edge
  counts) and `crease_constraints` (T's hard edges: constrained, misaligned, re-rounded).
- Found: the stage losses hold over replicates. The plain synthetic inputs have exact right angles;
  every float32 replicate puts 25–64 % of right-angle crease length below 90°, so a crease fix without
  the θ − 1° classification keeps a coin flip of the creases (crude remesher fix: box T 0.70, not 1.00).
  The crude fix's hard-surface fold cost was a plain-input outlier (5.2 → 9.3, not 6 → 38); skipping the
  extractor smoothing costs folds (5 → 47) and SJ p1 (0.63 → 0.48); brush_box_relief's hole holds.
- Measured: `ab.py` run folders, 5 replicates both sides; research/crease-preservation-2026-10-26.md §10.
- Next: Kajetan reviews the design; 2.6 (one classification) before 2.5 (remesher rules), then the
  feature-aware extractor smoothing (2.8), each gated on A1θ, crease-bent quads and the cad set.
