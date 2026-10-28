# Raise Target Quads to bring lost parts back: design

Design note for the panel hint that tells a user which Target Quads brings back the parts a run
lost (Kajetan, 2026-10-28: "we have to work on an algorithm for this"; not 0.2). Thread "Design
Raise-to-N suggestion in panel", master `aeb72dd`. Nothing here is built; no code before Kajetan
agrees on the design. It builds on:
- PR #138 (not merged at `aeb72dd`): `lost_parts` split by cause (thin, pieces, open edge, other)
  and the warning "a limit of Target Quads, not a remeshing error";
- research/thin-warning-2026-10-27.md (project files): which lost parts come back at ×1 to ×8;
- research/thin-cap-2026-10-28.md: the local cap is a no-go, raising Target Quads is the lever;
- research/target-quads-2026-10-27.md and G17 (PR #137): one run predicts the next for the
  distance, not for the parts.
- research/target-guidance-2026-10-28.md (thread "Research Target Quads guidance algorithm"): the
  gates of §8, measured on 22 meshes that lose parts at T₀ and 15 that do not, 6 replicates, plus
  held-out start targets and a disprover. Its "keep small parts" retry in the core (TODO G18, row
  added by PR #149) takes most lost parts in small islands off this hint's hands (§0.5).

Updated 2026-10-28 with the gate results: the fixed-rung Tier A of the first version failed G1,
Tier C failed G3, and the search's step and cost changed (§2, §8).

Labels: **[M]** measured (in the cited research note, 6 replicates unless said), **[C]** checked in
code at `aeb72dd` or on #138's head `061a9a3`, **[I]** inferred here. Nothing new is measured in this
note.

## 0. Result

1. **"Raise to N" cannot mean "raise until the warning goes away".** The far rule is relative: a
   vertex is lost when it is more than 3 h₀ from the output, with h₀ = √(area / target)
   [C, `remesh.cpp` `measureLostParts`]. Each raise shrinks h₀, so finer parts count as lost at the
   higher count. The warning still fired at 24,000 on 5 of 10 real models although the parts lost
   at 3,000 had come back on 7 of them [M]. A hint that promises the warning clears would chase its
   own tail (the old thin-part estimate did exactly that: 1.6–2.1× the count at every count from
   3,000 to 75,000 on the sword [M]).
   **Goal instead:** *the parts this run lost come back*, measured at this run's distance
   3 h₀(T₀), not at the new run's.
2. **No N is computed from geometry.** The lost parts' thickness misses the count that brings
   them back by up to 30× [M, #138 code comment and research]; input-only predictors fail for
   parts [M, target-quads §2.4]. The response is also not monotone: sword at 3,500 had 38–62 far
   vertices, at 3,700 124, at 4,500 0 [M]; a closed ellipsoid was clean at 300 in 6 of 6 replicates
   and lost parts at 600 in 2 of 6 [M, ladder-summary]. A single predicted number would be a
   promise Quadami cannot keep.
3. **So the hint offers a step, and only a run checks it.** Two tiers:
   - **Tier B, the search (the main tool):** a "Find Target Quads" button that runs 1.5×, then
     ×1.33 per step (2×, 2.67×, 3.56×, 4.74×, 6.32×, capped at 8×), each judged at 3 h₀(T₀), and
     stops at the first run with nothing far anywhere. The user gets *that run's result*, not a
     number. It found every reachable case in 2.5 runs on average, 1.23× above the smallest clear
     count (held-out start targets: 3.2 runs, 1.63×) [M, research §5B]. Every N it shows was run on
     this input.
   - **Tier A, no extra run:** the same steps one at a time by hand: "Try 4,500 (1.5×)", labelled
     as untested, with the honest range "parts like these came back between 1.1× and 6× on test
     models, sometimes never". After the user's next run the panel says what came back and offers
     the next step. A fixed single rung is not enough: 2× brings back the parts on 27 % of the test
     meshes, the median needs 5× (real models 3×) [M, G1].
   - **Tier C, dropped:** no report field ranks the needed raise (G3: best ρ −0.62 for the lost
     parts' lateral width, and one-shot it hits 37 of 121 cases) [M].
4. **It fails by saying what it found**: "Not back at up to 24,000 (8×): 130 of 308 lost points
   remain. These parts are too thin for any count you would want: thicken or delete them." This is
   common: 43 of 121 lost cases never cleared up to 8× (mosquito, plant, iron gate wires, the
   thinnest stress capsules) [M].
5. **Scope shrinks once G18 lands.** Most lost parts in real models are their own small islands
   (hair, leaves, petals, wires). Raising only those islands in the core keeps 64 of 78 such cases
   for 1.01–1.41× the quads, where a global raise needs 1.8–4.4× or never works [M, research §4].
   The Raise hint is then for parts inside the main island (about a third of the cases), which
   mostly clear early (vines, rubber plant, dragon at 1.1×, sword 1.25–1.5× [M]), and for what G18's
   budget leaves.

## 1. When the hint shows

From the report of a finished run (`status` ok) [C for the fields on #138]:

| Condition | Hint |
|---|---|
| `lost_parts.far_input_vertices` = 0 | none |
| thin class ≥ ¼ of far vertices | **Raise hint** (§2) |
| pieces class ≥ ¼ | Raise hint, plus #138's "delete specks you do not need"; if every lost piece has fewer than 64 input triangles, only the delete line (they are specks, not parts) [I: the 64 is the per-island floor's own size] |
| open-edge class ≥ ¼ | #138's Solidify line first; Raise hint as the second choice (vaseflowers: 82–90 % back at 2×, 10 points never back [M]) |
| only "other" | no Raise hint: it is not a Target Quads limit, and #138 already asks for a report |
| next rung > `MaximumTargetQuadCount` (2,000,000 [C]) or > 8× T₀ | the "not back" text of §5, no button |

The hint never replaces the warning; it sits under it, like #138's Alt+S line. The **Missing parts**
summary row stays as it is.

**Dismiss:** a small "Keep these quads" (X) hides the hint for this source until its input changes
(hash, §4). A user who wants 3,000 quads and does not care about the vines must not be nagged on every
run.

## 2. How N is chosen

### 2.1 The step

Rungs 1.25–8×, fixed distance 3 h₀(T₀), all replicates [M, research §5C, G1]:
- the median of 22 meshes is back only at 5×; 27 % by 2×, 68 % by 8×;
- real models only (sword, sword_vines, 7 external): median 3×, 4 of 9 by 2×, 7 of 9 by 8×;
- main-island parts (vines, rubber plant, dragon) clear at 1.1×, the sword at 1.25–1.5×.

So no single rung is worth offering alone. The search's steps are **1.5×, then ×1.33**, the best of
the sequences the research compared (§0.3). For main-island parts after G18 the research suggests a
first step of 1.1× [I, not measured for that subset]; that is a gate for the G18 follow-up (§8, G7).
Displayed counts are rounded up to two significant digits (6,140 → 6,200), as the old estimate
did [C, `remesh.cpp`].

### 2.2 Tier B: the search

```
N = round_up(1.5 × T₀)
while N ≤ 8 × T₀:
    if estimated time(N) > budget or memory(N) > free memory: stop, say why
    run at N with reference T₀ (§3), under the worker's time limit
    record (N, far at the reference, folds, holes, time) in the history (§4)
    if far == 0: stop, keep this result
    N = round_up(1.33 × N)
otherwise: keep T₀'s result and show the §5 text with the best run found
```

- **"Clear" is strict: nothing far anywhere at 3 h₀(T₀), new losses included** (option 2 of §3).
  With the softer rule (only T₀'s far points back), 6.6 % of back rungs are followed by a lost one;
  with the strict rule 33 of 78 cases relapse at a higher rung, 58 of 82 from held-out start
  targets, some in every replicate (sword_vines clear at 1.1–1.5×, lost at 1.75×; vaseflowers clear
  at 6×, lost at 8×) [M, G2]. So the search keeps the clear run and never suggests a higher one.
- **No bisection below the clear step.** Not monotone, so a bisection finds *a* clear N, not the
  smallest, at a full run per probe.
- **Every run gets the worker's time limit.** ext_iron_gate hangs in extraction cleanup at 3,300
  (u0) and 4,000 (u1) (TODO 2.30, row in PR #149) [M, research §2]; a search tries more counts, so it meets such
  hangs more often. A run that hits the limit counts as not clear and the search goes on.
- **Three counts at once** (1.25×, 1.5×, 1.88×) needs 1.6 rounds at 1.10× the minimum [M]. It triples
  peak memory and runs all cores hot, so not on a 4-core laptop by default; an option for machines
  with the room [I].
- **The kept result is the run itself**, made through the normal result path (G10: Remesh again
  uses the source).

### 2.3 Cost

Measured per rung, relative to the T₀ run [M, G4, under 2–4 parallel jobs]: time ×1.1 at 1.1×,
×2.1 at 2×, ×3.8 at 4×, ×10.8 at 8× (p90 ×27.7); peak memory ×2.0 at 2×, ×6.3 at 8×. Super-linear
above 4×, so the cost text must not scale the first run linearly.
- A search that clears in 2.5 runs (1.5×, 2×, half the time a third at 2.67×) costs about 5× one run
  [I, from those factors]. A search to 8× that never clears costs about 30× one run [I]: on a
  3,000-quad model a couple of minutes, on a 50k-quad sculpt the better part of an hour.
- So Tier B shows the worst case before it starts ("up to 7 runs, about 4 min"), has a time budget
  (default 5 min), runs one run at a time at the add-on's thread limit, and Esc cancels the search.
- The existing memory estimate stops the search before a rung that would not fit.

## 3. Core change: measure against the first run's distance

Today `measureLostParts` uses only the current target's h₀ [C]. The goal of §0.1 needs the far count
at the reference distance:
- `RemeshParameters` gains `lostPartsReferenceTarget` (0 = off); the CLI gains
  `--lost-parts-reference <target quads>`. The reference width is √(area / reference target), the
  same input area, so the add-on passes T₀, not a length in scene units (the add-on's object scale
  cannot drift between runs [I]).
- The report gains `lost_parts.reference`: `{"target": T₀, "far_input_vertices": n, "thin": …,
  "pieces": …, "open_edge": …}`, computed with the same classifier at radius 3 h₀(T₀).
- The pass already computes every input vertex's distance to the output [C]; only the threshold and
  the classification change, so the cost is one more counting loop, plus the thickness sampling
  when there are reference-far vertices [I; gate G5 measures it].
- Output geometry does not change: lane **light** (`ab.py lane`), interface minor bump.

**Same points, not just the same count.** "Back" should mean *the points lost at T₀* are now
within 3 h₀(T₀). Counting all reference-far points at N also counts new ones; the research found 6
new points on the plant [M]. Two ways:
1. store the indices of T₀'s far vertices (up to some thousands) on the source and pass them in;
2. count all reference-far points at N (simpler, slightly stricter).
Recommend (2) [I]: it can only under-report recovery, never over-report, and needs no list in the
.blend.

## 4. The history

The add-on keeps, on the **source** object, a short list of runs: input hash (vertex count, face
count and a hash of positions, like G16 (c) needs), settings that change the output, T, quads,
far points at the reference, folds, holes, seconds. It is what lets Tier A say "back" after the
user's own next run, and what makes Tier B's choices visible.
- A new hash or changed settings (Quality, Adaptivity, Sharp edges, Follow Marks, Repair Folds)
  start a new history: a rung checked under other settings proves nothing.
- At most 8 entries; JSON in an ID property next to `REPORT_PROPERTY` [C for that property].
- **A checked N is checked on this machine.** Linux and Windows outputs differ (cube 3k: 2,938 vs
  2,923 quads [M]); on one machine the same input and N give the same output (the ×6 side is
  deterministic, CLAUDE.md) [C]. A .blend moved to another OS keeps its history, so the text says
  "checked on this computer" only when the history's platform matches [I].

## 5. Wording

Constraints: the panel wraps to Blender's label width (#138's `_text_room`, 190 px at scale 1 on
4.2 [M]); every line must pass the CI OCR gate (no "…"). One sentence per line where possible,
numbers with a thousands separator, no "h₀" or "quad widths" in the hint (the warning has those).

**Tier A, first run lost thin parts** (under #138's warning, which already says "a limit of Target
Quads, not a remeshing error"):

> Try 4,500 Target Quads (1.5×). On test models such parts came back between 1.1× and 6× as many,
> sometimes never; only a run on this model shows it.
> [ Set 4,500 ]  [ Find Target Quads… ]  [ × ]

**After the next run, parts back:**

> All 208 points lost at 3,000 are back at 4,500.

**After the next run, partly back:**

> 178 of 308 points lost at 3,000 are back at 4,500; 130 are still missing. Try 6,000.

**Tier B finished, clear:**

> Back at 6,000 Target Quads (checked: 2 runs, 9 s). This result is the 6,000 run. A higher count
> can lose parts again, so keep this one.

**Fails honestly** (8× reached, cap, or time budget):

> Not back at up to 24,000 (8×): 10 of 82 points still missing. These parts are too thin for any
> count you would want: thicken them (Alt+S) or delete them.

> Stopped at 14,000: the next count (19,000, about 9 min) is over your time budget.

Each text says what was run and what was not. "Checked" appears only for an N that was run on this
input with these settings.

## 6. Relation to G17 (Finer / Coarser)

G17's Finer is a *distance* step (N·2 corrected by slope 0.87 [M]); it cannot see parts. Two
buttons with two numbers under one result would confuse. One row:
- parts lost (thin or pieces class): the Raise rung of §2 replaces Finer;
- no parts lost: G17's Finer / Coarser.
Both write Target Quads and both read the history, so G17 should be built on the history of §4 too.
After G18, the core's "Kept N lost parts: +Q quads" line comes first; the Raise row shows only for
what it left.

## 7. What Kajetan may have missed

1. **The warning moves with the target**, so "Raise to clear the warning" has no answer on 5 of
   10 real models (§0.1). The design has to fix the reference distance at the first run, which
   needs the small core change of §3. Without it any Raise hint is the old 1.6–2.1× estimate again.
2. **A number before a run cannot be honest** (§0.2): not monotone, and a 1-ulp change of input can
   flip a part. The only number Quadami can stand behind is one it ran. Tier A says "try", Tier B
   says "checked".
3. **A global raise pays everywhere.** On the bee a global raise needs 2.5× the target (1.8× the
   quads) for 308 points [M]. When the lost parts are separate small islands, raising only those
   islands costs 1.01–1.10× [M, research §4]. That is now TODO G18 (row in PR #149), for the core;
   this hint is the fallback for parts inside the main island, where nothing cheaper is known (the
   local cap moved the losses [M, thin-cap]).
4. **"Back" at 3 h₀(T₀) is lenient.** A vine within three old quad widths can still be a flat
   ribbon with folds. The hint must not say "fixed" or "clean"; it reports points back, and the
   history keeps folds and holes so the panel can say "back, but 12 more folds" [I]. Whether 3 h₀ is
   the right distance for a user's eye is a hand check (gate G6).
5. **Some users do not want the parts** (specks, hair cards for a game cage). Hence the dismiss and
   the delete line for specks.
6. **Tier B costs heat and time on Kajetan's own laptop**: about 5× one run when it clears, about
   30× when it never does (§2.3). Opt-in, a budget, one run at a time, the thread limit and the
   worker's time limit are not optional.
7. **Better than any of this, for the user:** a Target Quads that is not the knob. G17's later
   Edge length / Detail modes would let a user ask for "parts down to 2 mm" directly. That needs a
   count-independent thin-part measure on the input (target-quads §2.4, a real gap), so it is later
   than Tier A, but it is where this should end up.
8. **Probe TODO 2.29 first.** Curvature sizing normalised per island may be why separate thin
   islands are lost at 1× at all [I, research §5, not measured]; if fixing it keeps them, both G18
   and this hint get cheaper.

## 8. Gates

Results from research/target-guidance-2026-10-28.md §5C (rungs 1.25–8×, fixed distance 3 h₀(T₀),
6 replicates, 22 lossy meshes) [M]:

| Gate | Bar | Result | Verdict |
|---|---|---|---|
| G1 smallest rung | median model by 2×, ≥ 70 % by 8× | median 5×; 27 % by 2×, 68 % by 8×. Real models: median 3×, 4/9 by 2×, 7/9 by 8× | **fails**: the fixed 2× rung is dropped, Tier A uses the search's steps (§2.1) |
| G2 not monotone | < 10 % | 6.6 % on the soft rule; strict rule 33 of 78 cases relapse | soft passes only because it ignores new losses; the search uses the strict rule and keeps the clear run |
| G3 a field ranks k | ρ ≥ 0.6 on two sets | far count +0.41/+0.52, farthest +0.36/+0.14, thickness ≈ 0; lateral width −0.60/−0.62, one-shot 37/121 | **fails**: Tier C dropped |
| G4 time and memory | speed model within p90 | time ×2.1 at 2×, ×3.8 at 4×, ×10.8 at 8×; memory ×2.0 / ×6.3 | super-linear above 4×; §2.3 uses these factors |
| G5 reference pass | ≤ 10 % of `lost_parts_ms` | the whole pass is 0.7 % of a run; output byte-identical; the core's count matches independent distances on 1,635 of 1,635 runs | likely passes; time with the reference radius not measured on its own |
| G6 hand check | Kajetan agrees on 3 models | not done (needs Blender) | open |
| G7 first step after G18 | — | 1.1× for main-island parts suggested, not measured on that subset [I] | open, for the G18 follow-up |

## 9. Build order (each its own PR, after 0.2)

1. Core: the reference target (§3) and `lost_parts.reference` in the report, tests in
   `tests/quality_report_test.cpp` (conflict-prone: `remesh.cpp`). Lane light. The research probe
   `1617027` has this as an env var; the interface name is settled in that PR (a target count, not
   a length in scene units, §3).
2. G18 (keep small parts, row in PR #149), since it changes what this hint has to cover; then G7.
3. Add-on: history (§4), Tier B operator (modal, sequential worker runs under the time limit,
   budget, Esc), the cost estimate.
4. Add-on: Tier A text and Set button, dismiss, unit tests in `report.py` style (no bpy), OCR panel
   shot with a lost-part model.
5. With G17: one row of buttons (§6).

## What would falsify this

- A report field that predicts the clearing step within one step on ≥ 80 % of cases: then a
  predicted N is honest enough. G3 found none (best one-shot 37 of 121) [M].
- The relative far count settling to 0 at some raise on most real models: then the reference
  distance of §3 is not needed. The research found the opposite on 5 of 10 [M].
- A step sequence much cheaper than 1.5× then ×1.33 on the same data, or the held-out cost (3.2
  runs, 1.63×) getting worse on a new set.
- Users in a hand test reading "points back" as "fixed" despite the folds (G6): then the wording
  needs the fold count in the same line.

## What I did not re-check

- Every number above is quoted from the cited research notes; nothing was re-run here. The gate
  results had a disprover pass that found two script bugs (fixed in the report); the fixed version
  was not disproved a second time.
- The cost of §2.3 for a whole search is derived from per-rung factors measured under 2–4
  parallel jobs, not timed as a search on an idle machine.
- The 1.1× first step for main-island parts (G7) and the three-at-once option on a 4-core laptop.
- That #138 merges with its class fields unchanged; §1 depends on them.
