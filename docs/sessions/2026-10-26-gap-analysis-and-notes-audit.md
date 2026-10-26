# 2026-10-26 · Claude (cloud thread "Gap analysis against ZRemesher and notes audit") · Gap analysis, notes audit, TODO update
- Repos/commits: quadami master `7054a3c` (CLI Release), Blender 5.2.2 LTS as `bpy` (PyPI) driving the add-on's code path.
- Did: feature-by-feature comparison with ZRemesher, Quad Remesher and Blender's QuadriFlow (vendor docs; ZRemesher
  from secondary sources), measured what survives a run in Blender, audited the plan, research notes, stress and
  speed notes and every session's Next line for dropped items, and brought docs/TODO.md up to date: stale Parked rows,
  today's findings from other threads (crease design, map untangling stopped, fold bisect on #82, thin parts), new
  Track G items G9–G13, 1.15. Note: research/gap-analysis-zremesher-2026-10-26.md (scripts, repro meshes alongside).
- Found: the add-on result keeps none of the source's data (UVs, materials, vertex groups, colours, face sets,
  sharp/crease/seam marks, parent) and comes out flat-shaded, where Blender's QuadriFlow keeps nearly all of it;
  Remesh on a result remeshes the result; non-uniform object scale stretches quads; the source still renders.
  New bug: sliver islands (near-zero area, real length) get a tiny voxel size from the small-island floor, and a
  `bad_alloc` in `resample` aborts the whole run (no per-island catch there). Symmetry works today by remeshing one
  half and adding Blender's Mirror modifier.
- Measured: result 0/1,974 faces smooth on a smooth source; non-uniform scale (3,1,1) world aspect p90 4.34 vs 2.08
  applied; Blender frozen 1.1 / 5.0 / 17.4 s writing the input OBJ at 0.33M / 1.3M / 5.2M triangles (raw arrays
  0.02 / 0.16 / 0.53 s); sliver repro 536 s and 8.8 GB peak RSS at 2k (normally ~1 s), whole-run `bad_alloc` under a
  3 GB cap; Blender's Bisect tool (default threshold) gives a clean mesh; half + Mirror at 1k/4k: all quads, 0 open,
  0 non-manifold edges. Single runs, one shape each.
- Next: 0.2 ranking in the note §5: sliver fix (2.7), G9 + G10 result data and re-run, thin parts (2.21), G11 scale,
  G12 symmetry, fold repair default (2.20), raw-array hand-over (G2), pre-checks (G13), crease design (2.5).
