# 2026-10-26 · cloud thread "Prototype thin-part sizing floor" · thin-island floor (TODO 4.1 step 1)
- Repos/commits: core aaa9b4e (master, no core change up to 91f3c48) → branch claude/project-thread-lqmy8v v1 9440288, v2 7a5a6ec.
- Did: per-island thickness floor with a budget clamp from research/thin-part-sizing-note-2026-10-23.md §4.
  `islandThinWidth` (thinparts.cpp) measures each input island: area-weighted median of max(t, 2 s), t the
  nearest vertex facing back (dot < −0.5), s the nearest vertex turned sideways (−0.5 ≤ dot < 0.6), search
  radius k·h. An island narrower than k = 2 quads (`--thin-island-floor`, 0 = off) asks for h = width / k;
  islands with share ≥ 64 triangles that are not thin pay, at most 25 % of their quads, shared by water
  filling. A single island is never changed. Also a lower bound on T's edge for raised islands
  (extent / (4 quads)) after the gap-analysis thread's sliver repro; the sliver policy itself is TODO 2.7's
  (sanitizer thread). Unit test `thin_island_floor_test`.
- Found: on the private sword the vines island is 56 % of the area, so the clamp lets it have about a tenth of
  its ask at 3k. Vines read as ribbons (width about 2 quads at 4k with 2 s), so the floor barely acts at 4k–6k.
  Payers lose quads: on meshes with many small islands (mosquito, 1,282 islands) more of them drop; on
  hard-surface props (corset, flighthelmet) straps and plates read thin and ask for 10–75× the budget.
- Measured (ab.py, ×6 replicates, master aaa9b4e vs 9440288; tables in project files
  research/thin-island-floor-2026-10-26/):
  - closed, corpus, brush, suzanne, realsculpt: byte-identical (single islands or no thin island).
  - private sword 3k/4k/6k: area_uncovered ×0.73, Hausdorff ×0.75 (2 runs separated), p99 ×0.84, folds ×0.96,
    time ×1.17. Budget 0.5: uncovered ×0.65, folds ×1.12. k = 3: worse at 3k (uncovered 0.063 → 0.079).
  - stress_thin: islands_dropped 20.8 → 2, uncovered ×0.42, Hausdorff ×0.18 (33 runs separated), folds ×0.75;
    cost: thin_r0.002_L1 at 2k gets libQEx fallbacks (0 → 3 of 336) and folds 0.5 → 14.8, topology 6/6 → 2/6.
    stress_ribbon: uncovered ×0.02, dropped 4.3 → 0. stress_attached, stress_islands: identical.
  - sculpt: islands_dropped 2 → 0, Hausdorff ×0.94, SJ ≤ 0 ×0.96 (3 runs separated better, 0 worse);
    sculpt_parts fast 5k flips 18 → 51.
  - external: non-quads ×0.88, flips ×0.96, but islands_dropped 1151 → 1485 (mosquito ~500 → ~675 at 3k),
    Hausdorff ×1.04; flighthelmet 3k drops 2 islands in 6/6 replicates (master 1/6), corset uncovered ×4;
    scifihelmet 3k uncovered 0.013 → 0.0006.
  - Cost: thin_floor_ms up to 0.3 s (spot_x8 2k, 9.5 % of the run; 4,096 samples × dense neighbourhoods);
    time over sets ×0.93–×1.04 under load.
- v2 (a22f3f0, 7a5a6ec): islands under 256 triangles don't pay, an ask is capped at 4× the island's count,
  1,024 width samples, and the raised-island T bound is gone again. That bound, not the floor, caused most of
  v1's external loss: it coarsened 1,138 of mosquito's hair islands at the small-island minimum. The sliver
  policy is TODO 2.7's (the sanitizer thread leaves sliver islands out before sizing).
- Measured v2 (same A/B, ×6): closed, corpus, brush, suzanne, realsculpt byte-identical. stress_thin dropped
  20.8 → 9.7, Hausdorff ×0.52, SJ ≤ 0 ×1.01, the r0.002 needle regression gone (topology 56/56).
  stress_ribbon uncovered ×0.05. sculpt dropped 2 → 0, Hausdorff ×0.94. Sword uncovered ×0.71, Hausdorff ×0.76,
  but 3k folds 21 → 50. external dropped 1151 → 1130, non-quads ×0.93, SJ ≤ 0 ×0.95, Hausdorff ×1.08
  (scifihelmet 10k 0.015 → 0.117, corset 3k ×1.7, flighthelmet 3k uncovered ×1.7).
- Next (v1 and v2): do not ship as is. Payers must stay resolved (only islands far above their own width / minimum pay),
  plates and straps need the extent filter (note §3.3) or an ask cap, fewer samples for speed; then rerun the
  same A/B. DO NOT MERGE before the v0.1.0 tag.
