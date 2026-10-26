# 2026-10-26 · cloud thread · Result keeps shading, parent, transform; Remesh redoes the original (step 1)
- Repos/commits: master 91f3c48
- Did: add-on only (TODO G10, G11, part of G9 from the gap analysis 2026-10-26). The result gets the
  source's parent (type, bone, vertices, parent inverse), local transform with deltas and collections; it is
  smooth shaded when most source faces are smooth. A result points to its source (pointer property
  `quadami_source`, survives renames, shown as *Source* in the panel and clearable there); Remesh and
  Repair Folds on a result run on that source and hide the old result. Hidden source and old results get
  `hide_render` too (F12 drew both). Panel and report warn when the world matrix's singular values differ
  by more than 5 %. Headless test extended: parent, transform, collection, shading, scale warning, source
  link, and the panel's draw() on a recording layout.
- Found: marking sharp edges by angle is not safe for any single angle: on 5k-quad results, 30° marks
  7-21 % of the edges of sculpts and hard-surface meshes (lion 13 %, boot 21 %), 90° marks 4 stray edges
  of a smooth-shaded cube (its creases sit at the threshold). So no sharp marks; carrying the source's own
  sharp/crease marks is step 2.
- Measured: sharp-edge shares above (bpy 5.2.2, master CLI, target 5000; ad-hoc script, not ab.py, since
  the core output is unchanged). Smoke test passes on bpy 5.2.2; Blender 4.2 is checked by CI.
- Next: step 2 after the v0.1.0 tag: materials, vertex groups, sharp/crease marks, opt-in UVs.
