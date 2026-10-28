# Usage coverage: what users do with a remesher, and what Quadami was tested on

Audit of 2026-10-27 (thread "Audit typical remesher uses we never tested"), after Blender's default
cone and cylinder, tried by hand, turned out never to have been run before 0.1. Master `fca96dc`
(the v0.1.0 core plus add-on changes), Blender 5.2.2 as `bpy` from PyPI driving the add-on's own code
path (`_mesh_arrays` → `worker.remesh` → `_build_object` → `_keep` → `_show_result`, as
`addon/tests/blender_smoke.py` does), and `tools/bench/ab.py` for every quality number. New inputs:
the `usage` set (`tools/bench/make_usage.py`).

Labels: **[M]** measured with `ab.py` over 6 runs per case (the input plus 5 one-ulp replicates; a
range is min–max over them); *unconfirmed* marks one run; **[A]** measured on the add-on's Python
path in `bpy` (a code path, not geometry: one run decides it); **[C]** checked in code or docs;
**[I]** inferred; **[P]** public source, linked. h = sqrt(input area / target), the planned quad edge.

Owned elsewhere, not repeated: cone, cylinder, icosphere (research/primitives-2026-10-27.md, TODO
2.23, 2.24), modifiers (G14), data kept from the source (#117, G9), thin parts and islands (TODO 4.1,
the flower and sword threads), the input sanitizer (#112, TODO 2.7), the measurement audit (1.13).

## 1. Result

1. **Imported glTF files and converted text never reached the bench as users have them** [M, C]. The
   bench welds glTF before running, and nothing ever ran Convert > Mesh. As Blender gives them:
   smooth models get open cracks along every UV seam and the panel says nothing; flat-shaded models
   and files written without shared vertices break into thousands of pieces (the Khronos sci-fi
   helmet at 1k: 6× the quads in 5,600–6,300 islands, hundreds of folded quads, 66–94 s instead of
   1–2 s); extruded text comes out as open sheets. The open sanitizer PR #112
   fixes all of it on this set: with its seam weld every split mesh matches its welded twin [M, §4.1].
   Until it merges, the user guide says to Merge by Distance first.
2. **Smooth Normal above about 55° destroys Blender's default cube** (9–17 holes, hundreds of
   non-quads and folded quads, 20–40× the time) and from 45° damages the cylinder [M]. The tooltip and the user
   guide recommended it for low-poly input; the guide is fixed here, the core fix is new TODO 2.25.
3. **Very low counts give wrong shapes silently** [M]: below 24 quads the cube is not a cube (no
   warning in half the runs at 6), and Suzanne's eye sockets close at every target up to 200 with no
   word about it. Edge Scaling triggers false
   warnings on every run (new TODO 2.26).
4. **The add-on around the object** [A]: greyed button with no reason in Sculpt or Edit Mode; only the
   active object of several is remeshed; edits on a result are skipped by the next Remesh; constraints
   and animation are not carried over; local view likely goes empty [I]. New TODO G16.
5. **Fine**: negative scale, linked duplicates and library objects, stray vertices [A]; plane, grid,
   filled circle, torus, a cube subdivided to 393k triangles (the input that froze Quad Remesher in a
   forum report), bevelled and rotated cubes, Booleans, re-remeshing a result five times [A, one run
   each, *unconfirmed*]; UV sphere, Suzanne, capped tube after Merge by Distance [M].

Ranked list in §5, matrix in §3.

## 2. What people use remeshers for

From the vendors' own documentation and user threads [P] (sources in §6): character and creature
sculpts, often dyntopo or voxel sculpts, remeshed, sculpted on and remeshed again; heads with fingers
and mouths that need high counts (one user needed about 100k faces to keep fingers); scans and
photogrammetry; CAD, Boolean and 3D-print parts with hard edges; background props where good enough
is enough; a low count followed by Shrinkwrap to the hidden original plus Subdivision or Multires.
Before a run, users are told to merge doubles, fix flipped normals, apply scale, triangulate n-gons
and give the input enough polygons. Reported failures: dense meshes that are not solid, thin fins and
sharp silhouettes dissolving, a heavily subdivided box with 90° edges freezing Quad Remesher (fixed
by softening the edges or tilting the object), pinched parts cut off by QuadriFlow, spirals. Quad
Remesher asks for exactly one selected mesh and hides the original; the closest competitor
(Superhive "AutoRemesher Quad") lists "nothing happens on click: select and activate a mesh" and "no
zero-area faces or fully disconnected pieces" as its first troubleshooting rows.

## 3. Coverage matrix

Status: **tested** (a set, gate or note covers it with numbers), **partly** (some of it, or only by
code reading), **untested** before this audit. "This audit" gives what was run now.

### 3.1 Inputs

| Use | Status before | Where, numbers | This audit |
|---|---|---|---|
| Organic sculpts: heads, bodies, creatures | tested | corpus (spot, koala, cat, hand, human, nefertiti, demosthenes), sculpt and brush sets, external (heads, bodies, creatures), hand-downloaded models, private sculpts; TARGETS baseline | – |
| Dyntopo, voxel-remeshed, decimated sculpts | tested | sculpt set (`sculpt_dyntopo`, `sculpt_voxel`, `sculpt_decimated`), brush set | – |
| Scans and photogrammetry | tested | corpus `demosthenes`, `hammer` (raw scan), `nefertiti`; external `ext_lincoln`, `ext_washington`, `ext_lionstatue`, `ext_scatteringskull` | – |
| Hard surface and CAD | partly | corpus `csg_bracket`, `wingnut`, `boat`; external helmets, watch; primitives (2.23, 2.5, 2.6); crease note; `cad` set in #113 (open). Creases rounded is a known 0.1 limit | Boolean cube ± cylinder (applied), 1k/5k: closed, 0–2 non-quads [A, unconfirmed] |
| Blender default primitives | partly | cube, UV sphere, icosphere, cone, cylinder (`primitives` set) | **plane, grid, filled circle, Suzanne, torus** at 500/5k: all fine except Suzanne's thin-part warning [A, unconfirmed]; circle without fill: "The mesh has no faces" [A] |
| Edited primitives: subdivided, bevelled, rotated and applied | untested | – | Cube subdivided to 1.4k / 45k / 393k triangles (the forum's freezing box): 1k/5k quads in 0.6–10 s, corners within 0.06 of the cube at 1k [A, unconfirmed]; bevelled, rotated 30°/45° and applied: no failure [A, unconfirmed] |
| Game and film assets, retopo targets | partly | external glTF meshes (10 of 20), hand-downloaded models; **all welded by the bench** (`fetch_external.py` joins bit-identical glTF positions) [C] | **Blender's glTF import (defaults) of the same files**, §4.1 |
| Imported files as Blender gives them | untested | – | glTF with the importer's defaults: §4.1. OBJ and FBX not run [I: both keep shared vertices, so they arrive welded] |
| Text and curves converted to mesh | untested | – | §4.1 |
| Props with parts of very different sizes | tested | stress crumbs and islands, private sword and flower (other threads) | – |
| Hair, cloth, foliage cards | partly | corpus `thin_leaf`, `cloth_sheet`; external `ext_sheencloth`, `ext_plant`, `ext_tree`; hand `ext_rain_hair`; thin parts are 4.1 | – |
| Open meshes and shells | tested | corpus `plane`, `cloth_sheet`; suzanne set; open tube in `ci/output_gate.py`; open cone and cylinder | plane, grid: one flat sheet, boundary kept [A] |
| High poly, 100k–5M triangles | tested | stress 5.2M (49 s at 5k, 3.1–3.3 GB); baseline 6.4M; hand-over freeze G2 | – |
| Many islands plus high poly | partly | stress 1,000 islands; hand-downloaded knight; #132's figure with bow (320k triangles, 121 islands): one run stalled 27+ CPU-minutes in rounding at 84 %, *unconfirmed*, owned by the thin-part thread | – |
| Tiny and huge scale | tested | stress: ×10⁻⁴…×10⁴, offsets to 10⁶ robust; USER-GUIDE §7 still said "not tested" [C] | guide row corrected (this PR) |
| Non-uniform scale | tested | G11: warning before and after the run | – |
| Negative scale (mirroring by scale) | untested | – | (−1, 1, 1) and (−1, −1, 1): result faces outward, same placement [A] |
| Object rotation | partly | the core works in object space, so object rotation cannot matter [C] | – |
| Very low-poly input (big facets) | partly | icospheres (primitives); USER-GUIDE §7 advised Subdivision Surface or Smooth Normal | Smooth Normal on low-poly input, §4.2; guide row corrected (this PR) |
| Symmetric models | partly | half + Mirror workflow (G12); no symmetric output from a symmetric input | – |
| Loose vertices, stray points | tested | #112's loose-vertex cases; here a vertex 10⁴ away: ignored, output unchanged [A] | – |
| Remeshing a quad result again (Source cleared) | untested | – | Suzanne + Subdivision 2 at 3k, 5 chained runs: input-to-result p99 0.0165 → 0.0296 (of a 2.7 diagonal), no defects [A, unconfirmed] |

### 3.2 Scene and object state

| Use | Status before | Where, numbers | This audit |
|---|---|---|---|
| Parent (rig, empty) | tested | #110, `blender_smoke.py` | – |
| Constraints placing the object (Copy Location, Child Of, Follow Path) | untested | – | **Result lands where the object would be without its constraints**, §4.4 |
| Animated object transform | untested | – | **Result does not follow the animation**, §4.4 |
| Shape keys | partly | G14 (current mix baked in); USER-GUIDE: not kept | The current mix is remeshed with no warning [A] |
| Modifiers | owned by G14 | – | – |
| Materials, UVs, vertex groups, colours, marks | tested | #117, `blender_smoke.py` | – |
| Linked duplicates (Alt+D), library-linked objects | untested | – | Both work: result placed right, only the remeshed one hidden [A] |
| Collection instances, curves, text objects | partly | poll needs a mesh object [C] | Button greyed with no reason given for a curve object [A] |
| Edit Mode, Sculpt Mode | partly | poll needs Object Mode; USER-GUIDE §3 says so [C] | Button greyed with no reason given in the panel [A], §4.4 |
| Several objects selected | untested | – | Only the active one is remeshed, nothing says so [A] |
| Local view | untested | – | Result not added to the local view, so the viewport likely goes empty [I, code: no `local_view_set`] |
| Undo, Ctrl+Z during a run | untested | – | Not testable headless; the modal handler passes Ctrl+Z through while the worker runs [C] |
| Sculpting on the result, then Remesh | partly | G10: Remesh on a result runs on its source, documented in USER-GUIDE §3 | The edits are silently not in the new result (the edited result is hidden) [A], §4.4 |
| Long runs: progress, cancel, timeout | partly | worker tests: Esc, panel cancel, kill after 5 s grace on all three platforms | Progress names the step; a 220 s run sat 53 s at 49 % [A]; no elapsed time, no timeout, no "longer than usual" hint [C] |
| Non-ASCII user folders | tested | `test_non_ascii_temp_folder` on all three platforms | – |
| Blender versions | partly | CI: 4.2 LTS on Linux, 5.2 on Windows and macOS; 4.3–5.1 never run [C] | – |
| macOS | untested | known: "experimental, untested" | – |

### 3.3 Settings

| Setting | Status before | Where, numbers | This audit |
|---|---|---|---|
| Target Quads 150–50,000 | tested | corpus, closed (from 150), sculpt, external | – |
| Target Quads below 100 (field allows 1) | untested | – | §4.3 |
| Target Quads 200,000+ | partly | confirm dialog; memory constants; hand-over at 200k | – |
| Adaptivity 0–1 | tested | baseline, primitives probes | – |
| Sharp Edge 30–180° | tested | defaults sweep (8 meshes, every angle) | – |
| Anisotropy 0–1 | tested | defaults sweep (18 meshes) | – |
| Edge Scaling 1–4 | untested | – | §4.3 |
| Smooth Normal 0–180° | partly | icospheres at 30° only (primitives) | §4.2 |
| Resolution Fast | tested | corpus, closed and sculpt sets run fast and quality | – |
| Quad Smoothing Keep Ridges | tested | #124 | – |
| Repair Folds, Flatten Spikes | tested | fold-repair notes | – |
| Combinations of non-default settings | untested | – | §4.3 |

## 4. Findings

"Uncovered" is `metrics.py`'s `area_uncovered`: the share of the input's area farther than one output
edge from the output. Every [M] row is 6 runs; ranges are min–max.

### 4.1 Split imports and converted text: cracks, soup, no warning

Blender hands the remesher whatever vertices the mesh has, and the core deliberately joins only
zero-length edges, not coincident vertices (`cleanInput`, `autoremesher.cpp`: welding every
coincident pair merged the cat's self-touching surface) [C]. Three everyday sources leave vertices
split where the surface is meant to be joined:

- **Object > Convert > Mesh on extruded text** leaves the letters' front, back and side walls unjoined
  (1,530 duplicate vertices on "Quadami", extrude 0.1) [A]. The result is 28 open pieces where 8
  closed letters were meant: 2,972–3,114 boundary edges at 5k quads against 6–16 after Merge by
  Distance [M]. A Bezier tube with fill caps: caps stay separate, 168–184 boundary edges against 0 [M].
- **Blender's glTF importer with its defaults** splits vertices along UV seams, and on flat-shaded
  models at every face; a file whose exporter wrote no shared vertices at all (the sci-fi helmet:
  70,074 vertices for 23,358 triangles, smooth normals) comes in the same way. The `external` set never
  saw this (the corpus and private sets were not checked for split files): `fetch_external.py` welds bit-identical glTF positions
  before the `external` set runs [C], so 10 of its 20 meshes were measured welded.

  | glTF file, as imported (target 1k / 5k) | islands in → out | boundary edges out | quads | uncovered | panel says |
  |---|---|---|---|---|---|
  | Suzanne + Subdivision 2, smooth shaded | 3 → 5 (welded: 3) | 252 / 508 (welded: 52–54 / 92–94) | on target | 0 | thin parts at 1k only; nothing about the cracks |
  | Leeperrysmith head scan | 15 → 13–14 (welded: 1) | 343–363 / 642 (welded: 0) | on target | ≤ 4e-5 | "2 of 15 islands produced no faces" |
  | Barramundi fish | → 6 (welded: 3) | 262 / 536 (welded: 0) | on target | 0 (welded: 0.7–2.4 %, thin fins) | nothing |
  | Suzanne + Subdivision 2, flat shaded | 7,872 → 6,077–6,218 | 11,384–13,970 | **9,726–9,728 / 10,775–10,777** | 49 % / 26 % (see below) | "1,795 of 7,872 islands produced no faces" |
  | Sci-fi helmet (Khronos sample, no shared vertices in the file) | 23,358 → 5,641–6,274 | 21,273–23,517 | **7,080–7,348 / 7,798–7,992**, 304–441 non-quads, 789–921 folded | 52–53 % / 42–44 % (see below) | "17,525 of 23,358 islands produced no faces"; 66 s alone, 73–94 s with 4 jobs, against 1–2 s welded |

  [M] all rows; the helmet's 66 s and the CLI's own peak memory report (687 MB) are one run each
  (*unconfirmed*; `ab.py`'s peak column is not usable, see §7). **The uncovered column overstates
  the loss for the shattered rows:** `metrics.py` counts input area farther than one *output* edge,
  and a shattered output's mean edge is about 10× shorter than a whole one's. Measured against the
  welded run's edge, flat Suzanne leaves 1.4 % uncovered and the helmet 1.7 % [C, disprover's own
  code]. The failure is the thousands of islands, the folded quads and the time, not missing area.
- The stress test's unwelded soup (TODO G13) is the flat-shaded and no-shared-vertices case; the smooth case (seams only)
  is new: it **never warns**, and its cracks are invisible in solid view.

Welding is not free either: welded text at 1k loses 9–19 % of its area (thin closed letters collapse,
the thin-part warning fires), where the split open sheets lose 0.3–2.2 % [M]; the welded fish loses
its thin fins the same way. So "merge first" is right, and the thin-part warning then explains the
loss. **Second route:** the open sanitizer PR #112 welds open seams in the core; the same set on its
head (`fc85aa6`), 6 runs per case, makes every split mesh behave like its welded twin [M]:

| As imported, on #112 (1k / 5k) | islands out | boundary edges out | quads | uncovered | s |
|---|---|---|---|---|---|
| Sci-fi helmet | 16 / 17 (welded: 16 / 17) | 212–226 / 420 (welded: 218–236 / 402–420) | 1,137–1,181 / 5,068–5,071 | 2.3–4.7 % / 0.1 % | 2.4–2.8 / 4.7–6.0 |
| Suzanne, flat shaded | 3 | 54 / 92–94 (= welded) | 1,001 / 5,052–5,093 | 0 | 1.1–1.4 / 6.9–10.2 |
| Suzanne, smooth shaded | 3 | 54 / 92–96 (= welded) | on target | 0 | as welded |
| Leeperrysmith, barramundi | 1, 3 (= welded) | 0 | on target | as welded | as welded |
| Text, as converted | 8 (= welded) | 0–16 / 4–26 (welded: 0–4 / 6–16) | on target | 10–20 % / ≤ 0.3 % (= welded) | as welded |
| Tube with caps | 1 | 0 | on target | 0 | as welded |

The corset stays at 75 islands either way: it is made of separate pieces, not split ones. The helmet's
remaining open edges are its real open borders. So the core fix exists. What stays open after #112
merges: the panel still does not say when it welded something (G13), and the text's thin letters
still collapse at 1k (the thin-part warning covers it).

### 4.2 Smooth Normal breaks boxes and cylinders, and the guide recommends it

Smooth Normal leaves a face out of a vertex's normal when the face turns more than the angle from
the vertex's area- and angle-weighted average normal, then projects onto PN triangles built on those
normals (`computeSmoothVertexNormals`, `thirdparty/isotropicremesher/isotropicremesher.cpp`) [C]. At a
box corner every face is 54.7° from the average, at a cylinder rim 45°, so above those angles no face
is left out and the box becomes one bulging PN surface through 8 corners [C for the angles, I for
the mechanism]. A dihedral test between neighbouring faces (as Blender's auto smooth does) would
keep the corners [I]. Measured on Blender's defaults [M]:

| Smooth Normal | cube 1k | cube 5k | cylinder 5k | UV sphere, Suzanne (1k, 5k) |
|---|---|---|---|---|
| 0 (default) | clean | clean, 4.6–5.4 s | clean | clean |
| 45° | clean | clean | 0–76 non-quads, 0–2 handles added | clean |
| 60° | closed and whole, but a puffed cube bulging up to 0.32 past the faces (half-size 1) | **9–17 holes added (the panel says "12 holes" and "9 handles more"), 32–49 islands, 2,698–3,463 boundary edges, 3,995–8,199 quads, 345–427 non-quads, 754–1,096 folded, 140–184 s** | 0–94 non-quads, 0–5 handles added | clean |
| 90°, 180° | same as 60° | same as 60° (104–179 s) | **4–6 holes added, 0–141 non-quads, 120–169 folded** | clean |

At 1k the cylinder stays clean at every angle. The cube's runs at 60°, 90° and 180° are identical:
above 54.7° nothing is left out; at 54° the output stays inside the cube, at 55° it reaches 1.32 [M,
disprover's runs]. The puffing itself is what the option is for on a rounded shape; on a box it is
the wrong surface, and at 5k it tears. (`metrics.py` reads the 1k puffed cube as 55–58 % uncovered
and the cylinder at 90° as 21–23 %: distance from the flat input, not lost surface.)

The tooltip says Smooth Normal "helps on low-poly input", and USER-GUIDE §7 told users with big
flat facets to "use Smooth Normal" (fixed in this PR); a default cube is the first low-poly input
anyone tries. Combined with Edge Scaling 4 on the cube one run took 220 s and another was stopped
after 705 s at 1.9 GB [A, *unconfirmed*].

### 4.3 Very low counts and setting combinations

**Very low Target Quads** (the field accepts 1; soft minimum 100) [M]:

| target | default cube | default Suzanne (head with two open eye sockets, two eyeballs) |
|---|---|---|
| 6 | 3 quads on 5 vertices in every run: no cube left (Hausdorff 39–84 % of the diagonal). In 3 of 6 runs the 3 quads span the cube and **nothing warns**; in the other 3 the cube shrinks to a speck and "far fewer quads than planned" fires | 72–84 quads (the small-island floor), 43–88 % uncovered, **both eye sockets capped** |
| 12–20 | 3–27 quads, Hausdorff 23–87 %; "far fewer quads than planned" in at least half the runs at each target | as at 6 |
| 24 | 24 quads in every run, a strongly rounded cube (Hausdorff 18–22 % of the diagonal), no warning | as at 6 |
| 50–200 | on target; corners still rounded (Hausdorff 14–16 % at 50, 5–7 % at 200), no warning | 100: 159–163 quads, 200: 230–232; 15–18 % uncovered; sockets still capped |
| 1k | clean | clean, openings kept |

At every target from 6 to 200 the head island's 2 boundary loops become 0 in every run (Euler
characteristic 0 → 2) and nothing reports it; only the thin-part warning fires [M, and checked on
`out.obj` by the disprover].

So below about 24 quads a cube is not a cube, half the time without a warning, and up to 200 quads
Suzanne's eye sockets close without a word. The holes warning watches only closed islands that gain holes; an open island that loses a
boundary is not reported (stress G8 asked for the converse, holes on open islands).

**Edge Scaling** works as documented (4 → about a sixteenth of the target: 292–336 at 5k, 56–72 at
1k), but the report then says "1 island came out with far fewer quads than planned (smallest: 336 of
5000)", and at 1k also "Parts of the model are missing (up to 3 quad widths)": the collapsed-island
check compares with Target Quads, not with Target Quads / Edge Scaling² [M]. A false alarm on every
run with Edge Scaling above 1.

**Settings combined**: on the default cube at 5k, Adaptivity 1 with Sharp Edge 30° gives 1,111–1,256
quads (with Anisotropy 0 as well, 959–1,038), where each setting alone gives 5,040 and 5,079 (one run each) and either with Anisotropy 0 gives 4,926–5,065 [M]. The report does say "1 island came out with far fewer quads than planned (1169 of 5000)". On the
cylinder, UV sphere and Suzanne + Subdivision 2 the same pair stays on target [*unconfirmed*, one run
each], so it is a few-triangle box effect. Non-default settings are a ratchet, not a gate (ENVELOPE
row 15), so this is recorded, not a bug by the targets' rules.

### 4.4 Add-on behaviour around the object [A]

- **Constraints**: a sphere placed at (4, 2, 0) by a Copy Location constraint gets its result at
  (0, 0, 0): `_place_like` copies the local transform and parent, not constraints [A, C]. Same for
  any constraint that moves the object (Child Of on a rig, Follow Path).
- **Animation**: an object keyed from x = 0 to 5 gets a result that stays at the frame it was made on;
  `animation_data` is not copied [A].
- **Shape keys**: with a key at value 1, the current mix is remeshed (bulge in the result), keys are
  dropped, and nothing says which shape was used [A] (in G14's list).
- **Sculpting on the result, then Remesh**: Remesh on a result runs on its source (G10, documented),
  so the sculpted result is hidden and its edits are not in the new result; nothing in the panel
  says the result was changed since the run [A].
- **Several objects selected**: only the active one is remeshed, nothing says so [A]; Quad Remesher
  refuses unless exactly one mesh is selected [P].
- **Edit Mode, Sculpt Mode, curve or text object**: the button is greyed with no reason in the panel
  [A]; the guide says to use Object Mode [C]. Sculptors work in Sculpt Mode.
- **Local view**: the result is linked into the source's collections but not added to the local
  view, and the source is hidden, so the viewport likely shows nothing [I: the add-on never calls
  `local_view_set`; local view cannot be entered headless].
- Fine: negative scale, linked duplicates, library-linked objects, a far stray vertex, Booleans, the
  plane, grid, filled circle and torus [A].


## 5. Ranked gaps and where they go

Ranked by how likely a new user hits it, then by how bad it is. Likelihood is judged from §2's
sources and the default workflow [I]; severity from §4 [M/A].

| # | Gap | Likely | Severity | Where it goes |
|---|---|---|---|---|
| 1 | **Imported glTF** (Sketchfab, Khronos samples, most free assets): cracks along every UV seam with no warning; flat-shaded and no-shared-vertex files break into thousands of pieces, many times the target, hundreds of folded quads, 10–60× the time | high | high (flat), medium and silent (smooth) | #112's seam weld fixes it on every mesh here [M, §4.1]; until it merges, USER-GUIDE §7 row (this PR); G13 (panel says what it welded) |
| 2 | **Smooth Normal on a box or cylinder**: holes, extra handles, hundreds of folded quads, 20–40× the time; the tooltip and the guide pointed low-poly users to it | medium (every curious user tries the cube with every setting) | high | 2.25 (core: dihedral test); USER-GUIDE §7 fixed here; tooltip in 2.25 |
| 3 | **Text and curves converted to mesh**: open pieces with gaps | medium-high (text is a classic first test) | medium | as #1 (#112 welds it [M]), USER-GUIDE §7 row (this PR) |
| 4 | **Remesh greyed in Sculpt or Edit Mode** with no reason in the panel | high | low (confusion) | G16 (a) |
| 5 | **Sculpting on a result, then Remesh** runs on the original and hides the edits | medium (remesh–sculpt–remesh is a common loop, §2) | medium (work looks lost; it is only hidden) | G16 (c) |
| 6 | **Very low counts**: a cube below 24 quads is not a cube, Suzanne's eye sockets close at every target up to 200; missing (half the cube runs, all the socket closings) or wrong warnings | medium (game props, LODs) | medium | 2.26 |
| 7 | **Several objects selected**: only the active one is remeshed, silently | medium | low | G16 (b) |
| 8 | **Local view**: the view empties after a run | low-medium | medium (looks like data loss) | G16 (e), check by hand first [I] |
| 9 | **Edge Scaling** above 1: false "far fewer quads" and "missing parts" warnings on every run | low (the guide says leave it at 1) | low | 2.26 |
| 10 | **Constraints or animation** on the source: result misplaced or static | low | medium | G16 (d) |
| 11 | **Long runs** show no elapsed time | medium | low | G16 (f) |
| 12 | Blender 4.3–5.1 never run in CI; macOS never run on a Mac | low | unknown | CI matrix (not filed); macOS known |
| 13 | Adaptivity 1 + Sharp Edge 30° on a 12-triangle box: a quarter of the target | low | low | recorded only (non-default settings are a ratchet) |

What the matrix says beyond the list: the bench's sets were chosen for the *core* (sculpts, scans,
thin parts, genus), and every input reached it already cleaned (welded glTF, applied transforms,
panel defaults). What a user hands the add-on, straight from an importer, a conversion or a
non-default setting, had no set until `usage`. A rule that would have caught #1–#3 earlier: every
public mesh source enters the bench twice, as the bench cleans it and as Blender gives it, and every
panel setting gets one sweep on the `usage` and `primitives` sets at 1k and 5k.

## 6. Sources

Reached [P]:
- Exoside, *QuadRemesher 1.3 User Doc*: https://www.exoside.com/quadremesherdata/QuadRemesher_1.3_UserDoc.pdf
- Superhive, *AutoRemesher Quad* documentation: https://superhivemarket.com/products/autoremesher-quad-one-click-quad-remesh-for-blender/docs
- Blender Artists, *How good is QuadriFlow remesh compared to other auto-retopo solutions?*: https://blenderartists.org/t/how-good-is-quadriflow-remesh-compared-to-other-auto-retopo-solutions/1547001
- Blender Artists, *Quad Remesher auto-retopologizer* (posts 113–131): https://blenderartists.org/t/quadremesher-auto-retopologizer-now-available-for-blender/1170913/117
- Blender Artists, *Tesselator Quad-Remesher* (page 14): https://blenderartists.org/t/tesselator-quad-remesher/1131624?page=14
- Blender Artists, *Quadremesher breaks the model*: https://blenderartists.org/t/quadremesher-breaks-the-model/1507151
- Nomad Sculpt forum, *Quad remesher does not handle modified boxes*: https://forum.nomadsculpt.com/t/quad-remseher-does-not-handle-modified-boxes/23660
- Blender bug T69838, *The new quad remesher badly deforming the mesh*: https://developer.blender.org/T69838
- Blender issue tracker search "quadriflow" (API): https://projects.blender.org/api/v1/repos/blender/blender/issues?q=quadriflow&type=issues&state=all

Not reached: the projects.blender.org issue search page (client error; its API worked); Reddit,
Polycount and ArtStation are blocked from the cloud (2026-10-27, guides-in-practice thread), so
r/blender and Polycount retopology threads are missing, and they are where beginners post most.

## 7. What would falsify this, and what was not re-checked

**Would falsify:**
- §4.1: a Blender user path that welds on import by default (an importer default other than the one
  measured in Blender 5.2.2, or Sketchfab's Blender download using another format); then #1 and #3
  would fall in likelihood, not in severity.
- §4.2: Smooth Normal at 60° and above on other boxes (bevelled, subdivided, rotated) staying clean;
  then it is the 12-triangle cube only, and #2 drops to "default cube".
- §4.3: a Suzanne whose sockets stay open at 6–200 in Blender itself (the add-on path) would mean
  the CLI runs differ from what users get.
- Any [A] row: the modal operator in a real Blender window behaving differently from the headless
  steps (`blender_smoke.py`'s premise).

**Not re-checked:**
- Local view (§4.4) is inferred from code; nobody has opened local view and remeshed.
- Undo during a run; Blender 4.3–5.1; Windows and macOS (everything here ran on Linux).
- Peak memory from `ab.py`: on this machine its `peak_rss_mb` read 1,460–3,652 MB for runs that
  peak at 57–130 MB in a fresh process. In the master run every case measured after the flat-shaded
  Suzanne read 1,447–1,460 MB, and every case after the helmet read 3,584–3,652 MB; the #112 run, with
  no big case, read 63–153 MB throughout. That fits an RSS high-water mark the CLI inherits at fork
  from the harness, which grows while it measures big meshes [I, mechanism not traced]. So memory
  numbers here come from single fresh runs only (passed to the measurement audit, TODO 1.13).
- The bpy quick checks marked *unconfirmed* (one run each): plane, grid, circle, torus, subdivided,
  bevelled and rotated cubes, Booleans, chained re-remeshing.
- §2's likelihood judgements are mine [I]; no usage data exists.
- Disprover: a separate worker re-checked the headline claims from the run folders with its own
  code (read `out.obj`, welded the imports itself, re-ran the CLI). It confirmed that the core does
  not weld, the smooth-import cracks (median gap 0.06 quad edges, largest 0.84), the text split
  (1,530 duplicates), the flat-import quad counts (helmet re-run alone: same counts, 66 s), the
  Smooth Normal holes and the 54.7° threshold, and the Adaptivity + Sharp Edge drop. It corrected
  five claims, all fixed above: "uncovered" on shattered or puffed outputs is not lost area; the
  helmet file has no shared vertices rather than flat normals; the cube at 6 warns in half the
  runs; Suzanne's sockets close at every target up to 200, not only at 100–200; and this audit's
  table script divided Hausdorff by the diagonal twice (§4.3's figures are corrected). It did not
  check #112's run, its own CLI runs with replicates, or whether the corpus and private sets hold
  split meshes.
