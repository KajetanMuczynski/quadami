# 2026-11-04 · cloud thread · Speed up remesh without quality loss
- Repos/commits: core b6da17f (base), branch `perf/rounding-row-delete`
- Did: profiled head, cube and sword (private sculpts) at 3k-24k, Quality and Fast; timed upstream 1.2.0 on
  the same meshes; dumped the head/cube 10k solver systems and timed supernodal factorisation and row
  deletion on them (CHOLMOD as a measuring stick only, not adopted); implemented row deletion for the
  rounding loop (TODO 8.8).
- Found: rounding refactorised a ~50-60k system 11 times to hold ~600-700 integers; deleting the held
  rows from one factor gives the same system. Upstream's "2× faster at defaults" comes from returning
  30-50 % of the requested quads; per output quad it is 1.6-2× slower than Quality.
- Measured: two interleaved timing passes (3 rounds each; the second by a disprover worker): head 10k
  −11 to −15 %, cube 10k −29 to −35 %, cube 3k −11 to −20 %, sword 10k/24k −4 to −9 %, head 5k 0 to
  −10 % (rounding solves 0.76 → 0.22 s). Full lane ×6, 10 public sets + realsculpt, realsword: no mesh
  fixed or broken across replicates (5 single-replicate flips, 3 fixed and 2 broken), aggregate rows
  within 2 % except near-zero area rows, time row ×0.77-0.98 under `--jobs 4`. Disprover fuzz test of
  the deletion (6,065 passes, n 1-3000): worst difference from a fresh factor 7e-8, on near-singular
  systems only.
- Next: more, smaller rounding batches (quality A/B); supernodal curl solve is held (needs a decision on
  outside code or an own implementation).
