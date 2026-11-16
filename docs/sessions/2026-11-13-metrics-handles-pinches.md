# 2026-11-13 · Claude (cloud thread) · Metrics: lost handles across splits, pinched input islands
- Repos/commits: quadami master 4b12e49; branch fix/metrics-handles-pinches
- Did: `metrics.py` counts handles lost over all output pieces of an input island (was: only islands
  kept in one piece); handles added stay on one-piece islands (a split island can hold a neighbour's
  pieces). The input's topology is taken after splitting pinched vertices per fan like the core's
  `splitPinchedVertices` (new `topology.input_split_pinched_vertices`; global `*_input` numbers too), so
  islands with a pinch are judged instead of skipped (audit 2026-11-05 #14). Islands with an edge of
  three or more triangles stay out of the topology counts.
- Found: ext_happy 50k u0 split into two pieces and read handles_lost 0 (core: 93). An all-pieces
  rule for handles added over-counts (ext_flighthelmet 4 per run vs core 0); triangles repeating a vertex are left out of the input topology like the core (verifier find).
- Measured (identical master outputs, old vs new metrics, closed/corpus/sculpt/external/external_thin
  ×2 replicates, testonly 10k ×6, happy 50k ×6): handles lost/added equal the core's report on 500/500
  runs (old: 496); pinch split count equals the core's on 494/494. Corpus handles_lost 16 -> 20
  (demosthenes); external islands judged 6292 -> 7872, holes_closed 422 -> 581, dropped 3615 -> 3713;
  closed and sculpt unchanged. Table: research/metrics-handles-2026-11-13/table-final.md (project files).
- Next: `ci/output_gate.py` still on the old coverage rule; metrics' island count differs from the
  core's on mosquito, nemetona and watch (core 1282/625/89 vs 1235/154/62: the core's cleanInput merge
  and its own island split are not modelled).
