# 2026-10-31 · Claude (cloud thread) · Research automatic closed edge loops
- Repos/commits: core 1596a75, 6f5f76f on feat/tube-ring-loops (no PR), base master 57c19f9
- Did: holonomy probe of the cover around dual rings; prototype `--closed-rings <ratio>` (tube rings
  constrained to zero cross shift, off by default); `tools/bench/loops_report.py`; ab.py A/B with 5 replicates
  on closed, primitives, external, sculpt; disprover pass.
- Found: two spiral types (helix around a tube; loops winding between singularities, the head's kind, which
  only guides fix); two causes of the helix (field twist from one-way diagonals in the input; period-2 rounding).
  ZRemesher and Quad Remesher spiral too; their fix is region borders.
- Measured: flag off byte-identical to master on 197 runs; flag on raises the closed-edge share in every
  replicate on 7/34 primitives, 19/40 external, 12/42 sculpt cases, lowers it on none; costs on external:
  folded quads +5 %, SJ p1 x0.86, time +7 %; needle quads on cone frustum and 64-segment cylinder.
- Next: research/handoff-closed-loops-2026-10-31.md (shared folder) section 6.
