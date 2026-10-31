# 2026-10-31 · Claude (cloud) · Design preview before remeshing
- Repos/commits: core `57c19f9` (ab.py key `395e0f46e4d2`)
- Did: design note [PREVIEW.md](../PREVIEW.md) for a decimator-like preview; `tools/bench/preview_report.py`
  (pole, loop and size agreement of a cheaper run with the kept run, stage-cost prefixes) with a unit test;
  user guide and Resolution tooltip: Quality takes about twice as long as Fast and is not previewed by it.
- Found: a run at a fifth of the target or at Fast resolution predicts poles, loops and the size pattern far
  worse than the 1-ulp noise floor, and the fifth-target run still costs 0.68-0.71 of a run on 1.7-2M-triangle
  inputs; the core's plan (input handling, before the isotropic remesher) is a median 5-6 % of a Quality run
  but under 1 s to 16 s by input size. An independent check found and fixed two errors in the first script.
- Measured: corpus and sculpt sets, Fast/Quality at 2k/10k and 2k/5k/15k plus one replicate (PREVIEW §3).
- Next: Kajetan's call on the overlay (§4.1); if yes, G13's `--plan-only` first, then gates P1-P3.
