# Quality targets

What good output means for this remesher, as numbers: the positioning, the gates every output must
pass, per-family goals with a minimum and a stretch value, and which goal wins when they conflict.
Defines TODO 0.7. Written 2026-10-19 from the 2026-10-15 comparison and a re-run of our side at the
current default; an independent critique changed the first draft (§8). **Status: decided** by Kajetan
on 2026-10-20 (§6): positioning, priority with margins and release bar accepted; the artist-side
reference is deferred. **2026-10-23** (§6 item 5): V1 is all-quad again (the sculpt tolerance of
2026-10-21 is retired), V3 counts flips over folded input apart, and ratchets use the mean over
replicates (§3.2).
**Scope:** the gates apply as written inside the supported-input envelope ([ENVELOPE.md](ENVELOPE.md));
outside it a defect within 2 h₀ of the out-of-envelope region is an expected limit, still counted and
ratcheted. ENVELOPE §2 lists the defects that are never exempt.

Tags as in [README.md](README.md): **[M]** measured, **[I]** inferred, **[P]** primary source, **[E]**
external claim. Metric IDs (T*, G*, D*, A*, P*, S*) are those of research/extraction-layout-metrics.md
§5.2; the definitions needed here are inline. Every number below comes from
research/quality-targets-2026-10-19/ (`numbers.md`: the frozen comparison; `rerun.md`: the re-run; the
scripts that make both).

---

## 1. Positioning

**Aim: faithful, valid, predictable.** Kajetan asked for "precision, no bugs, no malformations". Read as:
- *Faithful* (precision): at the requested count the output keeps every part of the shape (thin parts,
  small islands, creases) and lies on the input. Measured by distance, normal deviation and crease
  coverage (§4.1).
- *Valid* (no malformations): a 2-manifold with the input's topology, without folded or flipped
  quads, doublets or valence ≥ 7 poles (§3.2), all-quad.
- *Predictable* (no bugs): completes on every input, gives the same output for the same input on one
  build, lands on the count, and small input changes give small output changes (§3.1, §4.4).

Element shape and pole count must reach the free field-aligned tools (QuadriFlow, Instant Meshes): a
floor, not a lead.

**Not competing on:**
- *Automatic artist-style edge flow* (loops ringing eyes and mouth, poles where an animator wants them):
  ZRemesher's strength, built on unpublished heuristics, and not measurable yet (§7). Flow control comes
  later from the user: guides (TODO 4.4, 5.7), symmetry (5.5), density paint (5.8).
- *Speed*: Instant Meshes is 6× faster (median; 3–11× interquartile) [M]. We only guard against being
  slower than Blender's QuadriFlow (§4.4).

**Where we stand** (current default, adaptivity 0.5, all 18 meshes, against the frozen free tools;
ratio < 1 = ours better) [M]:
- *Lead:* worst-case distance to the input. The 99th percentile of input-to-output distance is 0.59 of
  the best free tool's (better in 43 of 54 runs), from the curvature-adaptive sizing; it is weakest on
  hard surface (0.90, 90 % interval 0.66–1.24; csg_bracket, 0.70 without it) and scans (0.80, 0.64–1.01).
  The count is within ±5 % on the first try in 52/54 runs (QuadriFlow 18/54). No island dropped, no
  input refused, a quarter of Blender QuadriFlow's memory.
- *No lead yet:* mean distance (0.92 overall, but 1.15 on organic and 1.10 on scans) and normal
  deviation (1.36, better in 5/54), because vertices are not projected onto the input (vertex-to-input
  distance ~160× the best tool's; TODO 2.8). So "precise" today means "loses no parts", not "closest
  surface".
- *Gap:* malformations. Non-quads in 44/54 runs (free tools: none; libQEx made us all-quad outside
  tracer fallbacks by 2026-10-22, §3.2 V1); doublets or valence ≥ 7 in 17/54
  (Blender: none); hard surface misses element-shape parity (SJ 1st percentile 0.46 against 0.81);
  poles on hard surface and thin shells 2.0–2.7× QuadriFlow's.
- *Not checked:* the competitors ran at their defaults. QuadriFlow's `-adaptive` and `-sharp` were never
  tried, so the fidelity lead is against the free tools as users run them, not against their best
  settings (§7).

The three goals conflict today: the fidelity lead comes from adaptive sizing, and the same sizing causes
most malformations. Adaptivity 1 against 0 over the 54 frozen runs: non-quads ×3 and folded quads ×5 in
total, Hausdorff ×0.5 [M]. Moving the default to 0.5 traded some of each (§2.4). The sizing work (4.1,
4.5) has to deliver both at once; §4.2's stretch row is that test.

## 2. How the targets are measured

### 2.1 Corpus, families and what they can decide
The 18-mesh corpus (`tools/bench/corpus.json`) at 2k / 10k / 50k quads, default settings: organic 6,
hard surface 3, thin shell 4, scan / noisy 4, multi-island 1. Genus also uses the map-fix thread's closed
set (thin ellipsoids and tori, 78 runs; research/rounding-greedy-2026-10-19.md).
Family results are only as good as the family. Bootstrap 90 % intervals over meshes for the Hausdorff
ratio at adaptivity 1: organic 0.36–0.72, hard surface 0.57–1.07, thin shell 0.28–2.59, scan 0.78–1.20;
multi-island has one mesh [M]. So:
- **PRs are judged on the whole corpus** (geo-mean over all runs, with its interval).
- **Family targets judge releases**, and only for families with at least 5 meshes. Today that is organic
  alone; the others are indicative until TODO 1.5 grows them.

### 2.2 References
"Best free tool" = per mesh and count, the best of QuadriFlow `810b7a0`, Blender 5.2.2's QuadriFlow and
Instant Meshes `7b31608` for that metric, from the frozen comparison runs
(`research/remesher-comparison-scripts/results/runs.csv`, every tool calibrated to ±5 % of the count).
They are never re-run, so a ratio against them moves only when our output moves.
"Today" = master `47529e1` (greedy rounding, PR #37) at the default adaptivity 0.5, re-run on 2026-10-20
for all 18 meshes × 3 counts (research/greedy-rounding-rerun-2026-10-20.md). The previous baseline
(master `507f9b3`, 2026-10-19) had 16 meshes, without csg_bracket and blob_sym, so a family value can move
because the family grew: hard surface went from 2 to 3 meshes. The min and stretch values of §4 were
derived on those 16 meshes and are unchanged.

### 2.3 Paired ratios and noise
Soft goals are geometric means of ours / reference over runs of the same mesh and count (counts get +0.5
per 1k so a zero reference stays finite). Single runs mean little: defect counts move 1.5–2× under a
1-ulp change of the input, Hausdorff is one maximum (one run moved 2.2 → 7.2 % of the diagonal on a small
change), times ±6 % [M]. That is why the 99th-percentile distance (D1r) is the fidelity target and
Hausdorff only a diagnostic.

### 2.4 Priority when goals conflict
**gates > fidelity > element shape > layout > cost, each lower goal protected by a margin.** A change may
give up something lower in the list for something higher only within the lower one's margin: fidelity
10 %, element shape 5 %, layout 10 %, time 30 % (corpus geo-means). Beyond a margin Kajetan decides.
Example: adaptivity 0.5 cost Hausdorff ×1.11 for non-quads and folded quads ×0.72; p99 distance moved
0.53 → 0.57 against the free tools, inside the margin. Without margins the order would favour uniform
sizing (more clean runs, fidelity ×2 worse) and throw away the lead.

### 2.5 Replicates and platforms
- A **replicate** moves every input coordinate by one float32 ulp (random sign), 5 per run. Double-ulp
  moves can vanish inside the pipeline (the review saw identical output in 16 of 16 such runs), so a
  replicate that reproduces the plain run bit for bit does not count.
- **Determinism** is per build: same binary, same input, same output across repeats and thread counts.
  Linux and Windows and macOS builds differ (TODO 2.9: container iteration order), so release checks run
  on all three platforms.
- A release check is the corpus × 6 (plain + 5 replicates) on each platform, corpus and closed sets:
  the manual workflow `release-check.yml` (RELEASING.md), not a per-PR job. Both sides on Linux,
  4 cores: 54 min [M] (2026-10-22).

## 3. Gates

### 3.1 Structural gates (every run, zero failures)
Held on the corpus today, so they are cheap to keep: CI gates them on its procedural set now (where
marked), and a release needs them on every corpus run and replicate.

| ID | Gate | Today [M] | In CI |
|---|---|---|---|
| H1 | Completes: exit 0, status ok, non-empty output; no input refused | 54/54; Blender refuses 2 meshes. **2026-10-22** (master `c2ffd24`, 1,482 replicate-runs over corpus, closed, sculpt, brush, Suzanne and the private sets): all complete | yes |
| H2 | 2-manifold: no non-manifold edge or vertex | 54/54 edges; vertices: 0 in 150 corpus and closed runs on master `124aeb8`, but 18 of 154 sculpt-set runs (15 of them sculpt_parts, the sunk-eyeball caps) [M]. **2026-10-22** (`c2ffd24`, libQEx default): 0 non-manifold edges and 0 non-manifold vertices in all 1,482 replicate-runs, sculpt_parts included [M] | edges; vertices counted by `sculpt_report.py`, a gate once 2.18 is fixed. A non-manifold vertex is a bug wherever it occurs (ENVELOPE §2); the gate waits only so CI isn't red on a known, owned bug |
| H8 | No face with 6 or more sides | 0 in 304 runs (corpus, closed, sculpt set) [M] | add |
| H3 | Consistently oriented | 54/54 | add |
| H4 | Islands kept: one output component per input island, none dropped (under half its area within one output edge), split or merged | 54/54; QuadriFlow 50/54. **2026-10-22**, per island (`metrics.py` `validity`): corpus 72/72, sculpt 41/42, private 22/22; **closed 72.7/78**: thin ellipsoids at 150 quads shrink to a blob (up to 86 % of the input area farther than one quad from the output) and torus_0.03 at Quality 150 came out as 3 quads in 2 of 6 replicates, while their topology reads "same as input" [M] | dropped and collapsed islands: yes (coverage in `ci/output_gate.py`); split or merged: add (`gates.py` computes it) |
| H5 | Boundaries kept: input boundary loops stay open; closed inputs get no hole | 54/54; csg_bracket fails at adaptivity 1 (2.13). **2026-10-22** per island: corpus 70.5/72 (human_neutral mouth 2 holes in 8 replicate-runs, csg_bracket 1), joined private sword 14/16 (a loop closed) [M] | after 2.13 |
| H6 | Count within ±10 % of the target, first try | 54/54; QuadriFlow 34/54. **2026-10-22**: corpus 70.7/72, closed 59.8/78 (thin shapes at 150 quads) [M] | yes at 0.6–1.5; tighten to 0.9–1.1 |
| H7 | Deterministic per build (§2.5) | Linux yes (REFERENCE P8) | later (8.6) |

### 3.2 Validity (malformations): a ratchet toward zero
Zero in every run is the goal, but no method here guarantees it yet: the map still folds before any
cleanup, and on parts thinner than a quad every tool folds (in 13 of the 54 mesh/count pairs none of the
five tools or settings is fold-free, among them all three human_neutral counts) [M]. A per-run zero would hold only by luck. So:
- each gate is tracked as **runs clean** and **defects per 1k faces**, on the corpus and its replicates;
  for V1, defects per 1k is the plain non-quad count, whether or not the run passes V1;
- **ratchet**: no release is worse than the previous one on either number. Runs clean counts as the
  **mean over the plain run and its 5 replicates**, with the runs clean in every replicate next to it
  (2026-10-23): a single plain run flatters, since 14 of the 33 plain corpus passes of 2026-10-22 fail
  a 1-ulp replicate;
- the **1.0 bar** is the column below; **stretch** = zero everywhere, made a hard gate once validity
  is guaranteed by construction (an injective map and an all-quad extraction; map-fix thread's next
  step, 6.1/6.2).

| ID | Gate | Today: runs clean (per 1k, o / h / t / s / m) [M] | Best free tool [M] | 1.0 bar [I] |
|---|---|---|---|---|
| V1 | All-quad (restored 2026-10-23; the cluster tolerance of 2026-10-21 is retired, see below) | Non-quads per 1k faces, max per family on master `124aeb8`: o 6.8, h 20.1, t 26.2, s 5.6, m 2.1. **2026-10-22** (`c2ffd24`, libQEx default; corpus 72 runs × 6 replicates, mean over replicates): corpus all-quad 71.5/72 (69 in every replicate; the 3 replicate-runs with non-quads, boat, csg_bracket and hammer, are all libQEx fallbacks), closed 77.8/78, sculpt 41.2/42, brush 23.5/24, Suzanne 8.5/9, private 22/22; non-quads per 1k faces o 0 / h 0.06 / t 0 / s 0.003 / m 0 [M] | all runs | every run |
| V2 | No folded quad: every corner's scaled Jacobian > 0 (`folded_faces`) | 29/54 (0 / 0.31 / 0.72 / 0.04 / 0). **2026-10-22**: corpus 51.5/72 (o 20.0/24, h 9.8/12, t 7.5/16, s 11.2/16, m 3.0/4; per 1k 0.11 / 0.07 / 0.56 / 0.14 / 0.04), closed 54.3/78; on organic only human_neutral folds, on scans all four (demosthenes, spot_noisy, nefertiti, hammer) [M] | Blender 14/48 | every run on organic and scan inputs; elsewhere ≤ the best free tool per 1k |
| V3 | No face flipped against the input (catches pleats of convex quads that V2 misses), not counting faces within 1.5 output edges of folded input (`metrics.py` `faces_off_input_folds`, 2026-10-23: a triangle turned against its surroundings within 1 % of the diagonal, in a cluster with an edge turned over by more than 150°; there the output follows the sculptor's fold, counted apart as `faces_over_input_folds`; without the turned edge human_neutral's lips and eyelids counted as folds). Faces where the input is thinner than a quad excluded once 1.4 can measure it | 25/54 (0 / 0.10 / 1.09 / 0.09 / 0). **2026-10-22**, flips now against the input oriented like the core (`metrics.py`, 1.4): corpus 35.5/72 (o 16.2/24, h 7.2/12, t 5.8/16, s 6.2/16, m 0.2/4; per 1k 0.64 / 0.23 / 2.34 / 0.55 / 0.70), closed 44.2/78. **2026-10-23**, flips over folded input left out (`faces_off_input_folds`): corpus 39.0/72 (o 17.5/24, h 8.0/12, t 5.8/16, s 7.5/16, m 0.2/4; 30 in every replicate), closed 44.2/78 (no folded input there), sculpt 17.3/42 (was 11.0), brush 19.0/24 (16.3), private realsculpt 1.8/6 (0.2), realsword 0/16. Flips over folds per 1k faces: corpus 0.07 of 0.93 (scan 0.21 of 0.55), realsculpt 1.33 of 2.56 [M] | Instant Meshes 26/54 | as V2 |
| V4 | No degenerate face (area < 10⁻⁶ h²) | 54/54. **2026-10-22**: corpus 72/72, closed 77.7/78 [M] | Blender 45/48 | every run |
| V5 | No interior vertex of valence 2 or ≥ 7 (feature corners exempt once Wave 5 marks them) | 37/54. **2026-10-22**: corpus 53.7/72 (36 in every replicate), closed 76.7/78 [M] | Blender 48/48 | every run |
| V6 | Genus per island kept for every handle whose shortest loop is ≥ 4 quad edges; thinner handles reported | 63/72 corpus runs (2k/10k) with the right topology, 61.5/72 over the 5 replicates (55 right in all six, 6 wrong in all six); closed set 75/78 (75.5/78); greedy rounding fixed or broke none unanimously. **2026-10-22**: corpus 66.5/72 (64 right in all six; demosthenes loses its 4 scan handles in every run, human_neutral opens its mouth in 8 of 24), closed 77.7/78; per island (`validity.islands_same_as_input`) the same counts on corpus and closed. Handles lost or added are now counted per island, which catches a handle moved between islands; one lost and one added in the same island still passes (1.14) [M] | not measured | every run (needs a loop-length measure in 1.4) |

All of V1–V5 together (headline validity number): ours 9/54, ours at adaptivity 0 9/54 (frozen run),
Blender's QuadriFlow 11/48, Instant Meshes 8/54, QuadriFlow 4/54 [M]. Report it as the mean over
replicates with the runs clean in every replicate, and since 2026-10-23 with V3 off input folds; the
number with every flip counted goes next to it, since the frozen tools were counted that way.

**Re-baseline 2026-10-22** (master `c2ffd24`, libQEx default, `tools/bench/gates.py` over `ab.py`
runs with 5 replicates) [M]: corpus 33/72 on the plain inputs (32/72 on `124aeb8`), **28.3/72 as the
mean over the six replicates, 19 in every replicate**; closed 41/78 plain, 43.3/78 mean, 33 in every
replicate. V1 no longer costs anything (libQEx output is all-quad outside 1 hard-surface run), so V2–V5
alone give the same counts: folds (V2) and flips (V3) hold the number down, V3 most (corpus 35.5/72).
The plain-input number flatters: 14 of the 33 plain passes fail in at least one replicate, so the
ratchet uses the replicate mean. Other sets (mean over replicates): sculpt 10.5/42, brush 15.8/24,
Suzanne 1.8/9, private realsculpt 0.2/6, realsword 0/16.
**2026-10-23** (all-quad V1, V3 off input folds; the same runs re-measured): corpus **31.2/72 as the
mean over replicates, 20 in every replicate** (28.3/72 and 19 with every flip counted), closed 43.3/78
(no folded input, unchanged), sculpt 15.0/42, brush 17.0/24, Suzanne 1.7/9, realsculpt 1.2/6, realsword
0/16. The all-quad V1 costs at most half a run per set (Suzanne 1.8 → 1.7) [M].

**V1 history.** From 2026-10-21 to 2026-10-23 V1 tolerated edge-connected clusters of up to 2
triangles or pentagons outside hard surface, since they subdivide cleanly
(research/targets-v1-2026-10-21/, research/sculpt-inputs-2026-10-21.md). With libQEx as the default
extractor the rule stopped changing any count: on `c2ffd24` 15 of 1,482 replicate-runs have non-quads
(1–22 triangles each, never a 5+-gon), every one of them an island where libQEx was rejected and the
tracer took over, and the V1–V5 counts are the same under both rules (Suzanne 1.8 vs 1.7). All-quad
catches a return of the tracer's non-quads at no cost, so Kajetan restored it (§6 item 5). The
tracer fallback itself is tracked by `gates.py`'s "no libQEx fallback" row: 59 of the 1,482
replicate-runs fell back on some island, 44 of them all-quad anyway, so V1 sees only a quarter of
them [M].

Why "every run" for folds on organic and scan inputs: those are the add-on's
main inputs, they are thick and smooth, and a fold there is a defect a user sees as a shading crease [I];
15/18 organic runs are already fold-free. The exception to trace first: human_neutral folds with every
tool at every count (cause not traced) [M].

## 4. Soft goals per family

Columns: organic / hard surface / thin shell / scan / multi-island. Only organic is release-grade
(§2.1). "Today" is measured [M]; min and stretch are chosen [I], with the reason in the last column.
Bold today values already meet the minimum.

### 4.1 Fidelity (lead)

Ratio = ours / best free tool, paired, geo-mean; lower is better. **Today** in §4.1 and §4.2 re-measured 2026-10-22 on master
`c2ffd24` (corpus, Quality, 2k and 10k, mean over 6 replicates; `research/rebaseline-2026-10-22/ratios.py`
against the frozen competitor runs at the same counts); "was" = the 2026-10-19 values (2k/10k/50k). Relax
(2.8, #62) put the vertices on the input; D2 now meets its minimum outside multi-island, D3 only on thin
shell; hard surface misses G1 and G2, multi-island G2 [M].

| Metric | Today o / h / t / s / m | Min | Stretch | Why |
|---|---|---|---|---|
| D1r input-to-output distance, 99th percentile | **0.55** / 0.92 / **0.30** / 0.83 / 0.84 (was 0.59 / 0.90 / 0.30 / 0.801 / 0.74) | ≤ 0.8 | ≤ 0.5 | A lead under 20 % is inside the scan interval and one sizing change; 0.5 halves the worst deviation, which shows on thin parts |
| D1 Hausdorff (diagnostic) | 0.56 / 0.81 / 0.57 / 1.05 / 0.94 (was 0.54 / 0.82 / 0.75 / 1.11 / 0.61) | – | – | One maximum per run; reported because it is the standard number |
| D2 mean distance | **0.84** / **0.82** / **0.36** / **0.83** / 1.11 (was 1.15 / 1.15 / 0.43 / 1.10 / 1.41) | ≤ 1.0 | ≤ 0.8 | Parity first; projection (2.8) is the known missing step |
| D3 normal deviation, mean | 1.13 / 1.25 / **0.91** / 1.11 / 1.14 (was 1.26 / 2.23 / 1.18 / 1.22 / 1.35) | ≤ 1.0 | ≤ 0.8 | Same; hard surface also needs creases (5.1–5.3) |
| Vertices on the input: median vertex-to-input distance, ×10⁻³ diag | **0.000005** / **0.000005** / **0.000002** / **0.000005** / **0.000006** (was 0.052 / 0.004 / 0.009 / 0.091 / 0.20) | ≤ 0.001 | ≤ 0.0001 | Instant Meshes projects to 0.0001; QuadriFlow sits at 0.0002–0.04 |
| A1 sharp-feature coverage, hard surface, per mesh (dihedral > 40°; output edge within 0.1 h and 10°; h = local quad size, not the global mean the comparison used) | boat 0.23, wingnut 0.20, csg_bracket 0.07 | ≥ 0.6 each | ≥ 0.9 each | QuadriFlow without its feature option reaches 0.42 on csg_bracket; a hard lock (5.2–5.3) should cover every crease longer than a quad |

### 4.2 Element shape (parity with the best free tool)

Family median of the per-run value; higher SJ and lower skew are better.

| Metric | Today o / h / t / s / m | Min | Stretch | Why |
|---|---|---|---|---|
| G1 scaled Jacobian, 1st percentile | **0.83** / 0.59 / **0.61** / **0.75** / **0.68** (was 0.84 / 0.46 / 0.54 / 0.78 / 0.75) | 0.80 / 0.80 / 0.40 / 0.75 / 0.65 | 0.85 / 0.85 / 0.70 / 0.85 / 0.80 | Min = best free tool's family median, rounded; stretch = our own adaptivity-0 output (0.84 / 0.84 / 0.71 / 0.84 / 0.81), so this pipeline can reach it |
| G2 angle skew, 95th percentile (°) | **22** / 31 / **25** / **24** / 33 (was 19 / 34 / 23 / 24 / 32) | 24 / 19 / 26 / 25 / 31 | 18 / 16 / 20 / 19 / 26 | Same rule |
| G4 aspect, mean | 1.59 / 1.81 / 1.85 / 1.57 / 1.47 (was 1.66 / 1.95 / 2.03 / 1.67 / 1.50) | reported only | | Anisotropy is on by default on purpose (stretched quads along flat directions spend fewer quads per unit of error); the others are at 1.1–1.5 |

The sizing work's success test (4.1/4.5): the stretch row here together with D1r ≤ 0.8 in every family.

### 4.3 Layout (parity floor, not artist flow)

| Metric | Today o / h / t / s / m | Min | Stretch | Why |
|---|---|---|---|---|
| T3 irregular vertices, ratio to QuadriFlow (paired) | **0.90** / 2.01 / 2.71 / **0.82** / **0.70** | ≤ 1.0 | ≤ 0.75 | Adaptivity 0 reaches 0.73 / 1.21 / 2.08 / 0.53 / 0.55 (frozen runs): organic and scans are sizing; hard surface and thin shells need features and thin-part handling (Wave 5, 4.1) [I] |
| T5 3–5 pole pairs per 1k quads | **0.14** / 0.60 / **0.39** / **0.23** / **0** | ≤ 0.5 | ≤ 0.2 | Removable dipoles; adaptivity 0 has 0.10 / 0.12 / 0.51 / 0.07 / 0.82 |
| P1 clustered poles: share with another pole within 2 h | **0.40** / 0.83 / 0.84 / **0.46** / **0.48** | QuadriFlow's: 0.68 / 0.52 / 0.63 / 0.77 / 0.71 | 0.30 / 0.40 / 0.50 / 0.25 / 0.30 | Stretch from adaptivity 0 (0.26 / 0.36 / 0.72 / 0.24 / 0.29), thin shells set below it |
| S2 topological symmetry on symmetric inputs | 0.20–0.49 without a symmetry option (adaptivity 1) | reported only until 5.5; then ≥ 0.95 with a closed seam | 1.00, closed seam | Blender's mirror gives 1.00 but leaves the seam open |

Edge loops (A2–A4), pole placement against curvature (P2–P4) and patch count (T6): no target; not
computed and no reference values (§7).

### 4.4 Predictability and cost

| Metric | Today o / h / t / s / m | Min | Stretch | Why |
|---|---|---|---|---|
| G6 count on the first try, runs within ±5 % | 16/18, 9/9, 12/12, 12/12, 3/3 | all | – | QuadriFlow 18/54 |
| G6 count on the first try, runs within ±2 % | 15/18, 7/9, 10/12, 11/12, 3/3 | – | all | The count controller (4.7) aims at ±1–2 % |
| Count spread over the 5 replicates of a run | (max − min) / mean over plain + 5 replicates, corpus set at 2k/10k: median 1.9 %, 38/72 runs ≤ 2 %, worst 11.7 % (spot_x8 quality 2k) | ≤ 2 % | ≤ 0.5 % | A user re-exporting the same sculpt should get the same count [I] |
| Wall time, ratio to Blender's QuadriFlow | **0.65** / **0.47** / **0.90** / **0.62** / **0.30** (507f9b3; not re-measured: the 2026-10-20 machine ran our side 1.7× faster on the same meshes, so a ratio to the frozen Blender times would mix machines) | ≤ 1.0 | ≤ 0.6 | Kajetan's rule: quality over speed, no pointless slowdowns |
| Peak memory, ratio to Blender's QuadriFlow | **0.24** / **0.23** / **0.27** / **0.29** / **0.24** | ≤ 0.5 | ≤ 0.3 | Laptops |

## 5. How the targets are used

- **Every PR that changes output** (the corpus A/B the threads already run): CI gates pass; no V gate
  loses clean runs or gains defects per 1k on the corpus geo-mean beyond noise (confirm with replicates
  when a count moves less than 1.5×); no soft metric worsens beyond the §2.4 margins. Anything else needs
  a reason in the PR and Kajetan's yes.
- **Releases**: on every platform, the §3.2 ratchet; §3.1 on every run and replicate for 1.0, and
  for 0.x previews (§6, decision 3) no §3.1 gate failing on every replicate of more cases than in
  the previous release (a flip on some replicates only is noise, CLAUDE.md; strict §3.1 fails on master today: RELEASING.md, "Gaps"); for 1.0 also the §3.2 bar and the organic min
  targets of §4. Checked by `release-check.yml` (`tools/bench/release_gate.py`).
- **Gate 7.1** (proposal, for when 7.1 is decided): keep upgrading inside the current structure if,
  after 3.3, untangling and 4.1, the §3.2 1.0 bar holds on ≥ 95 % of corpus runs and the corpus meets
  the §4.1 and §4.2 minimums; otherwise start Stage B (7.2).
- **Follow-ups this file needs** (not done here):
  - move the comparison metrics into `tools/bench` (1.4), with V3's thin-part exclusion and V6's loop
    length, so every number is computed in the repo (2026-10-22: V3 against the oriented input, per-island
    topology, coverage and non-manifold vertices are in `metrics.py`, every gate of §3 in `gates.py`;
    2026-10-23: V3's input-fold exclusion; the thin-part exclusion, V6's loop length and the pole/loop metrics are still open);
  - in `ci/output_gate.py`, gate H3, H4's island count, H8 and a 0.9–1.1 count (H4's coverage is in
    since 2026-10-23: no input island more than 1 % farther than one output edge from the output); its `> 2 % non-quads` limit is a per-run guard and
    can become V1's all-quad once the CI meshes are all-quad on every platform;
  - ~~V1 cluster size and non-manifold vertices into `ab.py compare`, and a corner-Jacobian test on
    non-quads for V2 (1.4)~~ done 2026-10-22: `gates.py` (cluster size, every gate), `ab.py compare`
    rows `nm_vertices` and `folded_faces` (any face size);
  - decide whether to re-derive the §4 min values on 18 meshes (they are tool-relative and were taken
    from 16; e.g. QuadriFlow's P1 on hard surface is now 0.50, not 0.52); the "today" columns were
    re-run on master 47529e1 for all 18 meshes on 2026-10-20 (§2.2);
  - QuadriFlow with `-adaptive -sharp` on the hard-surface and thin-shell meshes, to see whether the
    fidelity lead survives their best settings.

## 6. Decisions (Kajetan, 2026-10-20)

1. **Positioning** (§1): *accepted.* "Faithful, valid, predictable" replaces "ZRemesher-like quality" as
   the goal in [README.md](README.md), with the reading of his three words given in §1. Reason: it is
   where the data says we lead, and ZRemesher's edge flow can't be measured, so it can't be a target yet.
2. **Priority with margins** (§2.4): *accepted.* "No malformations" wins without letting a defect fix
   throw away the fidelity lead.
3. **Release bar** (§3.2): *accepted.* 0.x previews ship under the ratchet, since the add-on panel
   already reports non-quads and folded faces; 1.0 needs §3.1 everywhere and the §3.2 bar, including
   fold-free organic and scan inputs.
4. **Artist-side reference** (§7): *deferred* ("later"). When it is picked up: Quad Remesher's trial
   first (one month, runs in Blender on Linux [P], reportedly by ZRemesher's author [E]); ZBrush's
   14-day trial is non-commercial and Windows/macOS only [P], so it needs another machine. Read each
   EULA before publishing its numbers (some forbid published benchmarks), and keep their output meshes
   out of the repo. Until then layout goals stay "parity with QuadriFlow" (§4.3).
5. **V1 for sculpts**: from 2026-10-21 (*provisional*; Kajetan: "i guess so?") isolated triangles and
   pentagons were tolerated outside hard surface (clusters of at most 2). *Reverted 2026-10-23*
   (Kajetan: "of course", after the re-baseline): V1 is all-quad on every family again, since with
   libQEx the tolerance no longer changed any count and only hid tracer fallbacks (§3.2 V1 history).
   Decided the same day: V3 leaves out flips over folded input and reports them apart; ratchets use
   the mean over replicates; coverage becomes part of H4 in CI. Homology for V6 (1.14) waits.

## 7. Not definable yet, and the cheapest way to get the data

| Missing | Why it blocks a target | Cheapest data |
|---|---|---|
| Artist-side reference values (poles, loops) | Layout goals stay "parity with QuadriFlow" | Quad Remesher trial on the 18 meshes at 2k/10k/50k, OBJs dropped into the comparison scripts (their README says how); one pass, so plan it before the trial starts |
| Loop and pole-placement metrics (A2–A4, P2–P4, T6; SQ-M2, SQ-M3) | Nothing to put a number on | Implement A3 loops and P2/P3 in the metrics (1.4); A2 and T6 later |
| Inputs like the add-on's users have | Public meshes only; no Blender or ZBrush sculpts, and Kajetan is new to Blender | 3–5 dense CC0/CC-BY sculpts (1–5M triangles), downloaded on Kajetan's machine (the cloud can't reach most hosts), licenses in LICENSES §5 (1.5, 1.12) |
| Release-grade families besides organic | Hard surface 3 meshes, multi-island 1, the others 4 | Licensed CAD parts, multi-island models, more scans and shells to 5+ each (1.5) |
| The competitors' best settings | The fidelity lead is against defaults | QuadriFlow `-adaptive -sharp` (§5) |
| When a number starts to bother an artist | Min and stretch are tool-relative, not perceptual | Blind pair ranking by 3–5 artists (0.7 calibration), after the Quad Remesher numbers; no composite score before SQ-M1 |
| Time and memory on real sculpts | §4.4 is from 1.2k–2M-triangle public meshes | 1.12 on Kajetan's machine |

## 8. Critique and what it changed

A reviewer that saw only the proposal and the data attacked the first draft (12 zero-tolerance gates,
strict priority order, family targets at a 0.8 lead). Outcome:

| Attack | Check | Change |
|---|---|---|
| Zero-defect release gates can't be met: no tool passes more than 26 % of runs, every tool folds on parts thinner than a quad | runs.csv, and float32 replicates of a thin leaf (2–12 folds in every replicate) [M, reviewer] | Confirmed. Structural gates stay zero; validity became a ratchet with a 1.0 bar (§3.2) |
| The priority order pushes toward uniform sizing and loses the lead; the 0.5 default may already have lost it | Re-run at 0.5 (this file's "today") | Partly refuted: p99 distance 0.57 overall, under 0.8 in four families; scans 0.82 and Hausdorff on scans 1.15. Order now has margins (§2.4) |
| Family targets can't be resolved on 1–4 meshes | Bootstrap over meshes | Confirmed. PRs judged on the corpus; family targets only for families with ≥ 5 meshes (§2.1) |
| Pleats of convex quads pass the fold gate | metrics.py measures SJ against each quad's own normal | Confirmed; V3 added |
| "Deterministic" and "1-ulp" were underspecified | Smoke-test counts differ between platforms and double-ulp moves often change nothing [reviewer's runs, not re-checked] | Per-build determinism, all three platforms, float32 replicates (§2.5) |
| The fidelity lead is against competitors at their defaults | Not run | Open (§5, §7) |
