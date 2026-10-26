# 2026-10-22 · cloud thread "Vines lost at 3k on sword" · non-manifold islands skipped libQEx
- Repos/commits: core master 0e9bec8 (libQEx default + fold repair), compared with 158b12e (before #72) and 4d15d5f.
- Did: reproduced Kajetan's Blender report (private joined sword, Quality 3000: 1979 quads, vines mostly gone,
  no warning) with `ab.py` on the private `realsword` set, 2k-6k, 6 replicates. Found the cause, fixed it in
  `extractWithQex` (cut each vertex into one copy per fan of manifold, oppositely oriented edges before OpenMesh),
  added a unit test (three sheets on one edge) and #75's `area_uncovered` as a `compare --per-run` column.
- Found:
  - The sword's vine island has 33 edges with three triangles (ribbons glued along an edge). OpenMesh refused
    every such island, so it always fell back to our tracing: master, `--extractor trace` and 158b12e give the
    same vine output, so the loss is old, not a libQEx regression. The tracer drops part of the vines in a
    replicate-dependent way (3k: 4 of 6 replicates lose 54-56 % of the vine surface; 4k: 3 of 6 lose ~17 %).
  - With the cut, libQEx runs on the vines. Two cleanup passes in `finishMesh` still delete thin-strip quads:
    `collapseThreeValenceEdgePairs` 8-10 % of the vine faces at 3k-6k, `repairTopology` 38 % at 2k (probe build,
    face count per step). The stress-test thread found the same pass on thin capsules.
  - Geometric limit: half of the vine area is thinner (ray-cast shape diameter) than 14 units; one quad edge is
    13 at 3k and 9.2 at 6k. Below about two quads across, some loss is unavoidable.
  - Fold repair on the private meshes (instrumented build, reason of the last failed attempt per cluster): head
    385 → 66 folded faces, cube 45 → 8, sword 0 of 33 spots; skips are non-disk patches, folds that keep growing
    past 8 rings, mostly steep patches and repairs that do not reduce the flags. On the sword the "folds" sit on
    ribbons thinner than the detector's radius (19 units), so many are likely false positives.
- Measured (`ab.py compare`, realsword sword 2k-6k × 6 replicates, master → fix): non-quads 179 → 1,
  SJ ≤ 0 ×0.70, folded faces ×0.61, Hausdorff ×0.85, count error ×0.51, area_uncovered 0.138 → 0.094
  (3k 0.247 → 0.098, 4k 0.132 → 0.044), islands dropped 1.17 → 0 per run, libQEx fallbacks 48 → 0. Corpus (72 runs), closed (78) and realsculpt (6): identical.
- Next: the thin-strip cleanup passes (separate PR); a panel warning when the output leaves part of the input
  uncovered (share of input area farther than one quad edge from the output, per island); thickness-aware
  fold detection (radius below the local thickness).

## Second PR: cleanup passes (TODO 2.21)
- Did: guarded `collapseThreeValenceEdgePairs` (offset of the collapsed pair over the new diagonal ≤ 0.4 of its length)
  and made `removeIsolatedFaces` keep every piece of 16 faces or more instead of only the largest. Probe build
  (face count per cleanup step, component sizes) on the sword, corpus and sculpt sets chose both thresholds.
- Measured (`ab.py`, master 0797a4c → branch, one replicate except the private sword ×6): area_uncovered corpus ×0.91,
  closed ×0.84, brush ×0.33, stress_thin ×0.63, stress_attached ×0.92, stress_ribbon ×0.86, sword ×0.52, realsculpt
  equal; separate capsules lost > 10 % in 41 → 33 of 56 stress_thin runs. Cost: SJ ≤ 0 ×1.02-1.10 on the public
  sets and ×1.24 on the sword, irregular vertices +0.4-5 %. Threshold 0.25 instead of 0.4: same coverage gain, SJ ≤ 0
  corpus ×1.035 instead of ×1.017.
- Replicates (×6, 1-ulp inputs) on the runs whose topology changed: 2 of them were 3-face closed crumbs the later
  passes pinched off a thin neck (attached r0.016 at 2k, 5 of 6 replicates); a second crumb sweep after the last pass
  removes them and changes nothing else on any set (only 2 stress_attached runs and 1 sword replicate differ). Every
  remaining change is an extra closed component: a thin part master deleted and the branch keeps, detached at its
  neck (attached r0.008 at 8k 6/6, Hausdorff 0.10 → 0.007-0.009; r0.004 at 32k 3/6, 0.11-0.14 → 0.054-0.058;
  corpus sword at 2k 1/6, 0.12 → 0.021).
- Next: the "Lost parts" warning; a do-no-harm guard per cleanup pass once the core measures coverage.
