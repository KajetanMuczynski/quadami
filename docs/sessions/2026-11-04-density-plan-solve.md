# 2026-11-04: density plan from the continuous cover, ±20 % bar

Thread "Narrow density overlay prediction bar" (branch `feat/density-overlay`, PR #164).

- Cause of P1's failure: the cover solve realises 43-77 % of the requested size variation (stage
  diagnostics `continuous_size_slope`); a size law cannot predict that.
- `planRemesh()` now runs each island up to the continuous cover solve and reads the quad edge from
  that map (8 smoothing steps). Region error 0.16 → 0.04 log₂ on corpus (5 replicates agree), sculpt
  0.17 → 0.04; independent check with its own metric: 0.05-0.06. Plan costs 0.6-0.7 of a run (Kajetan
  accepted). Change bar ±20 %; Anisotropy joins the planning settings. Interface 1.12.
- Results and rejected variants: docs/PREVIEW.md §4.5.
