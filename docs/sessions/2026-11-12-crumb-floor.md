# 2026-11-12 · Claude (cloud thread "Study crumb floor for thin parts") · Crumb floor: keep it, warn about it
- Repos/commits: core abe3d9e (master); local probe `exp/crumb-policy` (not pushed).
- Did: measured how often the small-island floor (64 triangles, about 32 quads per island) overshoots the
  target, costed four policies (floor, cap inside the budget, pass small parts through as triangles, drop
  them), and on Kajetan's choice (2026-11-13: keep the floor, build the warning) added the warning: the core
  reports `floor_raised_islands` and `floor_extra_quad_share` (run and plan) and warns above 25 % of the
  target; the add-on marks the Quads row and the density preview names the small parts before the run (TODO G13, ISSUES A25).
- Found: the thin-part floor is a different thing and was not redone (#123, thin cap 2026-10-28).
- Measured: `--plan-only` on 569 default mesh-target pairs: 28 above 25 %, all models of many loose parts;
  Kajetan's sculpts at most 3.5 %. Policies ×6 on 20 mesh-targets: cap brings the count to 1.0–1.7× but
  loses parts or adds folds where the parts are real (plant folds 66 → 400, Rain dropped islands 0.2 → 3.7);
  drop leaves 56 % of the plant uncovered; pass-through keeps everything but can never lower the face count
  (knight 71× the target). Checked by an independent worker. Details: research/crumb-floor-2026-11-12.md.
- Next: the "raise Target Quads" hints (thin parts, handles) still show on floor-raised islands, where raising
  does not help (F4's second half).
