# AutoRemesher upgrade: technical reference

Self-contained reference for upgrading AutoRemesher (core + Blender layer) toward ZBrush ZRemesher quality.
First written 2026-10-14 from a code read; the same day merged with the research threads and the 21 corrections of [research/code-to-math-spec.md](research/code-to-math-spec.md) §23 (log in §9). Measured numbers (Linux, 18 meshes × 2k/10k/50k quads) are in §4.1 and tagged **[M]**, from [research/baseline-and-profiling.md](research/baseline-and-profiling.md) ("baseline" below); the rest is a code read.

Legend: **[V]** verified in the code at the commit below. **[M]** measured by the baseline thread. **[P]** checked by a project thread against a primary external source (paper, LICENSE file, official doc). **[E]** external claim, not checked at the source. **[I]** inferred (derivation or reasoning, not measured).

Companion files: [README.md](README.md) (index), [TODO.md](TODO.md) (ordered plan), [SPECIALIST-QUESTIONS.md](SPECIALIST-QUESTIONS.md) (SQ-* IDs), [LICENSES.md](LICENSES.md), [RECONCILIATION.md](RECONCILIATION.md) (conflicts between sources), [sessions/](sessions/README.md) (log, one file per session), [research/](research/) (full reports). Pre-merge version: [archive/REFERENCE-2026-10-14-pre-merge.md](archive/REFERENCE-2026-10-14-pre-merge.md).

For exact formulas, constants and line numbers of every stage, read research/code-to-math-spec.md ("spec" below). This file keeps the summary, the issue list and the decisions.

---

## 1. What the pieces are

| Piece | Repo | Commit read | License | Role |
|---|---|---|---|---|
| AutoRemesher core + Qt app | https://github.com/huxingyi/autoremesher | `4a49f82` (2026-10-07), v1.2.0 | MIT | All the algorithms. Also ships a Qt GUI and a headless CLI. |
| AutoRemesher Bridge (Blender) | https://github.com/adriflex/autoremesher-blender-bridge | `75f0c22` (2026-08-07), v0.1.3 | GPL-3.0-or-later | Thin add-on: exports OBJ, runs the CLI, imports OBJ. |
| autoremesher-blender (fork) | https://github.com/jakeblakeley/autoremesher-blender | `5368a8c` (2026-07-07) | core MIT, add-on GPL-3.0+ | Full fork of upstream **1.0.0** with a native Python module + Blender extension. See §6.1. |

- **huxingyi** is the GitHub handle of Jeremy Hu, author of Dust3D and AutoRemesher. His repo *is* the remesher; every Blender "AutoRemesher" add-on calls his binary. Upgrading AutoRemesher means changing this C++ code. **[V]**
- Upstream is active: v1.1 replaced Geogram with a native quad parameterizer, v1.2 (2026-08) added extraction cleanups, external PRs merged 2026-09. **[V]** (CHANGELOGS.md, git log). Upstream removed its GPL dependencies in 1.0 to relicense as MIT **[V]**.
- Other Blender wrappers exist, all closed or paid: "Autoremesher Quad" (Superhive, GPL, streams mesh over stdin/stdout, bundles a Windows binary), "Autoremesher Now!". **[E]**

**License (decided by Kajetan, 2026-10-14):** public **GPL-3.0-or-later** fork; GPL solvers allowed, **GPL-3.0-only code not** (TODO 0.2, so no CoMISo); upstream contact deferred (TODO 0.5). Decision, dependency table, per-library licenses found by the research, flags (`OK-MIT`, `OK-GPL3`, `GPL3-ONLY`, `ASK`, `NO`) and the attribution checklist are in [LICENSES.md](LICENSES.md). Two facts that change plans: **CoMISo is GPL-3.0-only** **[P]** (also pulled in by quadwild-bimdf), and several of the best recent codes (Penner seamless/feature-aligned, topological IGM, probably RSP) have **no LICENSE file**, so they need the authors' permission or a re-implementation.

---

## 2. Build

From README **[E/V-readme]**:

- Deps: C++14, Qt 5.15.2 (or Qt 6 via MSYS2), TBB, OpenGL. Bundled in `thirdparty/`: Eigen, meshoptimizer, isotropicremesher, QtAwesome, QtWaitingSpinner, tbb source (Windows).
- Build system: **qmake only** (`autoremesher.pro`). No CMake.
- Linux: `sudo apt install build-essential qt5-qmake qtbase5-dev qttools5-dev-tools libqt5svg5-dev libtbb-dev libgl1-mesa-dev && qmake && make -j$(nproc)`. The README also lists `libqt5multimedia5-dev`, which does not exist on Ubuntu 24.04 and is not needed **[M]**.
- Windows MSVC: build TBB from `thirdparty/tbb` with CMake, then `qmake -spec win32-msvc && nmake -f Makefile.Release`.
- macOS: `brew install qt@5 tbb cmake`, `qmake CONFIG+=sdk_no_version_check && make`. Uses Accelerate for sparse Cholesky (`AUTO_REMESHER_USE_ACCELERATE`).
- Debug build defines `AUTO_REMESHER_DEBUG`; `AUTO_REMESHER_DEV` dumps intermediate OBJs (`debug-input-for-parameterization.obj`, `debug-quadextractor-*.obj`). Useful for diagnosis.
- CI: `.github/workflows/ci.yml` (fork; see CLAUDE.md). Only the Blender add-on ships; upstream's `release.yml` (AppImage, Windows zip, DMG) was removed on 2026-10-15.

**Measured build [M] (baseline §1):** `4a49f82` on Ubuntu 24.04, GCC 13.3, Qt 5.15.13, oneTBB 2021.11; `qmake && make -j4` ≈ 70 s, 24 warnings, no errors. Bundled **Eigen 5.0.1** and meshoptimizer 1020. The headless CLI aborts (exit 134) unless `QT_QPA_PLATFORM=offscreen` is set. The baseline thread also built `ar_bench`, a Qt-free driver over the same core and release flags (`-O3 -flto -funroll-loops -march=x86-64-v2`), checked equal to the CLI, plus a counters-only probe build: [research/baseline-scripts/](research/baseline-scripts/).

**Important for a Blender build:** `src/AutoRemesher/` uses only Eigen + TBB + meshoptimizer + isotropicremesher, no Qt **[V]** (grep). It can be built as a standalone library / Python module. The `.pro` file compiles it into the app; a CMake target for just the core is the first infrastructure step.

### CLI (what the bridge uses)
```
autoremesher --input in.obj --output out.obj --report report.txt \
  --target-quads 50000 --edge-scaling 1.0 --sharp-edge 90 \
  --smooth-normal 0 --adaptivity 1.0 --anisotropy 1.0
```
CLI defaults (`src/main.cpp:42-52`): Q = 50000, λ = 1, θ_s = 90°, ν = 0, a = 1, α = 1 **[V]**. `--report` writes totals only (quads, non-quads, vertices, total time); the per-step phase report goes to stderr **[M]** (corrects an earlier "per-stage timings in the report file").

---

## 3. Pipeline map (core, `src/AutoRemesher/`)

Entry: `AutoRemesher::remesh()`, `autoremesher.cpp:548`. Core ≈10.7k LOC, of which `quadextractor.cpp` is 4.5k **[V]**.
Notation (spec §0): M input mesh, T the isotropic triangle mesh of stage 4, Q target quads, λ edge scaling, θ_s sharp angle, a adaptivity, α anisotropy, β signed dihedral angle, ℓ̄_T mean edge length of T. All rows **[V]** unless marked.

| # | Stage | File:line | What it does |
|---|---|---|---|
| 0 | Edge length L | `autoremesher.cpp:157-161` | L = √((A/N)/(√3/4)) with N = 2Q triangles over the area of the **whole** input. If the quad edge were L the count would be (√3/2)Q ≈ 0.87 Q **[I]** (§5 M7). |
| 1 | Split islands | `meshseparator.cpp`, `autoremesher.cpp:579` | Faces are adjacent only through opposite directed half-edges, so orientation flips and vertex-only contacts split islands; at non-manifold edges the result depends on face order **[I]**. Islands share L (budget by area), run in parallel, no coupling. |
| 2 | Decimate | `autoremesher.cpp:177-302` | If |F_I| ≥ 8 n_I, meshoptimizer QEM to 4 n_I with error bound **FLT_MAX** (only the count stops it). Sharp-edge vertices get a **soft** priority weight (1.0 vs 0.1 quadric weight), not a lock. |
| 3 | Target length h | `autoremesher.cpp:330-451` | On the **decimated** island. κ_v = mean normal deviation per length over the one-ring; ref = 75th percentile; I_v = 1 + 7·min(a,2)·min(4, κ_v/ref)²; h_v = L·clamp(√(Ī/I_v), 0.35, 3). Budget-preserving only before the clamp, which is active in the normal case. Law h ∝ 1/κ (constant normal deviation per edge), not the error-driven κ^{−1/2}. h only sets T's resolution (row 13). |
| 4 | Isotropic remesh | `isotropicremesher.cpp`, `thirdparty/isotropicremesher/` | Botsch–Kobbelt style, **3** iterations, split above 4/3·ℓ*, collapse below 4/5·ℓ*, ℓ* = (h_a + h_b)/2. **Flips have no feature check** (`isotropicremesher.cpp:463` commented out); **featured vertices are relaxed** (`:483`) **but never projected**; projection is a ray along the vertex normal with "no hit ⇒ stay", not closest-point. Serial. Its feature flags are discarded afterwards (M11). |
| 5 | Face scale m | `parameterizer.cpp:223-292` | Second, different estimator on T: κ_v = **max** over the one-ring, κ̄ = face-count mean (floor 1e-3), m_f = clamp((κ_f/κ̄)^{−a}, 0.3, 3). **Not** budget-normalised, so it changes the quad count. |
| 6 | Cross field | `framefield.cpp:86-220` | z_f = e^{4iθ_f} per face, unit edge weights, Levi-Civita transport by hinge unfolding. Locked faces: any boundary edge or interior edge with |β| > θ_s; with two such edges the **last one wins**. Curvature target from the Cohen-Steiner–Morvan tensor without area normalisation; weight c_f = (|λ₁−λ₂| / global max)², which → 0 at umbilics. 5 solves with one factorisation: **the curvature target enters only the first**; solves 2–5 pull toward the previous normalised iterate, i.e. drift to the smoothest field with the locks. |
| 7 | Singularity simplify | `singularitysimplifier.cpp`, call `parameterizer.cpp:380-386` | Cancels any pair whose charges sum to 0 mod 4 (including (2,2)) within 6 face hops, ≤ 4 rounds, by shifting matchings along a path and re-smoothing locally (Gauss–Seidel). Acceptance is topological only (no energy test). Sharp freeze uses the **signed** test β ≥ θ_s, so concave creases are not protected. |
| 8 | Anisotropy | `parameterizer.cpp:146-219` | Signed CSM tensor smoothed by 12 hop-based passes; ρ = clamp(((alongV+φ)/(alongU+φ))^{α/4}, 1/√2.3, √2.3); su = sρ, sv = s/ρ. Gives aspect √(κ_max/κ_min) at α = 1, the classical optimal law **[I]**, elongated along the low-curvature direction. |
| 9 | Conformal sizing | removed 2026-10-15 (was `parameterizer.cpp:42-138`, call commented out) | **Was disabled upstream** in `dd87c6e` "Add curl correction" (2026-08-12): soft Poisson L φ = K − κᶜ (cones κᶜ = (π/2)q̃) plus a fitting term to log m, a density shift, clamp to [0.2, 5]. Its least-squares form (TODO 4.5) was measured and not adopted (M3), so the dead code was deleted. |
| 10 | Combing, matchings | `quadparameterizer.cpp:42-75, 190-218, 246-258` | BFS from face 0 rotates each face to its parent; matchings r ∈ ℤ/4 per interior corner; odd rotations swap su/sv. The 40-iteration `smoothCrossField` is **skipped** on the active path (`:680-681`). |
| 11 | Feature constraints | `quadparameterizer.cpp:80-94` | A feature corner (boundary or |β| ≥ θ_s) gets an isoline constraint only if its edge is within 10° of a field axis; otherwise it is silently dropped (M8). |
| 12 | Curl correction, cut graph | `quadparameterizer.cpp:267-427` | Objective: exact per-edge integrability of the scaled frame under the matchings (the integrable-PolyVector condition). Solve: **one** Gauss–Newton step, δθ = 0 on anchored faces, a 1e-4 Tikhonov term as the only sizing anchor, clamps 0.3 rad / ln 1.5, count-preserving rescale. Seam = complement of a BFS spanning tree over r = 0 dual edges, plus boundary; dangling r = 0 seam edges pruned, r ≠ 0 edges never. |
| 13 | Quad cover solve | `quadparameterizer.cpp:447-608` | Per-corner uv and per-corner translations t with **period 2**, i.e. t ∈ 2ℤ² after rounding. Energy Σ_f Σ_c su sv[(Δu − b·e/su)² + (Δv − b⊥·e/sv)²], residuals in world units (UV-unit weight s_f², stiffer where quads are large). Gauge, seam, transition, wheel (redundant given transitions) and feature (period 1) constraints. **No integer constraint on cone UVs**: period 2 puts cones on ℤ² implicitly but over-restricts (M12). Quad size s_f = λ·ℓ̄_T·m_f·ρ_f^{±1}. |
| 14 | Constraint elimination | `mixedintegerleastsquares.cpp:209-396` | Kernel x = K y via M0 (single-term rows), M1 (union-find of ±1 pairs, group period = max), M2 (general rows, pivot with smallest period·|a|). **Pass 3's consistency result is ignored** (`:222-224`). Integrality of eliminated integer variables is not guaranteed in general (safe for today's ±1 rows) (M13). Not a port of Geogram's public code (RECONCILIATION C1); provenance unknown. |
| 15 | Integer rounding | `mixedintegerleastsquares.cpp` `fixNextBatch` (since 2026-10-19; was `:449-487`) | **Batched greedy** since 2026-10-19 (TODO 3.3): each pass fixes the ⌈m/10⌉ unfixed integers closest to p·ℤ, plus all within 1e-3, ties by index, then re-solves; at most 10 passes. Was **direct rounding**: threshold = max(d + 0.001, 1.0) = 1, so after the first solve every integer was fixed to p·round(y/p) at once; one re-solve; two factorisations in total. Then values within 0.01 of an integer are snapped (`quadparameterizer.cpp:617`). (M1) |
| 16 | Linear solver | `constrainedleastsquares.cpp` | Normal equations AᵀWA + 1e-10·mean-diagonal ridge, `Eigen::SimplicialLDLT` (Accelerate LLᵀ on macOS). A **new** `ConstrainedLeastSquares` was built every MILS iteration (`mixedintegerleastsquares.cpp:472`); since 2026-10-19 (TODO 3.2) one per island, with fixed integers held by `fixVariables` (rows and columns reduced to the diagonal, pattern kept), so each pass is one numeric factorisation. The SparseLU/KKT fallback is not reached on the active path **[I]**. |
| 17 | Quad extraction | `quadextractor.cpp:48-289` | Integer isoline crossings per triangle **in 3D**, merged on an absolute 1e-5 grid (`positionkey.cpp:26`) → separatrix-completion walk → graph simplification (also removes legitimate valence-2 corners **[I]**) → greedy cycle search (lengths 4,5,6,7,3; unordered-container order, so results can differ between standard libraries **[I]**) → hole fill (≤ 65 edges) → keep **only the largest component** → uniform (non-tangential) Laplacian ×5 with closest-point projection onto **T, not the input** → exactly **12** cleanup passes (P3). |
| 18 | Merge islands | `autoremesher.cpp:922-942` | Concatenate with index offsets. |

Data-flow facts that matter (spec §1):
1. The input M is not used after stage 4. Field, parametrization and final projection all use T, so output error vs. M is bounded only by d(out, T) + d(T, M) **[I]**.
2. Stage 4's feature flags are discarded. Stages 6, 7 and 11 re-detect creases from T's dihedrals with three different tests (|β| > θ_s, β ≥ θ_s, |β| ≥ θ_s), after stage 4 may have flipped across them.
3. Quad size is λ·ℓ̄_T·m_f·ρ_f^{±1}. Stage 3's local h enters only through T's resolution and the global mean ℓ̄_T, so T's triangle size and the quad size follow two different fields; T can be coarser than the quads locally **[I]**.
4. Parallelism is per island only (TBB at `autoremesher.cpp:738, 876`).

Inputs not used: `ModelType` (Organic/HardSurface) is stored and never read **[V]**.

---

## 4. Performance (code read; measured verdicts in the last column and §4.1)

The progress bar splits resample 17 % / parameterize 33 % / extract 50 % (`autoremesher.cpp:84-85`) **[V]**. They are UI constants. Measured stage shares are 36/35/29 at 2k, 25/37/38 at 10k and 15/40/46 at 50k **[M]**, so the UI split only matches at 50k.

| ID | Bottleneck | Evidence | Assessment |
|---|---|---|---|
| P1 | **Islands are the only parallelism.** A single sculpt runs parameterize + extract on one core. | `autoremesher.cpp:738, 876` **[V]** | Typical sculpts are one island **[I]**. Fix: intra-island TBB on crossings, curvature, projection; coloured Gauss–Seidel for the field (perf §7). **Confirmed [M]:** 1.00 cores busy on every single-island run; 1 vs 4 threads 1.10× on koala, 3.5× on 8 equal islands. |
| P2 | Sparse solve via normal equations + single-threaded `SimplicialLDLT`. | `constrainedleastsquares.cpp:47-111, 247` **[V]** | Systems are ≈10⁵ unknowns at 50k quads (T has ≈2 triangles per target quad), so the solve is probably not today's bottleneck **[I]**. Conditioning: for a gradient-fitting energy AᵀA is the Laplacian-like system every method factors; the real risk is rank deficiency hidden by the ridge **[I]** (RECONCILIATION D7–D8). Becomes important after P7 + greedy rounding and M4. **Confirmed, systems larger [M]:** solves are 14/20/27 % of run time at 2k/10k/50k; the largest system is the curl correction (n = 3F, 2.2–3.9·10⁵ at 50k, nnz(L) up to 21.6M), not the cover system (≈10⁵). CHOLMOD supernodal: factorisation 1.6–2.2× faster at 50k, no gain at 10k, whole run −1 % to +13 %. KKT/SparseLU fallback never taken (0/54); ridge-masked rank deficiency untested. |
| P3 | Extraction cleanup: each of the 12 passes loops rounds that rebuild a global `std::map<edge, faces>`; `mergeSharedFiveEdgeFaces`, `cleanupTriangles` and `collapseThreeValenceDiagonals` apply **one** operation per round; every pass ends with `smoothAroundVertices`, which rebuilds the AABB tree over all of T. | `quadextractor.cpp` §18 of spec **[V]** | O(k·F log F) for k defects **[I]**. Mostly a symptom of M1/M2: a valid integer-grid map needs almost no repair. **Confirmed [M]:** cleanup is 55–64 % of extraction; `mergeSharedFiveEdgeFaces` is the costliest single step (15 % of all time; median 11 rounds, max 96); extraction scales like n^1.37, merge-5 like n^1.68. Spearman ρ(UV flip %, cleanup rounds) = 0.80/0.85/0.65 at 2k/10k/50k. |
| P4 | Serial pre-processing on large inputs. (Was: "isotropic remesher is serial", which is true but aimed at the wrong component.) | no TBB in `thirdparty/isotropicremesher` **[V]**; `meshseparator.cpp:94` serial flood fill copying every face **[M]** | **Corrected [M]:** the remesher only sees the decimated mesh (≤ 4× target), so its cost tracks the target. At 6.4M input triangles 24.1 of 27.5 s are serial pre-processing: OBJ parse 4.3 s, island split 6.7 s (≈1 µs per triangle), island contexts 3.2 s, QEM decimation 9.9 s. Fix those first: parallel island split, contexts and decimation (TODO 8.2). Peak RSS ≈ 400 B per input triangle (2.5 GB at 6.4M). |
| P5 | `vector<vector<size_t>>` faces, `std::set`/`std::map` graphs, per-row `vector<Coeff>` sparse rows. | throughout **[V]** | Do flat arrays as part of the half-edge rewrite of extraction, not separately. **Confirmed, plus a free win [M]:** allocator + `std::map`/`set` + `unordered_*` are 45–53 % of CPU time at 50k. Preloading TBB's scalable allocator (`libtbbmalloc_proxy`) makes runs 1.22–1.54× faster (extraction 1.8–2.3×) with byte-identical output (TODO 8.0). |
| P6 | Bridge: blocking `subprocess.run`, OBJ text round trip. | bridge `__init__.py:148` **[V]** | UX, not algorithm (§6). **Confirmed on the C++ side [M]:** OBJ parse 1.1 s at 1.6M triangles, 4.3 s at 6.4M; Blender-side export not measured. |
| P7 | MILS rebuilds the whole constrained system (substitution, symbolic analysis, factorisation) every iteration. | `mixedintegerleastsquares.cpp:449-486` **[V]** | Free with 2 solves; a ~100× regression once greedy rounding lands. Fix before M1 (TODO 3.1–3.2). **Confirmed [M]:** exactly 2 solves and 2 full factorisations per island; rounding step 6.4 % of run time today (median, max 34 %). The predicted regression is untested. |
| P8 | Determinism. Multithreaded solvers (Accelerate, CHOLMOD with threaded BLAS) change the last bits and rounding amplifies them into different topology; the face search iterates unordered containers. | perf §2.3, spec §17.5 **[I]** | Requirement: same input ⇒ same mesh. Test 10 runs and several thread counts. **Not observed [M]:** byte-identical output over 10 repeats, 8 islands in parallel, 1 vs 4 threads, two allocators and threaded CHOLMOD (CHOLMOD vs LDLT: same connectivity, positions within 2·10⁻⁹ diag). One platform and compiler only; cross-platform untested. **2026-10-26:** deterministic per build on Linux, Windows and macOS (same binary twice per runner, Intel and AMD runners) [M]; builds of different platforms never give the same output, causes in TARGETS §2.5 (research/platform-divergence-2026-10-26.md). |
| P9 | **Singularity simplification is O(S·F)** (new, baseline). `SingularitySimplifier::cancelPair` allocates and scans face- and vertex-sized arrays, including two full `vertexCharges()` calls, for every candidate pair. | `singularitysimplifier.cpp:263` **[V by baseline]** | **[M]:** 22.8 of 54 s (42 %) on sword 50k (1686 cones), 20 % on noisy spot 50k, 11.6 % over the corpus (second costliest step). Hard-surface and noisy inputs are slow because of cones, not size. A local implementation should not change results **[I]** (TODO 8.7). |
| P10 | **Peak memory** (new, baseline). | **[M]** | Median peak RSS 50/136/501 MB at 2k/10k/50k (max 1.0 GB). RSS first reaches 95 % of peak in "Rounding cover to integers" in 37 of 54 runs; for large inputs at low targets, in the isotropic stage holding the input. Relevant once greedy rounding and finer field meshes grow the systems. |

### 4.1 Measured baseline: AutoRemesher 1.2 (`4a49f82`)

Source: [research/baseline-and-profiling.md](research/baseline-and-profiling.md) (finished 2026-10-14, 20:25 UTC); per-run tables in [research/baseline-scripts/results/](research/baseline-scripts/results/); scripts to re-run in [research/baseline-scripts/](research/baseline-scripts/). All **[M]**.

**Setup.** Cloud VM, 4 vCPU Xeon 2.1 GHz, 15 GB RAM, Linux. 18 meshes (organic, hard surface, thin shell, scans, noisy, 8-island; 1.2k–2.0M input triangles; 12 public PD/CC meshes from `odedstein/meshes@8352132`, 6 synthetic) × 2k/10k/50k quads, default parameters: 54 runs, 0 failures. Single runs; run-to-run noise ±6 %. No Kajetan sculpts, no macOS, no ZRemesher reference yet (TODO 1.12).

**Time and memory.**

| target | median run s | iso / param / extract % | solves % | cleanup % of extract | peak RSS MB median [max] |
|---|---|---|---|---|---|
| 2k | 0.7 | 36 / 35 / 29 | 14 | 55 | 50 [805] |
| 10k | 3.1 | 25 / 37 / 38 | 20 | 59 | 136 [805] |
| 50k | 20.4 | 15 / 40 / 46 | 27 | 64 | 501 [1043] |

Costliest named steps over the corpus: merge-5 cleanup 14.7 %, singularity simplification 11.6 % (P9), curl-correction factorisation 11.1 %, rounding 6.6 %. Slow runs are hard-surface and noisy inputs (cones and cleanup rounds), not large ones: csg_bracket (1.2k triangles) 56.7 s at 50k, cat (1.7M) 35.6 s.

**Quality.**

| target | quads / target median [min, max] | interior valence 3 / 5 / ≥6 % | non-quads per run median [max] | runs with SJ ≤ 0 faces | runs with non-manifold edges |
|---|---|---|---|---|---|
| 2k | 0.51 [0.14, 1.06] | 4.36 / 2.46 / 0.30 | 6 [64] | 11 | 8 |
| 10k | 0.47 [0.19, 0.71] | 1.56 / 0.99 / 0.08 | 10 [84] | 11 | 6 |
| 50k | 0.37 [0.16, 0.56] | 0.55 / 0.37 / 0.02 | 16 [105] | 11 | 6 |

- Clean organic inputs are good at 10k: angle deviation 5.7–8.6°, scaled Jacobian < 0.5 on < 1 % of quads, 10–24 irregular vertices per 1k quads, Hausdorff < 1 % diag.
- Hard-surface, thin-shell and scanned inputs are not. Sword at 10k: 84 non-quads, 9.2 % of quads with SJ < 0.5, 46 faces with SJ ≤ 0, 24 non-manifold edges.
- Low targets lose features: Hausdorff 7–9 % diag at 2k on five meshes.
- Parametrization: one-shot rounding in 81/81 island solves, rounding moves up to 0.5; flipped UV triangles in **54/54** runs, median 0.75 %, max 10.1 % (2.22 / 0.58 / 0.25 % median at 2k/10k/50k) (M1, M2).
- Symmetry: on exactly symmetric inputs the layout is no better than an unrelated one (2–15 % of vertices have a mirror partner within 0.1 edge; mean distance 0.34–0.42 edge vs. 0.38 for a random grid). Symmetric output needs F5.

**Experiments.**
- Threads: one island uses one core (P1).
- Input ladder 0.1M → 6.4M: everything after decimation is bounded by the target; pre-processing is the growth (P4 corrected).
- CHOLMOD: helps the factorisation at 50k only; not worth it before greedy rounding (P2, TODO 8.1).
- tbbmalloc preload: 1.22–1.54× with identical output (P5, TODO 8.0).
- Adaptivity 0: quad count 0.80–1.16 × target on 3 meshes (all 18, measured in TODO 2.11: median 0.82, 0.61–1.57), UV flips 4–20× lower (M3, M7).
- CPU profile at 50k: allocator and node containers 45–53 %, Eigen Cholesky 8–14 %, isotropic remesher 9–15 %.

**Verdicts applied.**

| Item | Verdict |
|---|---|
| §2 build/CLI | Confirmed with fixes (apt package, headless env var, `--report`) |
| P1, P3, P5, P6, P7 | Confirmed |
| P2 | Confirmed, systems larger |
| P4 | Corrected |
| P8 | Not observed |
| M1, M2 | Confirmed |
| M3 | Supported indirectly; measured directly in TODO 2.11 (§5 M3) |
| M4, M7 | Corrected |
| M14 | Partly confirmed |
| New | P9, P10, M15, M16 |
| Untested | M5, M6, M8–M13 |

**Baseline's priority reading [I], adopted in TODO.md.**
1. Quality through M1/M2: the flip rate predicts both cleanup time and final irregularity (Spearman ρ up to 0.85).
2. Free performance: tbbmalloc, a local `cancelPair`, a parallel island split and decimation.
3. Correctness bugs regardless of track: boundary capping (M15), non-manifold output (M16), and a quad count 2–3× under target (M7).
4. Single-island parallelism is the only route past one core.

### 4.2 Research recommendations

The ranked list R1–R13 is in [research/performance-robustness.md](research/performance-robustness.md) §10 and scheduled in TODO.md. Summary **[I]**: measure first; put a solver-backend interface in place and stop rebuilding the MILS system before greedy rounding; extraction on a half-edge worklist is the largest single-thread win on bad maps; for big inputs coarse-to-fine (Ebke et al. 2016, 2M faces 11.3 s vs 318 s **[P]**) beats GPU (≈1.75× mean, CUDA-only **[P]**); excluded: MKL/Panua PARDISO, cuDSS, Gurobi (LICENSES.md).

---

## 5. Math issues

Each item: what the code does, why it is a problem, what the research recommends, and the open questions (SQ-* in SPECIALIST-QUESTIONS.md). Ordered by expected impact **[I]**. M11–M14 were added from the code-to-math spec, M15–M16 from the baseline measurements.

### M1. One-shot integer rounding (highest impact)
- **Code [V]:** stage 15. After the first continuous solve every integer is fixed at once (threshold max(d + 0.001, 1.0) = 1), one re-solve, done.
- **Problem:** direct rounding ignores coupling between integers. Known to produce fold-overs, degenerate and flipped UV triangles and collapsed or lost singularities (Bommes et al. 2009; IGM 2013 **[P]**). These become the non-quads the 4.5k-line extractor patches.
- **Provenance:** present since `8c9f01f` "Replace geogram with a native quad parameterizer" (2026-08-09) **[V]**. It is **not** a port of Geogram's rounding: Geogram's public quad parametrization (PGP) has no integer variables **[P]** (param §0.2). `max(…, 1.0)` and the `break` after the first unfixed variable look like a mangled greedy threshold **[I]**; only upstream could confirm the intent (contact deferred, TODO 0.5), and the fix doesn't depend on it.
- **Fix path (param §4, perf §3):**
  - *Stage A (days, own code):* greedy/batched rounding (fix all integers with distance < δ_k, δ growing), a separation rule so two cones never round to the same grid point (IGM's idea), deterministic tie-breaking, local Gauss–Seidel updates, optional MIQ stiffening driven by a flip counter. Needs P7 fixed first. CoMISo is not needed (and is GPL-3.0-only).
  - *Stage B (weeks–months, only if A misses the targets):* QGP-style pipeline: continuous seamless map with fixed cones → injectivity repair (foldover-free maps, SLIM) → motorcycle graph / T-mesh → integer arc lengths by **Bi-MDF (libSatsuma, MIT)** → per-patch Tutte + SLIM → extraction. Metriko (MPL-2.0) as reference and test oracle; it has no boundary support.
  - Neural rounding: **not recommended** (param §3.6): the integer problem is small once posed as Bi-MDF, and validity is global, so a predictor can't certify it.
- **Measured [M]:** confirmed in 81 of 81 island solves: one relaxed solve, every integer fixed at threshold 1.0, max |x − round(x)| ≈ 0.5 in every run.
- **Measured, greedy rounding [M] (TODO 3.3, 2026-10-19, [research/rounding-greedy-2026-10-19.md](research/rounding-greedy-2026-10-19.md)):** at default adaptivity 0.5, 5788 of master's 11480 final flips (72 runs) already exist in the continuous map; rounding made the other half. Batched greedy (closest tenth per pass, ≤ 10 passes, one numeric factorisation each) cuts the rounding-made flips to 2846: final flips ×0.75, non-quads ×0.73, SJ ≤ 0 324 → 250, rounded / continuous energy 1.37 → 1.20 (means). Over plain + 5 float32 replicates (2026-10-20, [research/greedy-rounding-rerun-2026-10-20.md](research/greedy-rounding-rerun-2026-10-20.md)): final flips ×0.79, non-quads ×0.80, SJ ≤ 0 315 → 258. Genus neutral, no run changed in all replicates (corpus 60.5 → 61.5/72, closed set 76.7 → 75.5/78, replicate means): fewer flips do not remove the folds where the extractor adds or drops handles (M16). Open from Stage A: cone separation. The continuous half needs an injectivity step (M2, TODO 3.8) or the sizing fix (M3).
- **Questions:** SQ-P3 (greedy order), SQ-P4 (validity certificate), SQ-L1 (route to quantization).

### M2. No validity guarantee for the parametrization
- **Code [V]:** no flip or injectivity check between the cover solve and extraction; `buildResultUv` snaps values within 0.01 of an integer.
- **Problem:** extraction assumes a locally injective, seamless map. Without it isolines cross and the graph extractor must guess.
- **Fix:** count flipped/degenerate UV triangles with exact `orient2d` (Shewchuk, public domain) as the first diagnostic. Robust seamless maps: Campen et al. 2019 (guaranteed, high distortion), Penner-coordinate methods 2024/2025 (strongest today; **no LICENSE**, ask), ConformalIdealDelaunay (MIT). Injectivity repair: foldover-free maps (Garanzha et al. 2021), SLIM, TLC.
- **Measured [M]:** flipped triangles in the integer-grid map in 54 of 54 runs (median 0.75 %, max 10.1 %); the flip rate predicts cleanup rounds (ρ 0.65–0.85) and final irregularity (ρ 0.48–0.81).
- **Measured, untangling [M] (TODO 3.8, 2026-10-20, [research/untangling-2026-10-20.md](research/untangling-2026-10-20.md)):** Garanzha 2021 energy plus α|J − I|² over the continuous kernel variables near each inverted face, integers held, after greedy rounding. 72-run corpus: final flips 8634 → 2709 (×0.34), non-quads 661 → 507, SJ ≤ 0 250 → 237, time within noise; closed set flips 5096 → 562. Genus about neutral (fewer flips still do not decide what the extractor does at the folds left, M16). What stays bad has three causes: faces with two edges on one constrained isoline, degenerate by the constraints alone (2310; TODO 3.9); faces degenerate by their integers alone (none is improved); and spots where no map with these integers is injective: with the region's integers free 90 % of the rest untangles, but re-rounding gives most of it back, so the remainder is a quantization problem (Stage B). Cone winding: 1622 of 6714 cones wound one turn short after rounding, 1334 of them already in the continuous map; 1111 after untangling.
- **Questions:** SQ-P4, SQ-P5, SQ-E4.

### M3. Sizing field not consistent with singularities
- **Code [V]:** stage 9 is disabled; instead stage 12 applies one clamped Gauss–Newton step toward integrability, with sizing held only by a 1e-4 Tikhonov term.
- **Problem (corrected, RECONCILIATION D1–D2):** with ρ = 1/h and φ = log ρ, a seamless square map needs ⋆ω = dφ (Myles & Zorin 2013 **[P]**), hence Δφ = K − K̂ with K̂ the cone deltas. Two consequences:
  - With fixed cones on a closed surface φ is determined up to a constant. An exactly conformal solve has **no local size freedom**; the disabled solve gets adaptivity only by violating its own Poisson equation through the fitting term.
  - The direction is constrained too: a given field admits an exact φ only if its rotation form is co-closed, i.e. it is the smoothest (trivial-connection) field for its cones. A curvature-aligned field generally is not, so solving for φ alone cannot remove curl.
  "log s satisfies a Poisson equation" is necessary, not sufficient. Density painting (F3) inherits all of this: sharp density changes force pole pairs.
- **Fix (size §2, [I] design):** place cones using the effective curvature K − Δφ* (φ* = target log density from the sizing field), then project by least squares: (L + λM)φ = div(⋆ω_X) + λMφ*. One SPD solve; residual curl is absorbed by the cover energy. Replaces both the curl correction and the disabled solve. For fixed period jumps the joint problem in (θ, φ) is linear least squares (size §2.3). Research-grade alternatives that move cones by distortion: moving frames (Coiffier–Corman 2023, MIT Python), RSP (Corman–Crane 2025, `ASK`), integrable odeco fields (Couplet et al. 2026, no code).
- **Measured, indirect [M]:** turning curvature sizing off (adaptivity 0) cuts UV flips 4–20× at the same target, and at 10k beats the default at 50k. Consistent with M3 but does not separate it from plain scale-gradient distortion; no direct curl measurement.
- **Measured, direct [M] (TODO 2.11, 2026-10-15):** at 10k quads the continuous cover solve covers 0.40–0.97 of the cells that the sized frame field asks for (Σ A/(s_u s_v)) with the curvature scale field, against 0.94–1.00 with a uniform one. Hard-surface parts lose most (boat 0.40, sword 0.50, csg_bracket 0.52). The lost part is the curl that the least-squares projection drops. 2.11 rescales the continuous solution to the expected count before rounding, so the per-island rescale factor is now a cheap M3 indicator (log it in 1.6); 4.5 should bring it toward 1.
- **Measured, 4.5 projection [M] (2026-10-15, research/sizing-projection-2026-10-15.md):** the stage records now carry `continuous_rescale` and the realised-vs-requested size slope. Projecting master's stage-5 sizes with (L + λM)φ = β + λMφ* (β = circulation of the field = K − K̂, λ = c/h̄²) brings the rescale factor from median 1.27 / max 2.37 to 1.06 / 1.14 and continuous flips to ×0.18 (c = 1, 10 meshes). On the output it is no better than lower adaptivity: final flips ×0.42, non-quads ×0.70 over 72 runs, but mean distance ×1.13, against ×0.48 / ×0.72 / ×0.98 for adaptivity 0.5. At equal λ the circulation term gives 15–25 % fewer continuous flips than plain smoothing of log h and nothing measurable on the output; the gain is the removal of small-scale size variation, and rounding then makes about two thirds of the remaining flips. Not adopted; the curl correction stays (off: ×3 flips; a stronger regulariser: ×1.4–2.2).
- **Questions:** SQ-Z1, SQ-Z2, SQ-Z3, SQ-Z5.

### M4. Field and parametrization solved at quad resolution
- **Code [V]:** T has ≈2 triangles per target quad, and quad size = λ·ℓ̄_T·m_f·ρ_f^{±1}, so the quad edge ≈ the mean triangle edge **on average**. Locally T's resolution follows h (stage 3) and quad size follows m (stage 5); where they disagree T can be coarser than the quads (more than one isoline per triangle, the hard case for extraction) **[I]**. **Measured [M]:** "quad edge ≈ triangle edge" holds only with adaptivity 0 (2.4–2.6 triangles per quad, ratio 1.01–1.06). With the default, each quad covers a median 5.8 triangles (3.3–13.4), quad edge ≈ 1.6× triangle edge, so on average T is *finer* than the quads; the local "coarser" case was not measured.
- **Measured after TODO 2.11 [M] (2026-10-15):** the default's 5.8 triangles per quad came from the count undershoot. With the count fixed, T has 2.3–2.7 triangles per output quad (median at 10k / 2k), quad edge ≈ 1.0 T edge, at any adaptivity. Against master run at the raised target that gives the same output count (2k / 10k): time 0.73 / 0.62×, but angle deviation p95 26.7 → 29.5° / 24.1 → 26.8°, Hausdorff 0.52 → 0.87 / 0.10 → 0.17 % of the diagonal, UV flips 1.36 → 2.02 / 0.50 → 0.51 %. Building T 2.25× finer (same count; 5.9 / 4.7 triangles per quad) gives UV flips 1.08 / 0.42 %, Hausdorff 0.44 / 0.107 %, angle p95 30.4° / 24.7°, at 1.53 / 1.65× the time of the current default. So finer T recovers or beats master on flips and distance and on angles at 10k, not on angles at 2k. Since 2.11 the count does not depend on T, so T's resolution is a pure quality/time setting (TODO 2.15). **Sweep [M]** (2, 3, 4.5, 6 triangles per quad; per-mesh geometric mean against 2 at 2k / 10k): 3 gives 1.36× the time for small gains; 4.5 cuts Hausdorff by 20 / 19 %, mean distance by 34 / 27 % and UV flips by 30 / 19 % at 1.6×; 6 adds 4–14 points on flips and mean distance, none on Hausdorff, at 2.0×. 4.5 is the Quality default; there angle p95 improves by 5 / 6 % and irregular vertices change by ≤ 2 %.
- **Problem [I]:** curvature directions are computed after decimation (no error bound) and remeshing to quad resolution, so creases and small forms are gone before the field sees them; singularities can only sit on coarse vertices.
- **Fix:** compute the curvature tensor on the input at radius r ≈ c·h and transfer it (RECONCILIATION D3); optionally solve the field on a mesh k = 2–4× finer with an index-preserving restriction to the cover mesh; for 5M+ inputs, Ebke et al. 2016 proxy + transfer. Costs 4–16× larger systems, so it waits for the solver interface.
- **Questions:** SQ-X3.

### M5. Singularity placement is a by-product, not optimised
- **Code [V]:** stage 6 drifts to the smoothest field after one aligned solve; stage 7 cancels pairs greedily within 6 hops with no energy test.
- **Problem:** pole count is fixed in sum (Poincaré–Hopf); *where* poles go is what makes ZRemesher topology look artist-made. The 5-iteration loop is an MBO/Ginzburg–Landau scheme with an implicit, resolution-dependent ε, so pole count depends on mesh resolution **[I]** (cross §3.2).
- **Fix (cross §4):** (1) explicit MBO with ε = c·h and a fixed alignment target, iterated to a stable singularity set (S); (2) then placement by distortion: *cones first* (integer-constrained cones, Du et al.; or sparse cones from K − Δφ*) → trivial-connection field → φ, or *joint frames* (port moving frames). Prototype offline and decide with corpus numbers. Learned fields (NeurCross, CrossGen: AGPL; TopGen, Dielen: no code) are priors at best, later.
- **Questions:** SQ-X1, SQ-X2, SQ-X5.

### M6. Curvature alignment weight
- **Code [V]:** CSM tensor over 3 vertices per face, no area normalisation; certainty (|λ₁−λ₂| / global max)².
- **Problem (corrected, RECONCILIATION C8):** the weight does go to 0 at umbilics. The real issues: one very sharp region shrinks every other weight (global max); without area normalisation the weight scales like |κ₁−κ₂|ℓ², so it drops exactly where stage 3 refined; and the target is used only in the first of 5 solves. Noisy on scans.
- **Fix:** Knöppel discretisation (connection Laplacian + mass matrix), λ = strength·h⁻² so it is resolution-independent, a local saturating certainty such as ((κ₁−κ₂)h)² / (1 + ((κ₁−κ₂)h)²), and a scale-aware tensor (Rusinkiewicz or quadric fit at radius r ≈ c·h).
- **Questions:** SQ-X4.

### M7. Two density estimators and an uncontrolled quad count
- **Code [V]:** stage 3 and stage 5 differ in estimator (mean vs. max over the one-ring), normalisation (75th percentile vs. face mean), law and clamps. Neither is count-normalised after clamping.
- **Problem, measured [M]:** with the default adaptivity the output has **0.37–0.51 × the requested quads** (median at 50k/10k/2k; below target in 53 of 54 runs). The stage-5 factor m_f enlarges quads by ≈1.5× in edge length on average. With adaptivity 0 the count is 0.80–1.16 × target, which confirms the uniform ≈0.87 Q estimate (spec §16). The spec's "mesh-dependent direction" is **refuted**: the bias is systematic. **Traced and fixed (TODO 2.11, PR #2) [M]:** m_f is normalised by κ̄, a per-vertex mean on T, which stage 3 refined in curved regions, so κ̄ is 1.2–12 × the area mean (median 2.3) and the field asks for Σ A m⁻² / Σ A = 0.27–0.85 of the base count; the clamps change this by < 3 %. The rest of the shortfall is the M3 cover loss (0.40–0.97) and the uniform ≈0.87. On all 18 meshes adaptivity 0 gives 0.82 median (0.61–1.57), not 0.80–1.16. After 2.11: 0.98–1.00 × target median, 107 of 108 runs within ±10 %, adaptivity 1 and 0.
- **Fix (size §1, engineering):** one sizing field: scale-aware tensor, κ = max(|κ₁|, |κ₂|), percentile clamp, implicit smoothing, h ∝ κ^{−a/2} (chord-error law, a = 0 uniform), thin-part cap h ≤ SDF/k, density paint as h·2^p, log-domain gradation (Dijkstra), count normalisation h ← h·√(∫h⁻² dA / N_target). Both T's resolution and the quad size come from it.
- **Law, measured [M] (2026-10-15):** stage 5's m_f ∝ κ^{−a} is the chord-error law at a = 0.5, and a = 1 is the 1/κ law. Over 72 corpus runs (18 meshes, 2k / 10k, Fast and Quality) a = 0.5 against a = 1: final UV flips ×0.48, non-quads ×0.72, folded ×0.72, irregular vertices ×0.86, angle p95 ×0.76, mean distance ×0.98, Hausdorff ×1.11, count unchanged. The default adaptivity is now 0.5 (interface, CLI, app, add-on); the slider still reaches 1. Stage 3's own law is unchanged (its weight halves with a).
- **Questions:** SQ-Z3, SQ-Z4.

### M8. Feature edges only respected if the field already agrees
- **Code [V]:** stage 11 constrains a feature edge only within 10° of a field axis. Stage 6 locks each face to one feature edge (last wins), so at non-right-angle corners the second crease exceeds 10° and is dropped.
- **Fix (size §3):** one `FeatureEdges` set (dihedral, crease, face set, material, normal split, seam, guide; hard/soft); always-hard field lock; per-sector valence by the sector rule q = max(1, round(2α/π)), corner index (2 − q)/4; every hard chain an integer isoline, dropped only when infeasible (strips narrower than ≈h/2), with a warning. Needs M11 fixed and at least Stage A of M1. The quadwild-bimdf hard-surface backend is excluded (`GPL3-ONLY` through CoMISo, TODO 0.2).
- **Questions:** SQ-F1, SQ-F2.

### M9. Anisotropy
- **Code [V]:** stage 8.
- **Corrected (RECONCILIATION C9):** the exponent is the classical optimal anisotropy law, not ad hoc. Heuristic parts: hop-based smoothing radius (resolution-dependent), signed tensor (saddles cancel before |·|), aspect clamp 2.3.
- **Open:** is anisotropy wanted for ZRemesher-like output (mostly near-isotropic)? If yes, should the aspect bound be a constraint (RSP-style rectangular parametrization)?
- **Questions:** SQ-X6.

### M10. Extraction is graph-based, not parametric tracing
- **Code [V]:** stage 17.
- **Fix (extr §7):** fix the map first (M1/M2); then A/B **libQEx** (GPL-3.0) behind `QuadExtractor`; replace the 12 passes by a half-edge worklist of grid-preserving operators (doublet collapse, quad-vertex merge, diagonal collapse, chord collapse, cavity re-quadrangulation) under a validity guard; Ray 2025's dual-segmentation extractor (no code, re-implement) only if Stage A still leaves folds.
- **Questions:** SQ-E1 – SQ-E7.

### M11. Creases are eroded before the field sees them (new)
- **Code [V]:** stage 4 flips across features, relaxes featured vertices in the tangent plane three times and never projects them back, and projects other vertices by a ray that may miss; then its feature flags are thrown away and stages 6, 7, 11 use three different sharp tests (stage 7's is signed).
- **Problem [I]:** crease vertices drift off the crease, creases cut diagonally by triangles can fall out of the feature set, and concave creases are unprotected in stage 7. Hard-surface quality is capped before any field or rounding work.
- **Fix:** restore the flip feature check, relax feature vertices only along the feature (or not at all) and project them onto it, closest-point projection fallback, and pass one `FeatureEdges` set downstream with a single |β| test. Prerequisite for M8 and F1/F2.

### M12. Translation lattice 2ℤ² (new)
- **Code [V]:** translations have period 2; there are no integer variables for cone positions.
- **Derivation [I] (spec §13, param §0.3, agree independently):** around a cone with holonomy R the transitions force uv = (I − R)⁻¹τ. With all t ∈ 2ℤ², τ ∈ 2ℤ², which puts every cone on ℤ² without explicit constraints. Cost: each seam translation lives in 2ℤ² instead of ℤ² (index 4); the composed translation τ at a cone lives in 2ℤ² instead of the exact (I − R)ℤ², index 2 for ±90° cones (D₂ = {x + y even}) and 1 for 180° cones. Consequence **[I, consolidation thread, unchecked]**: every closed loop has even length, so the layout induced by the map is bipartite and all ±90° cones (valence 3 and 5) sit at uv ∈ D₂, one colour class; e.g. no valence-3/valence-5 pole pair at odd distance. Also **[I]**: once integer t and one cone fix the global translation, the gauge uv = 0 at corner 0 still pins a regular vertex to a grid point; loops with trivial holonomy (handles, tubes) get an even number of quads; rounding can move a translation by up to 1 quad instead of ½. MIQ uses t ∈ ℤ² plus explicit cone constraints.
- **Spot check [M] (2026-10-19, spot Quality 2k only):** t ∈ ℤ² with integer cone uv halves the largest rounding shift (1.00 → 0.50) but gives 142–227 final flips against 24 (one-shot) and 146 against 4 (greedy). The 376 kernel integers left by elimination are combinations of many translations, so rounding errors grow through the substitution: the exact lattice needs M13's lattice-preserving elimination first.
- **Measured 2026-10-21 [M] ([research/seam-lattice-2026-10-21.md](research/seam-lattice-2026-10-21.md), `ab.py`, 5 float32 replicates):** the lattice loses handles by rounding a handle's holonomy to 2, not to 0: torus_0.03 F150's meridian (continuous 2.7–4.0 quads) rounds to 2 in 5 of 6 first attempts, and a 2-quad ring has no mesh, so the map degenerates (83–116 of ~390 triangles). ℤ² + integer cone uv rounds it to 3 with a valid map, but keeps the handle only together with the extractor fix of the torus thread (`collapseTriangles` and the side test). demosthenes' tunnels have holonomy ≈ 1 quad and no lattice keeps them. Integrality residual stays 0, so elimination (M13) is not what spoils ℤ²: most of the extra flips came from the rounding batch size counting the nearly integral integers (changed in 3.3, 2026-10-21); with that fixed ℤ² still costs corpus final flips ×1.16 against the same rounding on 2ℤ², closed set neutral. Not adopted.
- **Questions:** SQ-P1.

### M13. Constraint elimination can fail silently (new)
- **Code [V]:** pass 3 detects residual constraint rows but its return value is ignored; M1 merges a group to the maximum period of its members; M2 can eliminate an integer variable with non-integer coefficients.
- **Problem [I]:** safe for today's ±1 constraint set, but any new constraint (features at arbitrary angles, symmetry, layout constraints) can be silently violated or round on the wrong lattice.
- **Fix:** report residual rows; print the integrality residual over the *original* integer variables after the final solve; decide on a lattice-preserving elimination.
- **Questions:** SQ-P2.

### M14. Output fidelity and determinism of extraction (new)
- **Code [V]:** final smoothing is a uniform, non-tangential Laplacian projected onto T, not M; no feature handling; points merge on an absolute 1e-5 grid; face search order follows unordered containers; only the largest connected component survives.
- **Problem [I]:** creases rounded and chamfered; fidelity measured against an already smoothed proxy; robustness depends on model units; results can differ between platforms; parts that extraction splits off are deleted.
- **Fix:** project onto M (feature-aware, tangential smoothing); scale-normalise the input or use relative tolerances; ordered iteration; report rather than delete extra components.
- **Measured [M]:** small islands vanish. On human_neutral (body + 2 eyes), decimation shrinks each eye from 1088 to 84 triangles at 10k and they don't come back as quads: 2 of 3 components survive at 2k, 1 at 10k, 3 at 50k. Which step drops them was not traced (TODO 2.14). Determinism: see P8. Unit dependence untested.

### M15. Real boundaries get capped (new, baseline)
- **Code [V by baseline]:** `QuadExtractor::fixHoles` (`quadextractor.cpp:1295`) closes every output boundary loop of ≤ 65 edges, whether or not the input has a boundary there.
- **Measured [M]:** an open boundary survives only if it is long relative to the quad size. The hand's wrist (128 input edges) is capped at 2k and 10k; Demosthenes' open base at 2k and 10k; the human's only boundary loop at every target. The hand's 8.3 % diag Hausdorff comes from this. The parametrized mesh still has the boundary, so this is the extractor alone.
- **Fix:** fill only loops that do not correspond to an input boundary (map output boundary loops to input boundary loops first) (TODO 2.12).

### M16. Output is not always a valid 2-manifold (new, baseline)
- **Measured [M]:** 20 of 54 runs have non-manifold edges, in every input class; hard-surface and thin-shell inputs have the most (16–32 edges; sword 50k worst). 4 runs on closed inputs have holes (csg_bracket 2k and 50k, sword 10k and 50k). 11 runs per target contain faces with scaled Jacobian ≤ 0.
- **Problem:** a non-manifold or holed result breaks downstream tools (subdivision, UVs, sculpting) and Blender operators.
- **Traced 2026-10-15 [M]:** the non-manifold edges came from `fixHoles`, not from the cleanup passes. Its second, unscored `fixHoleWithQuads` pass ran on holes the first pass had already closed at 4 vertices and added the closing quad again: a duplicate face with 4 non-manifold edges. Fixed (PR "do not fill the same hole twice"): 26 of 90 corpus runs (18 meshes + Suzanne 1–3, 2k/10k, Fast and Quality) had 4–36 such edges, all now 0; boundary edges unchanged in every run. The holes in closed inputs are a separate defect and remain.
- **Traced 2026-10-15, holes and genus on closed inputs [M]:** on master `6f68495` (18 meshes, 2k/10k, Fast and Quality, 72 runs) 1 closed run had a hole (sword 2k Fast), 19 had the wrong Euler characteristic and 10 a pinched vertex (two fans meeting at one vertex). Topology is set by `extractMesh` plus the repair block after it (`fixHoles`, keep the largest piece, drop faces at non-manifold vertices, `fixHoles` again); the 12 cleanup passes never change χ (per-step probe on 4 runs). The integer-grid map is not injective on these inputs (5–8 % of T flipped, ~2 % degenerate on csg_bracket and sword 2k), so `extractMesh` leaves 40–80 holes and 30–100 pinched vertices per island, and every hole lies at a flipped triangle. Two defects in the repair turned that into holes and pinches in the output, both fixed (PR "close every hole, remove every pinch"):
  - `fixHoleWithQuads` refuses any quad that crosses the rest of the loop or reuses an edge, so on a loop bent around a fold or corner both passes gave up and the hole stayed open. Such a loop is now closed by a fan of quads around a new centre vertex.
  - `removeNonManifoldFaces` found a non-manifold vertex only by counting > 2 open edges at it, so a pinch whose second fan was already closed by the first `fixHoles` survived to the output. It now checks the fans around every vertex.
  - Result: 61/72 runs byte-identical; holes on closed inputs 1 → 0, runs with pinched vertices 10 → 0, wrong χ 19 → 14. Thin torus (r 0.05) at 150 quads keeps its handle (Hausdorff 61 → 3.9 % diag).
- **Still open [M]:** the 14 remaining runs (csg_bracket ×4, sword ×3, demosthenes ×3, boat, nefertiti, plane, human_neutral, all with flips) have extra or missing handles that are already in the extracted faces: on csg_bracket 2k Quality they have genus 5 before any repair (input 3), and dropping the faces touching flipped triangles brings it to ≤ 3. Each extra handle sits in one fold (csg_bracket: the bottom rims of the two bolt holes). `extractMesh` accepts short cycles of the isoline graph as faces instead of tracing the faces the graph bounds on T, so in a fold it can pick cycles that are not faces. Fixes: an injective map (4.1/4.5, 3.3), or face tracing in the extractor (6.1).
- **Measured 2026-10-21 by intervention [M]** ([research/genus-diagnosis-2026-10-21.md](research/genus-diagnosis-2026-10-21.md)): master 4d15d5f, corpus + closed set with 5 float32 replicates. 20 runs have a wrong replicate, 75 replicate-runs in all. A probe build hands the same map to either extractor: with the face-tracing prototype 64 of the 75 come out right (all boat, csg_bracket, sword, thin_leaf, torus and human_neutral 2k failures, 13 of 24 demosthenes), and 5 others break (human_neutral Fast 10k's mouth splits). So `extractMesh` causes them, not the map. The 11 left (demosthenes, three scan tunnels of 0.6–3.2 quad edges) have an undetermined cause. Extra handles sit within 1 quad edge of a fold or cone in 53 of 53 cases, against 23 % of random output faces; 37 of 53 are 3-edge loops. The map holds the tori's lost handles: holonomy 2–4 grid units around the meridian.
- **Fix:** manifold and hole check in the health counters (TODO 2.3); 6.2's validity-guarded worklist should make it structural.

---

## 6. Blender integration notes

Current bridge (adriflex) **[V]**:
- Exports the active mesh to a temp OBJ (optionally with modifiers), runs `subprocess.run(...)` synchronously, imports the OBJ as a new object, copies material slots, optional UV transfer (data transfer from source), reads `--report` for stats.
- Exposes: target quads, adaptivity, edge scaling, sharp edge, smooth normal. **Not** anisotropy.
- Finds the executable via prefs → `AUTOREMESHER_PATH` → `PATH` → `/Applications/autoremesher.app`.
- Blender ≥ 4.2 extension (`blender_manifest.toml`), tested on 5.2.

Integration options, long-term view:

| Option | Pros | Cons |
|---|---|---|
| A. Keep CLI + OBJ (current) | Zero C++ work | UI freezes; slow IO; no way to pass per-vertex data (density paint, guides, face sets) except via OBJ hacks |
| B. CLI + binary stdin/stdout protocol + modal operator | Async, no temp files, can stream extra attributes | Must maintain a protocol; still a separate process (good: crash isolation) |
| C. Python extension module (nanobind/pybind11) of the Qt-free core, shipped as platform wheels inside the Blender extension | Fastest, numpy arrays straight from `mesh.attributes` | Must build per OS/Python version; a crash takes Blender down |

**Recommendation [I]:** use the jakeblakeley pattern: the core as a nanobind module, run in a **subprocess** of Blender's Python, with numpy `.npz` data in and out. Crash isolation, in-memory-speed IO, per-vertex attributes pass through easily. One more reason for the subprocess: Blender bundles oneTBB 2022.3.0 as a shared library **[P]**, and loading a second libtbb into Blender's process could clash **[I]**; static oneTBB in a worker process avoids it (perf §2.4).

### 6.1 jakeblakeley/autoremesher-blender (reviewed 2026-10-14) **[V]** unless marked

What it is: a fork of upstream at `061f740a` (2026-07-06, **v1.0.0, still Geogram-based**). All 15 fork commits are from 2026-07-07, with no activity since. The README says it was only tested on macOS Apple Silicon and that the Windows and Linux builds are "untested, vibe-coded". Upstream has since replaced Geogram (v1.1) and added extraction cleanups (v1.2), so the fork's core is two releases behind.

What it changes (`git diff 061f740a HEAD`, about 1.7k lines added excluding thirdparty):
- **Build/packaging (the valuable part):** `CMakeLists.txt` builds a Qt-free core as a Python module `autoremesher_core` with **nanobind** + scikit-build-core (`pyproject.toml`). oneTBB fetched and linked statically, stable-ABI wheels (cp312) plus cp311 for Blender 4.2–5.0, `cibuildwheel` on Linux/Windows/macOS in CI, `scripts/package_extension.py` for per-platform extension zips, a headless Blender smoke test (`tests/test_extension.py`).
- **Blender add-on (`blender_addon/`, GPL):** N-panel UI; exports with `foreach_get` into numpy, writes a `.npz`, runs `remesh_worker.py` in a **subprocess of Blender's own Python** that imports the native module. Progress streamed as `PROGRESS x status` lines; modal operator with Esc to cancel, so a native crash can't kill Blender. Result returned as n-gons via `face_sizes`.
- **Core changes (`autoremesher.cpp` only, about +400 lines):** input scale normalisation to a unit box; "Preserve thin features" clamps the isotropic target length by local feature size from **Geogram's LFS/medial-axis poles** with an 8-NN search; a per-island detail floor; retries for failed islands and a triangle fallback; an `isIslandResultHealthy` check (non-finite positions, valence > 50); crash fixes it reports for 1.0.0 (use-after-free in parallel islands, n-gon out-of-bounds, an assertion turned into a skip).
- **Weld shells:** done Python-side with Blender's voxel Remesh modifier before remeshing.
- **Not touched:** parameterizer, field, rounding, extraction. None of M1–M14 is addressed.

Verdict: **borrow parts, don't build on it.**
- Borrow: the CMake/nanobind/cibuildwheel/extension pipeline and the subprocess-worker add-on. Port onto current upstream (same API: vertices + triangles in, quads out).
- Borrow ideas: island detail floor, health check, weld-shells option, scale normalisation (check whether upstream 1.2 already fixed the same crashes). For thin features, use a Shape Diameter Function cap in the unified sizing field (M7) instead of re-vendoring Geogram's LFS.
- Don't build on its core: stuck on 1.0.0, one day of work, unmaintained, tested on one OS.
- Licensing: core MIT, add-on GPL-3.0+, so copying the add-on is fine.

Blender data to feed the core (all via `bpy`/`mesh.attributes`): sharp edges (`sharp_edge`), creases (`crease_edge`), seams, face sets / material indices, vertex groups or color attributes (density paint), Grease Pencil / annotation strokes or curve objects (guides), mirror axis from the Mirror modifier.

---

## 7. ZRemesher-like features

Effort: S (days), M (1–3 weeks), L (1–3 months). Risk = chance it doesn't deliver at ZRemesher quality. Updated with size and spec (RECONCILIATION D4–D6).

| # | Feature | What ZRemesher does | How in AutoRemesher | Effort | Risk |
|---|---|---|---|---|---|
| F1 | **Respect polygroups / face sets / materials** | "Keep Groups": loops follow polygroup borders | Label borders → `FeatureEdges` (M8, M11); always-hard lock, sector rule, integer isolines | S wiring, M quality | Medium until M1 Stage A and M11 land |
| F2 | **Use Blender sharp/crease/seam marks** | Creases kept | Same as F1, input from the bridge | S wiring, M quality | Medium (same) |
| F3 | **Density painting** | "Use Polypaint" | Per-vertex log₂ multiplier into the single sizing field before gradation (M7), then the M3 projection | S wiring | **Medium**: painted borders force poles unless graded and projected (M3) |
| F4 | **Guide curves** | ZRemesherGuides brush (soft: one forum report says guides are ignored even at full strength **[E]**) | Soft: stroke tangent as alignment target in the power field (RHS change). Hard: embed the curve, lock the field, integer isoline, sector rule at corners and crossings | S–M soft, M–L hard | Low soft; Medium–High hard (strip collapse under rounding) |
| F5 | **Mirror symmetry** | X/Y/Z symmetry | Symmetric **triangle** remesh (half, mirror, weld), field constraint z_σ(f) = conj(z_f), param constraint f∘σ = A f + t with joint rounding of σ-pairs, pair averaging after extraction. Not cut-mirror-weld of the output | M | Medium |
| F6 | **Exact target count** | Approximate (documented) | Secant on log s with the field reused (±1–2 % in 2–4 cover solves), then strip (chord) knapsack, then ±1 local ops. Parity: mirror edge-loop-on-plane mode forces even F | S controller, M exact | Low / Medium |
| F7 | **Half / double, subdivision-ready** | "Half", "Same", "Double" | Remesh at target/4, Catmull-Clark once, project to source | S | Low |
| F8 | **Feature-line graph** | Strong hard-surface loops | Detected ridges/valleys + sharp edges as a curve network with corner singularities (QuadWild-like); soft crest lines for bevels | L | Medium–High |
| F9 | **Pole placement control** | Poles in sensible spots | M5 energy + user "pole budget" | L | High (research) |
| F10 | **Partial remesh / freeze border** | "FreezeBorder", remesh selection | Island = selection, boundary fixed and copied, stitch. Needs boundary support in any Stage B quantizer | M | Medium |
| F11 | **Detail-aware field** | Follows small forms | M4; "detail" slider = k | M | Low–Medium (time) |
| F12 | **Edge loops around holes / tubes** | Clean loops round limbs, eyes, mouth | Anisotropy from tube detection and concentric field near boundaries; closed-loop constraints (SQ-F3) | M–L | Medium |
| F13 | **Cached field for fast re-runs** | Interactive feel | Cache field + singularities; density changes re-run sizing projection + cover + extract only | M | Low |
| F14 | **Attribute transfer** | Keeps UV/polypaint via projection | Bridge does UV; add color attributes, custom normals | S | Low |
| F15 | **Quality report in Blender** | – | Metrics from TODO 1.4: poles, % quads, flipped faces, Hausdorff | S | Low |

Baseline for F5 and F6 **[M]**: today's output has no symmetric layout at all on exactly symmetric inputs (§4.1), and the count misses the target by 2–3× (M7).

Order by value (size §0): one sizing field (M7) → size into the param (M3) → features F1/F2/M8 → exact count F6 → symmetry F5 → soft guides → hard guides; intrinsic symmetry deferred. Hard features, hard guides and symmetry add integer constraints, so they wait for M1 Stage A. All per-vertex/edge inputs need the npz worker protocol (§6).

---

## 8. Reference papers and code

Surveys
- Bommes, Lévy, Pietroni, Puppo, Silva, Tarini, Zorin. *Quad-Mesh Generation and Processing: A Survey.* CGF 2013.
- Vaxman et al. *Directional Field Synthesis, Design, and Processing.* CGF 2016 (STAR).
- Campen. *Partitioning Surfaces into Quadrilateral Patches: A Survey.* CGF 2017.

Fields and singularities (stages 6–7)
- Ray, Vallet, Li, Lévy. *N-Symmetry Direction Field Design.* TOG 2008.
- Crane, Desbrun, Schröder. *Trivial Connections on Discrete Surfaces.* SGP 2010.
- Knöppel, Crane, Pinkall, Schröder. *Globally Optimal Direction Fields.* SIGGRAPH 2013.
- Beaufort, Lambrechts, Henrotte, Geuzaine, Remacle. *Computing cross fields: a PDE approach based on the Ginzburg–Landau theory.* 2017. Viertel, Osting. *…harmonic cross-valued maps and the Ginzburg–Landau theory.* SIAM J. Sci. Comput. 2019.
- Cohen-Steiner, Morvan. *Restricted Delaunay triangulations and normal cycle.* SoCG 2003 (curvature tensor).
- Diamanti, Vaxman, Panozzo, Sorkine-Hornung. *Integrable PolyVector Fields.* SIGGRAPH 2015.
- Panozzo, Lipman, Puppo, Zorin. *Fields on Symmetric Surfaces.* SIGGRAPH 2012.
- Coiffier, Corman. *The Method of Moving Frames for Surface Global Parametrization.* TOG 2023 (MIT code).
- Corman, Crane. *Rectangular Surface Parameterization.* TOG 44(4), 2025.
- Couplet, Chemin, Bommes, Chien. *Surface Quadrilateral Meshing from Integrable Odeco Fields.* CGF (SGP) 2026.
- Du, Fang, Liu, Fu. Integer-constrained cones, arXiv 2512.20904.
- Soliman, Slepčev, Crane. *Optimal Cone Singularities for Conformal Flattening.* 2018.

Sizing (stages 3, 5, 9; M3, M7)
- Dunyach, Vanderhaeghe, Barthe, Botsch. *Adaptive Remeshing for Real-Time Mesh Deformation.* EG 2013 short (chord-error sizing).
- Persson. *Mesh size functions for implicit geometries and PDE-based gradient limiting.* Eng. Comput. 2006.
- Shapira, Shamir, Cohen-Or. *Shape Diameter Function.* 2008.
- Ben-Chen, Gotsman, Bunin. *Conformal Flattening by Curvature Prescription and Metric Scaling.* EG 2008.
- Springborn, Schröder, Pinkall. *Conformal Equivalence of Triangle Meshes.* SIGGRAPH 2008. Gillespie, Springborn, Crane. *Discrete Conformal Equivalence of Polyhedral Surfaces* (CEPS). 2021.
- Myles, Zorin. *Global Parametrization by Incremental Flattening.* SIGGRAPH 2012; *Controlled-distortion constrained global parametrization.* SIGGRAPH 2013.

Parametrization, rounding, quantization (stages 13–16; M1, M2, M12, M13)
- Kälberer, Nieser, Polthier. *QuadCover.* EG 2007.
- Bommes, Zimmer, Kobbelt. *Mixed-Integer Quadrangulation.* SIGGRAPH 2009.
- Bommes, Campen, Ebke, Alliez, Kobbelt. *Integer-Grid Maps for Reliable Quad Meshing.* SIGGRAPH 2013.
- Myles, Pietroni, Zorin. *Robust Field-aligned Global Parametrization.* SIGGRAPH 2014.
- Campen, Bommes, Kobbelt. *Quantized Global Parametrization.* SIGGRAPH Asia 2015.
- Campen, Shen, Zhou, Zorin. *Seamless Parametrization with Arbitrary Cones for Arbitrary Genus.* TOG 2019.
- Campen et al. *Efficient and Robust Discrete Conformal Equivalence with Boundary.* SIGGRAPH Asia 2021 (ConformalIdealDelaunay, MIT).
- Lyon, Campen, Kobbelt. *Quad Layouts via Constrained T-Mesh Quantization.* EG 2021.
- Shen, Zhu, Capouellez, Panozzo, Campen, Zorin. *Which Cross Fields can be Quadrangulated?* SIGGRAPH 2022.
- Heistermann, Warnett, Bommes. *Min-Deviation-Flow in Bi-directed Graphs for T-Mesh Quantization.* SIGGRAPH 2023 (libSatsuma, MIT).
- Coudert-Osmont, Desobry, Heistermann, Bommes, Ray, Sokolov. *Quad Mesh Quantization Without a T-Mesh.* CGF 2024.
- Capouellez, Zorin. *Seamless Parametrization in Penner Coordinates.* SIGGRAPH 2024; Capouellez et al. *Feature-Aligned Parametrization in Penner Coordinates.* SIGGRAPH 2025.
- Garanzha et al. *Foldover-free maps in 50 lines of code.* SIGGRAPH 2021. Rabinovich et al. *SLIM.* 2017.
- Ebke, Schmidt, Campen, Kobbelt. *Interactively Controlled Quad Remeshing of High Resolution 3D Models.* SIGGRAPH Asia 2016.

Extraction and cleanup (stage 17; M10, M14)
- Ebke, Bommes, Campen, Kobbelt. *QEx: Robust Quad Mesh Extraction.* SIGGRAPH Asia 2013 (libQEx, GPL-3.0).
- Ray. *On Quad Mesh Extraction From Messy Grid Preserving Maps.* arXiv 2507.15404, 2025.
- Bommes, Lempfer, Kobbelt. *Global Structure Optimization of Quadrilateral Meshes.* EG 2011.
- Reberol, Georgiadis, Remacle. *Quasi-structured quadrilateral meshing in Gmsh.* 2021.
- Tarini et al. 2010, Bozzo et al. 2010, Daniels et al. 2008/2009 (local quad operators, poly-chord collapse).

Full pipelines to compare against
- Jakob, Tarini, Panozzo, Sorkine-Hornung. *Instant Field-Aligned Meshes.* SIGGRAPH Asia 2015.
- Huang, Zhou, Funkhouser, Niessner, Guibas. *QuadriFlow.* SGP 2018 (Blender's built-in remesher).
- Pietroni, Nuvoli, Alderighi, Cignoni, Tarini. *Reliable Feature-Line Driven Quad-Remeshing (QuadWild).* SIGGRAPH 2021.
- Fang, Xu, Desbrun, Liu, Tong. *Quadrangulation through Morse-Parameterization Hybridization.* SIGGRAPH 2018.

Remeshing (stage 4)
- Botsch, Kobbelt. *A Remeshing Approach to Multiresolution Modeling.* SGP 2004.

Learned methods (deferred): NeurCross (TOG 2025), CrossGen (SIGGRAPH Asia 2025), TopGen (2026), CrossLift (2026), Dielen et al. (SGP 2021). See cross §3.5, param §3.6.

Test meshes: Thingi10K (CC0/CC-BY subset), Objaverse (CC0/CC-BY), Smithsonian CC0, common-3d-test-models (per-source licenses), Stanford repository (download at test time), plus Kajetan's own ZBrush sculpts (most representative). QuadWild300 and MAMBO for comparability with the literature. See LICENSES.md §5.

ZRemesher background: ZRemesher and Exoside Quad Remesher are reported to share an author (Maxime Rouca, per CG Channel 2019 **[E]**); neither is published, so comparisons are black-box on metrics. All commercial tools document their quad count as approximate **[P]**.

---

## 9. Corrections applied from code-to-math-spec §23

All 21 items were applied on 2026-10-14. "Where" names the place in this file.

| # | Correction (short) | Where |
|---|---|---|
| 1 | Stage 3 budget preserved only before the clamp; law h ∝ 1/κ | §3 row 3, M7 |
| 2 | Remesher: 3 iterations, no feature check on flips, featured vertices relaxed but not projected, ray projection | §3 row 4, M11 |
| 3 | Field: curvature target only in the first of 5 solves; last crease wins | §3 row 6, M5, M6, M8 |
| 4 | Simplifier: any pair summing to 0 mod 4; signed sharp test; topological acceptance | §3 row 7, M5, M11 |
| 5 | Anisotropy exponent is the classical law | §3 row 8, M9 |
| 6 | Conformal solve: only constant size freedom with fixed cones; field must be harmonic | §3 row 9, M3 |
| 7 | 40-iteration `smoothCrossField` skipped | §3 row 10 |
| 8 | Curl correction objective is exact integrability; the solve is the heuristic part | §3 row 12, M3 |
| 9 | Seam = complement of a BFS tree over r = 0 edges, plus boundary, pruned | §3 row 12 |
| 10 | Period 2 ⇒ translations in 2ℤ², cones on ℤ², over-restriction; world-unit energy; redundant wheel rows; no cone constraints | §3 row 13, M12; TODO open question closed |
| 11 | Pass 3 result ignored; integrality not guaranteed in general | §3 row 14, M13 |
| 12 | Linear solver confirmed; KKT fallback not reached | §3 row 16 |
| 13 | Quad size λ·ℓ̄_T·m_f·ρ_f^{±1}; ≈0.87 Q; two size fields disagree locally | §3 rows 0, 13, data-flow 3, M4, M7 |
| 14 | Exactly 12 passes; uniform Laplacian; projection onto T; absolute 1e-5 grid; largest component only | §3 row 17, P3, M10, M14 |
| 15 | M6 umbilic claim was wrong | M6 |
| 16 | M8 confirmed; one lock per face | M8 |
| 17 | M1 confirmed; two factorisations; 0.01 snap after the solve | §3 row 15, M1 |
| 18 | P3 extended: more one-op passes, AABB rebuild per pass | P3 |
| 19 | Islands: half-edge adjacency only | §3 row 1 |
| 20 | Decimation priority is soft; error bound FLT_MAX | §3 row 2 |
| 21 | Stage 4 feature flags discarded; three different sharp tests | data-flow 2, M11 |
