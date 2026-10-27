# 2026-10-26 · cloud thread · Input sanitizer for messy meshes (TODO 2.7)
- Repos/commits: core branch `claude/project-thread-guabuy` from master 7054a3c (docs-only master merged later).
- Did: per defect class, reproduced on public/procedural inputs on master (new `stress_sanitize` family in
  `tools/bench/make_stress.py`, 5 replicates): unwelded seams and soups, Edge Split, duplicate faces (both windings,
  double-sided shell), pinched vertices, T-junctions sealed by near-zero triangles, a far loose vertex, sliver islands
  (from the gap-analysis thread). Added `src/AutoRemesher/inputsanitizer.cpp`: seam weld (matched boundary edge
  pairs, unique partner, per-part undo on a new non-manifold edge), duplicate-triangle removal, pinched-vertex split,
  and sliver islands left out in `remeshIslands`; report keys and the add-on's Input cleanup row.
- Found: seams, Edge Split, duplicates, double-sided shells, pinches and sliver islands are real defects (crack,
  hundreds of empty islands, over 90 % loss, >15 min, p99 ×5–18, out of memory). Scale (stress G9), a far loose vertex
  and near-zero T-junction caps are harmless: no fix. ENVELOPE row 10's "absorbed" was wrong for duplicates and pinches.
- Measured: research/input-sanitizer-2026-10-26.md (project files); numbers in the PR.
- 2026-10-27: a disproof pass found two counterexamples, both fixed: the 1e-3 sliver rule dropped a lone ribbon
  master meshes well (rule now also needs the island narrower than half its own T edge), and the weld closed a
  double-sided open sheet into a pillow (same-side pairs are no longer welded). ab.py's peak RSS is unreliable
  under parallel runs; memory figures re-measured alone (1.7-1.85 GB, not 3.6-3.9 GB). Merged master (#113).
- Next: crumb overshoot (G3/G5) is a sizing choice, not part of 2.7.
