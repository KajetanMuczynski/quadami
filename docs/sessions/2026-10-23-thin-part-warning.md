# 2026-10-23 · cloud thread "Remaining defect ledger after libQEx" · thin-part warning
- Repos/commits: core master dd03272 + this PR. Builds on the defect ledger (PR #93, 2026-10-22).
- Did: `ThinParts` (`src/AutoRemesher/thinparts.cpp`) measures, on each island's first isotropic remesh T, where the
  other side of the part lies within one quad edge (a vertex within h whose normal faces it, dot < −0.5), the spots
  this forms (area ≥ h²) and the quad count at which one quad fits across half of the thin area. Report fields
  `thin_area_share`, `thin_spots`, `thin_growth_p50`, `thin_parts_warned`; per island `thin_area_share`, `thin_spots`.
  `remesh()` warns at ≥ 3 % of the surface, or ≥ 10 spots on ≥ 1 %; the add-on adds how to thicken a part (Alt+S).
- Found: the rule was picked on a Python prototype of the same detector scored against the ledger's thin+tip defects
  (238 runs, 6 sets): precision 0.70, recall 0.84; no rule beats ~0.75 precision, since thin closed shells remesh
  cleanly and sharp creases read as thin. The suggested count is right where the part is merely small for the target
  (spot_x8, plane, human_neutral) and a lower bound where it is structurally thin (swords: it doubles with each target),
  hence "Try at least N". Fatten +h/2 on the real vines: thin share 29 % → 9 %, ledger score −46 %; Solidify made it worse.
- Measured: `ab.py run` corpus, closed, sculpt, brush (216 runs, u0) against master dd03272: out.obj byte-identical
  216/216; detector median 0.3 %, max 1.0 % of the run's total time; wall time 1088 s vs 1099 s; peak RSS of the 24 runs
  above 300 MB within 1 %. Warning fires on corpus 23/72, closed 48/78, sculpt 1/42, brush 2/24.
- Next: show it only together with output defects on those spots if it proves noisy (needs the Lost-parts check of
  the vines thread); local sizing on thin parts (TODO 4.1).
