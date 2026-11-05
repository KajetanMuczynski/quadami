# 2026-11-05: density plan on low-poly input

Thread "Fix density bar on coarse meshes" (branch `feat/density-overlay`, PR #164).

- Third check found the overlay 1.7-3.4x too small on CAD inputs: nearest-vertex transfer puts the
  corner quads' size on whole faces.
- Fix in the core: input triangles wider than T's edge are sampled k × k and their vertices fitted
  (least squares of the shader's linear interpolation, count kept). CAD + primitives + usage, 5
  replicates: 0.38 → 0.18 log₂ (uniform 0.25); organic sets unchanged (0.041 → 0.038). Box faces
  now show their average; in-face gradient needs a finer display mesh (not built).
- Results: docs/PREVIEW.md §4.6.
