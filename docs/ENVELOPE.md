# Supported-input envelope

Which inputs and settings the remesher is expected to handle. **Inside the envelope every gate of
[TARGETS.md](TARGETS.md) applies as written, and a failure is a bug. Outside it a failure is reported as
an *expected limit*:** it is still counted and ratcheted, but it does not fail a gate. Written
2026-10-21 (thread "Supported-input envelope for the remesher") for Kajetan's "strict, but not stupidly
strict". **Status: decided** by Kajetan on 2026-10-21 (§6): warn and run, the handle bands, eyeball caps inside
the envelope, the 2 h₀ radius (provisional), and ratchet only for non-default settings.
The [I] thresholds stay provisional until their experiments (§4) run.

Tags as in [README.md](README.md): **[M]** measured, **[C]** checked in code, **[I]** inferred. Every [I]
threshold names the experiment that would turn it into [M] (§4). Sources: the research notes named in each
row, master `124aeb8`.

**Unit.** h₀ = √(A / N): the input area divided by the target count, square-rooted. It is the core's base
quad edge (TODO 2.11) and the mean output edge length. Adaptive sizing makes the local edge smaller on
curved parts, so a feature measured in h₀ is an upper bound on its size in local quad edges.

---

## 1. Design critique first: how an envelope rots, and the guards

An envelope is a list of excuses. Each excuse is cheap to add and expensive to remove, and it hides bugs
by default. This project has already seen it happen once. V6's "handles shorter than 4 quad edges are
exempt" would have hidden an extractor bug: the tori lose handles of 2.1–2.7 h₀, and in torus_0.03 Fast 150
the map held a meridian of 4 grid units and the extractor still lost the handle
(research/genus-diagnosis-2026-10-21.md). The ways an envelope rots, and the guard against each:

| Rot | Guard |
|---|---|
| A threshold is set by a proxy (length) when the cause is a mechanism (the map holds the handle, the extractor drops it) | Where the mechanism is known, the condition names it. A length is allowed only where no mechanism is known yet, and it is marked [I] with the experiment that replaces it |
| A whole run is exempted because one part is out of range, and an unrelated bug elsewhere in the run hides behind it | **Exemptions are local:** only defects within 2 h₀ of the out-of-envelope region are expected. A defect anywhere else in the run still fails its gate (§5.3) |
| Exempt defects disappear from view | Exempt counts are still reported, per limit, and ratcheted like TARGETS §3.2: no release is worse than the previous one |
| A limit is added to make CI green | A row needs all of: an [M] measurement or an [I] with its experiment, a test mesh in `corpus.json` that sits outside the limit, a **canary mesh just inside it that must pass**, and an owning TODO item or the word *permanent* with a reason. A PR that adds or loosens a row needs Kajetan's yes |
| A fixed limit stays in the list | At each release (§5.4) every limit's test mesh is re-run. A limit whose mesh now passes is tightened or deleted in the same release |
| Things that can be fixed get documented instead | Rows tagged *temporary* (scale, degenerate edges, flipped patches) exist only until their TODO lands. The fix is better than the row |
| A real bug is filed as "weird input" | §2 lists defects that are **never** exempt, whatever the input |

**What Kajetan may have missed.** "Weird meshes may fail" splits into two different things, and the
envelope must keep them apart:
1. **Physically unrepresentable at the requested count.** A tunnel narrower than a quad cannot be
   represented by quads of that size, and no remesher can do it. This kind is *permanent*. The right
   answer is to tell the user the count they would need, not to hide the failure.
2. **Not handled yet.** Intersecting eyeballs, zero-area triangles and flipped patches are all ordinary
   sculpt inputs, and today they fail because of bugs. This kind is *temporary*: it is in the
   envelope's TODO column with an owner, not outside the envelope for good.

Most of today's sculpt failures are kind 2. If they were declared out of the envelope, the gates would go
green and the add-on would still break on eyes.

## 2. Never exempt (inside or outside)

Each of these is a bug wherever it happens, because no input makes it unavoidable [I, argument per row]:

- **Crash, hang, or no output** (H1). The worst outside-envelope result is a refusal with a reason, never
  a crash. The 2,000,000-quad ceiling already refuses instead of exhausting memory (TODO 2.17) [C].
- **Extra handles; boundary loops split or merged** (V6, H5). Every measured extra handle came from the
  extractor (53/53 near a fold or a cone; 64 of 75 wrong replicate-runs fixed by tracing alone) [M]. Losing
  a handle can be physical (§3 row 1). Adding one never is.
- **6+-gons, and non-quad clusters of more than 2 faces** in organic or sculpt runs (sculpt note §5: the
  largest cluster on common inputs is 2) [M].
- **Non-manifold output edges or vertices** (H2).
- **Lost input islands** (H4). A too-small island is enlarged or reported, never dropped silently (TODO 2.14).
- **A closed input island coming out open** (H5). Measured on sculpt_parts eyeballs, where it is a bug,
  not a limit (§3 row 6).

## 3. The limits

h₀ as above. The "user sees" column is a proposal. *pre* means a pre-run check on the input, cheap enough
for the add-on panel to show before Remesh. *report* means the core's quality report after the run.

| # | Quantity (how measured) | Inside the envelope | Evidence | Test mesh (outside) / canary (inside) | Outside: what is exempt | User sees | Kind / owner |
|---|---|---|---|---|---|---|---|
| 1 | **Handle size**: per input handle, the continuous map's meridian holonomy H (grid units = quads, along the shortest homology loop of T; `seam-lattice-2026-10-21/holonomy.py`). Pre-run proxy: shortest input loop L ÷ h₀ (`handles.py`) | H ≥ 3 | Three bands, §3.1 | demosthenes 2k (H ≈ 1) / torus_0.1 at 150 (bug band) and torus_0.05 at 150 (owner band) | H < 2: the handle may close. 2 ≤ H < 3: may close until the lattice problem is solved (7.2). Never: an extra handle | *pre:* "N handles narrower than 3 quads: raise Target Quads to ≈ N·(3/H)² to keep them, or they may close". *report:* handles closed | §3.1 |
| 2 | **Thin sheet**: local thickness t (inward ray from each input face centroid to the opposite side, area-weighted) ÷ h₀; the fraction of area with t < h₀ | t ≥ 1 h₀ everywhere for V2/V3 (folds, flips); **any t** for topology | Every tool folds on parts thinner than a quad (13/54 pairs have no fold-free tool) [M]. Yet topology holds: ellipsoid_0.4_0.003 (t ≈ 0.17 h₀ at 2k) keeps genus on 4d15d5f; on the closed set only the tori fail [M]. thin_leaf (t ≈ 0.32 h₀ at 2k, 0.71 at 10k): folds 2–12 in every replicate [M] | thin_leaf 2k / ellipsoid_0.4_0.1 (t ≈ 5 h₀ at 2k) | Folds and flips within 2 h₀ of the thin region. Topology, islands and the count are **not** exempt | *pre:* "thin parts below quad size: expect folds there" | permanent below 1 h₀ until 5.3/4.1 add a thin-part cap |
| 3 | **Sub-quad tips and crests**: input faces in the sharp/thin band (normal spread within one h₀ < 0.7, `sculpt_report.py`) | Not a run condition, a location. Non-quads there are tolerated (V1 rewording); folds are expected limits | Non-quads up to 5× concentrated in the band, folds often 100 % in it [M]. Kajetan's crater tip: 1 fold at a valence 10–12 pole [M, reported] | sculpt_voxel horn tips / spot (smooth: no exemption) | Folds and valence ≥ 7 within 2 h₀ of a band face. Counted and ratcheted | *report:* "N folds at sharp tips" | temporary: 5.3, 4.1 |
| 4 | **Input size**: triangles; **target**: quads | ≤ 6.4M triangles [M]; target ≤ 100k quads [M], 100k–200k [I] | 6.4M triangles in 27.5 s on the ladder (TODO 8.2) [M]. Memory 21 KB per quad at Quality, linear over 25k–100k [M]. Stress sweep 2026-10-22: 5.2M-triangle sphere at 5k/50k quads in 49/209 s, 3.1/3.3 GB; peak RSS ≈ 0.6 KB per input triangle + 21 KB per quad (an upper bound on 4 runs) [M] | koala ladder / spot 100k | Time and memory only. Quality gates still apply | Exists: the CLI warns above 200,000 with an estimate; the add-on asks; the core refuses above 2,000,000 [C] | permanent (hardware) |
| 5 | **Target vs input detail**: input triangles ÷ target quads | **No lower bound found** [M] | Kajetan's coarse 3.7k-triangle blob at 1500 quads (ratio 2.5): 0 non-quads, 1 fold [M, one run]. Stress sweep (E3, 2026-10-22): 12-triangle cube, 80- and 320-triangle icospheres at 500–8000 quads (ratio down to 0.0015): 0 non-quads, ≤ 2 folds, topology right in 9/9 [M]. A Blender-style 32-sided cylinder (n-gon caps) is fine; a CAD cylinder with 256 sides (triangle aspect > 100) is not, see row 19 | stress_coarse / – | Nothing | *pre:* "input coarser than the requested quads: the result follows the facets; consider Subdivision first" | open (E3) |
| 6 | **Joined / intersecting parts** (separate closed islands that intersect each other: eyes in sockets, teeth) | **Inside.** Ordinary sculpt layout | sculpt_parts: eyeballs come out as open caps in 19/36 runs, with non-manifold output vertices in 15/36 [M]. Cause not traced | sculpt_parts / sculpt_steps | **Nothing.** These are bugs (§2) | *report* (follow-up, §3.2): "island N was closed in the input and came out open" | bug: TODO 2.18 |
| 7 | **Self-intersection of one island** (a surface pushed through itself; triangle–triangle intersections within an island, BVH) | No self-intersection | E4 (2026-10-22): a sphere dented to the far wall and through it (depth 1.0 and 2.2 R) at 2k/8k: topology right in 24/24 replicate-runs, folds 0–7 [M, one shape]. Local, as assumed | stress_selfint | Folds and flips within 2 h₀ of the intersection | *pre:* "N self-intersections; result may fold there" | permanent [I] until E4 |
| 8 | **Degenerate input**: zero-length edges, zero-area triangles | Inside **after #51** (cleanup pre-pass) | sculpt_degenerate (21 % zero-area triangles) at 15k: 500–777 non-quads, wrong genus, 89–202 s. Cleaned: 0–8 non-quads, 4–16 s [M] | sculpt_degenerate / sculpt_steps | Nothing (#51 merged) | *report:* "Input cleanup: N degenerate triangles removed" (#51) | temporary: #51 (2.7) |
| 9 | **Inconsistent orientation** (manifold edges used twice in the same direction) | Inside: orientation repair (TODO 2.7) is done | sculpt_dirty: a flipped patch tears off as its own island in 36/36 runs [M] | sculpt_dirty | Nothing since 2.7; the panel's Input cleanup row counts the turned triangles | *pre:* "N faces with flipped normals" | temporary: 2.7 |
| 10 | **Non-manifold input** (fins, pinch vertices, duplicate faces) | Inside **after 2.7** (duplicates removed, pinches split) | Before 2.7 the earlier "absorbed" held for fins only. `san_dupfaces` (5 % of spot's faces twice): 1,175 islands, 1,050–1,064 empty, 59–78 folds; the same faces twice in opposite winding: 241 of 2,000 quads at 2k, over 90 % of the input uncovered; a double-sided shell: 98–141 s at 2k, over 15 min (timeout) at 8k in 6/6; 3 pinched vertices on spot: p99 distance ×5 (2k) and ×18 (8k) against the same shape unpinched. After: every case gives the clean spot's output (2,029 / 7,982 quads, 0 folds) or the unpinched shape's, 6/6 replicates [M] (research/input-sanitizer-2026-10-26.md). Fins (`topo_fin`) unchanged. A double-sided open sheet with its own vertices per side stays two coincident sheets, as before 2.7 (the weld never joins two triangles on the same side of an edge) | stress_sanitize, sculpt_dirty | Nothing | *report:* "Input cleanup: N duplicate faces removed, N pinched vertices split" | covered by 2.7 |
| 11 | **Open boundaries and holes** | Inside | hand, demosthenes, cloth_sheet, human_neutral and Suzanne keep their loops (TODO 2.12) [M]. sculpt_dirty: 3 input loops come out as 1–6 [M] | sculpt_dirty / hand | Nothing. The sculpt_dirty loops are a bug (or row 9's tear) | – | bug, after 2.7 re-check |
| 12 | **Islands**: count, and the smallest island's area ÷ h₀² | ≤ 1,000 islands [M]; any island size [M] | spot_x8 (8) and sculpt_parts (14) keep every island in 35/36 runs [M]. E5 (2026-10-22): 10/100/1,000 spheres of radius 0.06–1.6 h₀ next to a body keep every island in 18/18 runs, 1,000 islands in 12–43 s, ≤ 350 MB. **But the quad count overshoots**: each crumb gets the 64-triangle floor (≈ 28 quads), so 1,000 crumbs at target 2,000 give 27–29k quads (×14), and nothing says so [M] | stress_islands | Nothing | *pre:* "N small parts get at least 28 quads each: expect about Q quads, not the target" (Q = Σ max(share, 28)) | open: floor inside the budget, or the warning |
| 13 | **Triangle size ratio**: edge length p99 ÷ p1 | ≤ 350 [M] | sculpt_dyntopo (p99/p1 = 30): 2 folds, topology 6/6 [M]. E6 (2026-10-22): graded spheres at p99/p1 = 7, 38, 353 and 20 % slivers (p99/p1 = 1,180) at 2k/8k: 0 non-quads, topology right in 18/18 [M] | stress_degenerate | Nothing | – | covered |
| 14 | **Scale and position**: bounding-box diagonal d; centre offset | d ∈ [10⁻⁴, 10⁴] × spot, centre to 10⁶ [M] | E7 (2026-10-22): spot ×10⁻⁴ … ×10⁴ gives the same quad count (2,029) at every scale ≥ 10⁻³, 0 non-quads, topology right; offsets 10³, 10⁵, 10⁶ fine [M]. Older evidence: CI's output gate passes a sphere centred at 10⁵ [C]. `PositionKey` no longer overflows (now doubles) [C]. It still merges positions in absolute 10⁻⁵ cells, which is 1 % of a 10⁻³ model [C, I]. 1e-12 guards are absolute (TODO 2.7) [C] | needs a mesh (E7) | Below or above: not measured | – | inside as measured; scale normalisation dropped from 2.7 on this evidence (2026-10-26, stress gap G9) |
| 15 | **Settings**: adaptivity, anisotropy, sharp edge | Defaults (adaptivity 0.5, sharp 90°) [M]; adaptivity 1 [M] | Gates are measured at the defaults; csg_bracket opens a hole at adaptivity 1 (H5, 2.13) [M] | – | Non-default settings: the ratchet only, not the 1.0 bar. *Ratchet only* means no release may be worse than the previous one on that metric (runs clean, defects per 1k), with no promise of zero failures (TARGETS §3.2) | *panel:* "outside tested settings" next to a non-default value | permanent policy |
| 16 | **Folded input** (triangles turned over against their surroundings within r = 1 % of the diagonal of their part, in spots with an edge that turns over by more than 90°, `FoldRepair::detect`; counted on every run, `input_folded_triangles`, `input_fold_clusters`) | No folds [M] | Real sculpts: 94–100 % of folds, non-quads and holes within two quads of a fold (research/sculpt-defects-2026-10-21.md) [M]; repair on: head 10k non-quads 31 → 7, topology 0/6 → 6/6 (PR #71) [M] | private head and cube, brush_twist, brush_box_relief / canary: off = master byte-identical on brush, sculpt, closed, corpus | Folded spots and the defects within two quads of them, with repair off; with repair on only the spots it left (`input_fold_clusters_after`) | *report/panel:* "Folded input: N faces in K spots", **Repair Folds and Re-run** button; after a repair "Folds repaired: M of N faces" and "N folded faces in K spots could not be repaired safely"; with **Also Flatten Needle Spikes** "Spikes flattened: N, tip lowered by up to X quads" and a shape-change line | opt-in repair, opt-in spike flattening (shape change), 2.20 |
| 17 | **Thin parts** (limbs, vines, straps, hair strands): local diameter d (inward march along the normal until the winding number drops below ½, `stress_report.py` `local_thickness`) ÷ h₀ | d ≥ 0.8 h₀ attached; as a separate island d ≥ 3.1 h₀ before TODO 2.21, about 0.8 h₀ after it (one replicate) | Stress sweep 2026-10-22 (x5 replicates on the boundary): an **attached** capsule survives in every run with d ≥ 0.8 h₀ (12 runs, and 18/18 replicates at 0.8–1.6 h₀) and is gone (the surface smoothed over it) at d ≤ 0.4 h₀ in 13/14 runs and 34/36 replicates; the step is d ≈ 0.4–0.8 h₀ [M]. A **separate** capsule island collapses to 3–10 quads at d ≤ 1.6 h₀ (kept at d ≥ 3.1 h₀ in 12/12 runs, and 6/6 replicates at 3.1 h₀), because the last cleanup pass (`collapseThreeValenceEdgePairs`) eats a valid libQEx tube (72 → 6 faces) [M] | stress_thin, stress_attached / thin_r0.128_L1 at 2000 | Below 0.4 h₀: the part may vanish (permanent: a ring of quads needs about 2.5 quads around). Between 0.4 h₀ and the island limit: **bug** (cleanup pass, owned by the vines thread) | *report:* "Parts of the model are up to N quads away from the result" (coverage rule, §3.3) | permanent below 0.4 h₀; bug above |
| 18 | **Unwelded input** (vertices at the same position not shared: an import without merge, an applied Edge Split) | Inside **after 2.7**: open seams welded when two boundary edges match end to end within 10⁻⁵ of the diagonal (and a tenth of the shorter edge), one partner only | Before: `topo_soup` (spot, 23k triangles unwelded): 23,062 islands, 49–64 s, up to 3.9 GB, 56 % of the input uncovered; spot unwelded along one seam: two pieces and an open crack in the output, silent; csg_bracket after Edge Split at 30°: 46–58 output pieces. After: the clean mesh's output, soup in 1.5 s, 6/6 replicates [M] (research/input-sanitizer-2026-10-26.md). Three sheets meeting on one line, and gaps wider than the tolerance, stay unwelded | stress_topology, stress_sanitize | A seam with a gap wider than the tolerance: its crack | *report:* "Input cleanup: N seam edges welded" | covered by 2.7 |
| 19 | **Long CAD triangles** (fan caps and side strips of a finely segmented cylinder; triangle aspect > 100) | Aspect ≤ 100 [I] | `degen_cad_cylinder` (1,020 triangles, 256 sides): T gets 27–33 triangles per target quad (median over the sweep 4.5), 52 s and 370 MB at 2k, 104–191 s and 1.5 GB at 8k; genus wrong at 2k in 6/6 replicates (hole, reported). A Blender-style 32-sided cylinder is fine (5.5 per quad, 2 s) [M]. Cause not traced; adaptivity 0 does not fix it | stress_degenerate / Blender-style cylinder | Time and memory; the hole is not exempt | – | open: trace the isotropic remesher on long edges |

### 3.1 Handles: three bands

Measured on the continuous map, before rounding, because that is where the representability is decided.
A quad ring around a tube needs integer meridian holonomy of at least 3. Today's lattice is 2ℤ²: a
meridian rounds to 0, 2 (a collapsed ring) or 4 (research/seam-lattice-2026-10-21/, probe on tori and
demosthenes) [M].

| Band | Status | Evidence | What changes it |
|---|---|---|---|
| H < 2 | **Permanent limit** at this count. No lattice gives a ring of ≥ 3 without stretching the map by more than half (needs ×1.5 or more); the user is told the count that keeps it | demosthenes' three scan tunnels sit at H ≈ 1: 2ℤ² rounds them to 0 and ℤ² to 1, still no ring [M] | Nothing at this count: raise the count |
| 2 ≤ H < 3 | **Known limit with an owner.** Expected and ratcheted, and the canary below must not get worse | torus_0.03 / 0.05 at 150 (2.1–2.7 h₀). On master: 2ℤ² rounds to 2 (collapsed) or 4. ℤ² plus the torus thread's two extractor fixes keeps them (torus_0.05 Quality 0 → 5/6, Fast 1 → 5/6), but ℤ² alone regresses the corpus (flips ×1.35, plane 2k broken in 6/6) [M, seam-lattice and torus threads] | Owner: the quantization work (7.2), i.e. a lattice that allows odd meridians without regressing rounding. ℤ² as measured is not adopted. Until that problem is solved this band stays a known limit; after it, a **bug** |
| H ≥ 3 | **Bug.** Every loss here is the extractor's (2.13, 6.1) | 64 of 75 wrong replicate-runs on master are extractor losses or additions with the map right [M] | – |

Open edges:
- **2026-10-22 (genus-libqex note):** on libQEx every lost handle is lost by the map (96 % already missing
  in libQEx's raw output, the tracer loses the same ones), and demosthenes loses tunnels whose *input*
  girth is 3.4–4.9 quads at 20k/40k: T and the map are about half as wide at the tunnel as the input.
  So L/h₀ overestimates H by ~2× there, and "H ≥ 3 is a bug" holds for the map's H, not for L/h₀.
- The 2.0 boundary is [I]. It is the halfway point between holonomies that round to 1 and to 3 under
  ℤ²; no handle has been measured between 1.4 and 2.1. E1 settles it.
- H is a stage diagnostic, not an input property. The pre-run check can only use L/h₀. Adaptive sizing
  makes the local quads smaller, so H ≥ L/h₀ as a rule [I], and the proxy errs on the side of warning.

### 3.2 Runtime warning for an opened closed island (follow-up, not in this PR)

Kajetan's addition (2026-10-21): when the row 6 bug happens anyway, the user should see it named, not
only as a count. Detection is cheap, per island after extraction: the input island is closed (no
boundary edges) and its output component has boundary edges or non-manifold vertices. The core's report
already counts boundary edges and islands (TODO 2.3); this needs the count per island, matched to its
input island. The add-on then shows "Island N (closed in the input) came out open: try another Target
Quads". The warning is not an exemption: the run still fails H5 as a bug. It belongs with the
eyeball-caps fix or with 2.3.

Two corrections to the brief: the add-on's Target Quads guard is 200,000 (warn) and 2,000,000 (refuse),
not 300,000 (TODO 2.17) [C]; and #51 collapses zero-length edges only, without orientation repair (PR
body, checked 2026-10-21), so row 9 stayed open after #51 until orientation repair (2.7) landed.

### 3.3 Warning rules, measured (stress sweep 2026-10-22)

**Status 2026-10-26:** Coverage, Collapsed island and Handles changed are in the core and the panel
(TODO 2.21, 2.22; user guide §6). Thin part shipped as a different, post-run rule: the share of the
surface thinner than one quad (≥ 3 %, or ≥ 1 % in 10+ spots), with a suggested target (#98).
Unwelded, Memory and Crumb overshoot are not implemented.

Proposals for the panel, each a rule the core or the add-on can compute, scored with
`tools/bench/stress_report.py` on master `0e9bec8`: **stress** = the 124 stress runs whose feature has probe
points (a feature counts as lost when over 20 % of its probes are farther than max(3 r, h₀/2) from the
output); **baseline** = 238 runs of corpus, closed, sculpt, brush and Kajetan's private sculpts (u0), where
every firing was checked against the run's own measures. Details: research/stress-2026-10-22.md (project files).

| Rule | When | Condition | Stress: caught / missed / false | Baseline fires | Proposed text | Verdict |
|---|---|---|---|---|---|---|
| **Coverage** (lost parts) | post, core | some input point lies farther than 3 h₀ from the output | 53 / 11 / 0 | 21 / 238: private sword 3k–12k (vines), corpus sword 2k, plane 2k, sculpt_voxel 2k (horn), brush spikes, thin ellipsoids at 150. Each has a region ≥ 3 h₀ off, so none is false | "Parts of the model are missing in the result (up to N quad widths away). Thin parts need about two quads across: raise Target Quads." | **Built 2026-10-23** (input vertices, not area samples; same verdicts on 326 runs). The misses are short parts lost within 3 h₀ (spikes, short tubes at low counts) |
| **Collapsed island** | post, core | an island has fewer than half the quads it was planned (stage record `island`: output_faces < 0.5 × target_quads) | 42 / 22 / 3 (the 3: short capsules that keep their outline as 3–6 quads). **Coverage or collapse: 61 / 3 / 3** | 15 / 238: human_neutral eyes (6 of 32 quads), sculpt_parts eyeballs, sculpt_dirty, private sword 3k. All real | "K parts came out with far fewer quads than planned (smallest: 6 of 69)." | **Built 2026-10-23** with coverage: together 47 / 5 / 0 on stress_thin, attached and ribbon at master 877891d |
| **Handles changed** | post, core | per island whose input has no non-manifold edge, 2g = 2C − χ − B of the output ≠ of the input (a closed-only χ rule would miss demosthenes, which is open, and 26 of 72 external runs) | – (exact; a lost plus an added handle on one island cancel) | 23 / 238: demosthenes, human_neutral, sculpt_dirty, private sword; jitter and folds stress. The panel names it in none | "Output has N handles fewer than the input (tunnels or loops closed up, on K islands) …" / "… N handles more …" | **Built 2026-10-22** (TODO 2.22; research/genus-libqex-2026-10-22.md): Handles row + warning, equal to `metrics.py` per replicate-run |
| **Unwelded** | pre, add-on | islands > triangles / 10 | topo_soup, topo_soup_gapped | 0 / 238 | "The mesh is not welded (N separate pieces): run Merge by Distance first" | **Dropped** (2026-10-26): the core welds open seams (2.7) and reports the count, so the run no longer fails |
| **Memory** | pre, add-on | 0.6 KB × triangles + 21 KB × quads above half the RAM | fits 4 high-poly runs within +26 % (upper bound) | – | "This needs about N GB of memory" | Adopt, extends the 200k-quad confirm (2.17) |
| **Crumb overshoot** | pre, add-on | Σ max(target × Aᵢ/A, 28) > 1.5 × target | 1,000 crumbs at 2k: predicts 30k, got 27–29k | 0 / 238 | "N small parts get at least 28 quads each: expect about Q quads" | Adopt |
| **Thin part** | pre | any point thinner than 0.8 h₀ (row 17) | 7/7 attached limbs, 0 false within the family | **150 / 238** (folds, creases, tips, thin sheets) | – | **Not usable as is.** Needs the extent of the thin region (longer than a few h₀), not a point |

## 4. Experiments that settle each [I]

| ID | Row | Experiment (all through `ab.py` with `--replicates 5`, per CLAUDE.md) | Settles |
|---|---|---|---|
| E1 | 1 | Tori at r ∈ {0.03, 0.05} over targets that sweep H from 1 to 4 (≈ 8 targets), demosthenes 2k/5k/10k, on master and on any candidate lattice. Log H per handle (holonomy probe) next to kept/lost | The 2.0 band edge, and how well L/h₀ predicts H |
| E2 | 2 | Thickness column (ray cast) added to the metrics; ellipsoid set at 2k–10k | Whether folds are confined to within 2 h₀ of thin parts, as row 2 assumes |
| E3 | 5 | spot and sculpt_steps decimated to 1k / 3k / 10k triangles, run at 1k–15k quads (ratio 0.1–10) | The lower bound of input ÷ target |
| E4 | 7 | Generator: one island folded through itself (a sphere with a dent pushed out through the opposite wall) | Whether self-intersection breaks only locally |
| E5 | 12 | spot_x8 pattern scaled to 100 and 1,000 islands; crumbs of area 0.01–1 h₀² | Island-count time and the small-island floor |
| E6 | 13 | sculpt_dyntopo variants with p99/p1 = 100 and 300 | The size-ratio ceiling |
| E7 | 14 | spot scaled ×10⁻³, ×10⁻², ×10³, ×10⁴ (a scaled copy in `corpus.json`, no new generator) | The scale range; needed before 2.7 to prove its effect |

**Status 2026-10-22:** E3–E7 ran on master `0e9bec8` with `tools/bench/make_stress.py` (sets `stress_coarse`, `stress_selfint`, `stress_islands`, `stress_degenerate`, `stress_scale`); results in rows 5, 7, 12–14. E1 and E2 are open; E2's thickness column exists now (`stress_report.py` `local_thickness`).

Cost: E1, E5 and E7 are about 1 CPU hour each, the rest less [I]. They fit a single thread after the
speed thread's `ab.py` change lands.

## 5. How the harness decides inside or outside (design only)

`ab.py` is not edited here: other threads are changing it, and 1.14 (homology check) has to land first.

### 5.1 Per input: an envelope descriptor
Computed once per (mesh, target) from the input, and cached next to the mesh hash:
`h0`, `diag`, `centre_offset`, `triangles`, `islands`, `min_island_area_h2`, `edge_ratio_p99_p1`,
`zero_length_edges`, `zero_area_triangles`, `orientation_conflicts`, `non_manifold_edges`,
`self_intersections`, `handles[{L_h0, loop_vertices}]`, `thin_area_fraction`, `thin_faces`,
`sharp_band_faces`. Sources: `handles.py` (1.14), `sculpt_report.py`'s band, plus a ray-cast thickness
and a BVH intersection test (libigl is already in the bench venv).

### 5.2 Per run: inside / outside
`envelope = {"inside": bool, "violations": [{"row": 2, "value": 0.32, "limit": 1.0}, ...]}`.
A run is *inside* when no row is violated. Rows 4, 5 and 12–15 are run-wide. Rows 1, 2, 3 and 7 are
**regions**: the faces of the thin part, the handle's loop vertices, the band, and the intersection.

### 5.3 Per defect: bug or expected limit
For each gate defect (fold, flip, non-quad, valence ≥ 7, lost handle), find its nearest input point. It is
**expected** if that point is within 2 h₀ of a violated region whose row exempts that defect type.
Otherwise it is a **bug**, even in an outside run. Lost handles are matched by loop: a lost handle is
expected only if its H puts it in an expected band of §3.1, read from the stage diagnostics (so the core must export H per handle; until then the L/h₀ proxy decides and is reported as such). §2's defects are always bugs.

`compare` then prints per gate: `bugs` (fails the gate, A/B and release), `expected` (ratcheted, shown
per row) and `runs inside`. A PR that moves defects from `bugs` to `expected` without changing the code
that produces them, for example by editing a threshold, is flagged.

### 5.4 Release review
Before each tag: re-run every row's test mesh and canary. A canary that fails is a bug that blocks the
release. An outside mesh that now passes tightens its row. The envelope is re-signed with the release
version at the top of this file.

## 6. Decisions (Kajetan, 2026-10-21)

1. **Outside the envelope: warn and run.** *Decided.* The panel shows the *pre* warnings and runs anyway.
   Only the memory ceiling refuses, as it already does.
2. **Handle bands (§3.1):** *decided as written.* H < 2 is permanent. 2 ≤ H < 3 is a known limit owned by
   the quantization work (7.2) and becomes a bug once a lattice exists that allows odd meridians without
   regressing rounding. H ≥ 3 is a bug.
3. **Eyeball caps (row 6):** *decided: inside the envelope, a bug.* Kajetan added that the user should get
   a specific warning when it happens. The design is in §3.2, as a follow-up.
4. **Locality radius 2 h₀ (§5.3):** *accepted as provisional.* E2 measures it.
5. **Non-default settings (row 15):** *decided: ratchet only.* Runs at non-default settings must not get
   worse from one release to the next, but the 1.0 bar applies only at the defaults.
