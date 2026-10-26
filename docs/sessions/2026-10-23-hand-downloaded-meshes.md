# 2026-10-23: hand-downloaded external meshes

Thread "Scout external test models", second part (first part: PR #88).

- Kajetan downloaded the six models the cloud cannot reach (Sketchfab, Blender Studio, Zenodo):
  old knight speed sculpt, tentacle eye monster, Phenebub, Rain, Sprite, Cosmic Buddha.
- New `tools/bench/blender_to_obj.py`: .blend/.fbx/.obj to one triangle OBJ as the add-on sees it
  (evaluated, world transform, visible objects joined; .blend scripts never run). glTF stays with
  `fetch_external.py` (Blender's import left the Cosmic Buddha in 892 open pieces); Phenebub's
  Collada went through trimesh (Blender 5 has no Collada import).
- docs/EXTERNAL-MESHES.md: "Hand-downloaded meshes" (7 meshes, licenses CC BY 4.0 / CC BY / CC0,
  credits). Meshes, SHA-256 and the `external_hand` AR_CORPUS file stay in project files.
- Results (ab.py, origin/master de6ac3a, 5/5 replicates): knight at 3k gives 9.7x the target quads,
  ~600 non-quads, ~2,200 handles lost and ~2,400 islands dropped; Rain opens 11-13 holes at 3k;
  the tentacle loses its one handle at 10k. Cosmic Buddha, Phenebub and Rain's hair are clean.
- Found: metrics.py runs out of memory (~14 GB) on a 394k-triangle input and ab.py stores no
  metrics without saying so.
