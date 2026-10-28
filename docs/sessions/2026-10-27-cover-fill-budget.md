# 2026-10-27 · cloud thread · Fail islands whose cover elimination fills in
- Repos/commits: core fe96a0a (master, with #140), branch `fix/cover-fill-budget`
- Did: added a fill budget to the cover's constraint elimination, and an int-range guard before the
  normal matrix is formed. Follows `2026-10-27-diagnose-remesh-hangs.md`.
- Found:
  - `ext_nemetona` stalls in "Rounding cover to integers" at every target (2000 to 10000) on a
    chain-mail lace island (about 1,600 holes, χ −3,240). Its elimination fills to 326 non-zeros per
    kernel variable; the first normal matrix then passed 10 GB. The public sets' worst island fills
    to 14 (ext_watch u1, island 66); the budget is 64.
  - A first version also bounded the normal matrix at 1,024 Σlen² per unknown. It failed ext_watch u1
    island 66 at 3000 and 10000: Σlen² overestimated that island's real normal fill by about 60×
    (24.8M against 411k). That rule was dropped; only the int-range guard is kept.
  - `ext_hair_elf` (3000 and 10000) crashes in Eigen's AMD ordering for a different reason (the sliver
    island, #112). The budget does not catch it (fill 9.7 per variable) and is not meant to.
- Measured: `ext_nemetona` with the budget: 3000 finishes on u0 to u5 (6 of 6, 46 to 116 s under load),
  10000 on u0 to u5 (6 of 6, 45 to 184 s, 9,055 to 10,100 quads); master was killed at 600 s on u1 to u5
  at 3000. `ab.py lane`: light (numbers in the PR).
- Next: the per-island memory work belongs to the memory thread; the seam BFS that crosses turning
  edges stays parked (its only trigger is the sliver island).
