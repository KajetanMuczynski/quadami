# 2026-11-26: keep guides through the marked fallback (research, no PR) [TODO 5.7]

- Branch exp/guide-keep-fold off exp/guide-ring-pad. `--experiment guide_keep=1`: an unfaithful marked run is rerun
  without T's sharp edges before the marks are dropped; a marked run about to be dropped gets one more draw on its
  input moved by one float32 ulp, kept only if it passes every fallback test. Off by default (byte-identical, 18/18).
- Private head ring20 at 10k, 6 replicates: guide loop closed 4/6 -> 6/6 (three detectors), ring14 unchanged 6/6,
  ring26 still 0/6 (crease/isoline). Cost: one extra solve where it acts; ring20 u1 gives up its sharp edges.
- Note: research/guide-keep-fold-2026-11-26.md (private data, outside the repo).
