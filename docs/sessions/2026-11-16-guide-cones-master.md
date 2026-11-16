# 2026-11-16 · cloud · Cones on guides as the head-loop blocker (experiment, not for merge)
- Repos/commits: core master 2906a31 + exp/guide-cones-master 7aaf64a (ear flip and guide straightening from exp/free-guide, user marks as guides)
- Did: ab.py, 6 replicates, three arms (master, ear flip only, ear flip + straightening) on the private head with guides (3k-10k) and the guides set; independent loop tracer and a disprover.
- Found: moving cones off the guides gives no significant gain over the ear flip alone; rings with no cone near the guide still fail 2 times in 3; straightening makes the fold fallback drop more guides. The ear flip is the robust part.
- Measured: head followed loops 1 / 5 / 9 of 72 (C vs B p=0.22); guides set 48 / 60 / 60 of 84; dropped guide chains 10 / 7 / 15 of 72. Details: research/handoff-guide-cones-master-2026-11-16.md (shared folder).
- Next: trace where loops leave cone-free guides (smoothed guide) before any new change; ear flip alone is a PR candidate.
