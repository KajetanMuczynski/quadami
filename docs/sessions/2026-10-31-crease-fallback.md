# 2026-10-31 · cloud thread · Sharp-edge fallback guard (TODO 2.22)
- Repos/commits: base 2b858a3, branch fix/crease-fallback (PR #165).
- Did: an island whose run is not faithful (healthy, same Euler characteristic, at least half the
  planned quads) is run again with sharp edges off and the rerun is kept only if it is faithful;
  skipped on non-manifold input islands. Warning, per-island `sharp_edges_dropped`, user guide row,
  `sharp_edge_fallback_test` on the cad set's rotated bracket.
- Found: cause from research/why-behind-2026-10-31.md section 3 (crease locks on T's bevelled
  creases). A fresh disprover found that on non-manifold input islands the Euler test traded creases
  for nothing (sculpt_dirty crease coverage 0.58 -> 0.04-0.09), hence the skip. metrics.py's
  holes_added / handles_added misread unwelded glTF inputs (one island per face or seam): its "worse"
  rows on usage_suzanne_gltf_flat, usage_gltf_scifihelmet and usage_gltf_corset are outputs that match
  the welded input better (checked with separate topology code).
- Measured: ab.py, 5 float32 replicates + plain, 10 sets, 2310 runs per side. Moved runs = runs with a
  kept rerun, all others byte-identical. cad_csg_bracket_rot 1k topology changed 12/12 -> 0/12.
  Crease coverage theta: cad x0.90, corpus x0.965, brush x0.964, external x0.985, others ~x1.0. Time:
  set means x1.03-x1.09; reruns that do not help (open islands whose boundary loops extraction caps)
  cost up to x1.9 (demosthenes, ext_boot quality 10k).
- Next: the crease package (TODO 2.5 + sizing exclusion + feature-aware smoothing) to keep creases on
  coarse islands instead of dropping them; possibly skip reruns on islands that are unfaithful for a
  reason the rerun cannot fix.
