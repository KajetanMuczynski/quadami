# 2026-11-19 · Claude (cloud) · Crease slide choice on the input (TODO 2.35)
- Repos/commits: core from master 0ffb31d4, branch fix/noise-crease-slides
- Did: the extractor's choice between the finish with and without crease slides (#195) is now made
  again after both finishes are projected onto the input (folded faces first, then folded or
  twisted); when both have the same quads, a third candidate keeps the slides except around the
  faces they leave bad. Test `slide_choice`.
- Found: the 10k Hausdorff "regression" of #166 on barramundi and leeperrysmith is a single-target
  effect (21-target sweep 6k-14k: master/v0.2.1 geo-mean 1.007 and 0.975); usage_gltf_scifihelmet 5k
  is already better on master than on v0.2.1. The spot_noisy 10k fold came from the slide choice
  being made on T: the projection untangled the finish without slides, not the one with them.
  Dropping short crease chains from the slides fixed spot_noisy but added folds on ext_plant and the
  corset; deciding per island lost a whole island's slides on the corset over one fold.
- Measured: `ab.py`, 6 replicates per run, every public set plus the private sets, against 0ffb31d4
  (numbers in the PR).
- Next: TODO 2.35 is added by #196; mark it after both merge.
