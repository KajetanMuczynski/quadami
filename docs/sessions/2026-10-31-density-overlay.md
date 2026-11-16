# 2026-10-31 · Claude (cloud) · Build density preview overlay
- Repos/commits: core `feat/density-overlay` from master `8534e6e`, merged with `2b858a3`
- Did: core `planRemesh()` and CLI `--plan-only` (planned quad edge per input vertex and quads per island,
  from the Parameterizer's size law on the decimated mesh, smoothed and moved to the input vertices),
  `tests/plan_test.cpp`; add-on Preview Density with a Change bar whose moves are one shader uniform
  (`addon/quadami/density.py`), cached in memory and dropped on geometry change; `tools/bench/plan_report.py`
  (gates P1-P3 and the swipe check); user guide §4 Density Preview; PREVIEW.md §4.4.
- Found: the count and the swipe hold, the absolute local size does not reach gate P1 (plan contrast too
  strong on sword-like meshes, too flat on CAD and noisy ones); a ±20 % count band moves the edge only
  −9/+12 %, below the plan's own error, so the bar spans ½×-2×.
- Measured: corpus and sculpt sets, 7 targets 2.5k-10k, plans against Quality runs (PREVIEW §4.4).
- Next: Kajetan's call on shipping despite P1; on yes, the PR goes ready after CI. Later: warm input
  cache if re-plans on big sculpts bother, density paint (5.8).
