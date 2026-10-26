# 2026-10-26 · Claude (cloud thread "Design crease preservation in remesher") · crease loss per stage and a design for 2.5/2.6/5.1/5.2
- Repos/commits: core `7054a3c` (compiled tree as `988ec9e`); probe and intervention patches outside the repo
  (research/crease-preservation-2026-10-26/patches, branches not pushed).
- Did: per-stage crease probe (T, field, constraints, rounded map, quads before and after projection and
  relax; byte-identical to master on 42/42 runs) over a CAD probe set (box, rotated box, chamfer box,
  cylinder, wedge, rotated csg_bracket) and wingnut, boat, csg_bracket, brush_box_relief, scifihelmet,
  watch; two headroom interventions (remesh keeps crease vertices; plus no extractor smoothing); a design
  note with alternatives, risks, test plan and self-critique (research/crease-preservation-2026-10-26.md).
- Found: creases die in the isotropic remesh (T has 4–35 % of the ≥ 90° crease length, sees 1–15 % as
  sharp) and again in the extractor's `smoothAndProject`; field, constraints and map pass on what T has.
  Today's A1 is the final crease snap's, which bends quads across creases. 90° is a knife edge across five
  different sharp tests. Most hard-surface crease length is below 90°, and much of it lies in strips
  narrower than h/2, so TARGETS A1 is mis-specified.
- Measured: all with `ab.py` run folders, one replicate; tables in the note. Keep variant: T 0.95–1.00,
  isolines 0.80–1.00; plus no smoothing: output 0.96–1.00 on box, boat, wingnut; validity of the crude
  variants worse on hard surface (folds 6 → 38/94, brush_box_relief 3 holes), neutral on organic meshes.
- Next: Kajetan reviews the design; then a measurement PR (cad set, A1θ, crease-bent quads, `features`
  record), 2.5 + 2.6, the extractor smoothing, 5.2, and only then a threshold re-sweep.
