# 2026-11-07 · cloud thread · Handle-heavy islands: which envelopes to try
- Repos/commits: core branch `fix/handle-heavy-islands` (PR #177), 44adab4 and b8acced on top of 2c64e3b.
- Did: a third disprover on 98f51d3 found open one-sided nets paying 15–60 s for envelopes that never won,
  a torn bar-lattice result kept in 2 of 5 replicates, and that a single envelope run at a 6|χ| cut made
  honeycomb and woven strands worse. Reworked which envelopes the check tries: every radius on a closed
  input (except one with the last one's χ), only radii that close something on an open input, bound-enveloped
  islands re-tried only when broken; torn, spiky, collapsed (< ½ quads) and empty results count +0.25.
- Found: on closed inputs an envelope with the input's own χ still helps (woven 960: share 0.98 → 0.28), so
  skipping same-χ envelopes (tried as v7) lost 98 % on woven 960 and an open net 240. Collapsed results must
  count as broken, or an 11-quad shell beats a torn 70 %-lost result on an open net.
- Measured: research/handle-heavy-envelope-2026-11-05.md addendum 3 (lane full, 731/770 identical, no broken run;
  nemetona 10k folded 142–171, spikes 0–1; disprover meshes never worse than master).
- Next: thresholds are hand-set; honeycomb 2280 flips between the closed shell and master's result with 1 ulp.
