# 2026-10-27 · cloud thread "Assess guide curves and density painting" · Guides and density design
- Repos/commits: core master `0b0832e`, branch `docs/guides-density-design`
- Did: read the sizing, T remesh, frame field, cover and extraction code for where density paint and
  guide curves would enter; wrote [GUIDES-AND-DENSITY.md](../GUIDES-AND-DENSITY.md) (design, staged
  plan, falsifiers); updated TODO 4.4, 5.7, 5.8, G2 (G2a request format), G6 and added 1.17 (bench for
  paint and guides). Builds on the 2.6/5.4 threads' `FeatureEdgeSet` carrier (not yet in a PR).
- Found: five places use plain area as the quad budget, so whole-island paint would cancel; both
  sizing paths skip at adaptivity 0 and clamp to 0.3–3×; the frame field re-targets every row to its
  previous solution after the first solve, so guide rows must keep their target [all C]. Density
  steps force pole pairs that today's field never places; with Target Quads kept, large painted areas
  mostly coarsen the rest [I].
- Measured: nothing measured.
- Next: G2a request PR, then 1.17 and stage 1 (density), after Kajetan agrees on the design. The
  sibling report on guide use in published work moved soft guides to last and borders (face sets,
  materials, marks) before drawn curves.
