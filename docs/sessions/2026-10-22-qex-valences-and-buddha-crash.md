# 2026-10-22 · cloud thread "libQEx adoption 6.1b" · cone valences (dropped) and a libQEx heap overflow
- **Repos and commits:** master `c2ffd24`, then `ae27301`. The cone-valence PR #84 was closed unmerged. The crash fix is on branch `claude/project-thread-u7tk6v`.
- **Did:**
  - Passed the cross field's cone valences to libQEx (#84): `FrameField::coneQuarters` with the cover's rotations; libQEx patches for "0 = count it yourself" and for the decimator's valence merge.
  - Measured it with ab.py ×6 on every set, in two rounds. Kajetan closed it on the result.
  - Fixed a heap overflow in libQEx `add_face` (patch 10). On a folded map it set `next` through halfedge index −1, because the opposite of the previous connected local edge around a vertex had no halfedge yet.
- **Found:**
  - **Cone valences.** Where the map winds the way the field asks, libQEx already counts the valence right; passing it changes nothing (round 2 was neutral on every set). Round 1's whole effect sat at map vertices that wind against the field. There, forcing the field's valence trades corpus coverage and irregular vertices (×0.98) for folds (sword at Q6000: 4 → 15 in 4 of 6 replicates). The lever is those miswound map vertices, not the extractor.
  - **Decimator bug.** libQEx's `MeshDecimator` merges external valences with the wrong sign (a 3 next to a 4 becomes a 5). It only matters when valences are passed, so it is not patched on master.
  - **The crash.** The Happy Buddha scan (test-only) at Quality 10000 aborted with `free(): invalid size` in replicate u1. ASan showed a heap-buffer-overflow WRITE in `add_face` (`MeshExtractorT.cc:3295`). No public input reaches the same site:
    - corpus, closed, sculpt and brush ×6;
    - 20 public external scans ×6;
    - 300,000 small folded maps;
    - 6,000 folded spheres through the pipeline.

    Crops of the Buddha map don't reach it either, because the state depends on the trace order over the whole island. So there is no repo fixture; `thirdparty/libQEx/PATCHES.md` has the ASan recipe instead.
- **Measured** (ab.py ×6):
  - Crash fix against master `ae27301`:
    - Buddha: master crashes u1 at Q10000; the fix passes 12/12, and the other 11 runs are unchanged.
    - Corpus, closed, sculpt and brush: byte-identical in 1,296/1,296 runs.
  - Valences: research note `qex-valences-2026-10-22/README.md` in the project files.
- **Next:** untangle the map where it winds against the field (the miswound cones and regular vertices that `cover_result` counts). That is where the remaining folds near cones come from.
