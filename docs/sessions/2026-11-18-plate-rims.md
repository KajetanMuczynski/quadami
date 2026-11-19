# 2026-11-18 · Claude (cloud thread "Fix plate-collapse surface loss") · Drop crowded hole rims (TODO 2.34)
- Repos/commits: base master 3523a16; PR #193 branch fix/plate-rims.
- Did: rebuilt the 2026-11-16 thread's lost work. `plates` bench set (make_synthetic.py, corpus.json, 6 meshes);
  `dropSmallClosedLoops` before the isotropic remesh (closed non-hard loops under 13 T edges, at least 5 within
  one loop length); tests/small_loops_test.cpp; stage feature `set_small_loops`.
- Found: master now fails only 8x8 holes at 2500 quads (2 of 6 replicates collapse to a sphere). Dropping small rims
  without the crowd condition closes a lone hole (plate1 2500: 6/6 -> 1/6); on T's set alone it does nothing. A
  disprover found a crowd of 3 counted a close pair of holes (unconfirmed loss at 1000 quads), so the crowd is 5.
  `set_small_loops` records only the last resample attempt.
- Measured: ab.py x6, every public set, both sides: plate8_d06 2500 genus 1/6 -> 6/6, area_uncovered 4.1 % ->
  0.006 %; plate5_d15 1000 uncovered 0.67 % -> 0.13 %; cad_csg_bracket_rot fast 1000 1.05 % -> 0.48 %; no run
  fixed or broken elsewhere; lane 11 of 826 runs differ. Tables in research/plate-rims-2026-11-18/.
- Next: cause 2 (plates under ~10|χ|) stays open in 2.34.
