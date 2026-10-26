# 2026-10-26 · cloud thread · Why release-check #5 failed
- Repos/commits: candidate de6ac3a (master 2026-10-23), baseline 0e9bec8; run 39969164442.
- Did: read the gate tables of all three legs. No tooling error: every leg built and ran both
  sides (65-90 min of 240), the gate step exited 1 on the ratchet. Changed `release_gate.py` so the
  0.x structural ratchet counts cases that fail on every replicate, not single runs (noise rule).
- Found:
  - Structural (corpus): H4 got worse only on single replicates (Linux sword fast 2000 u0;
    Windows demosthenes 10k u2 fast and quality; macOS demosthenes fast 10k u2, u3). Noise under
    the CLAUDE.md rule; the new rule passes them. Closed set: H4 and H6 better on all three.
  - Validity and defects: folded quads and faces flipped off input folds per 1k worse on all three
    platforms in both sets (corpus +4 to +10 %, closed +2 to +7 %; one exception, macOS closed V2
    is better); valence 2 / ≥ 7 slightly worse. Consistent across platforms, so real. Core changes
    in between: #82 (cleanup keeps thin parts), #89/#94 (fold repair passes, cut at non-manifold
    vertices), #96. Which one adds the folds is not measured.
  - Windows "degenerate 0.000 / 0.000 **worse**" is one degenerate face (V4 432 -> 431) hidden by
    3 decimals; the table now prints 4.
- Measured: only the numbers of run 39969164442 (job summaries); artifacts could not be fetched
  from this container (blob storage blocked by the proxy).
- Next: Kajetan decides whether the fold increase blocks 0.1.
