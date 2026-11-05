# 2026-11-05 · cloud thread "Speed up remesh without quality loss" · clean-up passes on flat arrays
- Repos/commits: core branch `perf/extraction-cleanup` from master b6da17f (one commit). Builds on the
  island scout's prototype (2026-11-04) and the speed algorithm review of the same thread.
- Did: the post-extraction clean-up passes in `quadextractor.cpp` (switch, double shared quads, edge
  pairs, corners, fans, diagonals, five-edge merge, three/five triangles, split fans, triangle clean-up,
  six/seven splits, non-manifold removal, pinch search, smoothing) rebuilt maps of hash sets and
  `std::set` of faces every round. They now share one sorted flat adjacency per round (`PolygonAdjacency`,
  `EdgeFaceList`) with the same visiting order, flags instead of membership sets, a hash set of
  canonical faces, one vertex compaction helper (six copies before), a cached projection tree, and
  smoothing that touches only the moving patch. Six/seven splits and the triangle clean-up return early
  when nothing qualifies.
- Found: the triangle clean-up got ~30 % slower on triangle-heavy inputs when it built the full adjacency
  (disprover); it now builds only the edge list. Pruning the projection by box distance saved nothing
  measurable (the step is already parallel), so it was dropped. Separate bug on master: input faces with
  a repeated vertex (`f 1 1 2`) abort the CLI with heap corruption at extraction (handed on, not fixed here).
- Measured: `ab.py lane`: light (public 770/770 and private 56/56 runs byte-identical). Core tests pass.
  Wall time, two independent interleaved passes (5 and 6 rounds, the second by a disprover with its
  own script and alternating order), Quality: head 5k −6..−6.5 %, head 10k −5..−6 %, cube 10k
  −7..−8.5 %, sword 10k −10..−15 % (−15 % was lifted by one slow master run; −10 % is the safer number).
  Branch faster in every round of these four; no case slower beyond noise (also 3k-20k, Fast, open and
  1.7 M-triangle inputs). Clean-up step sums ×0.43-0.52 (head 10k 1.09 → 0.58 s), ×0.67 on ext_plant.
- Next: PR after #172 and the untangle PR merge (one open PR per thread). Next speed item: pre-decimation
  of dense sculpts (full lane).
