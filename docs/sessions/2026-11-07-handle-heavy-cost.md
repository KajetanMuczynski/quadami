# 2026-11-07 · cloud thread · Handle-heavy islands: fourth disprover and the open-sheet cost
- Repos/commits: core branch `fix/handle-heavy-islands` (PR #177), 1ede588 to df81c07 on top of 8a45bfa.
- Did: a fourth disprover (research/handle-heavy-check-disprover-4-2026-11-07.md) found cliffs on two new meshes
  (very thin woven strands, a thin perforated plate; never worse than master), an open perforated tube paying
  45–60 s for an envelope that never won, and frequent 1-ulp flips. Added: an open island whose own result loses
  at most 5 % of its input is not tried, and the check measures at the planned quad width, not one a retry grew.
- Found: rules on the envelope's Euler characteristic cannot tell a slab net from a solid with a few open edges:
  a private sword part's envelope adds handles (χ −12 → −26..−32) and still wins (folded 47–52 → 3–7), so both
  χ-based guards tried first dropped that gain. The lost share separates them (tube 0.2–0.8 %, sword 6 %).
- Measured: disprover cost cases byte-identical to before with open tube 1920 45–60 → 2–3.5 s and perforated
  sheet 396 22–30 → 6 s; lane full 731/770 identical, none broken; against 8a45bfa only ext_watch 3000 u0/u5 move
  (folded 53/59 → 63/67, master 86/79); nemetona, disprover meshes and plate sweep unchanged.
- Next: the cliffs where every envelope radius breaks (TODO 2.22 "Open").
