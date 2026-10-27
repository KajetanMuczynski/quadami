# 2026-10-27 · Claude (cloud) · Follow Blender's marked edges (TODO 5.4)
- Repos/commits: core on `feat/sharp-marks-input` (base master `fca96dc`, master `3167cee` with #113 merged in).
- Did: interface 1.10 `RemeshRequest::featureEdges`; the CLI reads OBJ `l` lines; the add-on's
  Follow Marks (Sharp, Crease, Seam; Sharp on by default, Kajetan's call) with a mark count in the
  panel; a mark on most edges of a mesh over 1000 edges is not sent (import floods). Hard edges in
  the triangle remesher (split on the line, never flipped, collapsed only along a straight marked
  chain or onto a marked vertex, marked vertices not relaxed or projected), decimation locks them,
  and the field lock, singularity simplifier, isoline constraint and projection snap take them.
  Two guards: marks over a quarter of the planned quad-edge length are not used; an island whose
  marked run folds, changes its Euler characteristic or collapses is rerun without marks. The
  `marked` A/B set (`make_marked.py`) and marked-edge coverage in `metrics.py`.
- Found: the first prototype A/B was byte-identical to master because the halfedge mesh applied
  the marks in its constructor, before the wrapper had set them; refusing collapses next to marks made Suzanne's T keep slivers
  (5x the quads, 261 folds); with every consumer on, two kinds of marks fold (a loop running
  diagonally across cube faces, Suzanne's 553 short crossing marks), and turning any one of field
  lock, simplifier or constraint off removes Suzanne's folds (one run each). Kajetan's FBX imports
  mark 54–93 % of edges Sharp; with marks followed the sword ran 16+ minutes at 5k (killed).
- Measured: `marked` set, 5 replicates: marked length followed 0.29 → 0.70, folded + non-quads
  48 → 35, topology same in every run; CAD set with 30° marks (from #113's generator, not in the
  repo): boxes, chamfered box and cylinder 0.25–0.71 → 0.82–0.99, the 30° wedge 0.49 → 0.16 at 1k.
- Next: port onto the sharp-edge thread's `FeatureEdgeSet` (2.6) after it merges; face-set and
  material borders as sources; 5.2's sector rule for acute and crossing marks.
