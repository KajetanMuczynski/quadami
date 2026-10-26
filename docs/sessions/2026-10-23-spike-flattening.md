# 2026-10-23 · cloud thread · Unrepaired fold spots classified (spike opt-in)
- Repos/commits: core dd03272 (master, after #89) → claude/project-thread-t2bgxn
- Did: opt-in spike flattening in `FoldRepair` (`flattenSpikes`), CLI `--flatten-spikes`, report diagnostics
  `input_fold_spikes` and `input_fold_spike_drop_quads` (interface 1.7), add-on checkbox and panel row, tests
  (a leaning needle on a sphere that untangling cannot fix; an upright needle that does not fold stays).
- Found: only a few spots are spikes the rule can clear (head 2, cube 1, cat 2 in the corpus); the other
  needles near left spots refuse the detector test (lowering them makes new flags around).
- Measured: see the PR and TODO 2.20.
- Next: non-manifold edges as fixed boundary (sword, 5 spots).
