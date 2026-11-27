# 2026-11-27 · Claude (cloud thread) · Hand-off note: guide loops, the kink falsifier
Written before the work it describes, so a fresh thread can continue (Kajetan, 2026-11-27). State of the
research: `2026-11-26-guide-ring-kinks.md` and the report doc's "Update 3, 26 November".

## Where things are
- Branch `exp/guide-ring-cycles` (experiment, no PR, never master). Earlier: `exp/guide-ring-pad`, and
  `exp/guide-keep-fold` (the fold-fallback thread's, merged in; do not edit its logic in `autoremesher.cpp`,
  and do not commit what `ci/lint.sh` reformats there).
- Cloud workspace: repo `/home/claude/quadami`; `AR_WORK=/home/claude/ar-work` (builds, `runs/<key>+<arm>/`,
  `meshes/private/`); `AR_CORPUS=/home/claude/private/corpus.json` (private sets: `realguides` = the 3-ring head,
  `headrings`, `ringsurvey`, `cutrings`, `stairrings`, `cleanhead2`, `headplain`). Private inputs come from
  `gunnar:~/Documents/Autoremesher/private-sculpts/derived/guides/` (`Sphere.obj`, `head_guides.obj`); never
  commit them or pictures of them. A fresh workspace rebuilds the rings with `tools/bench/make_cut_rings.py`
  and `make_guides.guide_along` (see `2026-11-26-guide-ring-both-keeps.md`).
- Arm "both keeps": `--cli-arg=--experiment=NAME=VALUE` for `guide_follow=1 guide_separation=1 guide_rings=pad
  guide_keep=1 guide_acute=keep`; debug dumps (output unchanged): `guide_rings_debug=1 flip_dump=1
  guide_separation_debug=1`. Always `ab.py run <ref> --set <set> --replicates 5 --jobs 2 --no-metrics`.
- Tools: `loop_gate.py` (the gate: closed loop within 1.0 h_loc of the smoothed drawn path), `ring_events.py`
  (gaps, kinks, re-rounds per case; `--cases`), `make_cut_rings.py`, `clean_guides.py` (prototype, made the head worse).

## What is known (26 November, measured, disprover-checked)
- Runs whose guide chain in T has no kink over 45 degrees close 63 of 66 at 6k/10k; 1-4 kinks 44 of 55; 5 or
  more 22 of 43. A correlation: 8 rings behind the cut-ring split (p = 0.14), kink-free loops on the head sets
  close 30 of 41, and the kinks were counted in 3D (a straight line over a ridge counts).
- Kink sources: stub edges about 0.15 of a median edge left on the chain (6k/10k, smooth rings); corners the
  remesher keeps where the guide's own edges are folded (ring26: 4); at 3k vertices that never moved, and after
  the pre-remesh decimation often no closed smooth chain at all.
- Flips at gaps are made by the guide handling; background flips are not the cause. The follow step is not the
  cause either. Decimation off at 3k helps smooth guides only (one input size: unconfirmed).

## The plan agreed with Kajetan (in this order)
1. Metric: the core prints the turn of the chain in the surface per vertex (left and right angle sums at the
   vertex), not in 3D. Re-score: does the none / 1-4 / 5+ split survive? If not, stop and report.
2. One arm alone: collapse stub edges on hard chains in the remesh (switch, default off). Two readouts in
   order: did the kink count drop; did closing rise. Nothing else changes in that arm.
3. Endpoint, fixed before the runs: closes by `loop_gate.py` on `cutrings`, `stairrings` and `realguides` at 6k
   and 10k, plain + 5 replicates on both sides, same commit. 3k is a readout only (decimation leaves no smooth
   chain there; stub collapse is not expected to help). ring26 is not promised: its kinks are kept corners.
4. Second route before any "this works": a fresh disprover with its own scripts.
5. Decimation: repeat on a second private sculpt (a subdivided public mesh only if none fits, labelled synthetic).
- Not in this arm: corners decided on the input, the guide embedded after decimation (bigger changes; one at a time).
