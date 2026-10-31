# 2026-10-31 · cloud thread · Build the Raise Target Quads hint (Tier A)
- Repos/commits: master 8534e6e, branch `feat/raise-target-hint` (PR #159)
- Did: core `--lost-parts-reference <count>` (interface 1.11): the lost-part pass also counts far
  vertices by cause at the quad width of an earlier target, report `lost_parts.reference`;
  `measureLostParts` builds the topology once and classifies per width. Add-on `raise_hint.py`: run
  history on the source object keyed by input and settings, the ladder of docs/RAISE-TO-CLEAR.md
  (1.5×, then ×1.33, up to 8×), panel hint with Set and a dismiss, and "M of N points back" after
  the next run. Tests: `quality_report` reference case, 14 `RaiseHintTest` cases, CLI flag test,
  panel shot with the hint.
- Found: the panel OCR gate does not read button labels, so a cut-off button would pass it; the
  dismiss is an icon beside Set for that reason.
- Measured: `ab.py lane 8534e6e c3f93df`: 770 runs byte-identical, 0 differ, 0 failed (lane light).
  The flag itself on the `external` set: see PR #159.
- Next: Kajetan's G6 hand check with the PR zip; Tier B (the search) after the worker time limit
  and G18; the speck rule needs per-piece triangle counts in the report.
