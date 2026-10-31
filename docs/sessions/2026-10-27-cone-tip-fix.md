# 2026-10-27 · cloud thread · Keep cone tips sharp
- Repos/commits: core master `8d3fa58` (merged in), PR #135
- Did: traced the cut-off cone apex of TODO 2.23 stage by stage with dev dumps (T, raw and fixed
  extraction), prototyped and measured a fix with `ab.py` (primitives at 500–10k, 6 replicates, 21 on
  the rim cases), then ran the full lane four times: each round found false tips on other sets
  (folded brush input, needle spikes, fingertips, noise spikes) and the tip test was tightened.
  A disprover re-measured the headline claims with its own code.
- Found:
  - The tip is lost three times over: the isotropic remesh collapses and relaxes the apex away (T's
    nearest vertex about 1.3 h off); when it is kept, the field gives it the right valence-2 cone but
    the extractor merges that doublet away and `smoothAndProject` pulls it in; and the curvature
    guidance (principal directions, index 1 at a cone) leaves the two quarter turns the tip does not
    take right next to it, where the quads fold. Prescribing cones (the earlier plan) is not needed.
  - A coarse pinned fan (valence 4) reads as creases at 84–90° and locks the field radially: flips and
    collapses keep at least 8 edges at a pinned vertex.
  - Telling a cone from other angle deficit takes four tests: deficit 140–225° gathered within half a
    T edge (off the 135°/150° CAD corners; above 225° a needle, TODO 2.19), triangles forming a cap
    (CAD corners, folded brush input), the apex holding at least half of it (rounded caps smaller
    than a quad), and the deficit holding out to one and two T edges (noise spikes, bumps).
  - Cone base rims (lost in T, 2.5) sometimes give libQEx a handle; tracing takes over and loses the
    tip as on master.
  - `ext_watch` Quality 3000 u2 segfaults on master too (libQEx `mergePolyToQuad`; 6 of 18 runs here),
    already fixed in draft #140.
- Measured: see PR #135 and TODO 2.23.
- Next: the rim (2.5) after 2.6's feature-edge set; needle tips stay as they are.
