# 2026-10-27 · cloud thread "thin-part local cap" · Lost-part causes and warning wording
- Repos/commits: core branch `fix/thin-warning-wording` on master a363436
- Did: the report splits lost parts (`lost_parts`, interface 1.10) into separate pieces, thin parts, open edges and other;
  the warning has one sentence per cause that says what a higher Target Quads did on test models; the panel wraps
  warnings by pixel width; CI's Linux add-on job takes panel screenshots in Blender 4.2 under Xvfb (`panel-linux-x64`).
- Found: the report's far count grows with the target on wiry models (plant 42 → 650 at ×8), which first read as "raising
  does not help". Tracked at a fixed distance, the parts lost first come back on 7 of 10 models at 1.5–8× (mostly on 2
  more, helmet on 2 of 6 replicates); the warning stays because smaller quads count finer parts as lost. Lost pieces partly
  come back (mosquito 25 of 50, iron gate 9 of 9 at ×8). A thickness-based "needed target" was off by up to 30× and was dropped.
- Measured: research/thin-warning-2026-10-27.md (6 replicates, second route by a fresh worker with its own distance code).
- Next: local thin-part cap probe and gate (4.1 / 2.21).
