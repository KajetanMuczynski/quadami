# 2026-11-05 · cloud thread · High targets and the AMD hash overflow
- Repos/commits: core b6da17f (master); prototypes on perf/rounding-row-delete (#172) and perf/extraction-cleanup, not pushed.
- Did: replicated the head 400k crash under gdb, dumped the factorized systems, compared orderings offline,
  patched Eigen's AMD hash to 64 bits, added `tests/amd_ordering_test.cpp`.
- Found: the crash is an int overflow in Eigen's AMD supervariable hash (Amd.h), not memory. The rounding
  system at 400k (1.72M unknowns, three rows of 2,496-4,087 entries) sums node indices past 2^31; the sum
  wraps negative and indexes `hhead` out of bounds. The 200k system peaks at 1.16e9 (54 % of the limit).
- Measured: rounding system 400k, ordering: unpatched ~29 min then SIGSEGV (2 standalone runs); patched
  1.8 s, factor 62 s. Synthetic test: unpatched > 60 s (timeout), patched 0.6 s. Head 400k end to end with
  #172 and a working ordering finishes in 41-48 min (1 run each), about 30 min of it in extraction clean-up
  after a libQEx fallback.
- Next: test lane on this PR (expect light); clean-up after a libQEx fallback is the next high-target cost.
