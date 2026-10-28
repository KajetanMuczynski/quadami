# 2026-10-27 · Claude (cloud thread) · Reduce memory use of the remesher
- Repos/commits: core master 8fec004 → branch `perf/memory-peak` (PR #141)
- Did: profiled the CLI's peak memory per stage (RSS sampled per progress step, heaptrack and massif for
  the live heap, glibc `mallinfo2` as a third route) on a ladder of 47 runs (corpus, thin and hand sets,
  stress meshes, private sculpts, 1k–200k quads, 1 vs 4 cores). Removed the largest temporaries and copies
  alive at the peak without changing any computation: inner AABB-tree index lists, the fold survey's corner
  copy and Topology temporaries, SurfaceProjector's edge list, libQEx handle-count map, MILS constraint
  lists and factorisation before untangling, CLS energy rows, a second SurfaceMesh, copied stage
  diagnostics, and the per-island preview meshes, UVs and connections when no preview is asked for.
- Found: three regimes. Big inputs at low targets peak in the always-on fold survey and the surface
  projector (per input triangle); single islands at high targets peak in the curl solve and the rounding
  loop (the curl factor L is about half of it and the floor without an output change); inputs with
  thousands of islands peaked on copies of stage diagnostics and preview data (knight, 4,888 islands:
  about 600 MB of the 1 GB). heaptrack's interpreter runs out of memory on the knight trace (13.7 GB),
  so its result there is a truncated trace: use massif or `mallinfo2` for many-island inputs.
- Measured: see the PR and the research note `research/memory-peak-2026-10-27.md` (project files).
  `ab.py lane 8fec004 HEAD`: light on the public sets and the private set.
- Next: further options are in the research note (METIS ordering for the curl solve, which changes the
  output; compact stage-diagnostic records and a uint32 fold topology, which don't); the add-on's memory estimate (G13) should use input
  triangles, target and island count.
