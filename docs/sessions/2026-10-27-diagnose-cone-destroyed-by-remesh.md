# 2026-10-27 · cloud thread · Diagnose cone destroyed by remesh
- Repos/commits: core master `2a0ef4b` (= v0.1.0 tag, tree key `547f7a3d0fce`)
- Did: Kajetan's Blender tests: the default cone "destroyed", the cylinder the same and its marked Sharp
  edges ignored, "weird loops" on the icosphere (panel defaults, mostly 5k). Made the primitives with
  bpy as the add-on hands them over (15 variants plus icosphere + Subdivision Surface), ran them with
  `ab.py` at 500–10k, 6 replicates, scored rims, thin rows and tips with a new report script, ran
  probe builds (adaptivity paths off, crease-keeping remesh, no or tip-local curvature guidance, tip
  snap) and Sharp Edge 88–91°, and had two disprovers check the headline claims with their own code.
  Report: `research/primitives-2026-10-27.md` (project files, with tables, patches and scripts).
- Found:
  - Cone tips are cut off 0.77–1.18 h at every target under a cap of four valence-3 vertices where the
    tip needs about two; not the sizing, not the rim. Curvature guidance implicated; the fix needs
    prescribed cones at tips plus a tip snap (TODO 2.23).
  - The cone's base rim (116.6°) is lost in the triangle remesh (2.5); box and cylinder edges are
    exactly 90°, the knife edge of the `> 90°` test (2.6): Sharp Edge 89° raises them (cube 5k 0.69 →
    0.91), 91° lowers them; 89° costs validity on the CAD test part, so it is advice, not a default.
  - The thin rows along rims come from the quad sizing field reading the bevelled crease as curvature;
    they are what keeps rims half sharp today.
  - The add-on sends no Sharp marks (5.4).
  - Icosphere: 15–29 irregular vertices per 1k quads inside the facets; Subdivision Surface first does
    not help; suspect the guidance weight's normalisation by the mesh maximum (TODO 2.24).
  - The 0.1.0 notes, README and user guide overclaimed sharp edges and tips; reworded.
- Measured: see the report; headline numbers in TODO 2.5, 2.6, 2.23, 2.24, 5.4.
- Next: 2.23 design (cones at tips from the deficit, then the snap); 2.6's θ − 1° classification;
  2.24 stage trace. Bench tools (`make_primitives.py`, `primitive_report.py`, `primitives` set) in a
  separate PR.
