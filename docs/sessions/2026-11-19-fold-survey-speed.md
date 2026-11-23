# 2026-11-19 · Claude (cloud) · Faster fold survey
- Repos/commits: core from master 0ffb31d (#194 merged), branch `perf/fold-survey`
- Did: the geodesic pass of `signTest` (`foldrepair.cpp`) takes step lengths and areas from arrays
  filled once per call, uses a 4-ary heap with the same pop order (ties by triangle index), and exits
  once the triangles already queued prove a triangle cannot be flagged (half the tolerance kept for
  the other summation order). Flags, sums and outputs keep their bits.
- Found (profile, tentacle 0.39M): the geodesic pass is ~85 % of the survey; 60 % of the triangles are
  candidates, 30k of them need their whole disc (thin parts: the Euclidean bound counts the far side),
  ~90 ns per heap pop. A cache-friendly face order gained nothing and was dropped.
- Measured: lane light, 826 public + 44 private runs byte-identical. CLI 10k, 3 repeats: fold survey
  tentacle 5.0 -> 4.0 s, happy 1.85 -> 1.48 s, private head ~0.1 s unchanged; runs -8 % / -4 %.
- Next: a bigger gain needs fewer full discs on thin parts (a tighter bound), not a cheaper pop.
