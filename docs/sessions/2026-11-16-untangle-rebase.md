# 2026-11-16 · Claude (cloud) · Rebase the untangle projection speed-up onto master
- Repos/commits: core on perf/untangle-projection (b824700, 25ef54c, 42be5fd from 2026-11-04), master 2906a31 merged in.
- Did: merged master; master had moved the vertex projection loop body into `projectVertex()`, so the
  allocation-free query (`queryBox`, fixed-size arrays) now lives there with the candidate and hit
  buffers as `IsotropicRemesher` members (one vertex at a time, no threads).
- Found: nothing new; outputs move as in the first A/B (closed-form PSD projection rounds differently).
- Measured (ab.py, 2906a31 vs e87c567): lane full (459 of 790 runs differ, 0 failed). Full ×6 on all
  11 public sets with default targets: fixed none, broken none on every set; no run separated on flips,
  non-quads, SJ <= 0 or Hausdorff; quality aggregates within ±2 % except brush flips ×1.04 (median CV
  of flips over replicates 1.0, no run separated). CLI time under --jobs 4 (load noise, indicative only):
  external ×0.85, sculpt ×0.92, usage ×0.93, others ×0.96-0.99, corpus ×0.98.
- Next: none for this change.
