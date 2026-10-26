# 2026-10-22 · Claude (cloud thread) · Two-lane PR testing
- Repos/commits: core master 0e9bec8; replay of #67 (9e84f93 → 9648bb6); lane checks on #60, #65, #67, #71, #72.
- Did: `ab.py lane BASE HEAD` (none / light / full from the build key and byte-identical `out.obj` over
  every public set, plain input + one float32 replicate, no metrics); runs made without metrics get them
  when a later `run` asks (they used to be reused without); tests `tools/bench/tests/test_ab_lane.py`;
  rule in CLAUDE.md and tools/bench/README "Test lanes".
- Found:
  - Size is the wrong axis: #67 (small) moved 50 of 225 plain runs; #60, #65, #71 (large or opt-in)
    moved none and would have been light; #72 moved 221.
  - 28 of the last 45 merges compiled nothing new (lane none).
  - "Replicates only where the plain run moved" is not a safe cheaper full lane: on #67, 28 of the 175
    runs with an identical plain output moved in some float32 replicate; sculpt flips ×0.79 at full ×6
    vs ×0.90 with the shortcut, p99 ×0.76 vs ×0.92. u0 + u1 as the trigger still missed 20 runs.
    Unanimous topology verdicts agreed (none either way).
  - Outputs are deterministic: 225/225 plain runs of master byte-identical between two work folders.
  - Master topology is noisy over replicates in only 6 of 225 runs (corpus 4, closed 1, brush 1).
- Measured (4 vCPU, `--jobs 4`, 0e9bec8): full ×6 one side 56 min (closed 371 s, corpus 1475 s,
  brush 553 s, sculpt 841 s, suzanne 103 s); plain runs only, no metrics, one side 378 s; `lane` with
  u0 only 787 s for both sides; u1 on top 705 s (#60), so about 25 min from scratch. #60, #65, #71 light with u1 too (450/450 identical).
- 2026-10-23: merged master (new `external` and `gallery` sets). `lane` skips sets without default
  targets (gallery) and now covers 265 runs; external costs 262 s per side in `lane` and 15 min per side at ×6.
- Next: Kajetan's call on a post-merge CI baseline run (drift guard; would also let threads skip
  recomputing master's side if they can fetch it).
