# 2026-11-26 · Claude (cloud thread) · Guide loops: both keeps on the head, and what ring26 is
- Repos/commits: exp/guide-ring-cycles with origin/exp/guide-keep-fold (aec1a99) merged in (6d3a5a2; experiment
  branches, no PR). Runs are of 6d3a5a2. Private head and rings staged into the cloud workspace only, never
  committed. `Sphere.obj` is Kajetan's original, the file `mk.py` reads.
- Did: ran ring14/20/26 and the 3-ring head with both keeps (`guide_keep=1`, the fold-fallback thread's, and
  `guide_acute=keep`) on top of B = `guide_follow=1 guide_separation=1 guide_rings=pad`; then tested two causes
  for ring26 (the crease rules; relief the ring crosses) and found neither holds. Added one debug line:
  `AUTOREMESHER_GUIDE_RINGS_DEBUG` now also prints T's guide chain.
- Measured (ab.py, 10k unless said, plain + 5 float32 replicates; core closes / loopcheck 1.0 h_loc /
  loopcheck 0.5 h_loc / guides_report h/2, of 6):
  - ring14: 6/6/6/6 in every arm from B on. ring20: B 4/4/0/0, both keeps 6/6/0/0 (the fold-fallback thread's
    6 of 6 reproduced; its loop sits 0.68-0.81 h_loc from the drawn staircase, so the two strict checks say 0).
  - ring26: master 0, B 0, B + guide_keep 0, B + guide_acute 0, both keeps 2/3/0/3, + yield 45 1/1/0/1,
    + yield 90 0, crease detection off (`--sharp-edge 180`) 2/2/0/1 with either keep set. Not solved, and no
    arm agrees over its replicates.
  - 3-ring head, 54 loops (h/2 · loopcheck 0.5 · loopcheck 1.0 · core closes · broken · dropped): master
    1·1·1·0·0·4; follow + row bound 7·10·18·12·31·10; B + guide_acute 14·15·23·23·12·18; B + guide_keep
    8·11·20·14·32·7; both keeps 18·19·30·30·17·6; + yield 45 19·22·34·32·17·3. The gain of both keeps over
    B + guide_acute is at 6k only (loopcheck 0.5: 2 -> 6 of 18); at 10k the outputs are the same runs (nothing
    was being dropped there) and 3k does not move.
  - Closes in all 6 replicates with both keeps: loop 0 at 10k (0.18-0.21 h_loc), loop 2 at 10k within 1.0 h_loc
    (0.47-0.50, so 5 of 6 at the 0.5 threshold). Nothing at 3k or 6k. The small ring never within 0.5 h_loc.
- ring26, what was tested:
  - Crease rules [M, 4 arms x 6 replicates]: not the cause. With no detected creases at all it closes 2 of 6,
    the same as with both keeps. My single-replicate trace had it closing with creases off; replicates undid that.
  - Relief [M on the input, deterministic]: ring26 is the only one of ring14/20/26 that crosses relief sharper
    than 40 degrees along its own edges: a slot about 1 quad wide and at least 2.5 quads deep (turn 148 degrees in
    3D at its bottom) and a bump zone. The remesher pins 4 of its vertices as corners, so T's chain keeps kinks
    of 116-148 degrees there (ring14: 52 at most, ring20: 35). 6 of 14 gaps of the non-dropped broken replicates
    lie within 1.5 quads of those kinks, which cover 14 % of the ring.
  - The prediction from that (rings with no such corner close 6 of 6, rings with one do not) failed on 10 more
    rings around other centres of the same head, both keeps, 6 replicates [M]: no corner: ring17 6 (core; 0 by
    loopcheck), yp32 6, yp26 1 (5 by loopcheck), xp26 2 (4 dropped), ym32 2; with corners: ring22 3, xm26 4,
    xm32 3, zm14 3, zm20 3. So relief explains part of ring26's gaps at most; a single ring on this head closes
    in some replicates and not others (9 of 13 rings), which no rule tested here accounts for.
- Detectors disagree on these rings [M]: yp26 has a closed valence-4 loop 0.29 h_loc from the guide in 5 of 6
  replicates that the core finder reports as broken (T's chain edges are 1.5 quads long there; two ring vertices
  are not on the chain within its 1e-6 tolerance). ring17's loop is on T's chain in 6 of 6 and 1.0-1.9 h_loc
  from the raw drawn path, whose edges are up to 10 units (2.7 quads) long.
- Not done: no change to the fold fallback (other thread). `ci/lint.sh` reformats that thread's lines in
  `autoremesher.cpp`; left as they are, to keep the two branches mergeable. ctest not re-run (debug print only).
  Folds and quad counts of the new arms not compared (`--no-metrics`).
- What would falsify: ring26 closing 6 of 6 with any crease switch on a re-run of the same commit; a relief
  measure that separates the 13 rings; the core finder and loopcheck agreeing on yp26 after a rebuild.
- Not re-checked: the pinned-corner reading of T's chain comes from one replicate (the input-side count is
  deterministic); gap positions were located on the 14 gaps of 6 broken replicates only; the survey ran one arm.
