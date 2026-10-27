# 2026-10-27 · cloud thread · Audit metrics, ab.py and CI
- Repos/commits: master fca96dc (core key 64e69878cbb8; closed and corpus also on a363436, key b079abc487bf)
- Did: independent re-check of `tools/bench/metrics.py`, `ab.py`, `gates.py`, `release_gate.py` and CI.
  Own topology and distance code (dict/union-find, trimesh nearest points), the CLI's `report.json` as a
  third route, 18 hand-derived meshes. PR fix/bench-silent-numbers: measurement failures retried and
  recorded, the measuring code recorded per run, compare and release_gate stop scoring unmeasured runs,
  release_gate fails on lost baseline runs, OBJ vertex colours and negative indices, chunked `input_folds`,
  `tests/test_metrics_basics.py`.
- Found: exact topology counts agree across the three routes on 150 runs (closed + corpus) [M].
  `distance.in_to_out_mean`/`p99` mix area samples with up to 100k input vertices, so dense tessellation
  weighs more (reported/area-only up to ×1.67 mean, ×2.54 p99 on the corpus) [M]; a vertex-coloured OBJ
  doubled the vertices, negative indices crashed [M]; touching islands give false flips and wrong-side
  area (TODO 1.13 F3) [M]; a degenerate quad counts in V2 and V4 [M]; release_gate passed with baseline
  runs missing from the candidate [C]. CI: Windows add-on job is the critical path (~15.5 min), mostly
  LTCG links of the 19 test executables [M from job logs].
- Measured: `metrics.json` byte-identical before and after the PR on closed (78) and corpus (72), other
  sets in PR #131; `ab.py lane origin/master HEAD`: none. Distance ratio over u0-u5 per run: thin_leaf
  10k ×1.62-1.67 mean, human_neutral 10k ×1.77-2.54 p99, sword ×1.00-1.05. Full findings:
  research/measurement-audit-2026-10-27.md (project files).
- Next: metric changes (area-only distance keys, F3, degenerate vs folded) only at a coordinated metrics
  freeze; CI speed (test link), runner and pip pinning, output-gate mutation cases as separate PRs.
- Peak memory (same session, from the usage audit): on Linux `ru_maxrss` of a process started with vfork
  (Python's subprocess) includes the parent's peak, so `ab.py`'s `peak_rss_mb` and the CLI's
  `peak_rss_bytes` read at least the harness's peak (spot 2k: 63 MB fresh, 2,012 MB after the harness
  touched 2 GB). The CLI now reads `VmHWM` on Linux (63 MB either way) and `ab.py` takes its figure.
  `lane`: 560/560 runs byte-identical on corpus, closed, sculpt, brush, external, cad (u0-u1).
