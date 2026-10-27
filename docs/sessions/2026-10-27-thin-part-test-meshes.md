# 2026-10-27 · cloud thread · Scout thin-part test meshes
- Repos/commits: core fca96dc (master)
- Did: searched GitHub-hosted mirrors (Khronos glTF-Sample-Assets, odedstein/meshes, three.js examples,
  BabylonJS/Assets, gkjohnson/3d-demo-data, google/model-viewer, alecjacobson/common-3d-test-models)
  for meshes with parts thinner than a quad (TODO 4.1). Added `tools/bench/external_thin.json`
  (6 meshes) and the opt-in set `external_thin` (no default targets, so `ab.py lane` is unchanged).
  `fetch_external.py` reads Draco (DracoPy, optional) and quantized glTF positions.
- Found: Sketchfab's API answers through WebFetch, so licences can be read at the source although
  curl to sketchfab.com is blocked. Two mirrored labels are stale: the octopus tea bath (mirror CC BY
  4.0, Sketchfab now "Free Standard") and Littlest Tokyo (three.js CC BY 4.0, Sketchfab now
  "Editorial"); both left out. three.js's Venice mask is CC BY-NC 4.0. Mirrors that use
  EXT_meshopt_compression (Just a girl, Ring Twist Halo, Mercury, threedscans) are not read; their
  originals are on the hand-download list (project files external-models/thin/HAND-DOWNLOAD-THIN.md),
  with hair, filigree, chains, scanned foliage and MakeHuman skirts.
- Measured: the 20 `external` OBJs and `check.json` are byte-identical with the patched reader;
  bench tests 57/57 in the CI venv (with DracoPy 2.2.0). Shape checks of the new set in
  docs/EXTERNAL-MESHES.md. No A/B (the thin-part thread measures).
- Next: kinds 3 (hair) and 6 (chains) have no fetchable public mesh; they wait on the hand downloads.
