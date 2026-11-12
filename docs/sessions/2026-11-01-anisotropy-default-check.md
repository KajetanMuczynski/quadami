# 2026-11-01 · cloud thread · Check Anisotropy 0.5 as default
- Repos/commits: core c12cb28 (master), no code change
- Did: A/B of the defaults against `--anisotropy=0.5` with `tools/bench/ab.py run --replicates 5` on every public set
  with default targets (usage without the four `usage_gltf_*` meshes), `external_thin` at 3k/10k and the private
  sculpt sets (metrics only), 2466 runs per side; folds also measured with separate fold code; a fresh worker tried to
  disprove the result with the original scripts.
- Found: keep Anisotropy 1. 0.5 lowers twisted quads (corpus 0.46 -> 0.36 per 1k faces, external 2.19 -> 1.90), but
  folded faces and fold sites do not follow (external flat, cad 0.42 -> 0.71 folded per 1k), and crease and mark
  following drops at ~1k quads on every replicate (marked cylinder 0.97 -> 0.37, cad box 0.78 -> 0.67), reversing at
  3k-5k on cylinders.
- Measured: per-set tables in the research note anisotropy-default-2026-11-01 (shared folder, not in the repo).
  Distance +0-10 % per set, run time unchanged, non-quads no signal.
- Next: nothing on Anisotropy; twisted quads go to the untangle pass.
