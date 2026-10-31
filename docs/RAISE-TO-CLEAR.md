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
- the research thread "Research Target Quads guidance algorithm", which measures the open gates of §8.

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
3. **So the hint offers a rung, and the next run checks it.** Three tiers, each usable alone:
   - **Tier A, no extra run (first to build):** "Try 6,000 (2×)", a fixed rung from the ladder the
     research measured, labelled as untested on this model. One click sets Target Quads. After
     the user's next run, the panel says how many of the first run's lost parts are back.
   - **Tier B, opt-in search:** a "Find Target Quads" button that runs the ladder ×1.5, ×2, ×3,
     ×4, ×6, ×8 itself and stops at the first rung where the parts are back. The user gets *that
     run's result*, not a number. Every N it shows was run on this input.
   - **Tier C, not planned:** a prediction from the report alone. Only if gate G3 (§8) finds a
     report field that ranks the needed factor.
4. **It fails by saying what it found**: "Not back at up to 24,000 (8×): 130 of 308 lost points
   remain. These parts are too thin for any count you would want: thicken or delete them."

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

### 2.1 Tier A: the fixed rung

The research ladder at a fixed distance (thin-warning §3, 6 replicates each) [M]:

| mesh | lost at T₀ = 3,000 | back in every replicate from |
|---|---|---|
| sword, sword_vines, rubber plant, dragon | 1–208 points | 1.5–2× |
| bee | 308 | 4× (130 left at 2×) |
| plant, iron gate | 42 / 168–645 | by 8× (iron gate not monotone: more at 2× on 2 replicates) |
| mosquito | 13–74 | not by 8× (1–4 left) |
| vaseflowers | 82–100 | not by 8× (10 left in every replicate) |
| flight helmet (T₀ = 6,000) | 266–837 | 2 of 6 replicates at 4× |

So **2×** clears 4 of 10 models, **4×** 5 of 10 (+ 2 of 6 helmet replicates), **8×** 7 of 10 [M, as
read from that table; the per-rung counts for plant and iron gate between 2× and 8× are gate G1].
Tier A offers 2× first, then on the next run 4×, then 8×: each step doubles, the user sees every
result, and a doubling is also where G17's distance law is reliable (97 % within a factor 2 [M]).
Displayed counts are rounded up to two significant digits (6,140 → 6,200), as the old estimate
did [C, `remesh.cpp`].

Why not 1.5×: it clears the easy four and costs one more run on the others. If G1 shows the median
model clears at 1.5×, the first rung becomes 1.5× [I].

### 2.2 Tier B: the search

```
rungs = [1.5, 2, 3, 4, 6, 8] × T₀, rounded up to 2 significant digits
for N in rungs:
    if estimated time(N) > budget or memory(N) > free memory: stop, say why
    run at N, with reference width h₀(T₀) (§3)
    record (N, back, left, folds, holes, time) in the history (§4)
    if left == 0 and the run's own holes ≤ T₀'s: stop, keep this result
otherwise: keep T₀'s result, show the §5 text with the best rung found
```

- **No bisection below the clearing rung.** The response is not monotone (§0.2), so a bisection
  between a failing and a clearing rung finds *a* clear N, not the smallest, and every probe costs a
  full run. A finer rung list (×1.5 steps) is the cheaper way to stop near the edge [I].
- **No stopping on a near miss.** A rung with 1 point left is not clear. The text can say "1 of
  208 left" and offer to keep it.
- **The kept result is the run itself**, made through the normal result path (G10: Remesh again
  uses the source), so nothing is promised that was not built.

### 2.3 Cost

Time per run ≈ 8.4 s/Mtri + 0.27·(kq)^1.47 + 0.9 s (kq = thousands of quads) [M,
research/speed-memory-2026-10-22.md; p90 ×1.6]. The quad term of the ladder, relative to one run at
T₀: 1.5^1.47 + 2^1.47 + 3^1.47 + 4^1.47 + 6^1.47 + 8^1.47 ≈ 1.8 + 2.8 + 5.0 + 7.7 + 13.9 + 21.2 ≈ 52× [I,
from the model]. A full ladder on a 3,000-quad sword (≈ 3 s per run) is about 2–3 minutes; on a
50k-quad sculpt it is an hour. So:
- Tier B shows the estimated worst case before it starts ("up to 4 runs, about 3 min") and a
  time budget field (default 5 min);
- it stops at the first clear rung, so the median cost is the first 2–3 rungs (≈ 5–10× one run) [I];
- it runs one run at a time on the worker, at the add-on's thread limit (speed note: a `--threads`
  limit keeps Kajetan's laptop cool), and Esc cancels the whole search.

Memory grows by 20 KB per quad [M, speed note]: 8× of 50k quads is 400k quads, about 8 GB. The
existing memory estimate stops the search before such a rung.

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

> Try 6,000 Target Quads (2×). On most test models that brought such parts back. Not yet checked
> on this model.
> [ Set 6,000 ]  [ Find Target Quads… ]  [ × ]

**After the next run, parts back:**

> All 208 points lost at 3,000 are back at 6,000.

**After the next run, partly back:**

> 178 of 308 points lost at 3,000 are back at 6,000; 130 are still missing. Try 12,000 (2×).

**Tier B finished, clear:**

> Back at 6,000 Target Quads (checked: 2 runs, 9 s). This result is the 6,000 run.

**Fails honestly** (8× reached, cap, or time budget):

> Not back at up to 24,000 (8×): 10 of 82 points still missing. These parts are too thin for any
> count you would want: thicken them (Alt+S) or delete them.

> Stopped at 12,000: the next count (24,000, about 9 min) is over your time budget.

Each text says what was run and what was not. "Checked" appears only for an N that was run on this
input with these settings.

## 6. Relation to G17 (Finer / Coarser)

G17's Finer is a *distance* step (N·2 corrected by slope 0.87 [M]); it cannot see parts. Two
buttons with two numbers under one result would confuse. One row:
- parts lost (thin or pieces class): the Raise rung of §2 replaces Finer;
- no parts lost: G17's Finer / Coarser.
Both write Target Quads and both read the history, so G17 should be built on the history of §4 too.

## 7. What Kajetan may have missed

1. **The warning moves with the target**, so "Raise to clear the warning" has no answer on 5 of
   10 real models (§0.1). The design has to fix the reference distance at the first run, which
   needs the small core change of §3. Without it any Raise hint is the old 1.6–2.1× estimate again.
2. **A number before a run cannot be honest** (§0.2): not monotone, and a 1-ulp change of input can
   flip a part. The only number Quadami can stand behind is one it ran. Tier A says "try", Tier B
   says "checked".
3. **A global raise pays everywhere.** On the bee, 4× the quads for 308 points; the main body gets
   4× too. When the lost parts are separate islands (pieces class: mosquito specks, gate wire), a
   per-island multiplier (5.8's effective area, target-quads §5.3) brings them back for a fraction of
   the cost [I]. Inside one island (sword vines) nothing cheaper is known: the local cap moved the
   losses [M, thin-cap]. So for the pieces class the better long-term hint is "give these parts
   more quads" once 5.8 lands.
4. **"Back" at 3 h₀(T₀) is lenient.** A vine within three old quad widths can still be a flat
   ribbon with folds. The hint must not say "fixed" or "clean"; it reports points back, and the
   history keeps folds and holes so the panel can say "back, but 12 more folds" [I]. Whether 3 h₀ is
   the right distance for a user's eye is a hand check (gate G6).
5. **Some users do not want the parts** (specks, hair cards for a game cage). Hence the dismiss and
   the delete line for specks.
6. **Tier B costs heat and time on Kajetan's own laptop**: up to ~52× one run (§2.3). Opt-in, a
   budget, one run at a time and the thread limit are not optional.
7. **Better than any of this, for the user:** a Target Quads that is not the knob. G17's later
   Edge length / Detail modes would let a user ask for "parts down to 2 mm" directly. That needs a
   count-independent thin-part measure on the input (target-quads §2.4, a real gap), so it is later
   than Tier A, but it is where this should end up.

## 8. Gates (open, for the research thread)

The design depends on these; each has a pass rule set before the runs. Fixed distance d = 3 h₀(T₀),
6 replicates, thin + hand + stress sets, `ab.py`.

| Gate | Question | Pass rule | Changes if it fails |
|---|---|---|---|
| G1 | Smallest rung k ∈ {1.25, 1.5, 2, 3, 4, 6, 8} where T₀'s lost points are back, per mesh | the median model clears by 2×; ≥ 70 % by 8× | first rung and rung list of §2.1 |
| G2 | Not monotone: given clear at k, how often is a larger rung not clear; do replicates of one N agree | < 10 % of clear rungs followed by a not-clear one | if higher, Tier A must say "may not hold at higher counts" and Tier B must keep the clear run, never a later rung |
| G3 | Does any report field (thin share, thickness p10/p50, far count, farthest distance) rank the needed k (Spearman ≥ 0.6)? | ρ ≥ 0.6 on two sets | if yes, Tier A picks the first rung from it (Tier C); if no, Tier C is dropped |
| G4 | Time and memory per rung vs the speed model | model within its p90 on the ladder | the cost text and the budget check |
| G5 | Cost of the reference pass (§3) | ≤ 10 % of `lost_parts_ms` | if more, compute it only when a reference is passed (planned anyway) |
| G6 | Hand check in Blender: on 3 models, are the parts at "back" visibly there? | Kajetan agrees on all 3 | a stricter distance (2 h₀(T₀)) |

## 9. Build order (each its own PR, after 0.2)

1. Core: `--lost-parts-reference`, `lost_parts.reference` in the report, tests in
   `tests/quality_report_test.cpp` (conflict-prone files: `remesh.cpp`, `tests/CMakeLists.txt` if a
   new test file). Lane light.
2. Add-on: history (§4), Tier A text and Set button, dismiss, unit tests in `report.py` style (no
   bpy), OCR panel shot with a lost-part model at 1× and 2×.
3. Add-on: Tier B operator (modal, sequential worker runs, budget, Esc), the cost estimate.
4. With G17: one row of buttons (§6).
5. After 5.8: per-island "give these parts more quads" for the pieces class (§7.3).

## What would falsify this

- A report field that predicts the clearing rung within one rung on ≥ 80 % of models (G3): then a
  predicted N is honest enough and Tier B is overkill for most users.
- The relative far count settling to 0 at some raise on most real models: then the reference
  distance of §3 is not needed. The research found the opposite on 5 of 10 [M].
- Users in a hand test reading "points back" as "fixed" despite the folds (G6): then the wording
  needs the fold count in the same line.

## What I did not re-check

- Every number above is quoted from the cited research notes; nothing was re-run here.
- The ×1.5 / ×2 / ×4 / ×8 clear counts of §2.1 are read from a table that gives "back from" per
  model, not a per-rung count for every model (G1 closes this).
- The ladder cost of §2.3 is the speed model's prediction, not a measured ladder.
- That #138 merges with its class fields unchanged; §1 depends on them.
