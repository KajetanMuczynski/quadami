# 2026-11-04 · Claude (cloud) · Stall warning for stuck remesh runs
- Repos/commits: branch `feat/stall-warning` from master `b6da17f`
- Did: the add-on warns (red text above Cancel, and in the status bar) when the worker has printed no
  progress line for `report.stall_seconds(target)`: max(5 min, 3 x the longest silent step measured for
  that target, 2 x the run's time before the silence). Warning only, nothing is stopped. Worker:
  `RemeshJob.silent`, `RemeshJob.elapsed` (same lines as #169). User guide troubleshooting row.
- Measured (`tools/bench/progress_probe.py` `longest_silent`, 4-vCPU cloud box, Quality, one run each,
  *unconfirmed* per run): longest silence 0.2 s sword 10k, 1.1 s head 10k, 4.2 s head 30k, 0.8-3.2 s
  demosthenes/cat/nefertiti/happy/tree 10k (input steps on 2M-triangle meshes), 5.4 s nemetona 3k,
  0.2 s iron gate 4k; 36 s nefertiti 100k (curl), 87 s head 100k ("Cleaning up triangles"), 114 s head
  200k (curl), 380 s head 400k (curl). Thresholds: 5 min up to ~150k, 5.6 min at 200k, 19 min at 400k,
  94 min at 1M (extrapolated, target^1.75 from 200k-400k).
- Found: head 400k sat 23 min in "Rounding cover to integers" with no progress line, then segfaulted
  (exit -11, write fault in autoremesher-cli) after 32 min, ~6 GB. One run, *unconfirmed*; not chased here.
- Lane: none (only add-on Python and docs).
- Next: Kajetan reviews; a finer progress report inside cleanupTriangles and the rounding loop would
  let the threshold be tighter.
