# 2026-11-18 · Claude (cloud) · Warm input cache for re-runs and previews
- Repos/commits: core from master 3523a16, branch `feat/warm-input-cache`
- Did: `--input-cache` (core `PreparedInput`, `inputcache.cpp`, interface 1.16): cleanup, orientation,
  fold survey/repair and island split kept in a file with the raw mesh; the add-on keeps one file per
  mesh and worker and skips the OBJ write on a hit. `ab.py lane --head-cli-arg` and an `{input_cache}`
  placeholder check an option that must not change the output. Tests: `tests/input_cache_test.cpp`,
  worker tests.
- Found: the density overlay itself was already merged (#164); the cache's gain is mostly the fold survey,
  far below PREVIEW §4.2's estimate.
- Measured: lane light, 834 runs byte-identical with and without the cache; timings in PREVIEW §4.2.
- Next: a cheaper fold survey helps every run; plan reuse (Remesh continuing a preview) not built.
