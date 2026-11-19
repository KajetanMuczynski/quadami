# 2026-11-18 · Claude (cloud) · Crease slides in the extractor smoothing
- Repos/commits: core master 3523a16 → branch `fix/crease-slide`
- Did: ported the 2026-11-04 slide-only prototype (research/crease-sizing-2026-11-04.md) onto master's
  refactored `smoothAndProject()`, with the extractor's existing FeatureEdgeSet. Removed the prototype's fold
  costs by finishing an island with slides and again without, keeping the result with fewer folded or twisted
  faces; the prototype's in-smoothing guards were dropped (they did not reach the folds, which appear in the
  clean-up passes after the smoothing).
- Found: a guard in `SurfaceProjector` against new twisted quads cut twisted quads by 25–60 % on hard-surface sets
  but added folds on the 30° wedge (6/6); left out, patch in the shared research folder
  (relax-twist-guard-2026-11-18.patch).
- Measured: TODO 2.8 (crease slides), `ab.py` full lane, every public set, 6 replicates per side.
- Next: private sculpts lane; the thin quad rows at rims (sizing).
