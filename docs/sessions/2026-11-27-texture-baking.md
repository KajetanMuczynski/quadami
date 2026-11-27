# 2026-11-27 · cloud thread · Test and document texture bake workflow
- Repos/commits: master 76a81ac (add-on code path and CLI built from it), bpy 4.5.17 LTS.
- Did: headless test of the add-on on 6 textured public models (Khronos samples, LeePerrySmith) and one
  painted sculpt (lion statue with a synthetic colour attribute): default, UV Maps on, bake onto kept UVs,
  fresh unwrap + bake (colour + tangent normal), 2k and 10k, 5 replicates; bake variants (hidden original,
  extrusion 0.25-10 quad edges). Wrote user guide §10 (keep or bake, step by step, failures); fixed the
  UV Maps row's unwrap menu name (4.3+: Unwrap Angle Based).
- Found: UV Maps off (default) with materials kept leaves textured results wrong on 5 of 6 models; the
  original's normal map does not fit the result; baking onto kept UVs fails where the source reuses texture
  space or lies outside 0-1 (2 of 6); a hidden original stops the bake.
- Measured: research note texture-bake-2026-11-27.md (shared research folder), not in git.
- Next: add-on gaps listed there (UV Maps default or warning, normal-map warning, Show Original, maybe a
  Bake from Original helper), each needs Kajetan's go.
