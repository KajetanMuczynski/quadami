# 2026-11-26 · Claude (cloud thread) · Guide loops: why rings are not edge cycles (head and public 6k/10k)
- Repos/commits: core on exp/guide-ring-cycles, from exp/guide-ring-pad 806bfb7 (experiment, no PR). Builds on
  2026-11-25-guide-ring-rows and on the disprover brief of 2026-11-26 (questions 1 and 4; question 2, the fold
  fallback, is another thread's). Private head staged into the cloud workspace only, never committed.
- Did: added the stage record `guide_constraints` (what became of the guides' edges alone: isoline kept,
  misaligned, re-rounded, acute) and three switches: `--experiment guide_acute=keep` (a guide edge keeps its
  isoline next to an acute corner of other hard edges), `--experiment guide_yield=<degrees>` (detected creases
  within that angle of a guide and two quad edges of it leave T's set), `--experiment guide_smooth_rounds=N`
  (Taubin rounds of the remesher's smooth hard chains, 4 as before). Default output unchanged.
- Found: two different causes, as Kajetan suspected.
  - Private head: the acute-sector rule takes the isoline off guide edges near an acute corner of detected
    creases (24 % of the guide length at 3k, 3 % at 10k on replicate 0; nothing misaligned or re-rounded). The
    ring is then not a grid line all round. The head's continuous map also has about 1,000 flipped triangles at
    10k before any rounding.
  - Public uvsphere/icosphere at 6k and 10k: no isoline is lost. The input is coarser than T, the drawn path
    zigzags over several of T's edges, four Taubin rounds do not remove waves that long, and the chain keeps 20
    to 26 kinks (cones on guide vertices; 15 to 23 row bounds raised).
  - The small head ring's closed loop sits 0.7-0.8 h_loc from the drawn path because the drawn path is a
    staircase of input edges there (0.31-0.43 h_loc from its own smoothed curve); on replicate 0 the loop runs
    through the middle of it (picture, not published).
  - One row bound was not kept as a kernel variable (22 of 23, head 10k with yield, replicate 0): its cone lies
    on the guide's grid line by other constraints. The falsifier named on 11-25 happened once.
- Measured (ab.py, plain + 5 float32 replicates; my master outputs equal the disprover's, 18 of 18 by hash;
  columns guides_report h/2 · loopcheck 0.5 h_loc · core closes · edge cycles · broken · dropped by fallback):
  - Head, 54 loops: master 1·1·0·0·0·4; row bound + follow 7·10·12·12·31·10 (the brief's numbers); + keep
    10·11·14·23·12·18; + keep + pad 14·15·23·23·12·18; + yield 45 11·11·12·16·26·11; + yield + keep
    8·10·15·25·14·14; + yield + keep + pad 16·18·26·26·15·12. One case closes in all 6 replicates (keep + pad,
    10k, loop 0); the small ring never does at 0.5 h_loc.
  - Public guides at 6k / 10k, 42 loops each, core closes: master 0 recorded (30 / 18 by loopcheck 1 h_loc),
    row bound + follow 36 / 30, + 16 smoothing rounds 42 / 39 with 0 broken; the 3 left are fallback drops.
    Strict counts fall with the smoothing (h/2 18 / 18 -> 12 / 12): the loops move 0.01-0.14 h_loc and several
    sit at the 0.5 threshold. At 1k/3k closes stay 84 of 84, h/2 72 -> 66.
- Next: the head needs (1) keep made the rule for guides, with the fallback thread's result, since keep raises
  the drops from 10 to 18; (2) the map's 1,000 flips looked at as their own problem; (3) a metric that scores
  against the smoothed drawn curve. Public: fit the chain to a smoothed curve once instead of more Taubin
  rounds, to avoid the drift.
