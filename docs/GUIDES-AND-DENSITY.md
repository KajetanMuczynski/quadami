# Guide curves and density painting: design

Design note for TODO 4.4, 5.7, 5.8, G2 and G6 (thread "Assess guide curves and density painting",
2026-10-27, master `0b0832e`). Nothing here is built or measured; no code before Kajetan agrees on
the design. It builds on the feature-edge carrier the 2.6 and 5.4 threads are building
(`FeatureEdgeSet`, below) and on research/sizing-guides-symmetry.md §1.5, §2, §3 (project files).
Priorities follow the sibling report on how ZRemesher and Quad Remesher guides are used in published
work (research/guides-in-practice-2026-10-27.md, project files; desk research, labelled there); §5.1
says what it changed.

Labels: **[C]** checked in code at `0b0832e` (file:line), **[I]** inferred or derived here, not
measured, **[P]** vendor documentation as quoted in research/gap-analysis-zremesher-2026-10-26.md,
**[U]** unverified (memory, not checked). No claim in this note is **[M]**.

## 0. Result

1. **Density paint is mostly plumbing, but five places in the core use plain area as the quad
   budget, and each would silently cancel paint** [C]: the per-island quad share
   (`autoremesher.cpp:1088`), T's edge length (`:297`, `:1099`), the scaling field's count
   normalisation (`parameterizer.cpp:203-209`), the base quad edge (`parameterizer.cpp:344`) and
   decimation's triangle budget. Painting a whole island denser does nothing today's way, because
   every island is normalised to its own area share. Fix: one *effective area* ∫ 4^p dA (p = painted
   log₂ density) wherever the code means "budget"; with no paint it is the area, so default output
   stays byte-identical.
2. **The paint must reach T and the quad field even at adaptivity 0**: both sizing paths skip
   entirely when adaptivity is 0 (`autoremesher.cpp:693`, `parameterizer.cpp:130`) and clamp to
   0.3-3× of the mean (`:697-698`, `parameterizer.cpp:170-171`), narrower than the 0.25-4× both
   commercial tools offer [P]. Paint goes in after the curvature clamp, with its own range.
3. **Paint and an isotropic cross field are coupled** (Δφ = K − K̂, research note §2.1). A density
   step forces pole pairs along the paint border; on a flat surface about 2Δφ/(πw) pairs per unit
   length of border, i.e. one pair every 2.3 quads for a sharp 2× step and one every ~5 quads when the
   step is graded to 1.35× per row [I, §2.3]. Today the field never sees the sizing, so the cover
   solve has to absorb the step as distortion and flips, as it does for curvature sizing; whether the
   painted ratio is then actually realised is the first thing stage 1 must measure. If it is not,
   stage 1b is 4.5's cone placement from K − Δφ*, which is parked today.
4. **The budget trap**: with Target Quads kept as the total, painting a large area denser mostly
   coarsens the rest. Sword arithmetic: vines are 56 % of the area; painted 2× denser at the same
   target, the vines get only 1.22× finer than today and the blade 1.64× coarser [I, §2.4]. A "keep
   unpainted size" option (count grows instead) is needed next to the paint, or the users who most
   need paint (thin parts) get little from it.
5. **Soft guides are a field-only change and cannot break validity directly**, but the field solve
   overwrites its targets after the first of its five iterations (`framefield.cpp:220-229` set the
   rows' right-hand side to the last solution) [C], so a guide added as one more target row is
   forgotten after one solve, as the curvature target already is (M6). Guide rows must keep their
   target in every iteration.
6. **Hard guides on marked edges are the 5.4 carrier with another source**: a guide edge path is a
   hard user feature that is not a crease. Field lock, singularity freeze, isoline constraint and
   no-flip/no-collapse in T come with 2.6 and 5.4; what is left is the extractor's feature-blind
   `smoothAndProject(5)` (`quadextractor.cpp:152`, crease note §5.4, TODO 2.8), the sector rule
   (5.2) and parallel guides closer than h/2 (5.3's failure) [C, I].
7. **Hard guides drawn as curves need embedding**: cut the decimated input along the projected
   curve, then the curve is a marked chain (6.). Recommended over snapping to edge paths (zigzag at
   the decimated mesh's scale) and over isoline constraints on barycentric points (no reuse of the
   carrier, seam transitions per face) [I, §3.4].
8. **The hand-over needs one carrier for optional arrays.** The worker gets an OBJ and flags; 5.4
   adds marks as OBJ `l` lines (interface 1.10). Neither a per-vertex scalar nor free 3D polylines
   fit OBJ without conventions of our own. Proposal: G2's raw-array request (a small JSON manifest
   plus little-endian array files written with numpy `tofile`), which also removes the measured
   1-17 s input freeze (TODO G2) [I, §4].

Recommended order (§5): request format (S) → density paint with measurement (S-M) → hard guides on
marked edges and face-set/material borders (S, after 2.6, 5.4, 2.8's extractor part, 5.2) → hard
closed-loop guides on curves (M) → soft guides (S-M), last. The guides-in-practice report moved soft
guides from second to last: artists use region borders most, and want drawn curves as exact loops at
a few places (eyes, mouth, limb rings), not as soft hints, which is what ZRemesher users complain about.

## 1. Where the features land today [C]

| Stage | Code | What it does with size or direction |
|---|---|---|
| Input clean-up | `autoremesher.cpp:338` `cleanInput` | merges vertices (union-find); a per-vertex input array must follow the merge |
| Islands, budget | `autoremesher.cpp:1083-1100` | each island gets target × its area share; small islands raised to a floor, which also sets their T edge |
| Decimation | `autoremesher.cpp:532-660` | meshoptimizer keeps a subset of input vertices (`outputIndexOfInput`), so a per-vertex array follows by index; sharp vertices get Priority (2.6/5.4 move this to `featureVertexLock`) |
| T sizing | `autoremesher.cpp:693-827` | per-vertex target edge on the decimated mesh from curvature, clamped 0.35-3× voxel, budget kept by ∫ A/h²; only when adaptivity > 0 |
| T remesh | `thirdparty/isotropicremesher` | carries `targetEdgeLength` per vertex: midpoint on split (`isotropichalfedgemesh.cpp:330-336`), kept by one end on collapse, moved with the vertex on relax |
| Quad sizing | `parameterizer.cpp:123-212` `computeFaceScalingField` | on T: m = (κ/κ̄)^(−a), clamped 0.3-3, normalised per island to Σ A/m² = Σ A; only when adaptivity > 0 |
| Base quad edge | `parameterizer.cpp:337-345` | √(island area / island target) |
| Frame field | `framefield.cpp:87-249` | faces with a sharp or boundary edge locked to it (last edge wins); others pulled to the curvature direction, weight = (certainty / max certainty)²; five solves, each re-targeted to the previous result |
| Singularities | `singularitysimplifier.cpp` | cancels nearby ± pairs (would cancel the pole pairs a density step needs if the field ever created them) |
| Cover | `quadparameterizer.cpp:86-101`, `:348-460` | isoline constraint on sharp/boundary edges within 10° of a field axis; per-face size su, sv = scale × m × anisotropy, with a curl correction clamped to ±0.3 rad and ×1.5 |
| Extraction | `quadextractor.cpp:152`, `qexextraction.cpp` | libQEx, then 5 rounds of feature-blind smoothing and projection |
| Final projection | `surfaceprojector.cpp:127-129`, `:384-398` | snaps vertices within 0.25 edge onto input creases found by dihedral |

## 2. Density painting (TODO 5.8, 4.1's paint term)

### 2.1 Meaning
- Per input vertex p = log₂ of the density multiplier: the local quad edge is h · 2^(−p). p = 0 is
  neutral, +1 twice as dense (edges half as long), −1 half as dense. Range [−2, 2] (0.25-4×, the
  commercial range [P]). INTERFACE.md §5 reserves `vertexDensity` as "log₂ size multiplier"; the sign
  is fixed here as *density* (positive = more quads), matching both tools' "red = denser" [P].
- Interpolated linearly inside triangles; vertices merged by `cleanInput` take the mean.
- An empty array means no paint; every code path then stays as today (byte-identical, lane light).

### 2.2 Where it enters the core [I, design]
1. **Effective area.** Per island A_eff = Σ_f A_f · 4^(p̄_f) (quads per area go as density²). Use it
   for the island's share of the target, the small-island floor, T's voxel size, decimation's
   triangle budget and the base quad edge. With p ≡ 0, A_eff = A exactly.
2. **T.** Multiply the per-vertex target edge on the decimated mesh by 2^(−p) after the curvature
   clamp, and build that array even at adaptivity 0. T then has its usual triangles per quad inside a
   painted region. The remesher already propagates the array to new vertices (§1). Its values drift
   with relax by at most a few T edges [I]; resampling by closest point on the decimated mesh is the
   alternative if the drift shows in the measurement (it needs the facing-side filter of
   `SurfaceProjector`, or thin parts read the other side's paint).
3. **Quad scaling field.** Carry p to T's vertices the same way (one more per-vertex double in the
   half-edge mesh), m_f = m_curv,f · 2^(−p̄_f) after the curvature clamp, combined clamp
   0.3/4 to 3·4 (both ranges), and normalise Σ A/m² = A_eff, not A. Compute it at adaptivity 0 when paint exists.
4. **Gradation.** log h(x) = min_y (log h₀(y) + γ d(x, y)) by Dijkstra on T's edges, γh ≈ 0.3
   (≤ 1.35× per quad row; research note §1.4). Dense strokes grow a graded halo; coarse strokes
   shrink from their border. Without gradation the 2026-10-20 tensor field had ×1.32 flips [from
   TODO 4.1], so it is part of stage 1, not an extra.
5. **Report.** Painted fraction of the area, min/max p, the realised density ratio inside/outside
   the paint (measured on the output, §6) and the unpainted edge change caused by the budget (§2.4),
   so a user sees when paint was eaten by the budget.

### 2.3 Paint forces poles [I, derived]
With an isotropic cross field the scale e^φ (φ = log density) must satisfy ⋆ω = dφ, so Δφ = K − K̂
away from cones (Myles and Zorin 2013, research note §2.1). On a flat surface (K = 0) a step of Δφ
across a band of width w gives cone density K̂ = −φ″: positive cones on one side of the band and
negative on the other, each of total ∫|φ″| ≈ Δφ/w per unit length of border. Quantised to ±π/2:

  pole pairs per unit border length ≈ 2Δφ / (π w)

- 2× sharp step (Δφ = ln 2, w = h): 0.44 pairs per h, one valence-3/5 pair every ~2.3 quads.
- Graded at γ (w = Δφ/γ): 2γ/π per unit length, independent of the ratio; γh = 0.3 gives one pair
  every ~5 quads, and a 4× step only widens the band (4.6 rows instead of 2.3).

Consequences: (a) a painted border shows a row of poles in any isotropic quad mesh, ZRemesher
included; the user should see that as expected, not as a defect. (b) Today's field does not know the
paint, so it creates none of these cones; the cover solve then gets a non-integrable size target and
absorbs it as distortion (curl correction clamped at ±0.3 rad and ×1.5, §1), so the realised ratio
may fall short of the request and flips may gather at borders. (c) If the field did place them, the
singularity simplifier would cancel the ± pairs it finds close together. Stage 1 measures (b); the
principled fix is 4.5's step (a), cone placement from K − Δφ* (research note §2.2), restricted to
painted islands, and the simplifier told to keep paint-induced pairs.

### 2.4 The budget trap [I, arithmetic]
With the total kept at Target Quads, painting a fraction α of the area at p raises A_eff to
A(1 − α + α·4^p), so the unpainted edge grows by √(1 − α + α·4^p) and the painted edge changes by
that factor times 2^(−p). Sword (vines 56 % of the area), p = 1: unpainted edge ×1.64, vines ×0.82
of today's edge, i.e. only 1.22× finer. At p = 2: unpainted ×3.07, vines ×0.77. A small stroke
(α = 5 %, p = 1) costs the rest ×1.07. So:
- **Keep unpainted size** (a toggle): target × A_eff/A, the count grows, the add-on's memory and time
  estimate uses the grown count. Quad Remesher's "Adaptive Quad Count" on/off is the analogous
  switch [P], although whether it applies to paint is not documented there [U].
- The panel shows the effective target before the run.

### 2.5 Paint and thin parts
Painting thin parts denser is the manual version of 4.1's thin-part cap: on the sword the per-island
floor could not work because the vines are 56 % of the area (#123, closed). Paint with "keep
unpainted size" is the first route where a user can ask for small quads on the vines without
coarsening the blade, so the sword is stage 1's test of whether local size reduces the folds on
ribbons thinner than a quad, which the 2026-10-26 notes attribute to sizing [from memory of those
notes; not re-checked here]. "Sizing cannot buy validity" (TODO 4.1, 4.5) was measured for automatic
fields at a fixed count, not for more quads where the shape needs them.

## 3. Guide curves (TODO 4.4 soft, 5.7 hard)

### 3.1 Kinds and sources
| Kind | What the user gets | Core mechanism | Source in Blender |
|---|---|---|---|
| Soft | quads tend to flow along the curve nearby; no promise of an edge loop | extra alignment rows in the frame field | curve objects; maybe annotation strokes |
| Hard on edges | an edge loop exactly on a marked edge path | hard feature edges (2.6 carrier, 5.4 hard bit) | edges marked as guide; face-set borders (5.4) |
| Hard on curves | an edge loop on a curve that is not a mesh edge path | embed the curve as an edge chain, then as above | curve objects |

ZRemesher's guides are soft (Curves Strength; one forum report says they are ignored even at
strength 100), drawn on the surface with the ZRemesherGuides brush; its hard borders come from
polygroups (Keep Groups) [P/E, research note §3.1]. Quad Remesher lists curves as "WIP" and takes
edge loops from materials, face sets, sharp edges and normal splits [P]. So the commercial
precedent is: soft curves, hard borders from groups and marks. In use, borders are the control
artists rely on (hard surface especially), density paint is the rescue on faces, and soft curves are
reported ignored or spiralling on eyes and mouths (5 ZRemesher threads, all 2013-2015), while users
ask for curves as exact closed loops (guides-in-practice report §0, §2).

### 3.2 Soft guides [I, design]
- Project each polyline onto its island's T (closest point, facing-side filter), sample every half T
  edge, and give each face it crosses the tangent's 4th power, averaged over samples, weighted by the
  user strength w ∈ [0, 1].
- Add one energy row pair per such face with target arg(z) and weight W·w, where W is the scale of
  the curvature rows (their weight is (certainty/max)² ≤ 1, so W = 1 makes a full-strength guide as
  strong as the most certain curvature face). Keep the guide rows' right-hand side fixed in all five
  iterations (§0 item 5). Faces locked by a hard feature ignore the guide.
- Influence: the field's smoothness spreads the guide; the energy has no mass matrix (4.2), so the
  reach is a fixed number of T faces, i.e. a fixed number of quads at any target, since T keeps its
  triangles per quad [I]. That is a reasonable default; 4.2 is not a prerequisite, but changing
  4.2 later changes how far a guide reaches (retune W then).
- Known interaction: certainty is normalised by the mesh maximum (`framefield.cpp:157-159`, TODO
  2.24), so one sharp spot makes every curvature row weak and guides dominate; on a smooth shape
  guides are relatively weaker. The weight should be set against a fixed scale once 2.24 is fixed.
- Closed loops (around an eye, a limb): the field turns once along the loop; on a disk-like region
  the four positive quarter cones this needs land inside the loop, as in ZRemesher output around
  eyes [I].

### 3.3 Hard guides on marked edges [C for what exists, I for the rest]
A guide edge path is a hard user feature without a dihedral. On the 2.6 carrier (`FeatureEdgeSet`
{edges, flags}, bit 0x1 = hard, island-local, built in `buildIslandContext`, remapped through
decimation, kept by the remesher, read by field, simplifier, constraints and projector) it needs no
new core mechanism: the add-on sends guide edges with the hard bit, like 5.4's marks. Still missing
for a clean edge loop, all already planned:
- extraction: `smoothAndProject(5)` moves output vertices off T's chains (crease note §5.4; TODO 2.8);
- corners and crossings: the sector rule q = max(1, round(2α/π)) and its tie at 135° (5.2); today
  a face with two feature edges keeps the last one;
- parallel guides closer than about h/2 collapse or fold (5.3's failure; libQEx fallbacks rose 1 → 7
  of 144 when strips were released, #58): demote the shorter one to soft and report it;
- the 10° drop in `edgeConstraint` should never fire on a locked guide; count it (crease note §5.3).
Zigzag: an edge path on a dense sculpt zigzags at the input edge scale, but with Lock on the chain
and collapse along it (5.4's rules) T's chain edges are chords over many input edges, so the lock
direction error is about (input edge / T edge) [I]. On a coarse input the path is the input's edges.

### 3.4 Hard guides on curves [I, options]
| Option | How | For | Against |
|---|---|---|---|
| A. Cut the decimated input | project the polyline onto the decimated island, split edges where it crosses them (snap to a vertex within ε of one), add the new edges as hard chain edges | reuses all of §3.3; exact | mesh cutting on noisy sculpts: slivers near vertices, crossings near each other |
| B. Snap to an edge path | Dijkstra on the decimated mesh with a cost for leaving the curve | trivial; transfer.py already does this for marks on the result | zigzag at the decimated mesh's scale, which is near h |
| C. Isoline constraints on barycentric points | per face the curve crosses, u(entry) = u(exit) on the locked axis, one integer per segment | no change to T | new constraint type; axis and integer must follow seam transitions per face; extractor and projector must learn the curve separately |

Recommendation: A, after §3.3 works on marked edges. Order inside the island: clean-up, fold repair,
project curves, cut, then the carrier as usual.

### 3.5 Shared rules
- Endpoint in a smooth region: no pole; the isoline runs on until it meets a singularity or boundary
  (research note §3.4). Crossing at angle α: sector rule; a 30° crossing forces a valence-6 vertex.
- A guide across islands is split per island by the closest input face of each sample.
- Guides never filter (marks are never filtered, crease note §5.1); a guide that cannot be honoured
  is reported, never dropped silently.

## 4. Blender side and the hand-over (TODO G2, G6)

### 4.1 Today [C]
The add-on reads the evaluated mesh (positions, loop triangles; `__init__.py:188-205`), writes an
OBJ and runs `autoremesher-cli -i in.obj -o out.obj --report-json ... --target-quads N ...`. The
5.4 thread adds marked edges as OBJ `l` lines and interface 1.10. Input write measured at 1.1 s /
5.0 s / 17.4 s for 0.33M / 1.3M / 5.2M triangles against 0.02-0.53 s for raw arrays (TODO G2).

### 4.2 Proposal: one raw-array request (G2a) [I, design]
- `autoremesher-cli --request req.json -o out.obj ...`, where `req.json` = `{"schema":
  "quadami.remesh-request", "version": 1, "arrays": {"positions": {"file": "positions.bin",
  "dtype": "f4", "shape": [V, 3]}, "triangles": {..."u4", [F, 3]}, "vertex_density": {"f4", [V]},
  "feature_edges": {"u4", [E, 2]}, "feature_edge_flags": {"u1", [E]}, "guide_points": {"f8",
  [P, 3]}, "guide_offsets": {"u4", [G + 1]}, "guide_flags": {"u1", [G]}, "guide_strength": {"f4",
  [G]}}}`, files little-endian, written by numpy `tofile`, read by `fread`. Unknown keys are
  ignored; a missing optional array means "not given".
- OBJ input stays for hand runs and the bench; 5.4's `l` lines stay readable there. No `.npz` (zip
  parsing in C++ for nothing).
- API side: `RemeshRequest` gains `vertexDensity` (1.11) and `guideCurves` (1.12) as INTERFACE §5
  reserved; the minor bumps follow the existing rule (empty = old behaviour).
- G2a is its own small PR before stage 1, so the density PR does not invent a sidecar file that the
  request would replace a week later.

### 4.3 Add-on [I; Blender API details U unless noted]
- **Density source**: None / Vertex Group / Color Attribute / Float Attribute, plus *Density
  Strength* (max ×, default 2) and *Keep Unpainted Size*.
  - Vertex group: p = w · log₂(strength), denser only (weight 0, the default, is neutral); an
    *Invert* toggle for coarser.
  - Colour attribute (what Sculpt Mode paints): p = log₂(strength) · (R − B): white and grey
    neutral, red denser, blue or cyan coarser (Quad Remesher's convention [P]); corner-domain
    colours averaged to vertices.
  - Read from the same evaluated mesh as the positions, so indices match. Attributes have
    `foreach_get` (transfer.py uses it for marks [C]); vertex-group weights do not, and a per-vertex
    Python loop may be slow on million-vertex sculpts [U, not measured].
- **Guides**: a collection picker; every curve object in it is evaluated (`to_mesh` of the evaluated
  object gives the polyline at its resolution) and brought into the source's object space; per-object
  custom properties *Hard* and *Strength*. Curves can be drawn onto the surface with Blender's Curve
  Draw tool (depth: Surface) [U]. Grease Pencil changed its data API in 4.3 [U], so annotation strokes
  come later, if at all: the one Blender workflow on record turns a grease pencil stroke into a mesh
  and Knife Projects it, i.e. wants an exact loop, which stage 3's cut does from a curve object.
- **Guide edges**: an edge attribute `quadami_guide` with *Mark Guide* / *Clear Guide* operators in
  Edit Mode; sent with the hard bit next to 5.4's marks.
- The panel says what will be sent ("density: vertex group Face, 12 % painted; 3 guides, 1 hard").
- G14: paint and guides are read after modifiers, like the positions; guides are in the source's
  object space, so a Mirror modifier mirrors the mesh but not the guide curves (say so).

## 5. Staged plan

Costs: S days, M 1-3 weeks. "Lane" per CLAUDE.md test lanes; every stage keeps default output
byte-identical until a decision says otherwise.

| Stage | What | Needs | Cost | Main risk | What would falsify it (stop or redesign) |
|---|---|---|---|---|---|
| 0 | This note, TODO rows | – | – | – | – |
| G2a | Raw-array request in CLI and worker; OBJ path kept | – | S | file handling on Windows paths and temp folders | input hand-over not faster than OBJ at 1.3M triangles; any default output change |
| 1.17 | Bench: `paint` and `guides` sets (procedural meshes with arrays, request files in the corpus), metrics: realised density ratio, poles per border length, guide coverage and angle | G2a | S | metrics bugs in shared code (disprover rule) | — (tooling) |
| 1 | Density paint: §2.2 (1-5), add-on source and toggle | G2a, 1.17 | S-M | realised ratio short; flips at borders | on the `paint` set at default gradation, 5 replicates: realised log₂ ratio < 0.7 p; or folds/flips within 2 h of borders > 2× the same region unpainted in every replicate; or count outside today's error band. Then 1b before shipping |
| 1b | Cone placement from K − Δφ* for painted islands; simplifier keeps paint pairs | 1, 3.3 | M | is 4.5 (parked: not better than lower adaptivity on curvature sizing) | realised ratio or border validity not better than stage 1 |
| 2 | Hard guides on marked edges, face-set and material borders (§3.3); a per-run toggle for face sets; border smoothing for noisy sculpt face sets | 2.6, 5.4, 2.8 (extractor part), 5.2 | S on top | strips < h/2, libQEx fallbacks; noisy organic borders | coverage < 0.9 of guide length on resolvable guides at 2k as well as 10k; libQEx fallbacks or folds up on the `guides` set |
| 3 | Hard closed-loop guides on curves: cut (§3.4 A); mirrored under symmetry (G12) | 2 | M | cutting noisy sculpts | slivers or holes on the sculpt/brush sets when guides are present; coverage below stage 2's on the same paths drawn as curves |
| 4 | Soft guides on curves (§3.2) | G2a, 1.17 | S-M | weight scale (2.24), more singularities; weak user evidence | guide alignment within h not better than without, 5 replicates; or validity worse beyond noise; or no user asks for a hint once 2-3 exist |

Stage 1 acceptance meshes: procedural sphere, torus and plane with painted caps, stripes and a
graded ramp (public); Kajetan's sword with the vines painted (private set, metrics only); a head with
eyes and mouth painted.

### 5.1 What the guides-in-practice report changed [its claims are desk research; my reading is I]
- **Soft guides last.** ZRemesher's soft curves are the most complained-about control on faces;
  users ask for curves as exact loops. Stage 4 stays only as a hint mode once exact loops exist.
- **Borders before curves.** Face sets, materials and Mark Sharp/Crease borders are what artists use
  and what upstream AutoRemesher users asked for; Quad Remesher users fake curve guides with Knife
  Project + Mark Sharp, which stage 2 serves directly. Read face sets directly (Quad Remesher's
  face-set-to-material round trip is a known annoyance), behind a toggle (sculptors use face sets for
  masking), with noise tolerance on organic borders, and a border must not hold density up.
- **Density paint second in use, first in our order**: it does not depend on 2.5/5.2, and the top
  complaint in both tools is "it did nothing", so the report's realised-ratio readout (§2.2 item 5)
  is part of stage 1, not polish. Users paint coarse zones (two or three levels), so gradation is a
  safety measure, not a feature; black, white and grey must all be neutral, so no fill step is
  needed (ZBrush's FillObject trap).
- **Test low counts**: users judge loops around small features at 2-3k quads; every guide A/B
  sweeps the target down to 2k.
- **Symmetry from the start**: two reported guide failures were symmetry problems; guides go through
  G12's half + Mirror run (only the half's guides are used, the others are mirrored, or a mismatch
  is reported).
- **Persistence**: curve objects and edge attributes stay in the .blend between runs, which ZBrush
  users miss; keep it that way (no guide data inside the result only).

## 6. Measurement needed (TODO 1.17)
- **Realised density ratio**: median output edge length over quads whose centre has p ≥ 0.9 p_max,
  over the median where p ≤ 0.1 p_max, against 2^(−p_max) and against the same regions unpainted
  (removes curvature sizing).
- **Border poles**: irregular vertices within 2 h of the p = p_max/2 level set, per unit length of
  it; compare with §2.3's estimate.
- **Guide coverage** (hard): the crease-coverage code of #113 with the guide polyline as the chain.
- **Guide alignment** (soft): length-weighted mean angle mod 90° between the guide tangent and the
  nearest output edge, within h of the guide.
- Validity as everywhere (folds, flips, non-quads, holes, libQEx fallbacks) within 2 h of paint
  borders and guides, and on the whole mesh.

## 7. What may have been missed (design critique)
- **Paint does not buy validity on its own.** It moves quads; the map still has to be valid. If
  stage 1 shows border flips, the fix is the map (3.3, 3.8, 1b), not more gradation.
- **Target Quads semantics change** with paint unless *Keep Unpainted Size* is on; users will read a
  coarser blade as a bug. The panel's estimate has to show it.
- **Guides are the planned route to edge flow** (TARGETS §1: no automatic artist flow; flow control
  comes from the user through guides, symmetry and paint), but a soft guide is honoured only as far
  as the field allows. The report must say how much of each guide was followed, or users cannot tell
  a weak guide from a bug.
- **Feature-edge carrier first**: hard guides and 5.4's marks are the same object; building guide
  plumbing before 2.6 and 5.4 merge would duplicate it (the coordinator's brief, and the reason this
  note only plans).
- **Determinism**: closest-point projections of guides need a tie-break by index, or replicates on
  float32 inputs will move guides between faces (the knife-edge lesson of 2.6) [I].

## What would falsify this
- A stage-1 run showing paint realised fully (ratio ≥ 0.9 p) with no border effect would mean the
  cover absorbs the non-integrable size better than §2.3 predicts; 1b would then be unnecessary.
- Border poles far from 2γ/π per unit length on graded paint would mean the flat-surface estimate is
  wrong (curvature, anisotropy, or rounding dominate).
- Soft guide rows that keep their influence without a fixed right-hand side would contradict the code
  read of `framefield.cpp:220-229`.
- If the 2.6/5.4 carrier ends up per-island with re-detection after decimation that can drop marked
  pairs, §3.3's "no new mechanism" is wrong for guides.

## What I did not re-check
- Nothing was built or run; every number in §2.3 and §2.4 is derived, not measured.
- Blender API details in §4.3 marked [U] (Curve Draw on surface, Grease Pencil 4.3 change,
  vertex-group read speed).
- The 2.6 and 5.4 designs are taken from their threads' messages of 2026-10-27 (not yet in a PR).
- Commercial behaviour is quoted from the gap analysis and the 2026-10-14 research note, not
  re-read at the vendors.
- The sword's folds being caused by quad size on the vines is from earlier notes (thin-strips,
  thin-island-floor), not re-measured.
