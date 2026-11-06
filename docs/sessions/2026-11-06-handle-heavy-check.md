# 2026-11-06 · cloud thread · Handle-heavy islands: no cliff at the cut-off
- Repos/commits: core branch `fix/handle-heavy-islands` (PR #177), on top of b2ac6a7.
- Did: the disprover (research/envelope-disprover-2026-11-05.md) showed the 3|χ| rule is a cliff:
  just above it perforated plates came out byte-identical to master's garbage, and a rod net at 5000
  stayed open in 5–11 pieces. Added an outcome check for every island with χ < 0 (input farther than
  a quad from the output plus output more than half a quad off the input, or more than twice its
  quads), which runs the island on its envelope too and keeps the better result; the envelope's
  closing radius grows while it keeps too many handles; enveloped vertices go back onto the input.
- Found: master's plates break from 3|χ| to about 10|χ| with creases on or off (the crease part is
  the plate-collapse thread's); above that the check leaves plates as master makes them. The check
  also takes collapsed or torn islands on ext_corset, ext_watch, ext_scifihelmet, ext_plant,
  usage_gltf_* and one CAD bracket (its master output had a 188-edge hole; enveloped it keeps χ −4).
- Measured: numbers in research/handle-heavy-envelope-2026-11-05.md (addendum 2026-11-06).
- Next: a fresh disprover on the new head; the check's 5 % share and 2× quads are set by hand.
