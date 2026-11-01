# 2026-10-31 · cloud thread "Audit metrics and fix islands bug" · fourth bench audit
- Repos/commits: master 57c19f9; PR #155 (a53f2f5), this PR.
- Did: PR #155 (`islands_same_as_input` ignored `islands_dropped`). Independent adversarial audit of
  `metrics.py` and `ab.py` by a fresh worker (known-answer meshes, fabricated run folders); every repro
  re-run; the top finding re-checked on real runs and by a disprover. This PR: the `ab.py` fixes that
  change no reported metric value (Kajetan: split, number-changing fixes in their own PR).
- Found: 13 issues. Number-changing (next PR): metrics.py reads the input unwelded while the core welds
  seams first (usage set: 51 / 228 replicate-runs "not same" only because of that, checked with a port of
  `weldSeams` and the core's own weld counts); crease rows on the raw winding; zero-area triangles as 90°
  creases; crease threshold θ−1° vs the core's θ−0.1°; `a b c c` faces as holes; zero-normal flips only
  when reversed; negative `l` indices. Harness (this PR): `--cli-arg=--sharp-edge=60` measured at 90°;
  `compare` ignored `input_sha`; `--remeasure` paired an old out.obj with a regenerated input; one
  unmeasured replicate hid the topology verdict of every run; records without `metrics_tool` never
  flagged as mixed; sj_p1's geo-mean dropped negative runs (62 / 468 closed, 23 / 228 usage, 5 / 432
  corpus replicate-runs have sj_p1 < 0); count_err None at 0 quads; an empty output not a failed run.
- Measured: PR #155 on a53f2f5 ×6: closed 465 → 439 / 468 same per island, corpus 396 / 432 unchanged,
  usage 96 → 95 / 228. Report: project files `research/metrics-audit-2026-10-31.md`.
- Next: metrics PR (seam weld first) with a before/after table on every public set.
