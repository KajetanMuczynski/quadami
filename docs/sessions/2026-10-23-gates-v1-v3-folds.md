# 2026-10-23 · cloud thread "Validity metrics and re-baseline" · all-quad V1, libQEx fallback gate, V3 fold mask
- Repos/commits: master after #90 (re-measured runs of `c2ffd24`); tools and docs only.
- Asked: Kajetan's answers to the #90 gate critique: restore the all-quad V1, add a libQEx fallback gate,
  leave flips over folded input out of V3, ratchets on the replicate mean.
- Did: `metrics.py` gains `flipped.faces_over_input_folds` / `faces_off_input_folds` (within 1.5 output
  edges of an input fold); `input_folds` moved here from `sculpt_report.py` (re-exported), evaluated only
  around flipped faces (the full test takes ~15 min on nefertiti), and a fold must sit in a cluster with
  an edge turned over by > 150° (`turned_over`). `gates.py`: V1 all-quad (old cluster rule printed as
  retired), V3 off folds plus the all-flips row, "no libQEx fallback" row, runs without metrics left out
  and listed instead of failing H1. `ab.py`: rows `flipped_off_folds`, `flipped_over_folds`; a failed
  re-measure no longer keeps the old metrics.json. TARGETS V1 row, V3 row, §3.2 ratchet, §6 item 5.
- Measured: V1 analysis: 15 of 1,482 replicate-runs have non-quads (triangles only), all on islands where
  libQEx was rejected (59 fallback runs). Without the turned-edge condition the fold test excused 440
  flips on human_neutral (lips, eyelids; 550 flagged triangles, 78 with it). Final: corpus V1–V5 31.2/72
  over replicates (28.3 with every flip), V3 39.0/72 (35.5), realsculpt V3 1.8/6 (0.2), closed unchanged.
- Next: coverage in `ci/output_gate.py` (H4); TODO 1.14 waits (Kajetan).
