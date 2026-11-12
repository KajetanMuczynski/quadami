# 2026-11-12 · cloud thread · Resolve 8 conflicts on guides PR 161
- Repos/commits: feat/guide-loops abe50be + master 8388006 (#163, #166, #135, #159) → merge 6a4c070
- Did: merged master into the guides branch. #166 replaced the isotropic remesher's marked/hard vertices with feature
  chains (corners, chain vertices projected onto the input's chain); guide straightening now sits on that model: a vertex
  on two hard feature edges between faces within 40° is no corner whatever the turn, is Taubin-smoothed in its tangent
  plane (with #166's fold check) and projected onto the surface, not the input chain. Extractor keeps guide pinning and
  #135's conical tips; add-on keeps #159's `_track` and the guide count. No interface change in this PR, so no bump.
- Measured (ab.py, plain + 5 float32 replicates): primitives byte-identical to master (68/68 outputs of u0/u1); guides set
  closed guide loops 36/84 (abe50be) → 48/84 (torus 1k and 3k now close in 6/6), cones on guides better on 3 cases and
  worse on 2 (blob 3k, capsule 1k: 0 → 1) in every replicate; head 0/36 → 0/36, marks fallback dropped the guides in
  3/6 (10k) → 2/6 (10k) + 2/6 (3k) replicates; marked set vs master: same cost pattern as the PR's original A/B.
- Not re-checked: the other public sets (only primitives as a no-marks control), Blender smoke locally (CI does it).
