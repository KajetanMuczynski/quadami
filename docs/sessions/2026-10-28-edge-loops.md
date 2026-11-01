# 2026-10-28 · cloud thread "Investigate destroyed edge loops" · Spiral edge loops and Sharp scribbles
- Repos/commits: master 57c19f9 (v0.2.0) and v0.1.0 release CLIs
- Did: traced edge loops (Blender Alt+click rule) on Kajetan's head sculpt and the default cone; stopped Keep from Original from copying marks that flood the source (the same > half of > 1000 edges rule Follow Marks uses).
- Found: loops spiral instead of closing (head 10k: 159 loops, 1 closed, longest 680-880 of 9.9k verts on 0.2.0, 1265 on 0.1.0): a known limit of field-aligned quads, not a 0.2.0 regression. The cyan zigzags on his result were his FBX import's Sharp flood copied onto the result as shortest paths.
- Measured: cone (5 replicates, 1k): 19-27 of 31-39 loops closed, longest 20-42 % of the vertices, same on 0.1.0 and 0.2.0; head numbers one run per version.
- Next: closed loops come from guides (TODO 5.7) or a loop-closing step; hand-off note research/edge-loops-handoff-2026-10-28.md (shared folder).
