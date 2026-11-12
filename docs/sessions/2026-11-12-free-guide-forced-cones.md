# 2026-11-12 · cloud thread · Free guide at forced cones
- Repos/commits: core exp/free-guide 5428093 (on feat/guide-kind 9241a0c, without 16df6d9)
- Did: guide straightening at forced-cone vertices (experiment switches); fixed the remesher dropping the guide bit on
  merged edges; flip away ears (two hard sides) on lines over smooth surfaces.
- Found: 16df6d9 (drop creases at guides) is a regression; 26-38 flat ears per head run made the degenerate map spots on guides.
- Measured: ab.py, 6 replicates: guides set 36 -> 49/84 loops (all from the ear flip, 2 of 14 cases), head 1 -> 9/36
  (strict tolerance 0 -> 2/36); details research/handoff-guide-free-2026-11-12.md (shared folder).
- Next: trace head holes with the ear flip alone; productize straightening; PR after #161.
