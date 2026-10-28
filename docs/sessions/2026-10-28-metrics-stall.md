# 2026-10-28 · cloud thread · Metrics stall on ext_rubber_plant
- Repos/commits: master c1f80a9 (core key f8b3baf8dd90); follows 2026-10-27-audit-metrics-ab-ci.md
- Did: found why `metrics.py` ran for hours on ext_rubber_plant (external_thin_hand, quality 3000, u0
  and u1) and gave `ab.py run` a time limit for measurement: `--metrics-timeout S` (default 1800 s, 0 =
  none) kills metrics.py and records `metrics: null`, `metrics_error`, `metrics_timeout`; a timed-out
  run is measured again only under a longer limit. Records gain `metrics_seconds`.
- Found: the cost is `input_folds` in metrics.py, not the output (2,849 quads, CLI 3 s) [M]. The input
  has two dense islands far smaller than the fold radius (1 % of the bounding-box diagonal, 1.34):
  48,926 and 11,776 triangles, 55 % of the input, inside 1 % of the box. The flipped output faces
  there put 72k input faces under test; the Euclidean pass returns 60,732 neighbours for each (7 min),
  and 33k candidates each walk the whole island in the Python geodesic search (209 ms each, about
  2 h) [M, py-spy plus a probe of 20 sampled searches]. With `input_folds` switched off, metrics.py
  takes 1.7 s on the same runs [M]. On an island smaller than the radius the "mean normal" is the
  island's net area vector, which is about zero on a closed island (1e-17 of its area), so the fold
  verdict there is rounding noise: a metric change for the next freeze, not fixed here.
- Measured: `metrics.json` of the closed set (156 runs, u0-u1) byte-identical measured by master's
  and this branch's ab.py; slowest metrics.py on the heaviest public runs 22 s (nefertiti, 2M
  triangles), closed set 1.3 s. Full write-up: research/measurement-audit-2026-10-27.md §11.
- Next: at a metrics freeze, cap the fold radius per island (or skip islands inside it) and move the
  geodesic search to scipy; both change numbers.
