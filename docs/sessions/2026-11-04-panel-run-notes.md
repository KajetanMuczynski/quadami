# 2026-11-04 · cloud thread · Panel notes for long runs and imported Sharp marks
- Repos/commits: quadami master b6da17f, branch feat/panel-run-notes
- Did: the panel shows "Running for N. There is no time limit ..." under a run's progress bar after
  30 s (TODO 2.27's left-over, decided as a note, not a timer). Follow Marks' flooded-mark line and
  warning now give the Edit Mode steps to clear the imported marks and mark chosen edges (menu names
  checked against Blender 4.2.0 and 5.0.0 `space_view3d.py`, VIEW3D_MT_edit_mesh_edges).
  The panel shot adds the flood warning to its report so the OCR gate covers the longest text.
- Found: no automatic timeout is needed for the known hangs (#140, #145, #153 fixed them); a fixed
  limit would also stop large runs that are only slow.
- Measured: nothing measured; add-on text only (lane: none).
- Next: none.
