# 2026-11-23 · Claude (cloud thread) · Density bar removal, guides off results
- Repos/commits: core f175150 (no core change)
- Did: from Kajetan's hand test of the 0.3 build (pr197 7208ff6): removed the density preview's Change
  bar (it wrote Target Quads, and Blender filled it from 0 so negative values looked like no change);
  the overlay follows Target Quads within ±20 % and the panel names the range. Draw Guide and Mark Guide
  refuse on a remesh result (Remesh reads the original's guides, so a guide on a result was silently
  ignored); the Guides section says so and offers Select Original.
- Found: guides are drawn only in Object Mode and the result carries no guide marker; within ±20 % the
  overlay hardly changes (+13 % quads ≈ −6 % edge ≈ 4 % of the colour ramp; checker steps 19 % in edge).
  Left for 0.4 (hand-off note in the shared research folder).
- Measured: nothing measured (add-on UI only; output unchanged).
- Next: 0.4 overlay rework (model's shading + continuous one-quad grid), guides in Edit Mode, exact guide
  edges on the result from the core.
