# 2026-10-23 · cloud thread "Validity metrics and re-baseline" · coverage in CI's output gate
- Repos/commits: master after #97; CI script and docs only.
- Asked: Kajetan's answer 1 to the #90 gate critique: coverage as a hard CI gate (a part collapsed into a
  blob keeps the input's genus, so topology counts pass it).
- Did: `ci/output_gate.py` fails a case when an input island has more than 1 % of its area (triangle
  centroids, area-weighted) farther than the output's mean edge from the output; standard library only
  (cell grid of size h, Ericson's closest point on a triangle). New `ci/tests/test_output_gate.py`: the
  input as its own output passes, a dropped island and an island shrunk to a blob fail.
- Measured: all six CI meshes 0 uncovered on `c2ffd24`; the check adds about 6 s to the gate.
- Next: TODO 1.14 waits (Kajetan); H3, H8 and a tighter count in the output gate remain (TARGETS §5).
