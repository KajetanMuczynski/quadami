# 2026-11-27 · Claude (cloud thread) · Guide loops: the stub switch limited to decimated islands, checked
- Repos/commits: exp/guide-ring-cycles (experiment, no PR), debcfe9 for the runs. Decision by Kajetan: go with
  the stub switch, limited to decimated islands; disprover before the lane; the band as the fallback.
- Did: `guide_stubs=decimated`. The switch is now a flag the caller sets (`IsotropicRemesher::setCollapseHardStubs`
  through the core's wrapper), no longer an environment read inside the vendored remesher; `resample()` sets it
  when `decimateIfTooDense` decimated the island. `guide_stubs=collapse` still acts on every island.
- Disprover on the three-way run (fresh worker, own walker and distance): closes hold, 70 / 196 / 209 / 207 of 354
  for baseline / band of 3 / stub / decimation off; its verdicts equal `loop_gate.py` and loopcheck in 1,416 of
  1,416. Added:
  - The baseline is last at every tolerance (0.25 to 1.5 edges); the order of the options is not stable. At 0.25:
    40 / 113 / 90 / 131. The stub arm's closed loops sit farther from the drawn line (median d95 0.27 against
    0.21-0.23).
  - Stub: no quality cost in the outputs (irregular vertices, edge-length spread, bad angles, distance to the
    input). Near the ring every option is better than the baseline.
  - Band: 10 of 72 knight runs at 750 quads took the envelope fallback and came out as a closed double-walled
    shell of the open torso; the run records do not show it. No other arm does this.
  - Decimation off: more folds and bad angles on phenebub, more guides dropped by the fallback on the head (37
    loops against 8-10).
  - Head, plain input: the options are as good there as on the replicates; it is the baseline that is unusually
    good on that one input (12 of 19 against 2 to 7 on the five replicates).
  - Corrections to my text: quad counts are within 1.5-1.9 % of target per arm (median), not 1.6 % everywhere.
- Identity checks of `guide_stubs=decimated` (out_sha of the run records, plain + 5 replicates):
  - not decimated, with guides (cut rings, staircase twins, head at 6k and 10k) against the baseline: 204 of 204
    identical (`decimated_islands` 0 in all);
  - no guides (head, knight torso, phenebub part at 750-3000; 11 of 12 cases decimated), switch on against off:
    72 of 72;
  - decimated, with guides (head inputs at 3k, knight, phenebub) against the earlier `collapse` arm: 342 of 342;
  - baseline of this commit against the earlier commit (head at 3k and 10k): 12 of 12.
- Lane against the merge base 76a81ac, switches off: light. 846 runs byte-identical on corpus, closed, suzanne,
  sculpt, brush, external, cad, primitives, plates, usage, marked and guides (plain + one float32 replicate), 0
  differ. The first pass printed "full" for 20 guides runs at 6k/10k that only the base side had from earlier
  work; after running them on the head side: 0 failed or missing.
- The stub switch alone, on default settings (no other experiment switch), same inputs: 15 -> 38 of 354 (knight
  4 -> 25, phenebub 6 -> 8, head inputs 5 -> 5). With the other guide switches on it was 70 -> 209. So by itself
  it is not the fix; it removes one obstacle that the rest of the stack then uses.
- Not done: nothing prepared for master. ctest not re-run. `ci/lint.sh` not applied to `autoremesher.cpp` (it
  reformats the fold-fallback thread's lines); my two additions there follow the file's style by hand.
- What would falsify: an undecimated or guide-less run that changes with `guide_stubs=decimated`; a dense sculpt
  with surviving guides where the stack with the stub switch does not beat the stack without it.
- Not re-checked: the loss in closeness to the drawn line (0.27 against 0.21-0.23) was not traced; timings come
  from one pass on a shared 2-core machine; all rings are generated; the lane covers defaults only.
