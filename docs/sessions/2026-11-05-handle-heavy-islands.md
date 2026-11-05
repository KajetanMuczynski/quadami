# 2026-11-05 · cloud thread · Fix nemetona spikes: envelope for handle-heavy islands
- Repos/commits: core branch `fix/handle-heavy-islands` from master b6da17f.
- Did: traced the ext_nemetona spike quads to islands with far more handles than quads (feathers of
  crossing strands, strung beads, a chain-mail sleeve; input χ −130 to −3238 at 13–150 quads). Added
  `envelope.cpp` (grow by one quad width, shrink to a quarter quad outside, cavities filled, marching
  tetrahedra on the Freudenthal split) and the island rule: fewer target quads than 3|χ| of the input
  means the island is remeshed on its envelope and its quads projected onto it. Report field
  `islands.enveloped` (interface 1.12), panel warning, unit test `tests/envelope_test.cpp`.
- Found: the broken map gives either garbage at 3–6× the island's count or almost nothing (the
  60k-triangle body island got 24–41 faces, the chain mail none). The cut 2|χ| (valence 8) left
  spikes at 10k on an island at 2.5|χ|. An offset of half a quad put 16 % of the output half a quad off
  the input (back of the slab over one-sided sheets); a quarter gives 1.2–1.6 % at the same spikes.
  The fx.py "flip" count rises on enveloped islands for the same reason (slab backs), not from folds.
- Measured (ab.py, 6 replicates, both sides; numbers in research/handle-heavy-envelope-2026-11-05.md):
  nemetona 3000 spikes 59–147 → 0–1, CLI folded 238–419 → 141–165, input farther than one quad
  5.3–10.6 % → 2.4–4.5 %, time ×0.66 on the thin set; 10000 spikes 15–304 → 0–25. Every other
  run of every public set and of the private sculpts byte-identical except ext_flighthelmet and
  ext_mosquito (UV flips ×0.63/×0.70, non-quads down).
- Next: the 10k u5 spike island sits at 3.3|χ| (master has the same spikes); an output-to-input
  distance row in ab.py would show slab backs directly.
