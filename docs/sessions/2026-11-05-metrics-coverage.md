# 2026-11-05 · cloud thread "Fix metrics surface-loss gap and audit" · coverage fix and metrics audit
- Repos/commits: master b6da17f, branch fix/coverage-own-island
- Did: changed `metrics.py` coverage (own island, one target edge), added `islands_reversed` and
  `area_uncovered_any_island`, ab.py compare rows; ran a synthetic audit (15 cases) and a disprover.
- Found: the old rule scored a variant losing 1-4 % of ext_nemetona as better (the output's own mean
  edge moved the threshold more than the loss); dropped islands next to other output read as covered;
  face centroids vote for the wrong layer on thin shells (fixed by vertex votes).
- Measured: before/after on 11 public sets (identical outputs, old vs new metrics.py): see the PR.
- Next: the audit's leftovers in TODO 1.13 (pinched islands, coincident layers, output_gate).
