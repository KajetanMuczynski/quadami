# Known issues after 0.2.0: fixable, partly fixable, no solution yet

Snapshot of 2026-10-28 for v0.2.0 (`57c19f9`). Every known issue sorted into three classes: **A** a fix
is known, **B** not fixable or only partly, and why, **C** no solution found yet. Built from the notes,
nothing re-measured: [TODO.md](TODO.md), the 0.2.0 known limits ([releases/0.2.0.md](releases/0.2.0.md)),
[ENVELOPE.md](ENVELOPE.md), [USAGE-COVERAGE.md](USAGE-COVERAGE.md), [RAISE-TO-CLEAR.md](RAISE-TO-CLEAR.md) and
the research notes they cite (gap analysis, both audits, measurement audit, hand failures, defect ledger,
crease preservation, primitives, cone tip, thin cap, target guidance, hangs, sanitizer, platform
divergence), plus the state of the threads running on 2026-10-28 (crease remesh 2.5, sanitizer
follow-up, nemetona) and a first code read of other open-source remeshers.

**This is a snapshot, not a second plan.** TODO.md stays the plan and the place to update; this file is
not kept current. Item numbers (2.5, G14, …) are TODO's.

**Labels.** Evidence carries over from the source: **[M]** measured (6 replicates unless the source
says otherwise), **[C]** checked (a second route, or read in the code), **[I]** inferred. **[C here]**
means this review read the code at `57c19f9` itself. *Unconfirmed* marks a single run. The class of
each row is a judgment from that evidence [I]; the *Falsified if* column says what would move it.
Difficulty: XS hours, S days, M 1–3 weeks, L months. Release: **0.3 (decided)** = Kajetan's 0.3 list
of 2026-10-28; everything else is a proposal.

## 0. In short

- **A, fixable: 31 rows.** Most of the quality upside left is here: creases kept through the remesh and
  the extractor (2.5, 2.8), cone tips (2.23), folded-input repair by default (2.20), the hangs on many
  thin pieces (2.30), small parts kept by a local retry (G18). Several rows are designed but never
  tried (marked *designed*); they are fixable in the sense that the mechanism is known, not proven.
- **B, not or only partly fixable: 12 rows.** Almost all are one bound: **quads per feature**. A ring of
  quads needs about 2.5 quads around, two isolines need about one quad between them, and at a fixed
  Target Quads every quad given to a thin part is taken from somewhere else. What can be fixed there is
  what the user is told and how quickly they find the count that works, not the result at the count
  they asked for. The rest are external: libQEx needs a valid map, Blender's extension rules, signing,
  generating modifiers.
- **C, no solution found yet: 15 rows.** The biggest is thin parts inside the main body when they are a
  small share of it (C1); the rest are untraced causes (eyeball caps, flat cards, CAD cylinders), the
  icosphere cones (C5, now traced to the field solve, with a candidate fix) or problems owned by the large Stage B work (C2, C4).
- The defect ledger's split still holds as the frame: 91 % of the defect score on our sets sits at
  input triggers, folded input 28 %, tips 29 %, parts thinner than a quad 34 % [M, 2026-10-22, before
  libQEx's later fixes and #82]. Tips and folds are mostly A; thin parts are B and C.

### Where "unfixable" is really "no way found yet"

These are easy to file as limits and should not be:

1. **Thin parts in the main body** (C1). The budget argument (∫ 1/h²) makes the sword's vines a true
   limit at 3,000 quads: they are 56 % of its area. It does not make a thin part that is 2 % of the area a
   limit; there the local cap failed because of how it took quads from the rest, not because the quads
   were not there [I]. Only two forms of one design were tried.
2. **Swirls flattened by Repair Folds** (C3). The overlapping triangles cannot survive in a valid mesh,
   but the relief around them could; nobody has designed a repair that keeps it.
3. **Handles 2 to 3 quads around** (C4). ENVELOPE files them under the 7.2 quantization work, which is
   an owner, not a proof that they are lost.
4. **Raising Target Quads loses parts again at a higher count** (C12, B7). Some of it is the discreteness
   of rounding [I], but several relapses are deterministic in every replicate (vaseflowers clear at 6×,
   lost at 8×), and their mechanism was never traced.
5. **Different output per platform** (A18) is fully fixable (2.9b); it is cost, not a limit.

## 1. A: a fix is known

*Prototype* = a branch or probe measured it; *designed* = mechanism known, never tried.

| # | Issue (what the user sees) | Evidence | Fix, difficulty | Release | Falsified if |
|---|---|---|---|---|---|
| A1 | **Runs for many minutes on many tiny thin pieces** (iron gate at 3,300 and 4,000 quads, over 15 min; nemetona at 3k/10k, over 30 min) | Iron gate [M, `ebab7db`]: islands fall back to our tracer, one fallback mesh about 763k polygons at target 3,300, then `cleanupTriangles` [C]. Nemetona: same function on both stuck runs (gdb, 57c19f9; nemetona thread, in progress, *unconfirmed*). [C here]: the loop rebuilds a `std::map` of every edge of the mesh on each pass (`quadextractor.cpp:4374`) | Fail an island whose fallback mesh is far over its target (like libQEx's grid cap) and make the edge map incremental (2.30). S. Instant Meshes' extraction (union-find collapse) is a linear model for the second part [I, code read] | 0.2.1 | An incremental cleanup still takes minutes on iron gate u0/u1/u5, or nemetona stalls somewhere else |
| A2 | **No time or memory limit for the worker**: a hang ends only with Esc | [C] (add-on has no automatic timeout) | Limit scaled to the input and target estimate (memory model 0.43 KB/triangle + 18 KB/quad, [M] #141), with a plain message. S. Needed before any multi-run search (A23) | 0.2.1 | A limit cannot be set without killing legitimate long runs (5.2M triangles at 50k: 209 s [M]) |
| A3 | **Smooth Normal (not default) tears boxes and cylinders apart**: cube at 60°, 5k: 9–17 holes, 345–427 non-quads, 140–184 s against 5 s | [M] usage audit. [C here]: a face is left out of a vertex normal by its angle to the vertex's average normal, not by the dihedral angle (`isotropicremesher.cpp:~117`); a box corner face is 54.7° off the average | Exclude by the dihedral angle between neighbours (2.25). S. Default output unchanged (Smooth Normal off) | 0.2.1 | With dihedral exclusion the cube at 60° still gets holes |
| A4 | **Wrong or missing warnings at very low counts and with Edge Scaling**: "far fewer quads than planned" at Edge Scaling 4; no warning on a cube at Target Quads 6 in 3 of 6 runs | [M] usage audit | Compare with the effective target (Target Quads / Edge Scaling²); warn below the count a closed island needs (2.26). S, report only. The shapes themselves: B6 | 0.2.1 | – |
| A5 | **Openings of open parts change with no warning** (Rain: a gum slit, jeans waist, hair ribbon, body opening at 3k, 6/6; the Holes row says 0) | [M] hand failures | Boundary loops per open island in the report and panel (2.3, stress G8). S. Keeping the openings: C9 | 0.3 | – |
| A6 | **Panel says nothing about why Remesh is greyed** (Sculpt/Edit Mode, curves), remeshes only the active object of several, silently skips edits on a result, drops constraints and animation, likely empties a local view | [M] in bpy on the add-on path; local view [I] (G16) | One message or fix each. XS–S | 0.2.1 (messages), 0.3 | – |
| A7 | **Modifiers are baked in silently** (Mirror, Subdivision, Armature pose) | [C] (G14) | (a) list them before and after the run, S; (b) keep Mirror, Subdivision and deforming modifiers per type (remesh the half / a fitted cage / the rest shape), M, *designed*. Generating modifiers: B8 | 0.3 (decided) | (b): the remeshed half does not weld at the plane, or the Subdivision cage cannot be fitted to the visible surface |
| A8 | **Non-uniform object scale stretches the quads** (warned since #110) | [M] world aspect p90 4.34 vs 2.08 applied | Remesh in world scale, give the result location and rotation only (G11). S | 0.3 | – |
| A9 | **Blender freezes while the input is handed over** (17.4 s at 5.2M triangles) | [M] gap analysis | Raw arrays instead of OBJ (G2a): 0.53 s for the same mesh [M]; also the carrier paint and guides need. S–M | 0.3 or with 5.8 | – |
| A10 | **Hard-surface edges rounded** (cube edge loop 90 % at 5k, 72 % at 1k; cone base rim mostly rounded): the triangle remesh keeps only 4–35 % of the crease length | [M] crease note, primitives note. Fix on a branch (thread "Preserve creases in triangle remesh", 2.5): cad, primitives and closed better against master ×6, brush fixed except box_relief 5k folds, a sculpt regression found and fixed; final lane running [M on the branch, not reviewed yet] | pmp-library rules (no flip across a chain, collapse only along it, slide and project) plus a noise filter for sculpt pleats (2.5). M, *prototype*. QuadriFlow and Instant Meshes only split the input near features, so creases survive by construction [I, code read]. Measured at matched counts: QuadriFlow follows the cone's base rim better (coverage 0.21–0.38 vs 0.01–0.15), the cube about the same (0.94–0.98 vs 0.72–0.91), cylinders worse (0.44–0.58 vs 0.74–0.81) [M, comparison thread] | 0.3 (decided) | The final lane shows the sculpt sets worse beyond replicate noise, or rims still round at 5k |
| A11 | **The extractor's smoothing rounds creases again** (box: isolines on creases 0.99, quads 0.41) | [M] crease note; dropping the step keeps creases but folds 5 → 47 [M] | Slide chain vertices along T's feature chains instead of 3D Laplace (2.8). S–M, *designed*. QuadriFlow `-sharp` resamples crease vertices evenly along the crease polyline and lets them slide only along it (`optimizer.cpp`) [I, code read] | 0.3, after 2.5 | Chain sliding keeps creases but costs folds like dropping the step |
| A12 | **Rows of thin quads along rims** (84–98 % of the quads within 2 h of a cylinder rim at 3k–5k) | [M, two routes]: the sizing field reads the bevelled crease in T as curvature | Leave creases out of `computeFaceScalingField` once T keeps them (2.5). S. Only after A10: today the rows are what keeps the rim sharp (without them rims round 3–5× more [M]). QuadriFlow at matched counts has no such rows (thin-quad share 0.00–0.05 against our 0.54–0.99) and better quad shape on the cad set (SJ p1 0.83 vs 0.60, skew p95 7.8° vs 29°), at 9× our p99 distance and equal crease coverage [M, comparison thread, 6 replicates] | 0.3, with 2.5 | Rims round more once the rows go, with T keeping creases |
| A13 | **Cone tips cut off** (apex 0.77–1.18 h from the output at every target) | [M, disprover]. #135 keeps the tip within 1e-14 h, Hausdorff 1.1–15.6× lower [M]. Open: cone_v64 5k genus change 2 of 21, realsculpt folds 30 → 38, sword_vines 6k Hausdorff 0.0146 → 0.052 in 21 of 21 after merging master [M] | Pin detected tips in T, no curvature guidance around them (2.23). M, *prototype* with an open regression. No other tool keeps the apex (tip gap QuadriFlow 0.86–1.85 h, Instant Meshes 0.66–1.0 h, ours 0.26–1.49 h), and our build with guidance off still misses it by 0.74–1.38 h [M, comparison thread], so pinning is the only route found | 0.3 (decided), after 2.5 | The vines loss comes from tip pinning itself, which would need a different design |
| A14 | **Marks or creases crossing at acute angles fold**, then fall back to no marks (30° wedge at 1k: coverage 0.49 → 0.16; diagonal loop over a cube dropped) | [M] Follow Marks A/B | Sector rule at feature corners (5.2). M, *designed* | later | – |
| A15 | **Folds, non-quads and holes next to folded sculpt input** (head: holes 6/6 plain at 35k and 50k) | [M] 96–100 % of these defects within 2 quads of folded input; **Repair Folds**: 0 of 24 runs with holes, folds 2–7× fewer, up to 18 non-quads at 50k [M] | Repair by default on closed sculpts (2.20) once it has a per-spot shape-change readout and repairs only spots that leave a defect (2.20a (a), *designed*). M. The fold itself: B4 | 0.3 (decided) | Repairing only defect spots still flattens the head's swirls at 35k (then C3 blocks the default) |
| A16 | **Fold detection costs 10 % of a 2M-triangle run** (budget 5 %) | [M] nefertiti | Run it on the simplified mesh or alongside the remesh. S, *designed* | 0.3, with A15 | – |
| A17 | **Dragon: welding joins the wings to the body, libQEx then fails on the big island** (3k: 65–91 non-quads in 3 of 6 runs instead of 2) | [M] sanitizer follow-up: libQEx fails at about 4 of 10 counts on the welded island | Re-solve a libQEx-failed island at ±2 % count and keep the first libQEx result: 4 of 5 failing counts recovered (prototype `dcd8085`, *unconfirmed*: lane and ×6 A/B queued) | 0.3 | The ×6 A/B shows new fallbacks or folds elsewhere |
| A18 | **The same input gives different results on Linux, Windows and macOS** | [M] 0 of 1,800 run keys byte-equal; four causes, each switched off by an intervention [C] | Tie-broken simplifier sort (2.9a, S), then one math library, `-ffp-contract=off` and scalar Eigen on arm64, ordered containers (2.9b). M | 0.3 (decided) | After all four, the runners still differ |
| A19 | **Release gate fails neutral changes** (same build on two platforms: 24 of 24 comparisons fail) | [C] platform study | Per-case sign test pooled over platforms (1.16): 2 of 120 false alarms, still finds #82 [C]. S | 0.3 (decided) | – |
| A20 | **Bench numbers wrong on some meshes**: per-island counts on multi-part models (28 % of the knight's parts on the wrong island), area uncovered on shattered outputs, fold verdict on islands smaller than the fold radius, V6 blind to a lost plus an added handle | [M] hand failures, measurement audit (1.13 F3, 1.14) | Slice by the core's island ranges; target-edge threshold; per-island radius cap; homology check. S each | after his metrics freeze | – |
| A21 | **CI green while output changes on public meshes** (libQEx off, small-island retry, projection, relax, fold-repair flag all pass CI) | [M] second audit | Add torus_0.03 Fast 150 and a ribbon case to the output gate, assert the extractor, gate mean distance. S | 0.3 (decided) | – |
| A22 | **Small code bugs from the audits**: island health check has no quad-count test, so a collapsed closed island is not retried; a `bad_alloc` in `resample` fails the whole run; libQEx step limit is 32-bit on Windows above about 238k quads; add-on stays busy until restart if reading the result raises | [C here] `isHealthyIslandResult` (`autoremesher.cpp:234`) checks components, non-manifold edges and Euler characteristic only; the rest [C/I] audits | One fix each. S | 0.2.1 (add-on), 0.3 (core, output-changing) | – |
| A23 | **The thin-part warning's advice chases the target**, and raising Target Quads need not clear it | [M]: the far rule uses h₀ of the current run, so a finer run finds finer parts; the advice was 1.6–2.1× the count at every count on the sword | Fixed reference width (`--lost-parts-reference`, probe byte-identical [M]) and a verified search: 1.5×, then ×1.33 until clear: every reachable case in 2.5 runs, 1.23× above the minimum [M] (RAISE-TO-CLEAR Tier B). M. Needs A2 | later (his call) | – |
| A24 | **Small separate parts lost** (bee, vaseflowers, plant, mosquito, separate capsules) | [M] keep-parts probe: 96 of 121 lost cases cleared, no new loss, 15 clean meshes byte-identical, 1.01–1.41× quads where a global raise needs 1.8–4.4× or never clears; up to 31× time on capsules that never clear | Retry only small islands that lost parts, ×1.25 up to 8×, with a time and quad budget (G18, docs PR #149). M, *prototype* | 0.3 | The full lane shows new losses, or the time budget leaves most cases uncleared |
| A25 | **Thousands of tiny pieces give far more quads than asked, silently** (knight 9.7× at 3k) | [M] hand failures, target-quads note | Warn from the core's own plan (`--plan-only`), before and after the run (crumb floor F4, G13): the rule fires on the knight (35.6×) and Rain (1.70×), not on Sprite or clean models [M]. S. The overshoot itself: B5 | 0.3 (decided) | – |
| A26 | **Uneven quad size on nearly flat parts** (Kajetan's flower cards: p95/p5 4.1 at 500) | [C] code: curvature is normalised per island; numbers one run, *unconfirmed* | Normalise curvature over the whole mesh, keep the per-island area budget (2.29). S | 0.3 | – |
| A27 | **Short handles in the main body close** (tentacle: a strand 0.6–1.05 quads around closes in 12 of 12 runs at 3k and 10k) | [M] hand failures | Shrink the size field around each shortest handle loop of the input until it is 3–4 quads (2.22 (a)), plus the retry keeping the least-bad attempt (2.22 (d)). M, *designed* | later | Local refinement does not make the map hold the handle (it is lost in the continuous solve, as on demosthenes) |
| A28 | **Keep Ridges smoothing folds faces in grooves** (swirled cube: flips +40 % at 3k) | [M] few-triangles note | Fold guard on the tangential step, then decide the default. S–M, *designed* | 0.3 | – |
| A29 | **Copied data is coarse**: material borders zigzag, marked lines under a quad dropped, up to 6 % of faces distorted by UV Maps on many-island models, shape keys and face sets not kept | [M]/[C] 0.2.0 notes | Face-set and material borders as Follow Marks sources (5.4); UV seams as marks; shape keys through Data Transfer (approximate, see B8). S each | 0.3+ | – |
| A30 | **Slower than upstream at default settings** (about 2.7× in a pilot, *unconfirmed*); progress bar off by up to 47 points; Esc ignored up to 46 s | [M] speed note; pilot head-to-head (thread running) | Rounding without full refactorisation (−26 % at 50k, estimate [I]), METIS ordering (−27 % at 50k [M], changes output), progress weights, cancel points. S–M each | later (parked by him for rounding) | The head-to-head's count-matched arm shows no gap |
| A31 | **No mirror symmetry** | [M] remesh one half at half the target, then Mirror with merge: closed, symmetric, all quads, one shape | G12 interim (S), core symmetry 5.5 (M). Guides and density paint are designed in [GUIDES-AND-DENSITY.md](GUIDES-AND-DENSITY.md) | 0.3+ | – |

## 2. B: not fixable, or only partly

The bound named in each row: **TQ** quads per feature at the count asked for, **GEO** the input has no
valid quad version, **EXT** outside our code or our control.

| # | Issue | Evidence | Why not (bound) | What can still be done | Falsified if |
|---|---|---|---|---|---|
| B1 | **Parts thinner than about a quad fold or vanish** (gone below about 0.4 h₀, at risk to 0.8 h₀; sword vines at 3k) | [M] stress sweep (ENVELOPE row 17). Every tool we compared folds on parts thinner than a quad: 13 of 54 mesh–count pairs have no fold-free tool [M, 1.8] | **TQ.** A ring of quads needs about 2.5 quads around; at a fixed Target Quads refining thin parts coarsens the rest (sword 3k: 29 % of the surface thin, a cap at h = d/2 needs about 1.7× the quads [M/I]) | Small islands: A24. Main body: C1. Everywhere: a warning that says it is a Target Quads limit (#138) and a search for the count (A23) | A remesher keeps the sword's vines at 3k fold-free with the same quad count |
| B2 | **Handles narrower than 2 quads close** (demosthenes' scan tunnels at every count from 2k to 40k) | [M] H ≈ 1: 2ℤ² rounds to 0, ℤ² to 1, no ring | **TQ.** No lattice gives a ring of 3 without stretching the map by more than half (ENVELOPE §3.1) | Say which count keeps them (≈ N·(3/H)²) | A valid map with a ring of 1–2 quads keeps the tunnel open |
| B3 | **Hard-surface creases closer than half a quad merge** (78–86 % of the sci-fi helmet's and the watch's crease length at their targets) | [M] crease note | **TQ.** Two isolines need about one quad between them; releasing the strip was measured and not adopted (5.3) | Count only resolvable creases in A1θ; say so in the report | – |
| B4 | **Folded input: the overlapping part cannot survive**; spikes under a quad are cut | [M] fold notes | **GEO.** A valid quad mesh has no overlap; a needle tip under a quad has no quad version | The relief around the fold: A15, C3. Spikes: **Also Flatten Needle Spikes** (opt-in, a shape change) | – |
| B5 | **Thousands of tiny loose parts**: far more quads than asked, some pieces empty; raising Target Quads does not help | [M] knight: per-island floor on 4,139 rings; floor off gives about 2,800 empty islands | **TQ.** Each piece needs a minimum (about 28 quads at the 64-triangle floor); below it the piece is empty | Warn (A25), advise merging or deleting small parts; a floor inside the budget trades overshoot for empty pieces (undecided) | – |
| B6 | **Very low counts lose the shape**: a cube under 24 quads is not a cube; Suzanne's eye sockets close at every target from 6 to 200 | [M] usage audit | **TQ.** A closed genus-0 island with 8 corners needs at least 6 quads, a cube shape 24 [I]; the sockets' loops are about a quad or less at those counts [I, not measured] | Warn (A4) | Suzanne's sockets close at a count where their loop is several quads around |
| B7 | **No count can be predicted from the input**, and raising it is not monotone (33 of 78 clearing cases lose parts again higher up) | [M] target guidance: curvature beats a fixed default on about one model in four; lost-part width ranks the raise (ρ −0.62) but its constant varies 1.3–4.9 by part type | Partly **TQ** (rounding is discrete [I]); partly untraced (C12) | One run predicts the next (G17: within 2× in 97 % at a 2× step [M]); verified search (A23) | A feature predicts the first clearing count within 1.5× on held-out meshes |
| B8 | **Generating modifiers cannot be kept** (Boolean, Decimate, Solidify, Remesh, Weld, Multires detail); shape keys and face sets only approximately | [I] by construction: they act on the old topology | **GEO.** New topology has no correspondence to replay them on | List them (A7 (a)); Data Transfer for shape keys | – |
| B9 | **libQEx rejects folded maps and the island falls back to our tracer** (with non-quads) | [M] fallbacks at adoption: corpus 6/432, closed 8/468, sculpt 40/252 | **EXT.** libQEx needs a valid map by design | Retries at another count (A17); the real fix is a valid map (C2) | – |
| B10 | **Distribution**: Blender Extensions will not list a bundled executable; Windows unsigned, macOS not notarized; macOS never run on a Mac | Moderators' answer (Kajetan, 2026-10-28); [C] | **EXT.** Platform rules, paid certificates, no Mac | Self-hosted extension repository on the site (designed); a Mac tester | The platform accepts an extension that downloads its CLI on first use |
| B11 | **Self-intersecting input folds where it intersects** | [M] one shape, local: topology right 24/24 | **GEO** [I] | – | – |
| B12 | **Very large jobs**: over about 6.4M triangles or 100k quads | [M] envelope rows 4; memory model [M] #141 | **EXT** (hardware) | Pre-run estimate (G13); A2 | – |

## 3. C: no solution found yet

| # | Issue | Evidence | What was tried | Next step, and what could turn it into A | Falsified if |
|---|---|---|---|---|---|
| C1 | **Thin parts inside the main body when they are a small share of it** (rubber plant, dragon, parts of the sword other than the vines) | [M] thin cap ×6 with an independent checker | Local cap without a budget: a global raise in disguise (bee: 20× the quads). Cap with a 25/50 % budget: moves the losses (rubber plant and dragon worse on every measure). Per-island floor: cannot fix the sword. Keep-parts on the main island: a global raise in coarser steps, dearer on 23 units [M] | Take the quads from flat, low-curvature regions instead of uniformly (2.21 (c), untried [I]); or size or untangle the kept ribbons (2.21 (d)) | A cap paid only from flat regions still moves losses on rubber plant and dragon |
| C2 | **Folds from the map with no input trigger** (7 % of defects near miswound vertices; 0 % on Kajetan's sculpts) | [M] miswound note | Untangler with integers held (3.8, merged); re-rounding the folded region: 12× the time for a quarter of the gain [M]. Stopped by Kajetan (2026-10-26) | Stage B quantization (7.2), L, literature only. QuadriFlow removes flips by local integer moves on integer edge vectors plus a local SAT solve (`hierarchy.cpp`, `localsat.cpp`) [I, code read; L, another map representation] | – |
| C3 | **Repair Folds flattens small swirls** (head at 35k, by eye, *unconfirmed*) | [M] the largest swirl is made of folded triangles: closer by distance, flatter to the eye | Projecting onto the pre-repair input everywhere: no change near folds [M] | Turn an overhang into a steep groove instead of smoothing it (2.20a (e)), no design yet | – |
| C4 | **Handles 2 to 3 quads around may close** (torus_0.03 at 150 quads, 2 of 78 runs) | [M] | ℤ² lattice plus two extractor fixes keeps them, but ℤ² alone regresses the corpus (flips ×1.35, plane 2k broken 6 of 6) [M] | A lattice that allows odd meridians without the regression (7.2) | – |
| C5 | **Icosphere: extra cones inside the facets** (144 irregular vertices at 5k on the default icosphere, 152 after two Subdivision levels; UV sphere 8) | [M]; QuadriFlow gives the minimum 8–10 on all three in every replicate at matched counts [M, comparison thread], so the cones come from our field, not the shape | Subdividing first: no help; Smooth Normal 30° halves them at 3k/5k but triples them at 1k [M]. Guidance switched off is far worse (343–352 on the icosphere, 303–313 even on the UV sphere) [M, probe ×6, comparison thread] | Cause [C, comparison thread, code]: `framefield.cpp` solves a linear least-squares field whose only anchors are the guidance rows; without them the solution is about 0 and normalising it gives noise, with them it interpolates noisy anchors. QuadriFlow and Instant Meshes relax a nonlinear smoothness energy on a multigrid hierarchy. Next: the smallest-eigenvector field (Knöppel 2013, TODO 4.2) with guidance as a weak term, S–M, own code. **Moving towards A**, unproven until measured | The eigenvector field still leaves the icosphere far above QuadriFlow's 8–10 |
| C6 | **Sunk eyeballs come out as open caps** (15 of 36 runs non-manifold) | [M] 2026-10-21, **before** libQEx, #82 and the cleanup fix 2.21: may be stale | Nothing | Re-measure on 0.2.0 first (cheap), then trace (2.18) | 0.2.0 keeps them closed |
| C7 | **Flat open parts depend on their rotation** (edge ratio 1.02 axis-aligned, 1.2–2.5 rotated) | One run per rotation, *unconfirmed* | Nothing | Replicates, then bisect by stage (2.28) | Replicates show no rotation effect |
| C8 | **Finely segmented CAD cylinders** (256 sides): T 7× too dense, a hole at 2k in 6 of 6 | [M] before libQEx | Adaptivity 0: no fix | Re-measure, then trace the isotropic remesher on long edges (ENVELOPE row 19) | 0.2.0 handles it |
| C9 | **Keeping the openings of open parts** where the map is under a quad wide at the boundary (A5 reports them) | [M] libQEx and the tracer fail on different islands; the map check's width threshold misses most cases | Nothing | Keep boundary loops at least one quad wide in the rounding (Lyon et al. 2019 §5.2), no design yet | – |
| C10 | **Bee at 10k: error ×1.4 from splitting pinch points** on one sheet whose border touches itself | [M] sanitizer follow-up | A/B of split all / keep boundary pinches / no split, running | The A/B decides | – |
| C11 | **Sword at 2k on macOS: 34–56 non-quads in 5 of 6 runs** (release check), and >10 % under the target on Windows and Linux | [M] from job logs; artifacts blocked by the cloud proxy | Nothing | Reproduce on Linux with float32 replicates; likely thin ribbons (B1) plus platform order (A18) [I] | – |
| C12 | **Deterministic relapses when Target Quads is raised** (vines clear at 1.1–1.5×, lost at 1.75×; vaseflowers 6× / 8×; sword 3,700 worse than 3,500) | [M] every replicate agrees | Nothing traced | Diff the two runs' stage records (cones, rounding, extraction) at the relapse | The relapse moves with 1-ulp replicates (then it is B7's noise) |
| C13 | **Sculpt creases under 90°** (73 % of the cube's and 95 % of the sword's crease length) | [M] crease note | Lower Sharp Edge: costs validity on CAD and marks organic noise [M] | The 2.5 branch's noise filter (pleats over 170°, zigzag clusters) may allow 40–60° on sculpts; Follow Marks is the user's route today | – |
| C14 | **Folds wider than the detector's radius** (1 % of the part's diagonal) are not seen | [I]; none found on our sets | Nothing | A radius tied to the target (2.20a (b)) | – |
| C15 | **Holes and lost parts that no rule explains**: the *other* cause in the Missing parts warning (sculpted test box: a spike reported as "not thinner than a quad") | [M] 0.2.0 notes | Nothing | Tips (A13) may cover the box; collect reports | – |

## 4. Proposed release split

- **0.2.1** (proposal; only if worth a release check and a hand test): A1 and A2 (the hang without a
  timeout is the worst failure a user can hit), A3, A4, A6's messages, A22's add-on bug. All leave the
  default output byte-identical except A1's cap, which only touches islands that would hang [I]. If no
  user reports a hang, fold them into 0.3 instead.
- **0.3 decided**: A10–A13 (creases, rims, cone tips), A15 (repair by default), A18 (byte-identical
  platforms), A19, A21, A25, A7, the panel line on clearing Sharp when Follow Marks is skipped.
  **Proposed for 0.3**: A5, A8, A16, A17, A24, A26, A28, C6 and C8 re-measurements.
- **Later**: A9, A14, A20 (after the metrics freeze), A23, A27, A29–A31, and C.

## 5. What you may have missed

- **0.3 is heavy on output-changing core work** (2.5, cone tips, 2.9b, repair by default, keep parts),
  and each moves outputs on every set. 2.9b moves every output once and its payoff is a release check on
  one platform; merge it in a window with no other core change, so the release check that follows it
  can be read.
- **Most of B needs UX, not core work.** The count is the user's lever, and the useful work is A23
  (search), A24 (keep small parts), A25 (say why the count overshoots). Every tool we compared folds on
  parts thinner than a quad (13 of 54 mesh–count pairs have no fold-free tool [M, 1.8]), so better core
  work there would beat the field, not catch up with it [I].
- **A9 (raw arrays) is cheap and blocks paint and guides**; it is listed later only because nothing
  user-visible asks for it yet.
- **Several C rows are stale measurements, not open problems** (C6, C8: measured before libQEx). Re-run
  them before anyone plans work on them.
- This snapshot will drift. If you want the classes kept, the durable place is a tag on each TODO item;
  I did not edit TODO here because the crease thread is editing 2.5 today.

## 6. What would falsify this, and what I did not re-check

**Would falsify the classification:** any row's *Falsified if*; and, across the board, the open-source
comparison's thin-part and handle runs on `57c19f9` (still running; its primitive and cad runs are in): if QuadriFlow or Instant Meshes keep thin
parts or handles at the same quad count where we lose them, B1 and B2 are our limits, not the count's.

**Not re-checked:** every number here comes from its source note; I re-measured nothing and ran no
probes. I read three causes in the code (A1, A3, A22). Rows from running threads (A10, A17, C10, the
nemetona half of A1) are their state at 13:15 UTC and may change. The open-source entries are that thread's: code reads are marked
[I]; its measured runs (primitives, cad, matched counts, 6 replicates) are marked [M] and were not re-run by me. The release assignments are proposals except where marked
*decided*.

**Not tested at all (one line, no plan):** a real Blender window on Windows and macOS, a real Mac, his
own 1–10M-triangle sculpts, rigged or animated meshes, and inputs with non-manifold fins.
