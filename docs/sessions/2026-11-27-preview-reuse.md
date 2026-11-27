# 2026-11-27 · cloud thread · Redo Preview Density for 0.4
- Repos/commits: branch `feat/preview-reuse` from master 76a81ac.
- Did: core `RemeshCallbacks::planReady` (interface 1.17) and CLI `--plan-output`, `--plan-report-json`,
  `--wait-after-plan`: a run plans itself after decimation and waits. Add-on: Preview Density starts that
  run and parks it (`plan_store.py`); Remesh with the same object, mesh content, worker and parameters
  sends `continue`. Plans kept per object and content (Hide only hides). Overlay: lattice lines over
  Blender's shading, optional tint; panel says what Remesh will do and the quads around each guide.
- Found: the plan code path only reads island state, so calling it inside a run leaves the run's mesh
  identical; the plan the run hands over equals `--plan-only`'s byte for byte (head 10k).
- Measured: see docs/PREVIEW.md §4.8 (head 10k: plain 8.97 s, preview 6.39 s, Remesh after it 5.64 s,
  medians of 5, OBJ identical in all 5).
- Next: Kajetan's visual review (screenshots in the thread); reuse of the plan's cover solve in the run.
