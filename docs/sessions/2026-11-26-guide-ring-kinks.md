# 2026-11-26 · Claude (cloud thread) · Guide loops: a fixed gate, flips, and kinks in T's guide chain
- Repos/commits: exp/guide-ring-cycles (experiment, no PR), commits d79fc93..df31388 on top of 56449c2. Runs are of
  the commits named per arm in the report doc. Private head and rings in the cloud workspace only, never committed.
  Order agreed with Kajetan: gate first, then whether gaps sit on flips, then which step separates closing replicates.
- Did:
  - `tools/bench/loop_gate.py`: one closure rule, fixed before scoring: a closed Alt+click loop whose two-way
    95th-percentile distance to the smoothed drawn path is at most 1.0 h_loc. Re-scored every arm.
  - Debug output behind switches, output byte-identical: `flip_dump=1` (where the map's flipped triangles lie),
    `guide_separation_debug=1` now also places the follow step's re-rounds and the cones on guides,
    `guide_rings_debug=1` now also prints T's guide chains and the remesher's corner count on hard lines.
  - Experiment switches: `guide_corners=smooth` (kinks of hard lines never corners), `decimate=off`.
  - `tools/bench/ring_events.py` (what lies at gaps), `make_cut_rings.py` (rings cut into the mesh instead of walked
    along edges), `clean_guides.py` (drawn loops re-embedded as smooth cuts; prototype).
- Measured (ab.py, plain + 5 float32 replicates, arm "both keeps" unless said; closes = loop_gate):
  - Gate against the disprover's loopcheck at 1.0 h_loc: 0 disagreements in 240 cut-ring runs, 1 in 132 head-set
    runs. 3-ring head, of 54: master 1, follow + row bound 18, B + guide_acute 23, B + guide_keep 20, both keeps 30,
    + yield 45 34. The largest d50 of any pass is 0.19, so no pass is a neighbouring row.
  - No stage record separates closing from broken replicates of the same ring: 363 scalar fields, 9 rings with
    both outcomes, 60 pairs; nothing before `guide_rings` passes the family-wise threshold. The map's flip counts
    give AUC 0.37-0.57.
  - Flips: 52 gaps in 86 non-dropped runs (head + 13 single rings); 40 have a continuous flip within one quad
    edge (chance 14.6). The flips are made by the guide: on the head a flip lies within one edge of 35 % of gap
    length in guided runs and of 4.5 % of the same places without guides; over whole ring length 3.6 % against
    0.8 %, with the same number of flips per run (881, 873). 58 of 77 closing loops also have a flip in reach.
  - Kinks: the follow step re-rounds at 278 chain vertices, 85 % of them turning more than 45 degrees (its own
    trigger; 3.4 % of chain vertices turn that much). 39 of 52 gaps have one within 1.5 edges (chance 16). Kinks
    without a re-round sit at gaps as often (27 % and 27 %), so the kink, not the follow step, marks the gap.
  - Runs by kinks over 45 degrees in T's chain, 6k and 10k, cut and staircase rings, non-dropped: none 63 of 66
    close, 1-4 44 of 55, 5 or more 22 of 43.
  - Where kinks come from: relief or creases under the ring (ring26: 4 corners kept by the remesher, 5-7 kinks);
    stub edges of 0.15 median length left on the chain at 6k/10k; at 3k vertices that never moved. 16 Taubin
    rounds do not remove them (head 32 against 30 of 54, kinks per run unchanged 9.0 / 3.3 / 1.0).
  - Decimation (only at 3k on this input, 139k triangles): off, the cut rings go 12 -> 29 of 48 and the
    re-embedded head 1 -> 15 of 18; the staircase rings 19 -> 16 of 48 and the drawn head 5 -> 6 of 18. With it,
    T has no closed smooth guide chain in 28 single-ring runs at 3k, 2 of which close.
  - Clean rings are not better in total: cut 78, staircase twins 86 of 144 (10k 36 / 34, 6k 30 / 33, 3k 12 / 19).
    Master on the cut rings at 10k: 10 of 48. Crease detection off on the cut rings at 10k: 30 of 48 (36 with).
  - `clean_guides.py` on the head: 16 of 54 against 30 drawn (6k 6 / 10, 10k 9 / 15); it leaves jogs of
    0.03-0.1 units that become 5 kinks per run in T. Its first version left degenerate triangles (5 of 54).
  - `guide_corners=smooth`: one replicate at 3k, worse (a 174 degree fold in the chain). Unconfirmed, not pursued.
- Disprover (fresh worker, own scripts, 2026-11-26): found that my scripts read the first printed chain, not the
  kept solve's (22 of 86 runs); the numbers above are after that fix and match its own. It refuted "the follow
  step causes the gaps" (kinks without it gap as often), corrected "no kink" to "under about five", and noted
  n = 8 rings behind the cut-ring split (Fisher p = 0.14) and that kink-free loops on the head sets close 30 of 41.
- Not done: no fix. Stub collapse on hard chains, corners classified on the input, and the guide embedded after
  decimation are the candidates; none is implemented. ctest not re-run (debug prints and two switches).
  `ci/lint.sh` still reformats the fold-fallback thread's lines in `autoremesher.cpp`; left alone.
- What would falsify: an arm that removes the kinks from T and does not raise closing; rings where kink count and
  closing diverge (cut_zp20 with 2-4 kinks closes 11 of 12; the re-embedded head at 3k without decimation closes
  15 of 18 with 5 kinks per run); a guided run without flips near the ring that still gaps.
- Not re-checked: whether guided flips come from the guided T or from the constraints; the coverage rule that
  finds gaps (0.15 edge); kinks are counted in 3D, which also counts a straight line over a ridge; the decimation
  result has one input size; the survey rings are generated, as are all three rings of the head.
