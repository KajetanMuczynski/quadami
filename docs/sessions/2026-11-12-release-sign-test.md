# 2026-11-12 · Claude (cloud thread) · Release gate: per-case sign test (TODO 1.16)
- Repos/commits: quadami master abe3d9e; branch feat/release-sign-test
- Did: `release_gate.py` decides per case: candidate minus baseline defects (V1-V5) and replicates
  failing each structural gate and V6, summed over replicates and `--pair`s (platforms), one-sided
  sign test per measure and set, Holm over all; p < α before Holm is a lead. Per run: every crash
  fails unless the baseline crashed in that case (audit B1), lost runs fail, `--hard` unchanged.
  release-check.yml: per-platform jobs run `--decide runs` and upload `pair.txt`; new job `verdict`
  pools the platforms' records. `gates.py` scores records without `out.obj` (retired V1 rule left out).
- Found: Holm over the 10 defect tests (or 24 with structural gates) lets #82's folds (14/3, p 0.006)
  pass; hence fail = Holm, lead = unadjusted. Per-case "all candidate replicates worse than all
  baseline ones" is no detector: 2-5 such cases in every same-build platform comparison.
- Measured (release-check run #5 records, de6ac3a vs e8d104c, 6 replicates, 3 platforms): #82 pooled
  fails on corpus valence 10/0 (p 0.001), leads corpus folds 14/3, flips 14/5, closed folds 15/5;
  same build on two platforms 0 of 12 fail (5 leads), 4 pooled pseudo-null triples 0 fail, 0 leads.
- Next: per-platform H7 (TODO 8.6); more independent cases (sculpt/external sets) for power.
