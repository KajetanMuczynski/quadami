# 2026-11-13 · cloud thread "Fix nemetona lace-island spikes" · lace islands closed further while broken
- Repos/commits: core on master a569948 (after #177), branch fix/lace-closing.
- Did: measured what #177 leaves on ext_nemetona and on the cliffs its fourth disprover found; grew the
  envelope's closing radius past 2.25 quad widths (up to 17.1) while an island's kept result stays at least
  0.25 unfaithful; built envelopes at radii over 9.5 cells by a distance transform from seed nodes plus an
  exact shell, so a large radius no longer costs (2 r / cell)^3 node tests per triangle.
- Found: nemetona with #177 has 0-1 spike faces at 3k and 10k (6 replicates); its remaining folds sit on small
  genus-0 islands, not on lace. The cliffs come from the radius cap: a hole of width s in a sheet thinner than a
  quad closes only at about s^2 / (8 offset), and the offset is a quarter quad, so the radius needed in quad
  widths grows with the target [inferred; a disprover found the formula over-predicts by up to 1.7x].
  A larger offset (0.6) did not help [3 replicates].
- Measured (ab.py lane, metric2.py, 6 replicates both sides vs a569948): lane light, 790 public runs
  byte-identical. Input farther than one quad: woven_r05 480 97.2-99.6 -> 0.2 %, 768 81-99.7 -> 0-2.1 %,
  960 47-99.7 -> 0.1 %; slabnet_t05 1008 20-68 -> 0.0 %, 1260 36-49 -> 0-0.1 %; the filled lace leaves
  26-51 % of the output more than half a quad off the input. Unchanged: woven_r05 1440, slabnet 882/2016,
  woven_r11 480/960. Disprover meshes (4 replicates): plate with 2.6-wide holes at 200 quads 58-98 -> 1.3 %,
  plate6_mid 600 61-70 -> 0 %, mixed 2000 6.6-7.7 -> 1.6-2.6 % (off50 0.7 -> 18 %).
  Cost: an island still failing pays up to 5 more envelope runs: 1.3-2.9x wall time on islands where nothing
  wins (woven_r05 1100/1300, wov_r03 2000, mixed 3000); before the distance transform it was up to 7x.
- Next: the cliff moves rather than vanishes on fine weaves (woven_r05 1100-1440, wov_r03 2000: 25-41 % lost):
  there the filled slab scores worse than the torn result because filled area counts as unfaithful. Notes:
  research/lace-cliffs-2026-11-13.md in the project files.
