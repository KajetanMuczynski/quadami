# 2026-11-16 · Claude (cloud) · Rebase the nested dissection ordering onto master
- Repos/commits: core on perf/nd-ordering (72a33ac, 123097e from 2026-11-07), master 2906a31 merged in.
- Did: merged master. #172 replaced the cached solver with the row-deleting `DeletableLDLT`; the
  permuted (ND or AMD, by predicted work) factor of large located systems sits beside it and sets
  `deletable = false`, so held columns there refactorise as before #172. Locations are mapped right
  before a refactorisation that analyses the pattern. Tests CMakeLists: master's glob (the test file
  is picked up by name).
- Measured (ab.py, 2906a31 vs cc7b05e): lane light, 790/790 public runs byte-identical (u0 + float32 u1).
  ×6 at 100k quality (nefertiti, demosthenes, sculpt_dyntopo; --jobs 2): ND chosen for the curl solve
  in every B run (ordering 6.2-6.4 s); outputs differ by roundoff only: handles, holes, SJ<=0 and
  twisted quads equal per replicate except demosthenes u5 SJ<=0 10 -> 7, u2 twisted 2 -> 1, nefertiti
  u4 twisted 12 -> 11, dyntopo u1 SJ<=0 9 -> 8; Hausdorff equal to 1e-11. CLI time (load noise):
  demosthenes ×0.92, nefertiti ×0.88, dyntopo ×1.01.
- Next: none for this change.
- 2026-11-18: merged master f907333 (#189). Lane vs f907333 light (790/790). Head 400k quality, private
  sculpt, one run each (unconfirmed): both complete; ND chosen for the curl solve (factor 72 s + ordering
  21 s vs AMD 374 s); total CLI 2,933 s vs 3,575 s; peak RSS 9.4 vs 9.9 GB; SJ<=0 323 vs 319, holes and
  handles 0 both.
