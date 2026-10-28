# Bench and baseline scripts

Build, corpus and measurement scripts behind the 1.2 baseline (plan: `research/baseline-and-profiling.md`
and REFERENCE §4.1). Tested on Ubuntu 24.04 x86_64, GCC 13.3, CMake 3.28, Qt 5.15.13, oneTBB 2021.11,
Python 3.12. Meshes, outputs and builds never go into the repo; everything is written to a work
directory you choose.

| File | What it does |
|---|---|
| `ab.py` | **A/B harness** for quality: builds `autoremesher-cli` from a git ref, runs it over a mesh set (optionally with 1-ulp replicates), adds metrics and topology, and compares two runs as Markdown tables. `lane` says how much A/B a change needs. See [A/B a change](#ab-a-change) and [Test lanes](#test-lanes). |
| `build.sh <work>` | apt deps (skip with `SKIP_APT=1`; `JOBS=2` to keep a laptop cool), out-of-tree qmake build of the Qt app, the Qt-free `ar_bench` build of the same core, and a Python venv. Builds this checkout unless `AR_SOURCE_DIR` points elsewhere. |
| `bench/ar_bench.cpp` | Qt-free driver for time and memory: same OBJ loader and `AutoRemesher::remesh()` call as the app's headless mode; writes JSON with wall/CPU time, peak RSS (overall and per pipeline step), the core's phase report, result counts and UV flip checks on the integer-grid map; `--stage-diagnostics` writes the core's per-stage records (solver sizes and times, rounding passes). |
| `bench/CMakeLists.txt` | Builds `ar_bench` from a source tree (`-DAR_SOURCE_DIR`) with the `.pro` release flags (`-O3 -flto -funroll-loops -march=x86-64-v2`). |
| `fetch_meshes.sh <meshes>` + `meshes.tsv` | Downloads the 12 external meshes (pinned commit of github.com/odedstein/meshes, ~170 MB) and verifies SHA-256. Per-mesh license and attribution are in `meshes.tsv`. |
| `fetch_external.py` + `external.json` | Downloads the `external` set (20 public meshes: full body, creatures, high-genus props, cloth, leaf cards, multi-part props, scans; CC0/CC BY/CC BY-SA, pinned by commit and SHA-256), converts OBJ, PLY and glTF to triangle OBJ and writes a topology check. Licenses and credits: docs/EXTERNAL-MESHES.md. Test-only meshes use the same tool with a manifest outside the repo (`--manifest`, `AR_CORPUS`). |
| `fetch_external.py` + `external_thin.json` | Downloads the `external_thin` set (6 public meshes with parts thinner than a quad: bow string, wings, insect legs, single-sided cloth, flowers, relief; TODO 4.1). No default targets: `lane` skips it, `run` needs `--targets`. Draco sources need `pip install DracoPy`. |
| `blender_to_obj.py` | Writes the meshes of a `.blend` or `.fbx` file as one triangle OBJ the way the add-on hands them over (evaluated, viewport modifiers, world transform, visible objects joined; `--objects` picks some). Used for the hand-downloaded meshes of docs/EXTERNAL-MESHES.md. Needs `bpy` (Python 3.13). |
| `make_synthetic.py` | Generates the procedural meshes (CSG part, symmetric blob, cloth sheet, thin leaf, noisy spot, 8 islands, the closed set, subdivision ladder, the `cad` parts). |
| `make_sculpt.py` | Generates the `sculpt` set: one creature with sub-quad detail in seven tessellations a sculpting tool leaves behind (voxel remesh, voxel terraces, the same with 21 % zero-area triangles, dyntopo, quadric-decimated, joined intersecting parts and crumbs, export damage). Needs scikit-image. |
| `make_sculpt.py --set brush` | Generates the `brush` set: twelve brush situations on a sphere (clay, crease/pinch, inflate, pits and sunk eyes, Snake Hook spikes, drapery pleats, noise/pores, mask extrusion, twist, grab, dyntopo strokes, a sculpted box), each from clean to over-strong, with the folded input triangles of each in `brush-manifest.json`. What each stands for: the docstring. |
| `make_stress.py` | Generates the stress families (`stress_<family>` sets in its own corpus file for `AR_CORPUS`): thin capsules as islands and attached, thin ribbons, up to 1,000 crumbs, intersecting/touching/nested/duplicated shells, slivers and graded density, fins, holes, triangle soups, flipped normals, scale and offset, 1.3M/5.2M triangles, spikes, jitter, brush-like twists, coarse input, self-intersection. Writes a manifest with probe points on each feature under test. Docstring lists the families. |
| `stress_report.py` | Scores `ab.py` runs of any set: whether a feature survived (probes), how far the input lies from the output in h0 (coverage: area shares and the largest distance), islands with under half their planned quads, the share of libQEx faces the cleanup keeps, local input thickness, and whether the add-on panel (`addon/quadami/report.py` on the same report) shows any red row or warning. Results: ENVELOPE §3.3. |
| `sculpt_report.py` | What a sculptor gets, over `ab.py` or `run_compare.py` runs: triangles/pentagons and their clusters, where non-quads and folds sit (smooth, curved, sharp/thin at quad scale), one Catmull-Clark step (folds survive it, isolated n-gons become poles), non-manifold vertices, folded input (`input_folds`) and how many non-quads and folds sit on it; summary per mesh and tool, replicate table. |
| `genus_report.py` | Wrong genus per island over `ab.py` runs (2g = 2C − χ − B from `metrics.json` → `validity` → `per_island`), joined with what the core recorded for that island (T's Euler characteristic, the `map_check` stage, the extractor used and any fallback) and the same island in a run with `--cli-arg=--extractor=trace` (`--trace`) or any other run (`--alt NAME=RUN`). Classes each changed island: non-manifold input, T wrong, map0/map2 (zero or 2-quad ring in the rounded map, tracer loses it too), extractor (tracer keeps it), both. Also checks the core's own `islands.handles_lost/added` (report interface 1.7) against `metrics.py`. `genus_report.py RUN --set S [--trace RUN_T] [--per-run] [--json f]`. |
| `map_check_eval.py` | Scores the core's `map_check` stage records (validity of the map before extraction) of an `ab.py` run against the output defects: precision, recall and lift per failure class, ROC AUC, and whether a feature separates failing from passing 1-ulp replicates of the same run. |
| `defect_ledger.py` | Per-run cause ledger over `ab.py` run folders: locates every non-quad, folded quad, face flipped against the input, hole, dropped or thinned part and wrong genus, tests the input and the map around it (folded input within 2h, tip, a part thinner than a quad, libQEx fallback, flipped map within h) and gives it one cause class; `summary` prints the share per class and kind, the lift of each feature against random surface points, and the worst runs; `--pair` against the same runs without `--fold-repair` adds the class "repair". The map test needs run folders of a probe build (patch in the research note `defect-ledger-2026-10-22`). |
| `speed_report.py` | Where time and memory of `ab.py` runs go (`--no-metrics --jobs 1` for timing): the core's step shares per target, island phases (input preprocessing, resample, parameterize, extract, surface projection), the rounding loop and curl solve (iterations, n, nnz(L), seconds), quad count against the parameterization's cell count, a peak-RSS model (KB per input triangle and per target quad, with the worst actual/model ratio as upper bound) and a wall-time model for a pre-run estimate; with several `--runs` folders, geo-mean time ratios and byte-identical outputs. Results: `research/speed-memory-2026-10-22.md`. |
| `progress_probe.py` | Runs the CLI once as the add-on does (`--stdin-control`), timestamps its progress lines (how far the bar is from the elapsed share of time), optionally sends `cancel` after S seconds and measures how long the process takes to stop, and with `--cpus` runs it under `taskset` for thread-scaling runs. |
| `make_suzanne.py` | Generates the Suzanne set with Blender's Python module (`pip install bpy`, Python 3.13) or inside Blender. |
| `make_primitives.py` | Generates the `primitives` set the same way: Blender's Add > Mesh primitives with default settings (cube, UV sphere, icospheres, cone, cylinder) plus variants that switch one suspect off (64 segments, triangle-fan caps, no caps, cone frustum). Kajetan's report of 2026-10-27 (default cone and cylinder destroyed at 5k). |
| `primitive_report.py RUN` | What happens at sharp features of `ab.py` runs, which whole-mesh metrics average away: rim rounding (distance from the input's sharp edges to the output, in h), thin quads crowding a rim, how deep a tip (cone apex, box corner) is cut off, and the angle deficit of the output's irregular vertices near each tip against the tip's own. Any set; made for `primitives`. |
| `make_usage.py` | Generates the `usage` set the same way: inputs users hand the add-on that no other set had (docs/USAGE-COVERAGE.md, 2026-10-27). Extruded text and a capped Bezier tube as Convert > Mesh leaves them (pieces not joined) and after Merge by Distance; Suzanne + Subdivision 2 after a glTF round trip (smooth: split at UV seams; flat: split at every face); Blender's default cube, UV sphere, cylinder and Suzanne for Smooth Normal and very low counts. With `--external-raw <fetch_external.py out>/raw` also the external set's glTF files as Blender's importer gives them (`gltfimport_<id>.obj`; their welded pairs are the `external/ext_<id>.obj` files, so fetch those four first). |
| `fold_repair.py` | Offline prototype of a local repair of folded input (TODO 2.19 candidate, not in the core): per fold cluster from `input_folds`, untangles the folded faces and a few rings in a planar chart (Tutte, then pulled back as close to the original as the faces stay positive), puts moved vertices back on the original surface, keeps every face. `fold_repair.py in.obj out.obj --json report.json`; the report has folds before and after, skipped clusters and why, and the largest move. |
| `corpus.json` | Mesh sets with their default targets and resolutions: `corpus` (the 18-mesh baseline corpus, 72 runs), `closed` (13 thin ellipsoids and tori, 78 runs; genus regressions show here first), `suzanne` (Suzanne + Subdivision 1–3, 9 runs; open, the add-on's first bug report), `sculpt` (7 sculpt stand-ins from `make_sculpt.py` at 2k/5k/15k, 42 runs), `brush` (12 brush stand-ins from `make_sculpt.py --set brush` at 2k/5k quality, 24 runs; about 1 h with `--replicates 5 --jobs 4` on 4 cores), `cad` (6 procedural CAD parts from `make_synthetic.py`: box and rotated box, chamfered box, cylinder, 30° wedge, rotated CSG bracket; at 1k/4k fast and quality, 24 runs; crease preservation, TODO 2.5), `primitives` (15 Blender primitives from `make_primitives.py` at 1k/5k quality, 30 runs), `usage` (15 everyday inputs from `make_usage.py` plus the four welded external meshes they pair with, 19 meshes at 1k/5k quality, 38 runs; run `fetch_external.py --manifest external.json --only ext_barramundi,ext_corset,ext_leeperrysmith,ext_scifihelmet` first), `gallery` (the public gallery's entries, each with its own target and camera; run through `tools/gallery/gallery.py`, public-license or procedural meshes only, checked by `tests/test_gallery_set.py`). Categories and symmetry planes per mesh. |
| `run_corpus.py` | Runs `ar_bench` (+ metrics) per mesh and target over the `corpus` set; writes `record.json` per run and `summary.json`. For time and memory; quality A/B goes through `ab.py`. |
| `gates.py RUN --set S` | docs/TARGETS.md §3 gates (H1–H8, V1–V6, coverage, libQEx fallbacks) over an `ab.py` run folder, per family: runs passing as the mean over replicates and in every replicate, defects per 1k faces, fidelity medians. |
| `release_gate.py CAND --baseline BASE` | The release check's decision (docs/RELEASING.md), on `gates.py`'s gates: structural gates (H1–H6, H8) no worse than the baseline for 0.x or on every run with `--hard` (1.0), and the §3.2 ratchet (V1–V6 clean runs, defects per 1k). Run by `.github/workflows/release-check.yml` on Linux, Windows and macOS. |
| `metrics.py` | Output quality: face/valence statistics, corner angles, scaled Jacobian, degenerate and flipped faces (against the input oriented like the core does; those over folded input counted apart for V3), non-manifold edges and vertices, topology against the input (Euler characteristic, components, boundary loops; globally and per input island: dropped, merged, holes, handles), input area lost or collapsed (thin parts), two-sided Hausdorff, geometric and topological mirror symmetry. |
| `summarize.py` | Markdown tables + CSV from `summary.json`. |
| `claims.py` | Aggregates per REFERENCE claim (stage shares, M1/M2 counts, P3 correlations and scaling slopes, thread/ladder/allocator/adaptivity comparisons). |
| `experiments.sh` | Thread sweep (P1), input-size ladder (P4/P6), allocator swap (P5), adaptivity off (M7), perf profiles. |
| `relax_probe.py` | Offline probe: smooths the outputs of an `ab.py` run folder in Python (tangential Laplace or parallelogram rule, optional crease snap and bend lock) and measures both sides, to see whether a post-extraction smoothing has headroom before building it (TODO 2.8). |
| `tests/` | Python tests of the bench tools (`python -m unittest discover -s tools/bench/tests`; run in CI by `qt-app`). |
| `perf_buckets.py` | Groups a `perf report` listing into allocator / containers / solver / remesher / extractor buckets. |

The counters-only `probes.patch` and the CHOLMOD experiment patch were retired on 2026-10-20: they no
longer applied to master, and `--stage-diagnostics` records what they measured except the extractor's
cleanup rounds (TODO 2.4). Both are in git history.

## Run everything

From the repo root:

```bash
W=~/ar-baseline                       # any folder outside the repo with ~2 GB free
tools/bench/build.sh $W
tools/bench/fetch_meshes.sh $W/meshes
$W/venv/bin/python tools/bench/make_synthetic.py --downloads $W/meshes/downloads --out $W/meshes/synthetic --ladder
$W/venv/bin/python tools/bench/run_corpus.py --meshes $W/meshes --bench $W/build/ar_bench \
    --out $W/runs/corpus --targets 2000,10000,50000
$W/venv/bin/python tools/bench/summarize.py $W/runs/corpus/summary.json --csv $W/runs/corpus/summary.csv
PYTHON=$W/venv/bin/python PERF=perf tools/bench/experiments.sh $W/meshes $W/build $W/runs
$W/venv/bin/python tools/bench/claims.py --runs $W/runs          # numbers behind each REFERENCE claim
```

## A/B a change

Every quality claim in a PR or research note comes from `ab.py`, so anyone can re-run it from the repo.
Commit your change first (a WIP commit is fine), then from the repo root:

```bash
export AR_WORK=~/ar-work                     # meshes in $AR_WORK/meshes (fetch_meshes.sh, make_synthetic.py)
P=$AR_WORK/venv/bin/python                   # numpy, scipy, libigl (build.sh makes this venv)
$P tools/bench/ab.py run master              # base: 72 runs of the corpus set
$P tools/bench/ab.py run HEAD                # your change
$P tools/bench/ab.py compare master HEAD     # Markdown tables, paste into the PR
$P tools/bench/ab.py run HEAD --set closed   # genus check; also --set suzanne
```

- **What it builds.** `ab.py` exports the ref with `git archive` and builds `autoremesher-cli` with CMake
  (Release, `nice`, 2 jobs). Builds and runs are keyed by the git trees of `src/`, `include/`,
  `thirdparty/` and `CMakeLists.txt`, not by the commit, so a rebase or a docs commit reuses them, and
  a finished run is never repeated (delete its folder under `$AR_WORK/runs` to force it).
- **Options.** `--only a,b`, `--skip`, `--targets`, `--resolutions` override the set's defaults;
  `--timeout S` kills a run after S seconds (recorded as `rc: "timeout"`); every record has the run's
  own peak RSS (`peak_rss_mb`). `--metrics-timeout S` (default 1800, `0` = no limit) kills metrics.py
  on one run after S seconds: the record keeps `metrics: null` with `metrics_error` and
  `metrics_timeout`, `compare` leaves the run out and `release_gate.py` stops (exit 2), and only a
  later `run` with a longer limit measures it again. Each record has `metrics_seconds`. `--cli-arg=--adaptivity=0` (repeatable) passes CLI options, and those runs get their own folder, so
  compare folders, not refs: `compare $AR_WORK/runs/<key> $AR_WORK/runs/<key>+<hash>`.
  `--cmake-arg=-DCMAKE_CXX_FLAGS=-fsanitize=address` (repeatable) configures the build with a CMake option, for
  experiments behind a build switch; such builds and runs get their own key. Each run's log keeps its
  first 8 MB, so a hung run that keeps printing can't fill the disk.
- **Speed.** `--jobs N` (default 2) runs N meshes at once. The CLI uses one core per island (TBB
  splits work by island only), so most corpus runs keep one core busy and `--jobs 2` leaves half of
  a 4-core machine idle. In a cloud container use `--jobs $(nproc)`. Measured 2026-10-21 on a 4-vCPU
  container: corpus set with `--replicates 5` 35 min → 20 min, closed set 59 s → 41 s per replicate
  (`--jobs 2` before this change → `--jobs 4` with the input cache below). Outputs don't depend on
  `--jobs` or the thread count (72 of 72 `out.obj` byte-identical), only the `time` row does
  (+20 % CLI time at 4 jobs), so `compare` flags a time row whose sides ran with different `--jobs`; keep `--jobs` equal on both
  sides with `--timeout` too, since a run near the limit can time out under heavier load.
  On Kajetan's laptop keep the default, or `--jobs 1` when it runs hot. metrics.py caches the
  input's share of its work (reading the OBJ, input topology; most of its time on large meshes) in
  `$AR_WORK/inputs/metrics`, keyed by the input's content and metrics.py's own hash, so the cached
  numbers are bit-identical (`tools/bench/tests`) and an edit of metrics.py starts a fresh cache.
  Builds take about a minute and happen once per key, so they are not worth caching further.
- **What it records.** Per run in `<set>/<mesh>/<resolution>-t<target>/u<k>/`: the output OBJ, the CLI's
  `report.json` and `stages.jsonl`, `metrics.json` and a `record.json` summary. `compare` reports the
  report's UV flips (continuous, rounded, final), degenerate UV triangles, non-quads, SJ ≤ 0 quads,
  faces flipped against the input, irregular vertices, in→out p99 distance (the fidelity target of docs/TARGETS.md), Hausdorff and mean distance, normal deviation (D3), median vertex-to-input distance, SJ 1st percentile (G1), skew 95th percentile (G2), crease coverage (A1, higher is better like G1), `crease_cov_theta` (A1θ: the same on creases at or above the run's `--sharp-edge` angle that are not in a strip narrower than h/2, higher is better) and `crease_bent_1k` (quads folded more than 30° across a crease within h/2, per 1k quads; research/crease-preservation-2026-10-26.md), quad count error, rounded/continuous energy and
  time, the runs that fail, and topology (Euler characteristic, components and boundary loops equal to
  the input, no non-manifold edge). `t_non_manifold` and `t_opened` count T's own defects from the
  core's run record: non-manifold edges after isotropic remeshing, and closed input islands that T
  left open or non-manifold (blank for cores before 2026-10-20). `qex_fallbacks` counts islands where
  libQEx gave up and our own tracing extracted the faces (blank for cores before TODO 6.1b; compare
  `--cli-arg=--extractor=trace` against the default to see what libQEx itself does). Validity rows
  (2026-10-22, `metrics.json` → `validity`): `folded_faces` (any face size, the CLI's definition),
  `nm_vertices`, and per input island, matched to output islands by nearest input triangle:
  `holes_added` / `holes_closed`, `handles_lost` / `handles_added`, `islands_dropped` (no output or under
  half its area within one output edge), `islands_merged`, `islands_changed` (Euler characteristic or
  loops differ, or split), `area_uncovered` and `area_wrong_side` (input area within one output edge
  but under a face facing the other way: thin parts collapsed into one sheet, pleats), and a per-island
  topology line under the global one. `flipped_vs_input` measures against the input oriented as the
  core orients it (`flipped_vs_raw` is the old count against the file's own normals); runs measured
  before 2026-10-22 need `--remeasure`, which `compare` points out. When B has libQEx
  records, a **libQEx fallbacks** section lists the replicate-runs with a fallback, the reasons, and folds,
  non-quads and topology on exactly those runs for both sides. `--per-run` adds one row per run.
- **Re-measuring.** After a change to `metrics.py`, `run <ref> --remeasure` recomputes `metrics.json` of the
  finished runs without running the CLI again. Each record names the code that measured it
  (`metrics_tool`: a hash of `metrics.py` and the numpy, scipy and libigl versions), and `compare`
  says when A and B were measured by different code. With `--jobs 4` on 2M-triangle inputs (nefertiti,
  cat) metrics.py can run out of memory: it is tried once more, then the run keeps `"metrics": null`
  (never the old file) with the end of the error in `metrics_error`; the next `run` measures it again,
  `compare` leaves that replicate-run out of the metric rows on both sides, `gates.py` lists it as
  "left out" and `release_gate.py` refuses to decide (exit 2). `--jobs 1` avoids it.
- **Timing.** The `time` row is the CLI's own total under `--jobs` load. Use it for effects of 20 % or
  more; for anything finer run `--jobs 1` on an idle machine, or `ar_bench` with `--repeat`.

### Noise rule

One ulp of input can change a run's genus or move its flip count by a lot (csg_bracket's topology flips
between 1-ulp copies on master). So:

- **Discrete claims** ("fixes mesh X", "breaks the genus of Y", "holes 3 → 0") need replicates:
  `ab.py run <ref> --only <meshes> --replicates 5` for both sides. Replicate u0 is the plain input; uk
  rounds every coordinate to float32 and moves it one float32 ulp up or down (seed k), which is the
  precision Blender hands the add-on (docs/TARGETS.md §2.5). `compare` calls a run fixed or broken only when all replicates
  agree on both sides and lists the rest as noise; with one replicate it labels its lists candidates.
- **Aggregates** over a whole set (sums, geo-means, "better in 50 of 72 runs") don't need replicates.
  Do add them when the effect is small next to the spread: `compare` prints, per metric, how many
  runs are fully separated and the median coefficient of variation over A's replicates.

The Qt CLI (what the Blender bridge calls) runs headless only with an offscreen Qt platform:
`QT_QPA_PLATFORM=offscreen $W/app/autoremesher --input in.obj --output out.obj --report r.txt
--target-quads 10000`. Without it, on a machine with no display, it aborts (exit 134). `--report`
holds only totals; the per-step phase report goes to stderr.

## Test lanes

How much A/B a change needs is decided by what it does to the output, not by its size or by the
author: a one-line change in rounding can move every run (#67 moved 50 of 225), a 3,000-line opt-in
feature can move none (#71). `ab.py lane` gives the lane:

```bash
$P tools/bench/ab.py lane $(git merge-base origin/master HEAD) HEAD --jobs 4
```

| lane | when (`lane` prints it) | what the PR needs |
|---|---|---|
| **none** | nothing compiled into the CLI changed (`src/`, `include/`, `thirdparty/`, `CMakeLists.txt`): docs, add-on, tools, CI. 28 of the 45 PRs #26–#73. | CI only. |
| **light** | compiled code changed, but `out.obj` is byte-identical to the base in every run of every public set with default targets (265 runs since `external` was added, each with the plain input and one float32 replicate; `gallery` and `external_thin` have no default targets and are skipped). Opt-in code behind a flag, report-only fields, refactors that keep the bytes. #60, #65, #71 would have been light. | The `lane` output in the PR, plus an A/B of the new option (`--cli-arg=...`) on the meshes it is for, with the noise rule for any discrete claim. |
| **full** | anything else: some run moved, failed or is missing. | `run --replicates 5` on every public set for both sides and `compare` per set, as before; private sets (`AR_CORPUS`) when the change is aimed at sculpts. |

- **Why a float32 replicate.** The plain input (u0) is double precision; Blender hands the add-on
  float32. Replaying #67 at ×6 (2026-10-22): 28 of the 175 runs whose plain output was identical moved in
  some float32 replicate. A check on u0 alone would pass changes the add-on sees. `--replicates N`
  checks more.
- **No replicate shortcut in the full lane.** "Replicates only where the plain run moved" was tested on
  #67 against full ×6: it reached the same unanimous verdicts (none either way) but missed most of the
  effect where the change acts through float32 noise (sculpt flips ×0.79 full vs ×0.90, p99 ×0.76 vs
  ×0.92). Checking u0 and u1 still missed 20 runs. So the full lane keeps ×6 on every run.
- **Base.** Compare against the merge base, or against master right after merging it, never against a
  newer master than the branch has: other merges would show up as differences.
- **Gap.** Light proves identical default output on the public sets only. A change in input handling
  (cleanup, orientation, fold detection) also runs `lane` with `AR_CORPUS` on the private set, or goes full.
  Non-default options (`--sharp-edge`, `--adaptivity`) are not covered either; a change that touches
  their code adds a `lane`-style check with that `--cli-arg` via `run`/`compare`.
- **Cost** (measured 2026-10-22, 4-vCPU cloud container, `--jobs 4`, master 0e9bec8): full lane
  56 min per side (closed 6, corpus 25, brush 9, sculpt 14, suzanne 2 min), plus 15 min for `external`
  (2026-10-23, master dd03272), so about 71 min per side and 2 h 20 min for an A/B, more with private
  sets; `lane` on a light change about 25 min for both sides from scratch without `external`, no metrics
  (plain runs 787 s, the float32 replicate 705 s more; #60, #65 and #71 all came out light, 450 of 450
  identical), plus 9 min with it (262 s per side); none: 0.
  Reruns of a key are free (runs are cached), and a `lane` run's folders are reused by a later full
  `run`, which adds the metrics.
- **Drift guard.** Light PRs cannot drift the default output (they are identical by construction), but
  full PRs each within noise can add up: the 2026-10-21 net-effect run found slips no single PR showed.
  Master's ×6 side is deterministic, so it needs computing only once per core merge (a change of
  `source_key`), not once per PR. After a core merge, one thread runs it and compares it with the
  previous core merge, set by set; a later full-lane A/B in the same work folder reuses it as its base:

  ```bash
  NEW=$(git rev-parse origin/master); OLD=<the previous core merge on master>   # lane none merges keep the key
  for s in closed corpus brush sculpt suzanne external; do
    $P tools/bench/ab.py run $NEW --set $s --replicates 5 --jobs 4 --timeout 1200
    $P tools/bench/ab.py compare $OLD $NEW --set $s     # needs OLD's runs in this work folder too
  done
  ```

  A row worse beyond the replicate spread is bisected by PR (`lane` on each merge in between finds the
  ones that moved it). Not automated yet: a CI workflow doing this after each core merge is a
  follow-up decision.
- **Determinism.** The lane test relies on outputs being reproducible: 225 of 225 plain runs of master
  were byte-identical between two work folders (2026-10-22), and earlier across `--jobs` and thread
  counts (72/72). Builds with another compiler or flags are not comparable byte for byte.

## Adding your own meshes

Add an entry to `corpus.json` (`file` relative to the meshes folder, optional `"sym": ["x", 0.0]`
or `["x", "auto"]`), then `run_corpus.py --only <id>`. ZBrush sculpts exported as OBJ work as is;
make sure positions are welded (AutoRemesher treats every connected component as an island and
indexes by position, so a triangle soup becomes thousands of islands).

Meshes that must stay out of this public repo (someone's own sculpts) go in a separate file of the
same shape as `corpus.json`, kept outside the repo, with absolute `file` paths. `ab.py` and
`sculpt_report.py` read it instead of `corpus.json` when `AR_CORPUS` points at it: `AR_CORPUS=~/private/corpus.json ab.py run
master --set <its set>`. Only metrics from such runs go into notes and PRs, never the meshes or their paths.
