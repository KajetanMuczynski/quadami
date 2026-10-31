# Preview before remeshing: design

Design note for a "see it before you run it" step in the Quadami panel (Kajetan's wish, 2026-10-28:
something like the Decimate modifier, which shows its result while the ratio is dragged). Thread
"Design preview before remeshing", 2026-10-31, master `57c19f9`. Nothing here is built; no code
before Kajetan agrees on the design. It overlaps with density paint (TODO 5.8,
[GUIDES-AND-DENSITY.md](GUIDES-AND-DENSITY.md) §2), the pre-run checks (G13: a `--plan-only` CLI
mode) and Finer / Coarser after a run (G17).

Labels: **[M]** measured here (§3, `tools/bench/preview_report.py` over `ab.py` runs, re-runnable),
**[C]** checked in code at `57c19f9`, **[I]** inferred, **[U]** unverified (memory or Blender API
details not tried).

## 0. Result

- **A cheaper run is not a preview of the run you keep.** A 1-ulp replicate of the final run (the
  noise floor: same settings, input moved by one float step) puts its poles where the final run
  has them with skill 0.97 (1 = same places, 0 = random; §3). Fast at the same Target Quads gets
  0.64 and a run at a fifth of the target 0.31; their size pattern explains 78 % and 59 % of the
  final run's variation, against 96 % for the replicate [M, corpus set; the sculpt set agrees].
  On the largest inputs the fifth-target run still costs 0.68-0.71 of the full run (cat 1.7M,
  nefertiti 2.0M triangles; 0.62 on the 10-island sword) [checked: 3 sequential runs each, tight],
  because input handling does not shrink with the target. So the low-count preview is rejected:
  it costs real time, shows a different mesh, and saves least where waiting hurts most.
- **The run already is the preview** [C]: the result is a new object, the source stays hidden, and
  Remesh on a result remeshes its source again and replaces the result (`addon/quadami/__init__.py`,
  `OBJECT_OT_remesh_quads.execute`). What the user lacks is (a) a sense of quad size *before* the
  first run and while dragging Target Quads, and (b) faster re-runs.
- **Recommendation: a density overlay** (§4.1). The core's own plan (`--plan-only`, which G13
  needs anyway) returns the planned quad edge length per input vertex and the planned count per
  island; the panel draws it as a colour overlay on the source with a legend in scene units and
  "about Q quads planned". Dragging Target Quads rescales it in Python (edge ∝ 1/√N), so the
  slider is instant like Decimate's; a new plan runs only when adaptivity, paint or the mesh
  change, or the target moves more than 2× from the planned one. Cost: everything before the
  isotropic remesher, **median 5-6 % of a Quality run at the same target** [M], but it is input
  handling, so it grows with the input, not the target: under 1 s up to 100k triangles, 2-5 s at
  0.24-0.57M, 13-16 s at 1.7-2.0M (55 % of nefertiti's run, 5.6 s of it the input fold check,
  which a plan can skip [I]). So on a heavy sculpt the first overlay takes seconds, and the
  overlay should come with the warm input cache (§4.2). It shows density only, never loops or
  poles, and says so.
- **Not recommended:** a flow preview (cross field and cones, 28 % of a run [M], misses the
  spiralling loops, which come from rounding later); an automatic re-run on every slider change
  (full runs, heat on a 4-core laptop). Fast stays a setting, but the user guide should stop
  calling it a preview (§4.3).
- Density paint needs this overlay anyway: painting without seeing the size it asks for is blind.
  Build order: G2a request → `--plan-only` + overlay (this note) → 5.8 paint, which then only adds
  a term to the planned size.

## 1. What the user decides with a preview

| Decision | Setting | What shows it | Today |
|---|---|---|---|
| How fine overall | Target Quads | quad size, total count | only after a run; G17 (planned) suggests Finer / Coarser from it |
| Where detail goes | Adaptivity, later density paint | size per region | only after a run |
| Will small or thin parts survive | Target Quads | lost-parts warning after the run | the core's far rule after the run; no input-only predictor works (research/target-guidance-2026-10-28.md: thickness correlates at about 0) |
| Edge flow, loops, poles | (none; guides later, 5.7) | the quads themselves | only after a run; spirals come from integer rounding (research/edge-loops-handoff-2026-10-28.md) |

Decimate can preview all of it because its result is cheap. For a field-guided remesher only the
first two rows can be shown without the run; the last two need the run itself [I, measured for
poles and loops in §3].

## 2. Options

| Option | Cost per update | Shows | Misses |
|---|---|---|---|
| A. Run at a lower target (and/or Fast) | 0.15-0.26 of a run (set medians), 0.68-0.71 on 1.7-2M-triangle inputs [M] | a mesh | its poles, loops and size pattern differ from the final run's (§3.1) |
| B. Full run as preview (today) | one run | everything | the wait: 2.5-20 s for 100k-1M-triangle sculpts at 3k-10k quads (research/speed-memory-2026-10-22.md) |
| C. Density overlay from the core's plan | median ~5-6 % of a run (<1 s to 16 s by input size) once, then free while dragging Target Quads [M for the prefix, I for the rescale] | planned quad size per region, planned count | loops, poles, lost parts, the realised count's retries |
| D. Flow overlay (cross field, cones) | ~28 % of a run (§3.2) [M] | field directions and cone positions | spirals (rounding), extraction failures, lost parts; cones also shift in singularity simplification [I] |
| E. Auto re-run on every change | one full run per slider release | everything, late | CPU and heat; cancelled runs waste up to a run each |
| F. Warm re-runs (cache the preprocessed input by hash) | saves the input preprocessing: 3-8 % median, 30-52 % on 1.6M+ inputs (speed-memory §1) | (makes B faster) | nothing; costs disk or resident memory |

## 3. Measurements

Runs: `ab.py run master` on the `corpus` set (18 meshes, targets 2,000 and 10,000, Fast and
Quality) and the `sculpt` set (7 tessellations of one creature, 2,000 / 5,000 / 15,000), plus
`--replicates 1` of the largest Quality target on both, `--jobs 4` on a 4-vCPU container, core
`395e0f46e4d2`. Analysis: `python tools/bench/preview_report.py $AR_WORK/runs/<key> --set corpus`
(and `--set sculpt`). Its metric code is separate from `metrics.py`. Aggregates over whole sets, so
no replicates are needed; the sculpt set is the second, independent set.

Metrics (per mesh, preview against the largest Quality run on the plain input):
- **Pole skill** = (recall − chance) / (1 − chance): recall is the share of the final run's poles
  (interior vertices of valence ≠ 4) that have a preview pole within 2h (or 4h), h the final run's
  mean edge, the same distance for every preview; chance is the same with as many random preview
  vertices. 1 = same places, 0 = no better than random.
- **Size r²**: share of the variance of log local edge length (per final-run vertex) that the
  preview's value at the nearest point explains, after scaling by √(preview quads / final quads).
- **Loops**: share of edge loops that close (Blender's Alt+click rule) and the longest loop's share
  of the vertices.

### 3.1 How well a cheaper run predicts the final one [M]

Medians over the meshes; time is the CLI time ratio preview / final.

| Set | Preview | Time | Pole skill 2h | Pole skill 4h | Poles (preview / final) | Size r² | Closed-loop share, abs. diff |
|---|---|---:|---:|---:|---:|---:|---:|
| corpus (final Quality 10k) | 1-ulp replicate (floor) | 0.93 | 0.97 | 1.00 | 1.00 | 0.96 | 0.02 |
| | Fast 10k | 0.51 | 0.64 | 0.78 | 0.81 | 0.78 | 0.07 |
| | Quality 2k | 0.26 | 0.31 | 0.62 | 0.65 | 0.59 | 0.11 |
| | Fast 2k | 0.16 | 0.24 | 0.41 | 0.59 | 0.25 | 0.08 |
| sculpt (final Quality 15k) | 1-ulp replicate (floor) | 0.94 | 0.92 | 0.95 | 1.00 | 0.98 | 0.03 |
| | Fast 15k | 0.48 | 0.72 | 0.82 | 0.93 | 0.94 | 0.05 |
| | Quality 5k | 0.35 | 0.70 | 0.86 | 0.91 | 0.91 | 0.07 |
| | Quality 2k | 0.20 | 0.41 | 0.64 | 0.81 | 0.79 | 0.20 |
| | Fast 2k | 0.15 | 0.32 | 0.44 | 0.71 | 0.67 | 0.19 |

- The final run is stable: a 1-ulp change keeps poles and sizes almost exactly. So the gap in the
  other rows is the preview's error, not noise.
- Fast at the same target moves about a third of the poles and drops 7-19 % of them; it is its own
  result, not a quicker copy of Quality.
- A run at a fifth of the target puts its poles worse than Fast at every distance (0.31 against
  0.64 at 2h, 0.62 against 0.78 at 4h, corpus), and its size pattern is the worst of the Quality
  rows; on single meshes r² falls below 0 (corpus minimum −0.70) [M]. A third of the target
  (sculpt 5k) does better (skill 0.70 / 0.86, r² 0.91) but costs 0.35 of a run.
- Closed-loop share: the final runs close 30 % (corpus median, range 0-64 %) and 52 % (sculpt) of
  their loops; previews miss that share by 5-20 points against a floor of 2-3.
- Time of Quality 2k against Quality 10k grows with input size: 0.14-0.30 up to 240k triangles in
  the set run. Single runs under `--jobs 4` are noisy (a replicate's time differs by 0.73-1.11×),
  so the large cases were re-timed sequentially, three runs each [checked]: cat (1.7M) 14.0 s
  against 20.7 s = 0.68, nefertiti (2.0M) 17.4 s against 24.4 s = 0.71, sword (45k triangles, 10
  islands) 4.4 s against 7.2 s = 0.62. Input size is not the only cause: many islands also keep
  the floor high.

Fast against Quality time, single runs without parallel load (koala, hand, demosthenes at 10k):
Quality takes 1.7×, 1.7× and 2.1× as long [M, one run each]; the set medians above (0.51, 0.48)
agree. The user guide and the Resolution tooltip say 1.5× (the 2026-10-15 figure, TODO 2.15);
§4.3 corrects it.

### 3.2 What a plan-only or flow preview would cost [M]

Share of a single-island run's time spent before each preview could stop (from `stages.jsonl` and
the report): plan-only = the whole run minus the isotropic remesher's own steps, the parameterize
and extract phases, retries, merge, the final surface projection and relax and the lost-parts
pass, i.e. input cleanup, fold check, split, contexts and decimation; flow = that plus the
isotropic remesh and everything up to and including "Simplifying singularities".

| Set | Run | n | Run time (median) | Plan-only share | Flow share |
|---|---|---:|---:|---:|---:|
| corpus | Quality 10k | 16 | 9.1 s | 0.05 (max 0.55) | 0.28 (max 0.67) |
| corpus | Quality 2k | 16 | 2.0 s | 0.17 (max 0.72) | 0.40 (max 0.75) |
| sculpt | Quality 15k | 5 | 16.3 s | 0.06 (max 0.27) | 0.28 (max 0.50) |
| sculpt | Quality 5k | 5 | 5.3 s | 0.24 (max 0.50) | 0.49 (max 0.61) |

The plan-only part is input handling, so in seconds it depends on the input, not the target
(Quality 10k / 15k): koala (99k triangles) 0.8 s, demosthenes (240k) 2.3 s, sculpt_voxel (449k)
4.2 s, sculpt_dyntopo (572k) 5.3 s, cat (1.7M) 12.6 s, nefertiti (2.0M) 15.9 s, of which the input
fold check is 5.6 s. The size the quads follow ("Computing scaling field") runs on T, after the
isotropic remesh, so a plan-only mode needs its own evaluation on the decimated mesh (§4.1). A
`--plan-only` mode would also skip writing and reading the output and add the transfer of sizes
to input vertices (not measured).

**Independent check** (a separate worker whose only task was to disprove these numbers, own code):
pole counts, loops' inputs, size r² (also with 8 neighbours: 0.97 / 0.98 floor, 0.84 / 0.95 Fast)
and the floor held. It found two errors in the first version of `preview_report.py`, both fixed
above: the pole tolerance scaled with the coarser mesh (which made the fifth-target run look as
good as Fast: 0.64 instead of 0.31), and the plan-only prefix left out input cleanup and the fold
check (max 32 % instead of 55 %).

### 4.1 Density overlay (recommended) [I, design]

**Core.** `--plan-only` (G13's mode) stops after the size field and writes, beside the report:
- per island: area, planned quads (with the small-island floor applied), whether the floor set it;
- per input vertex: the planned quad edge length h in scene units, as a raw `f4` array (the G2a
  request format; no OBJ round trip).

The size must be the one the quads follow, i.e. stage 5's per-face scale m_f (REFERENCE §3 row 5,
`parameterizer.cpp`), not stage 3's target edge h, which only sets T's resolution (M7: two
estimators). Today m_f is computed on T. The cheap route evaluates the same law on the decimated
mesh; whether that is close enough is gate P1 below. If not, the plan also builds T (the flow
prefix, ~28 %), which is still far below a run.

**Add-on.**
- Panel: a *Show Density* toggle under Target Quads. On: one plan in the worker (same subprocess,
  Esc cancels), then a viewport overlay on the source: colour by h relative to the mesh's median,
  legend "quad edge 1.2-4.8 mm", and the line "About 9,800 quads planned (10,000 asked; 3 small
  parts at their minimum)".
- While Target Quads is dragged: h·√(N₀/N) on every island not held at its floor, count
  Σ max(N·Aᵢ/A, floorᵢ); instant, no worker [I]. Re-plan on release when the target left
  [N₀/2, 2N₀] (decimation keeps about 8 triangles per planned quad, `decimateIfTooDense` in
  `autoremesher.cpp`, so the curvature estimate changes with N), or when adaptivity, paint or the evaluated mesh change; debounce 0.5 s, cancel a running
  plan when a newer one starts.
- Drawing: `gpu` draw handler (`SpaceView3D.draw_handler_add`) with a batch built from the
  decimated mesh the plan used (about 8 triangles per planned quad, so 80k for 10k quads, not the
  sculpt's millions) [U: cost not measured; offset or X-ray needed against z-fighting with the
  source]. No attribute is written to the user's mesh (no undo step, no file growth).
- Wording: "planned size". After a run, the report's realised count stays the authority (G17).

**Gates before it ships** (measured with `preview_report.py` extended by a plan column):
- **P1** planned h against the realised local edge length of the Quality run at the same target:
  size r² ≥ 0.8 on both sets (Fast's level on corpus is 0.78, the floor 0.96). Below that, build T
  in the plan; if still below, do not ship the overlay.
- **P2** planned count within ±10 % of the realised count in 90 % of the runs of all public sets
  (retries ×1.25 and extraction losses are not in the plan; G13's 584-run data says 45 runs came
  out above 1.1× the target).
- **P3** plan time ≤ 15 % of the run at the same target, median over the corpus, including the
  hand-over, and a re-plan on a cached input (§4.2) under 1 s for inputs up to 1M triangles.

### 4.2 Faster re-runs (complements the overlay)
- G2a raw arrays: the OBJ hand-over is 1.1 s / 5 s / 17 s at 0.33M / 1.3M / 5.2M triangles
  (GUIDES-AND-DENSITY §4.1) on top of the core.
- Warm input (option F): cache the cleaned, split and fold-checked input on disk keyed by the
  input's hash and the settings that affect it; a re-run of the same sculpt skips 30-52 % of its
  time on 1.6M+ inputs (speed-memory §1; 44-55 % on cat and nefertiti here), and a re-plan of the
  overlay becomes cheap. Disk, not a resident worker (a held 1M-triangle input is about 0.5 GB,
  speed-memory §1). Needs its own A/B (byte-identical output is the bar).
- G17 Finer / Coarser after a run: the realised result's detail, with numbers.

### 4.3 Small wording fix (in this PR)
User guide, Advanced > Resolution: Quality takes about twice as long as Fast (§3.1), and Fast is
not a preview of the Quality result (a third of the poles move). New text: "Fast to try settings
quickly; a Quality run at the same Target Quads has about the same quad size, but its poles and
edge loops differ." The Resolution tooltip in the add-on says "about twice as long".

## 5. Design critique (what may have been missed)

- **The Decimate analogy breaks at the loops.** Decimate's preview *is* its result. Ours can only
  be the result if it is a full run; anything cheaper shows another mesh, and users judge edge
  flow from whatever mesh they see. A preview that looks like quads invites exactly the wrong
  judgement, which is why the overlay draws colour, not quads.
- **The weak spot is not visible in any preview.** Spiralling loops (Kajetan's 2026-10-28 test)
  come from rounding; only guides (5.7) or a zero-shift constraint fix them. The overlay helps
  choose Target Quads and paint, not loops.
- **Lost parts.** It is tempting to colour "thinner than a quad" red. The keep-parts research found
  thickness useless as a predictor, so a risk map would give false calm on some parts and false
  alarms on others; it stays out until a gate shows it works.
- **Paint shows the request, not the result.** A density step forces poles that today's field does
  not place, so painted regions may come out short of the asked ratio (GUIDES-AND-DENSITY §2.3).
  The overlay must say "planned", and the 5.8 report must show realised against requested.
- **Heat.** A plan on every slider tick would keep his laptop's cores busy; hence the Python
  rescale while dragging and plans only on release.
- **Is the overlay good enough?** For choosing Target Quads it competes with G17's Finer / Coarser,
  which needs one run but gives real numbers. If gate P1 fails, G17 plus warm re-runs is the
  better spend. The overlay's unique value is before the first run on a heavy sculpt and for paint.
- **Commercial tools.** As far as I know neither ZRemesher nor Quad Remesher previews before the
  run; both rely on fast runs and undo [U]. Instant Meshes shows its field interactively, but its
  whole run is near-interactive, unlike ours.

## What would falsify this

- Gate P1 failing on both routes (decimated mesh and T): then no cheap density preview exists and
  §4.1 is dropped.
- Users who, given the overlay, still remesh 2-3 times to pick Target Quads (then G17 is the tool).
- A low-count run whose pole skill and size r² reach the replicate floor on another set (for
  example the private sculpts) would revive option A for those meshes.
- Timings on Kajetan's laptop where the plan-only prefix is far above 6 % (the cloud and his laptop
  agreed within 1 % on one run in speed-memory §1).

## What I did not re-check

- The plan-only route itself: no code exists, so its accuracy (P1, P2) and its real cost with the
  size transfer are not measured; the prefix shares in §3.2 are an estimate from the full runs.
- The loop columns were not part of the independent check.
- Blender drawing cost and z-fighting of a `gpu` overlay [U].
- Private sculpts (head, sword) and the external sets; the second route was the generated sculpt
  set only.
- Times in the set tables ran with `--jobs 4` (shared cores): set medians are comparable, single
  runs are not (only cat, nefertiti, sword and three Fast/Quality pairs were re-timed alone).
- One replicate (u1) as the floor; enough for set aggregates, not for any single mesh.
