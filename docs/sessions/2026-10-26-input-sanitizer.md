# 2026-10-26 · cloud thread · Input sanitizer for messy meshes (TODO 2.7)
- Repos/commits: core branch `claude/project-thread-guabuy` from master 7054a3c (docs-only master merged later).
- Did: per defect class, reproduced on public/procedural inputs on master (new `stress_sanitize` family in
  `tools/bench/make_stress.py`, 5 replicates): unwelded seams and soups, Edge Split, duplicate faces (both windings,
  double-sided shell), pinched vertices, T-junctions sealed by near-zero triangles, a far loose vertex, sliver islands
  (from the gap-analysis thread). Added `src/AutoRemesher/inputsanitizer.cpp`: seam weld (matched boundary edge
  pairs, unique partner, per-part undo on a new non-manifold edge), duplicate-triangle removal, pinched-vertex split,
  and sliver islands left out in `remeshIslands`; report keys and the add-on's Input cleanup row.
- Found: seams, Edge Split, duplicates, double-sided shells, pinches and sliver islands are real defects (crack,
  hundreds of empty islands, 99 % loss, >15 min, p99 ×5–18, out of memory). Scale (stress G9), a far loose vertex
  and near-zero T-junction caps are harmless: no fix. ENVELOPE row 10's "absorbed" was wrong for duplicates and pinches.
- Measured: research/input-sanitizer-2026-10-26.md (project files); numbers in the PR.
- Next: crumb overshoot (G3/G5) is a sizing choice, not part of 2.7.
