# 2026-11-06 · cloud thread "Fix perforated plate surface collapse" · surface-loss share and the plate cause
- Repos/commits: core b6da17f (master), crease PR #166 head 5b47b66 for comparison.
- Did: reproduced the #177 disprover's perforated-plate collapse on master; located the cause with
  replicates and two interventions; added the surface-loss share to the report, a leading warning and
  the panel row (TODO 2.34).
- Found: the 90° hole rims are creases, and crease rims a few quads around break the cross field
  (plate8 at 2500: 1048 cones for 504 needed, 197 of unusual angle); creases off keeps the plates from
  about 10|χ| up. Below that the target is too low for the handles even without creases. #166 helps,
  but its crease path still keeps the small rims.
- Measured: research/plate-collapse-2026-11-06.md (5 replicates, master / #166 / Sharp Edge 135°).
- Next: the rim fix on #166's crease path once it merges; the 3|χ|..10|χ| band with #177's rework.
