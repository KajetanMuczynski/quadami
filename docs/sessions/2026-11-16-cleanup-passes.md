# 2026-11-16 · Claude (cloud) · Incremental clean-up passes on master (TODO 2.30)
- Repos/commits: core perf/cleanup-passes (merge of master 2906a31 into perf/incremental-cleanup f97bb7a)
- Did: merged master into the parked clean-up branches (perf/extraction-cleanup + perf/incremental-cleanup).
  Two conflicts in `smoothAndProject` and the header: the flat-array version now also locks master's guide
  vertices (`pinnedVertices`, TODO 5.7) and conical tips (TODO 2.23). Closed TODO 2.30.
- Found: master's size cap (#153) already ends the iron gate and nemetona stalls (19-35 s); the passes still
  dominated high targets whenever libQEx falls back (head 100k: ~117 s of 240 s).
- Measured: lane light, 882 runs byte-identical (public 790, private 44, hand thin 24, external_thin 3k/10k
  24); head 100k wall 231-248 s -> 136-143 s, passes ~117 s -> ~20 s (3 interleaved runs, plus the CLI's
  stage timers); head 50k 40-43 -> 37-38 s, `--extractor trace` 77-86 -> 48-52 s.
- Next: perf/extraction-cleanup and perf/incremental-cleanup are superseded by this branch.
