# 2026-10-28 · Claude (cloud) · Reassess known issues after 0.2.0
- Repos/commits: core 57c19f9 (v0.2.0)
- Did: classified every known issue from TODO, the 0.2.0 known limits, ENVELOPE, USAGE-COVERAGE,
  RAISE-TO-CLEAR and the research notes into fixable / not or partly fixable / no solution found yet,
  with evidence, fix, difficulty, proposed release and a falsifier per row: [ISSUES-0.2.md](../ISSUES-0.2.md).
  Folded in the state of the running threads (crease remesh 2.5, sanitizer follow-up, nemetona) and the
  open-source comparison thread's preliminary code read.
- Found: almost every B row is one bound, quads per feature at the asked count; five issues filed as
  limits are really "no way found yet" (thin parts in the main body at a small share, swirls under
  Repair Folds, handles 2–3 quads around, deterministic relapses when raising the count, platform
  differences). C6 and C8 rest on measurements from before libQEx.
- Measured: nothing. Read in code at 57c19f9: the fallback `cleanupTriangles` rebuilds a whole-mesh edge
  map per pass, `isHealthyIslandResult` has no quad-count check, Smooth Normal excludes faces by the
  angle to the vertex's average normal.
- Later the same day: folded in the open-source comparison's matched-count runs and the math analysis (icosphere cones and rotated flat parts moved to fixable, nemetona's lace a topology limit, rim step-off traced to the extractor smoothing, vaseflowers relapse a rule artefact).
- Next: re-measure the eyeball caps and the 256-sided cylinder on 0.2.0 before planning them.
