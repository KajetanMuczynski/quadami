# 2026-11-12 · Claude (cloud) · Envelope check after the sharp-edge retry
- Repos/commits: core b2aab5d (merge of master f20e08c into fix/handle-heavy-islands 340791e)
- Did: merged #165 (sharp-edge retry) into #177 (envelope check). Order: health retry, marks pass,
  sharp-edge pass, envelope check. The sharp-edge pass skips enveloped islands and judges a run against
  `inputTopology` (the surface remeshed; for an island not enveloped that is the input). An island may
  go through both: sharp-edge retry first, envelope check on what it leaves. If the envelope replaces a
  kept run without sharp edges, `sharp_edges_dropped` becomes 2. Interface 1.15 (#159 took 1.12, #164 1.13, #178 1.14).
- Found (ab.py, 6 replicates, cad / closed / corpus / external; HEAD vs master vs 340791e by `out.obj`
  hash) [measured]: cad, closed, corpus: HEAD byte-identical to master. external: 13 runs move only
  as #177 alone, 10 only as #165 alone, 56 in both (mosquito, flighthelmet, watch, plant, corset), all
  on different islands except: 11 islands kept by the sharp-edge retry that #177 alone enveloped
  (ext_corset island 28 in 10 runs: envelope share 0.41-0.73 vs 0 now; ext_violin 10k u1: 0.029 vs
  faithful), and flighthelmet's bound-enveloped islands no longer tried without sharp edges (they were
  never kept on master). No island where the envelope replaced a kept sharp-edge run.
  external vs master: area uncovered x0.79, folded x0.94, non-quads x0.93, count error x1.14, time x1.14.
- Not re-checked: external_thin, usage, sculpt sets; envelope-first order not built (inferred worse from
  the 11 islands above).
