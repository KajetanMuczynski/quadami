# 2026-10-26 · cloud thread · Why the hand-downloaded models fail
- Repos/commits: core `7054a3c` (same compiled code as master `4018750`); no code change. A local
  experiment branch `exp-floor-off` (per-island floor off, one line) was built and run, never pushed.
- Did: 84 replicate-runs of set `external_hand` (3k, 10k, ×6) and exact per-island accounting from the
  core's island order (`out.obj` sliced by `stages.jsonl`, no nearest-triangle matching). Interventions,
  ×6 each: floor off, libQEx off on the tentacle and Rain (same maps), the tentacle at 30k and 100k, Rain
  at the advised 18k, and each object alone (17 objects of the knight, Rain and Sprite). Note, scripts
  and tables: `research/hand-failures-2026-10-26.md` in project files (the models are licensed downloads
  and stay there).
- Found:
  - Knight: the 9.7× count is the 64-triangle floor (TODO 2.14) on 4,139 chain-mail rings smaller than
    one quad; they get 2.3–2.9 quads around the tube and 84 % close. Floor off gives 10.1k at 10k but
    about 2,800 islands come out empty, and Rain and Sprite lose small parts, so the floor needs a
    policy, not removal.
    The chain mail alone gives byte-identical output at 3k and 10k, so the panel's "raise Target Quads"
    hints are wrong there.
  - Tentacle: the handle is 0.6–1.05 quads around at 3k–10k and kept 6/6 at 100k: a sub-quad handle,
    not an extractor bug; the tracer keeps it only with non-quads.
  - Rain: open parts change silently (gum slit, jeans waist, hair ribbon, a body opening; the Holes row
    counts closed islands only). Not in T; on maps narrower than about one quad at the boundary, where
    libQEx and the tracer fail on different islands. The jeans and body changes do not occur when the
    object is remeshed alone. Thin advice chases itself (18k → 91k → 200k; the shirt is a double sheet
    much thinner than its own edges).
- Tooling: `metrics.py` needs 12–16 GB on the 394k-triangle tentacle (chunked query: byte-identical,
  0.5 GB); `ab.py` stores a failed measurement as `None` silently, which happened once more here (an OOM
  under parallel load) and made `compare` report 14 metrics better and `release_gate.py` fail on
  identical output; `metrics.py`'s island matching is wrong for 28 % of the knight's and 10 % of Rain's
  output parts, which made the 2026-10-22 note's "Rain opens 11–13 holes" (really 1–3).
- Measured: see the note; every claim there is tagged measured / checked / inferred.
- Next: fixes F1–F8 (cheap) and B1–B4 (big) in the note; B1 (what to do with thousands of sub-quad
  parts) is Kajetan's call.
