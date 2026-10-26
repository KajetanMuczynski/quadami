# 2026-10-26 · cloud thread · Vines and swirls: what loses the shape, few triangles tested
- Repos/commits: core ee43cf3 (master) → claude/project-thread-pdep49
- Did: stage dumps through the extractor (probe build, output byte-identical); triangle variants
  (keep the extractor's non-quads, oracle diagonal split of 1-100 % of quads, triangle references at the
  same vertex count); switches for each suspected cause (no extractor smoothing, tangential smoothing,
  crease freeze at 45°, 2x quads); Repair Folds on the head at 10k and 35k. Added the opt-in
  `--quad-smoothing tangential` (interface 1.9, add-on Advanced > Quad Smoothing), a core test.
- Found: the "all-quad decision" (#97) changed no compiled code. Most of the lost shape on sculpts
  comes from the extractor's `smoothAndProject(5)` (3D Laplacian, then closest point on T); raw libQEx
  quads keep it. Triangles are not the lever (best diagonal split ≤ 9 %; isotropic triangles lose more
  than our quads); vertex placement is. The sword's thin vines at 3k also need smaller quads there.
- Measured: research/few-triangles-2026-10-26.md (private sculpts, 6 replicates per side) and the PR.
- Checked: a second worker with its own distance code confirmed the loss drop in every replicate pair
  and found the cost: more faces folded over in the swirled cube's grooves (flips +40 % at 3k).
- Next: finish the public guard of the switch on the PR build; after the v0.1.0 tag, a fold guard for
  the groove flips, then decide the default (full lane); local sizing for thin parts (2.21, 2.22 a).
