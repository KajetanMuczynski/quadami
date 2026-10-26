# 2026-10-23 · cloud thread "Vines lost at 3k on sword" · lost-parts warning
- Repos/commits: core master 877891d (after #82); branch claude/project-thread-z5qonh.
- Did: built ENVELOPE §3.3's *Coverage* and *Collapsed island* rules into the core report and the panel.
  `measureLostParts` (remesh.cpp): the distance from every input vertex to the output (triangulated; a new
  `SurfaceProjector::distance`), in planned quad widths h₀ = √(input area / target); vertices beyond
  `LostPartQuadWidths` = 3 h₀ raise "Parts of the model are missing in the result (up to N quad widths away).
  Thin parts need about two quads across: raise Target Quads." Islands with output below half their planned
  quads (`RemeshedIsland::targetQuads`, new) raise "K islands came out with far fewer quads than planned
  (smallest: F of P)". JSON `lost_parts` and `islands.collapsed*`, interface 1.7, panel row **Missing parts**.
- Measured (`ab.py`, every set: corpus, closed, sculpt, brush, stress_thin/attached/ribbon, private sword and
  sculpts, 326 runs; scored with `stress_report.py`):
  - output byte-identical to master on 326 of 326 runs;
  - stress (lost feature = > 20 % of probes beyond max(3 r, h₀/2)): coverage alone 41 of 52 caught; with
    collapse 47 of 52 caught, 0 false alarms. 2 h₀ catches 46 coverage-only but fires on 19 of 78 closed runs;
    4 h₀ misses 24 of 52;
  - silent failures (a defect and nothing red on the panel): 26 → 4 (thin_r0.008_L0.25 at 500 and 2000,
    thin_r0.064_L1 at 500, ribbon_t0.008 at 8000: parts lost within 3 h₀);
  - baseline fires (23 of 230 non-stress runs): thin ellipsoids at 150-500, sculpt_voxel's horn, brush_box_relief,
    private sword 2k-5k (vines; quiet at 6k, 2.8 h₀), human_neutral's eyes, sculpt_parts' eyeballs,
    sculpt_dirty. Each is a real loss: the igl area-sample distance agrees (≥ 3 h₀) or the island collapsed;
  - cost: median 1.0 % of a run, p90 3 %, at most 4.1 % on runs ≥ 5 s (four runs in parallel); alone,
    nefertiti 2k 161 ms of 14 s. A sort of the input's corners cost 380 ms on nefertiti before it became a
    flag array; the search starts from the previous vertex's distance plus the step (Lipschitz bound).
- Next: the do-no-harm guard (TODO 2.21 open): per-island distance before/after each cleanup pass. The
  thin-part sizing note (TODO 4.1).
