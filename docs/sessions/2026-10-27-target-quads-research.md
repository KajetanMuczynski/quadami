# 2026-10-27 · cloud thread · Target Quads prediction, overshoot, per-part budgets
- Repos/commits: core `b079abc487bf` (master `0b0832e`/`8d3fa58`); report and scripts in the project
  files (`research/target-quads-2026-10-27.md`, `research/target-quads-2026-10-27/`), not in the repo.
- Did: sweep of 17 meshes at 500–32k with 5 float32 replicates (679 runs, own distance code next to
  `metrics.py`); all public and stress sets at defaults for overshoot (584 runs); joint vs per-part
  budgets on 10 multi-part meshes × 5 totals × 6 replicates × 6 variants; Kajetan's flower file;
  bpy hints on Blender 5.2.3; disprover workers on the predictor and the per-part claims.
- Found: no input-only predictor is good enough; one run predicts the next (G16); overshoot is the
  per-island floor and is predictable from a plan (G13); remeshing parts separately at the same total
  changes nothing, automatic reallocation hurts on some meshes; flat open parts get uneven quad size
  from per-island curvature normalisation (2.26) and an axis dependence (2.25); two libQEx hangs (2.27).
- Measured: N′ = N·d(N)/ε within a factor 2 in 97 % at a 2× step, 75 % at 8× (biased low); 45/584
  default runs above 1.1× the target, 35 flagged by the input plan with 7 false flags; per-part
  variants vs joint in the report's §5.3 table.
- Next: G16 prototype (report field + two buttons), G13 overshoot lines, 2.25–2.27 probes.
