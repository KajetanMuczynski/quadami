# 2026-11-01 · Claude (cloud) · Twisted quads in the Folded faces count
- Repos/commits: branch `feat/twisted-quad-count` from master `c12cb28`
- Did: interface 1.11 report keys `twisted_quads` (quads folded under both diagonals) and
  `folded_or_twisted_faces`; the panel's and Copy Report's Folded faces row shows the union with
  "(N twisted)"; `folded_faces` unchanged (the core's choice between runs reads it, so the mesh is the
  same); `metrics.py` mirrors the core (`folded_mask`, `twisted_quads` in validity and in `ab.py compare`),
  `output_gate.py` lists the count; user guide row and §1 definition.
- Why: the untangler thread measured that the corner test catches 27 of 148 twisted quads (18 %, one
  replicate, *unconfirmed*); a saddle-shaped quad is twisted with every corner positive.
- Next: Kajetan decides 0.2.1 or 0.3. Not changed here: TARGETS V2 / `gates.py` (`sj_le_0`) and the
  core's pick-the-cleaner-run score still ignore twisted quads.
