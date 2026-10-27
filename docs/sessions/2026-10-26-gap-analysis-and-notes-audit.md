# 2026-10-26 · Claude (cloud thread "Gap analysis against ZRemesher and notes audit") · Gap analysis, notes audit, TODO update
- Repos/commits: quadami master `7054a3c` (CLI Release), Blender 5.2.2 LTS as `bpy` (PyPI) driving the add-on's code path.
- Did: feature-by-feature comparison with ZRemesher, Quad Remesher and Blender's QuadriFlow (vendor docs; ZRemesher
  from secondary sources), measured what survives a run in Blender, audited the plan, research notes, stress and
  speed notes and every session's Next line for dropped items, and brought docs/TODO.md up to date: stale Parked rows,
  today's findings from other threads (crease design, map untangling stopped, fold bisect on #82, thin parts), new
  Track G items G9–G13, 1.15; later the platform study (1.7, 2.9a, 8.6, two decisions for Kajetan), the hand-downloaded
  models (crumb floor B1, bench traps F1–F3, open parts, thin advice) and the sanitizer's final sliver rule (2.7, #112).
  Note: research/gap-analysis-zremesher-2026-10-26.md (scripts, repro meshes alongside).
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
- Decided by Kajetan (2026-10-26, in the thread): crumb floor kept and warned about for 0.1; sliver islands dropped in 0.2
  (#112); byte-identical platforms after 0.1 (2.9b); release gate becomes a per-case sign test (1.16). Recorded in TODO with
  G9–G11 progress from #110.
- Added TODO 2.20a after Kajetan's test (head at 35k: Repair Folds closed the holes but flattened small swirls; one run,
  unconfirmed): the repair radius is 1 % of the diagonal whatever the target (0.55 quad edges at 3k, 1.9 at 35k on the
  head) and the final projection lands on the repaired copy; measurement and four options recorded there.
- 2026-10-27: added TODO G14 (modifiers on the source are baked into the remesh; Kajetan's 0.2 ask, Mirror and Subdivision):
  list them first, then keep Mirror and Subdivision on the result per type; design only.
