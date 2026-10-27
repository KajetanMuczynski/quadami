# 2026-10-27 · cloud thread · Diagnose remesh hangs and crashes
- Repos/commits: core fca96dc (master), branches `fix/extraction-hang`, `fix/cover-fill-budget`
- Did: reproduced four hangs and crashes reported by the scouting and target-quads threads, found where
  each one runs away (gdb stack samples, ASan), and wrote guards that make the island fail or fall back
  without changing any other output.
- Found:
  - `ext_mosquito` at 2000 quads (plain input only): libQEx's `add_face` called OpenMesh's
    `adjust_outgoing_halfedge` on a vertex rotation that never closes, which looped forever. Fixed by a
    bounded walk that throws, so the island falls back (libQEx PATCHES.md row 11).
  - `ext_watch` island at 1462 quads: `create_faces` in the n-gon merge circulated a broken rotation
    forever. Fixed by a bounded circulation (row 12).
  - `ext_watch` at 2000 quads, an intermittent segfault in `garbage_collection`: not a race. ASan shows
    the same heap-buffer-overflow on 4 threads and pinned to 1 CPU: a vertex's outgoing halfedge lies past
    the halfedge array after the merge. Fixed by a range check that throws before the collection (row 13);
    where the handle comes from is not traced.
  - `ext_hair_elf` at 3000 quads (plain input only): a segfault in Eigen's AMD ordering. A sliver island
    (223 faces, area 1e-11) resamples to 66k faces, cone raising re-rounds 3,200 edges, the seam BFS
    (rotation-0 edges only) reaches 1,785 of 66,254 faces, so the cover is cut almost everywhere and its
    system is dense. #112's sliver rule removes it: elf finishes in 5 s with #112, and in 4 s on master
    with only that island removed.
  - `ext_nemetona`: one island is chain-mail lace with about 1,600 holes (χ −3,240) and about 49 quads at
    3000. Its cover elimination fills in to 326 non-zeros per kernel variable (normal islands: about 1),
    and the first solve passed 10 GB. 2000, 5000 and 10000 quads all reach that stage. #112 does not help.
    Fixed on `fix/cover-fill-budget`: the elimination stops above 64 per kernel variable, and a normal
    matrix bound (1,024 per unknown, and Eigen's int range) fails the solve before it is formed.
- Measured: mosquito plain 5 s with the guard, u1-u5 byte-identical; watch island 9 s; watch u3 whole
  6 of 6 plain runs succeed, same output each time; nemetona 3000 with the budget on u0-u5: 6 of 6 finish
  (46-116 s while other runs were going); `ab.py lane` fca96dc to eef303b: light (598 runs byte-identical).
- Next: lane for the full extraction branch; master replicates of nemetona; lane and calibration for
  the fill budget; a seam BFS that crosses turning edges (parked: its only known trigger is the sliver
  island that #112 removes).
