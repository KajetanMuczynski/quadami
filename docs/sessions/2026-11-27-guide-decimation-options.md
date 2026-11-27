# 2026-11-27 · Claude (cloud thread) · Guide loops on decimated inputs: three options on the same runs
- Repos/commits: exp/guide-ring-cycles (experiment, no PR). Arms of f21eaa0, one commit for all sides, ab.py, plain
  + 5 float32 replicates, arm "both keeps". Order set by Kajetan: disprover on the knight and the head at 3k
  first, then the options compared on those two plus new sculpts; no PR.
- Inputs: head inputs at 3k (8 cut rings, 8 staircase twins, the 3-ring head; private), knight torso at 750 and
  1400 quads, and new: `ext_phenebub.obj` (public, hand-downloaded, from Kajetan), its largest part (153k
  triangles), 4 cut rings + 4 staircase twins, at 1500 and 3000. Not counted, by the rule that a guide must
  survive into T: `ext_rain.obj` (98 small parts, none dense), `ext_cosmicbuddha.obj` and the Washington bust
  (no closed smooth guide chain in T even with decimation off).
- Did: `decimate_guide_band=N` (the vertices within N target edge lengths of a guide, along mesh edges, are
  locked for the decimation too; default off; the code runs only when an island is decimated). N = 3, chosen
  before the runs. Guide-aware decimation was not built: the decimator is meshoptimizer with an all-or-nothing
  lock per vertex, so coarsening along the guide means resampling the guide beforehand or adding a protected-line
  vertex kind to the vendored simplifier (its seam kind needs position merging, which our KeepTopology turns off).
- Disprover on the earlier arms (fresh worker, own scripts): knight 20 / 88 / 100 of 144 holds; head 3k 36 -> 67
  of 114 holds, but on the plain input alone it is 12 -> 13 of 19: the gain is in the five one-ulp replicates,
  where the baseline closes 25 % against 63 % on the plain input. Stub switch: no measurable cost. Decimation
  off: about +30 % time on the knight, memory 124 -> 223 MB on the bust. It found `ring_events.py` reading
  fallback drops from the last island only (fixed), and that "30 -> 3 runs without a chain" was of 102 runs.
- Measured, closes by `loop_gate.py` (loopcheck agrees in all 1,416 loop verdicts):

  | input | baseline | band of 3 | stub switch | decimation off | of |
  |---|---|---|---|---|---|
  | head inputs, 3k | 36 | 64 | 67 | 51 | 114 |
  | knight torso | 20 | 68 | 88 | 100 | 144 |
  | phenebub | 14 | 64 | 54 | 56 | 96 |
  | all | 70 | 196 | 209 | 207 | 354 |

  Each option against the baseline: 45-46 cases up, 3-8 down (59 cases). Between the options no difference
  shows: stub against band 24 up, 19 down (p = 0.54); decimation off against stub 20 up, 23 down (p = 0.76).
  Plain input only: head 12 / 11 / 13 / 9 of 19, knight 5 / 12 / 14 / 17 of 24, phenebub 3 / 11 / 11 / 10 of 16.
- Cost (medians per run; cpu seconds, peak memory): head 6.4 s, 130 MB baseline; band 8.6 s, 130; stub 5.6 s,
  130; off 7.8 s, 199. Knight 2.8 s, 60; band 2.7, 61; stub 2.1, 60; off 2.9, 95. Phenebub 6.1 s, 118; band
  5.5, 112; stub 4.1, 117; off 7.1, 216. Quad count within 1.6 % of target in every arm. Folded faces per run:
  head 1.2 / 1.3 / 1.2 / 1.4, knight 3.0 / 2.2 / 2.9 / 2.3, phenebub 1.1 / 1.0 / 0.8 / 2.0. Two cores shared with
  other jobs: cpu seconds are steadier than wall time, still not a benchmark.
- Checks: baseline outputs identical to the earlier commit in 192 of 192 runs; the band switch on an undecimated
  input (head at 10k) gives 6 of 6 identical outputs.
- Not done: the stub switch is not yet limited to decimated islands (needs a flag from the decimation to the
  remesher); on undecimated inputs it changes the output, and there loops that stayed kinked closed less often
  (27 -> 17 of 47, unadjusted). No `ab.py lane`, no quality metrics beyond the run records. ctest not re-run.
  `ci/lint.sh` not applied to `autoremesher.cpp` (it reformats the fold-fallback thread's lines).
- What would falsify: a dense sculpt with surviving guides where no option beats the baseline; the options
  separating in a larger run; the head's plain-input result (no gain) turning out to be the rule, not the
  one-ulp replicates.
- Not re-checked: why the head's baseline is fragile to one ulp at 3k; the three-way run has no fresh disprover
  yet (second loop check and paired tests only); band width 3 is one value; rings are generated, not drawn.
