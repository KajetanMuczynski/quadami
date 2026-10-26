# 2026-10-26 · cloud thread · Folds and flips since 0e9bec8: bisection
- Repos/commits: 0e9bec8 to master core e76d800 (#100); release commit for 0.1.0.
- Did: bisected release-check #5's fold/flip ratchet failure (lane per merge, then full x6), confirmed on every public set and the private sculpts; release notes, release-check baseline.
- Found / Measured: below (copied from the project-files note).
- Next: release-check on the release commit with baseline 877891d, Windows test, tag v0.1.0.

Release-check #5 (de6ac3a vs 0e9bec8) failed its §3.2 ratchet on folded quads and faces flipped off
input folds. This note finds which merge did it. Measured with `tools/bench/ab.py` in the cloud
container (Linux, 4 vCPU, system oneTBB 2021.11, `--replicates 5 --jobs 4 --timeout 1200`), work
folder `~/ar-work`; per-mesh tables from `release_gate.score()`.

### Which merge

Nine merges since 0e9bec8 change the core: #78, #80, #89, #94, #82, #91, #96, #98, #100.

- `ab.py lane` (u0 + one float32 replicate, every public set except suzanne, which needs Blender):
  #80, #89 and #94 are byte-identical to the state before them. #78 changes 26 of 512 runs (sculpt_dirty,
  ext_mosquito, ext_plant, ext_scatteringskull), none of them in corpus or closed.
- With all 6 replicates on corpus and closed, folds and flips over the steps:

| step | corpus folded | corpus flipped | closed folded | closed flipped |
|---|---|---|---|---|
| 0e9bec8 → #78 (= #80, #89, #94) | 526 → 526 | 2195 → 2199 | 608 → 608 | 1590 → 1590 |
| #94 → **#82** | **526 → 559** | **2199 → 2304** | **608 → 638** | **1590 → 1647** |
| #82 → #98 (#91, #96, #98) | no change | no change | no change | no change |

The 0e9bec8 → de6ac3a totals match release-check #5's Linux leg to the digit (e.g. closed folded per
1k faces 1.4608 → 1.5298), so the cloud run reproduces it.

### #82 on every set (#94 → #82, 6 replicates)

| set | folded quads | flipped off input folds | faces | runs changed |
|---|---|---|---|---|
| corpus | 526 → 559 (+6 %) | 2199 → 2304 (+5 %) | +0.13 % | 64 / 432 |
| closed | 608 → 638 (+5 %) | 1590 → 1647 (+4 %) | +0.20 % | 82 / 468 |
| sculpt | 242 → 258 (+7 %) | 1499 → 1510 (+1 %) | +0.08 % | 70 / 252 |
| brush | 151 → 177 (+17 %) | 273 → 313 (+15 %) | +0.34 % | 29 / 144 |
| external | 6716 → 6787 (+1 %) | 55353 → 57992 (+5 %) | +0.86 % | 116 / 240 |
| private realsculpt | 26 → 27 | 154 → 158 | +0.03 % | 9 / 36 |
| private realsword | 591 → 626 (+6 %) | 4508 → 4857 (+8 %) | +0.40 % | 60 / 96 |

Where the extra defects come from: in every set except external, all of them are in runs whose output
gained faces (parts the cleanup deleted before #82 now stay). In external, runs that gained faces add
204 folds and 2876 flips, while runs that lost faces remove 133 folds and 240 flips.

Concentrated, not spread:
- corpus: sword and human_neutral carry 30 of the 33 extra folds and 88 of the 99 extra flips
  (also plane, nefertiti, boat with a few each);
- closed: the thinnest ellipsoids (thickness 0.003 to 0.03);
- sculpt: sculpt_parts and sculpt_decimated; brush: brush_spikes, brush_dyntopo_strokes, brush_twist;
- external: ext_plant (leaf cards) and ext_flighthelmet carry most of the flips;
- private: the sword (+44 folds, +166 flips) and its vines (+129 flips); the head is unchanged.

What #82 bought in the same step (structural gates, runs failing / cases failing on every replicate):
H6 corpus 9 → 5 runs, closed 109 → 97 runs; H4 closed 32 → 28; realsword H6 1 → 0. One case got
worse on H6 (external, 4 → 5 cases on every replicate: kept parts add quads).

The platform-divergence thread checked release-check #5 per case: the shift is significant when the
three platforms are pooled (corpus folds 14 worse / 3 better cases, p = 0.01; valence 2 / ≥ 7 10/0).
It is not significant on each platform alone, and it sits in the same few cases on all three.

### Verdict and recommendation

#82 is an accepted trade: it stops the cleanup from deleting thin parts (the vines fix required for
0.1), and those parts are thinner than a quad, so they fold, which is the known thin-part limit the
panel warns about (#98). No bug: nothing outside the kept parts changed. Accept, document in the 0.1.0
notes (Known limits) and in TODO (sizing or untangling of kept thin ribbons), and use #82's merge
`877891d` as the baseline for the 0.1 release check, then `v0.1.0` for later checks.

Side finding: `ab.py measure()` sometimes records `metrics: None` (nefertiti quality 2000, 1 to 2
replicates per state at `--jobs 4`; metrics.py succeeds when rerun alone), and release_gate then counts
the run as failing H1. Remeasured before the numbers above; sent to the gap-analysis thread as a TODO.

### After the bisection

`ab.py lane 877891dd e76d8009` (master core): 512 of 512 runs byte-identical (u0-u1, every public set but suzanne), so a 0.1.0 check against 877891d sees the same output.
