# 2026-11-04 · Claude (cloud thread) · Headless Blender add-on smoke test
- Repos/commits: base master b6da17f, branch `ci/blender-operator-smoke`
- Did: `addon/tests/blender_smoke.py` (all three CI platforms) now also checks quads within 0.6-1.5x of
  Target Quads, the build line against `SOURCE_COMMIT` (no `-dirty`, no `dev`) and the Folded faces row
  of the Copy Report text against the report (`addon/tests/blender_checks.py`). New
  `addon/tests/blender_operator.py`: Blender 4.5.15 LTS on Linux CI under Xvfb presses the real Remesh
  operator (invoke, modal timer loop, finish), Copy Report (clipboard), Remesh again on a result, and
  Cancel; checks each run as above.
- Found: CI already had a headless Blender test (`blender_smoke.py`, 4.2 Linux, 5.2 Windows/macOS) and
  windowed panel screenshots (4.2). Missing were the real modal operator, Blender 4.5, the quad band, the
  commit check and the Folded faces format. The modal operator cannot run in `blender -b`.
- Measured (CLI, b6da17f): thin torus (R 1, tube 0.05, 400x24) at 300 quads gives 248 quads with 6 twisted
  unperturbed, but 0 folded in all 5 inputs perturbed by 2e-7 relative: no procedural mesh folds reliably,
  so the test checks the row's format and wiring, not that folds occur ("N (T twisted)" formatting is
  unit-tested in `test_worker.py`).
- Next: none; Windows/macOS windowed runs judged not worth it (no GPU, software GL on Windows is fiddly).
