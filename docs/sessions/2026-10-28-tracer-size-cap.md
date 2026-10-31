# 2026-10-28 · cloud thread · Check nemetona hang on 0.2.0, cap the fallback tracer
- Repos/commits: core 57c19f9 (v0.2.0), branch `fix/tracer-size-cap`
- Did: re-ran `ext_nemetona` on v0.2.0 after the head-to-head thread saw it stall on a2a966a; found
  where it stalls (gdb on two stuck runs, then the stage records); capped the fallback tracer; tested
  `ext_iron_gate` (TODO 2.30) with it. Follows `2026-10-27-cover-fill-budget.md`.
- Found:
  - #145's elimination budget ends the cover stall, but nemetona still stalls later: the small-island
    retry loop (`retry_wall_ms` 1,183 of 1,345 s on u0 at 3,000). A lace island (island 533, χ −484 after
    the isotropic remesh, target 127 after retries) gets "QEx returned no faces" and falls back to our
    tracer, which has no bound: 28,812 quads, then `cleanupTriangles`, which rebuilds a whole-mesh edge
    map per collapse. Island 593 did the same on u4 (24,958 quads for 95). Same mechanism as iron gate.
  - On iron gate the tracing itself runs away: libQEx stops at its grid cap, then `extractConnections`
    splits every isoline segment by every other isoline per triangle (about the square of the lines per
    triangle); island 351's map has a UV area of up to 541 million for about 120 quads (one run killed
    by the memory limit at 10 GB, one stuck past 30 min).
  - A first cap on grid crossings (`crossPoints`) was wrong: crossings are several per quad, and it
    emptied a sound island (501, target 949, about 1,000 quads) at 10,000 on 4 of 5 runs. The kept cap
    uses the map's UV area (about one grid vertex per unit) before tracing and the traced faces after.
  - `external_thin` has no default targets, so `lane` and the release check never ran nemetona; that is
    how the stall reached 0.2.0.
- Measured (`ab.py`, 6 replicates, `--timeout 1800`, `--jobs 2` or 3 while other runs were going):
  - nemetona on v0.2.0: 3,000: u1 and u5 killed at 1,800 s, u0 1,345 s and 32,164 quads, u2-u4 44-138 s
    and 4,147-32,991 quads; 10,000: u5 killed, u0-u4 62-153 s, 9,212-11,782 quads.
  - with the cap: 3,000: 6 of 6 finish, 57-106 s, 3,144-4,147 quads; 10,000: 6 of 6, 64-81 s,
    9,212-11,782 quads (u0-u3 byte-identical to v0.2.0). No
    island empty that v0.2.0 filled, on every replicate where v0.2.0 finished; the capped islands (157, 533,
    593) get their output from a later retry with a higher target on all 12 runs.
  - iron gate (hand set, 3,300 and 4,000): with the cap 12 of 12 finish in 9-20 s; on v0.2.0 see the PR.
  - `ab.py lane 57c19f9 HEAD`: light, 770 of 770 runs byte-identical (corpus, closed, suzanne, sculpt,
    brush, external, cad, primitives, usage, marked; u0 and u1).
- Next: an incremental `cleanupTriangles` only if a sound island ever traces near the bound; give
  `external_thin` default targets in the lane, or a smoke run in CI, so a stall like this is caught.
