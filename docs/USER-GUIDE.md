<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 Quadami contributors
-->

# Quadami user guide

For people who sculpt or model in Blender and want a clean quad mesh, and who are not programmers.
Written for **Blender 4.5 LTS**; Blender 4.2 and newer work the same way.

What the add-on does is checked against its code (`addon/quadami/`, master of 2026-10-26). Blender's
own menus are described from memory where marked *(from memory, check)*: if a menu looks different on
your screen, trust your screen and tell us.

Contents: [1 Words used here](#1-words-used-here) · [2 Install and update](#2-install-and-update) ·
[3 Your first remesh](#3-your-first-remesh) · [4 The settings](#4-the-settings) ·
[5 Reading the result](#5-reading-the-result) · [6 Warnings and what to do](#6-warnings-and-what-to-do) ·
[7 Sculpts: what works and what struggles](#7-sculpts-what-works-and-what-struggles) ·
[8 Folded input and Repair Folds](#8-folded-input-and-repair-folds) ·
[9 Troubleshooting](#9-troubleshooting)

---

## 1. Words used here

| Word | Meaning |
|---|---|
| **Remesh** | Throw away the old wireframe of a model and build a new one over the same shape. |
| **Quad** | A face with 4 corners. Animators and modellers prefer them: they bend cleanly and you can select whole loops of them. |
| **Non-quad** | Any face that is not a quad: a triangle (3 corners), a pentagon (5), and so on. A few are normal; the goal is zero. |
| **Edge loop** | A row of edges that runs around the model, like a line of latitude on a globe. |
| **Pole** | A vertex where more or fewer than 4 edges meet. Unavoidable on round shapes; good remeshes put few of them in sensible places. |
| **Folded face** | A face that is bent over itself: one of its corners points inwards, or it is twisted like a bow tie. It shades badly and breaks loops. |
| **Island** | One separate piece of the mesh that does not touch the others, such as an eyeball next to a head, or each part of an object made with **Ctrl+J** (Join). |
| **Closed** | A mesh with no holes in its surface, like a ball. *Open*: it has borders, like a sheet or a cup. |
| **Hole** | A gap in a surface that was closed before remeshing. |
| **Non-manifold** | Geometry that cannot exist on a real solid surface: an edge shared by three or more faces (a fin), or a vertex where two surfaces only touch at a point. It breaks modifiers, 3D printing and many tools. |
| **Winding flip** | Two neighbouring faces that disagree about which side is "outside". Shows as dark patches with some shading. |
| **Genus** | The number of handles a shape has: a ball has 0, a doughnut or a mug 1. A good remesh keeps it: no new tunnels, no closed ones. |
| **Thin part / ribbon** | A part thinner than one quad is wide: leaves, vines, cloth, a blade edge. |
| **Pleat / folded input** | Places where the *original* sculpt surface is folded over itself, often from a Twist/Rotate brush or a crease pinched too hard. You may not see them; they are inside a fold. |
| **Modifier** | A non-destructive effect on an object (Subdivision Surface, Solidify, ...), in the Properties editor under the blue **wrench** tab. |

---

## 2. Install and update

The add-on is one `.zip` file with the remesher program inside. Do **not** unpack it; Blender installs
the zip itself. You need **Blender 4.2 or newer**. Marks used below: *(CI)* checked by the project's
automatic tests, *(from memory, check)* not checked on a real screen.

### Get the zip

Download it from the [Releases page](https://github.com/KajetanMuczynski/quadami/releases/latest):
under **Assets**, click the zip for your system. No GitHub account is needed.

| System | Zip on the Releases page | Tested on a real computer? |
|---|---|---|
| **Linux** (64-bit Intel/AMD, any distribution with glibc 2.28 or newer: Ubuntu 20.04+, Debian 10+, Fedora 29+, RHEL 8+) | `quadami-<version>-linux-x64.zip`. Blender from blender.org or the snap (`/snap/bin/blender`). Or build it yourself ([addon/BEGINNERS.md](../addon/BEGINNERS.md)). | Yes (Ubuntu, Blender 4.5 snap) |
| **Windows** 10 or 11 (64-bit) | `quadami-<version>-windows-x64.zip` (the program is not signed) | No, only in CI (Blender 5.2, no window) |
| **macOS**, Apple silicon (M1 or newer), macOS 11.2 or newer: **experimental, untested** | `quadami-<version>-macos-arm64.zip` (not notarized, so macOS may block it) | No, only in CI |
| macOS on Intel, Linux on ARM, Windows on ARM | Not built. Intel Macs: build it yourself ([BUILDING.md](BUILDING.md)). | No |

`SHA256SUMS.txt` next to the zips lets you check a download; the release notes say how.

### Test builds between releases

Every change merged after a release is built as a test build: newer, but checked less than a
release. Use one only when asked to try a fix, and send its [build line](#which-version-do-i-have-the-build-line) with any report.

1. Sign in to GitHub (downloads of these files need an account).
2. Open <https://github.com/KajetanMuczynski/quadami/actions/workflows/ci.yml?query=branch%3Amaster>
   and click the newest run where the job for your system (**addon (linux-x64)**,
   **addon (windows-x64)** or **addon (macos-arm64)**) has a green tick. The run as a whole may be red
   only because the macOS job never got a computer; the Linux and Windows zips are still there *(CI)*.
3. Scroll to **Artifacts** at the bottom and click `addon-linux-x64`, `addon-windows-x64` or
   `addon-macos-arm64`.
4. GitHub wraps the add-on in a zip of its own. Unpack that outer zip once (Linux: `unzip`;
   Windows: right-click > **Extract All...**; macOS: double-click). The `quadami-0.1.0-<system>.zip`
   inside is the add-on: keep it zipped.

These files are deleted by GitHub after its retention period (90 days unless the repository says
otherwise *(from memory, check)*), so download a newer one if the link is gone.

### Install (first time and every update)

The same on every system:

1. Open Blender. **Edit > Preferences** (macOS: **Blender > Settings** *(from memory, check)*).
2. Left column: **Get Extensions**.
3. Top right of that window: the small **down arrow (⌄)**, then **Install from Disk...**, pick the zip.
   Dragging the zip from your file manager onto the Blender window does the same.
4. **Restart Blender.** Needed if Blender showed "Missing Add-ons" before, and the safe habit after every
   update.
5. Check: **Edit > Preferences > Add-ons**, find **Quadami**, make sure it is ticked, and click the arrow
   left of its name. Under **Remesher executable** (leave that field empty) it must say
   **Using: .../bin/autoremesher-cli** with a tick (Windows: `autoremesher-cli.exe`). "not found" means
   the zip has no remesher inside, or one for another system: get the right zip.

Installing a newer zip replaces the old copy (same add-on id, `quadami`). Every Blender version keeps its
own add-ons, so if you have two Blenders, install in the one you use.

### Where Blender keeps it

| System | Folder (replace `4.5` with your Blender version) |
|---|---|
| Linux, blender.org `.tar.xz` | `~/.config/blender/4.5/extensions/user_default/quadami` |
| Linux, snap (`/snap/bin/blender`) | Usually the same folder, since the Blender snap may read your home folder (classic confinement). Installing with `/snap/bin/blender` worked on the maintainer's laptop (2026-10-22); which folder it used was not recorded. The install script below asks Blender and prints the folder it really uses. |
| Linux, Flatpak | Not supported: its sandbox cannot start the remesher. Use the `.tar.xz` or the snap. |
| Windows | `%APPDATA%\Blender Foundation\Blender\4.5\extensions\user_default\quadami` *(CI, with 5.2)* |
| macOS | `~/Library/Application Support/Blender/4.5/extensions/user_default/quadami` *(from memory, check)* |

The remesher program is in its `bin` folder.

### Remove it

**Edit > Preferences > Add-ons**, expand **Quadami**, open the **⌄** or **...** menu next to it >
**Uninstall** *(from memory, check)*. Or quit Blender and delete the `quadami` folder from the table
above. Results you already made stay in your files; they are ordinary mesh objects.

### Install scripts (optional, they also clean up)

- **Windows:** [windows-test/quadami-update.ps1](windows-test/quadami-update.ps1), described in the
  [Windows test sheet](windows-test/README.md) §4. Quit Blender, then in PowerShell:
  `powershell -ExecutionPolicy Bypass -File .\quadami-update.ps1 .\quadami-0.1.0-windows-x64.zip "C:\Program Files\Blender Foundation\Blender 4.5\blender.exe"`.
  It only lists what it would do; add `-Apply` to do it. Tried only against a stand-in Blender, not
  on real Windows.
- **Linux:** `quadami-update.sh`, the same for Linux, comes with the maintainer's test builds (it is not
  in this repository yet). Give it the zip and the Blender you start, for the snap
  `/snap/bin/blender`: `sh quadami-update.sh --apply <zip> /snap/bin/blender`.
- Both install with Blender's own command, which also works without the scripts:
  `blender --command extension install-file -r user_default -e <zip>` *(CI)*. With the snap, write
  `/snap/bin/blender` instead of `blender` if your shell does not find `blender`.

### macOS: if the remesher is blocked

The program inside the macOS zip is not signed by a registered Apple developer ("notarized"). Nobody has
tried it on a real Mac yet, so we do not know whether macOS blocks it. If a remesh fails at once or macOS
says the program "cannot be opened", quit Blender and in the Terminal run
`xattr -dr com.apple.quarantine "<the quadami folder from the table above>"`, then start Blender again.
Please report what happened either way.

### Which version do I have? (the build line)

Blender's extension list shows version 0.1.0 for every build, so it cannot tell builds apart. Use the
**build line** instead: a small grey line at the very bottom of the Quadami panel (and in the add-on's
preferences), for example `Add-on build 4e9cdc7 2026-10-21`. The first part is the code version (a
commit), the second its date. Copy that line into every bug report.

- `pr65 abc1234+master`: a test build of a pull request, not a release.
- `-dirty` or `dev`: a developer's build.
- **Red** with a warning icon and a second line *Remesher build ...*: the add-on and the remesher inside
  it come from different builds. Reinstall the zip.
- No build line at all: a build older than 21 October 2026. Update.

---

## 3. Your first remesh

**Moving around on a touchpad.** Use the gizmo at the top right of the 3D Viewport: drag the coloured
axis ball to orbit, drag the magnifying glass up or down to zoom, drag the hand to pan. **View > Frame
Selected** brings the selected object into view. For Alt + drag to orbit: **Edit > Preferences > Input >
Emulate 3 Button Mouse**.

**A practice model.** Click the default cube, press **Delete**, then **Add > Mesh > Monkey** in the
viewport header. Press **Ctrl+2** to give it a Subdivision Surface modifier at level 2 *(from memory,
check)*. Target Quads 2000 is a good first try on it.

1. Stay in **Object Mode** (drop-down at the top left of the 3D Viewport). The Remesh button is grey in
   Sculpt or Edit Mode, when the selected object is not a mesh, and while a remesh is running.
2. Click your model so it has an orange outline.
3. Mouse over the 3D Viewport, press **N** (or **View > Sidebar**). On the sidebar's right edge are
   vertical tabs; click **Quadami** (scroll the tabs if there are many).
4. Set **Target Quads** (start with 5000, see [§4](#4-the-settings)), click **Remesh**.
5. The bottom status bar shows `Quadami: 42% <step> (Esc to cancel)`. Blender stays usable meanwhile.
   **Esc** or the panel's **Cancel** stops it; nothing is added then.
6. When done, a new object **<name>_remesh** appears and is selected, and your original is **hidden, not
   deleted**, in the viewport and in renders (Outliner, top right: click its closed eye icon to show it
   again; to render it again, also click its camera icon, which the Outliner's **Filter** menu can show).
   The panel shows the result rows ([§5](#5-reading-the-result)).

**What the result keeps:** the original's parent, position, rotation, scale and collections, so it
moves with the same parent (a rig or an empty). It is smooth shaded if the original mostly was, flat
if the original was flat. Under **Keep from Original** (a collapsed section of the panel):

| Option | Default | What the result gets | Limits |
|---|---|---|---|
| **Materials, Groups, Marks** | on | The original's materials (each face takes the material of the original under it), vertex groups and colour attributes (blended from the original's surface), and its **sharp**, **crease** and **seam** marks. | The quads do not follow material borders, and they follow marked lines only with **Follow Marks**: otherwise a border or a marked line becomes the connected run of quad edges nearest to it (straight where the quads run along it, a staircase where they cross it). Lines shorter than about one quad are dropped. Shape keys and face sets are not kept. |
| **UV Maps** | off | The original's UV maps, with each face kept inside one UV island, so a texture still fits. | A face that crosses a UV seam reaches a little past its island's border in the texture (into the margin around the island); on test models with many small islands up to 6 % of the faces came out distorted. For a clean new map instead: keep this off, select the result, **Tab**, **A**, **U > Unwrap** *(from memory, check)*: the copied seams cut the same islands as on the original. |

The panel line **"Copied from the original: …"** lists what came across. Copying takes a few seconds on
large models, and Blender waits meanwhile: at 50,000 quads about 1.5 s from a 330,000-triangle
original and 4 s from 1.3 million triangles, plus 3 s and 10 s with **UV Maps**.

**Remesh again, other settings:** with the result selected, change the settings and press **Remesh**.
It remeshes your **original** again (the panel's **Source** field under the button names it), never the
quad result, and the new result replaces the old one (the old one is hidden). To remesh the result
itself instead, clear **Source** with its **X**.

**Scale evenly, or apply the scale.** The remesher works on the object as if its scale were 1, so an
object scaled unevenly (say 3 along X, 1 along Y and Z) comes out with quads stretched 3 : 1. The panel
warns in red under **Remesh** when the scale differs by more than 5 % between axes. Fix: select the
object, **Ctrl+A > Scale**, then remesh.

To see the new wireframe: **Tab** into Edit Mode (Tab again to leave), or the **Overlays** drop-down in
the viewport header > tick **Wireframe** *(from memory, check)*.

**Modifiers count.** The add-on remeshes the model *as you see it*, with its modifiers (Subdivision
Surface, Solidify, Mirror ...) at their **viewport** level. You do not need to apply them first.

**Undo:** **Ctrl+Z** once should remove the result *(not tried in a real Blender window yet)*. The
original is still in the file either way.

---

## 4. The settings

The panel shows three settings and **Follow Marks**, then a folded **Advanced** section (click it to open). Start with the
defaults and change one thing at a time.

| Setting | Default | What it does | When to change it |
|---|---|---|---|
| **Target Quads** | 5000 | How many quads to aim for. The result lands near it (between 60 % and 150 % counts as normal). Memory: about 2 GB per 100,000 quads. | The main knob. 2000–5000 to judge the flow quickly; 10,000–20,000 for a detailed sculpt. Raise it when thin parts fold or details vanish ([§7](#7-sculpts-what-works-and-what-struggles)). Above 200,000 the add-on asks first and shows the memory it needs; the panel will not go above 2,000,000. |
| **Adaptivity** | 0.5 | 0: all quads the same size. 1: smaller quads where the surface curves strongly, bigger on flat parts. | 0 gives the fewest folded quads; 1 gives about twice as many. Use 0 for even topology (e.g. to sculpt further on it). It does **not** move quads onto thin parts. |
| **Sharp Edge** | 90° | Edges where the surface bends more than this angle are treated as creases: the quads follow them, but they come out slightly rounded ([§7](#7-sculpts-what-works-and-what-struggles)). Marked edges are only used with **Follow Marks**. | Lower (e.g. 60°) for hard-surface models with bevels you want to keep, but it makes about twice as many triangles and other non-quads. Raise to 180° for soft organic sculpts, so nothing is treated as a crease. Tested quality numbers are for 90°. Edges of exactly 90° (Blender's cube and cylinder) sit right on the default: see *Hard edges* in [§7](#7-sculpts-what-works-and-what-struggles). |
| **Follow Marks** | off | Tick Sharp, Crease or Seam: edges with that mark keep a row of quad edges along them, whatever their angle, also on smooth parts (a design line, a sphere's equator). The line under it counts the marks on the active object. | For hard-surface models where you marked the edges to keep. On Blender's cube, cylinder, cone and bevelled boxes, a quad edge then follows 75–99 % of the marked length at 5,000 quads instead of 7–72 % (at 1,000 a cone's rim is still rounded off). Leave it off when the line says most edges are marked: an FBX or OBJ import often marks nearly every edge, and such marks are skipped anyway (with a warning). Marks that cross other creases at an angle or each other in many short lines can fold the quads; such a part is remeshed without its marks, and a warning says so. |
| Advanced > **Resolution** | Quality | Quality solves on a finer mesh: cleaner quads, fewer flipped ones. Fast is quicker and rougher. | Fast for quick previews (Quality takes about 1.5 times as long), Quality for the result you keep. Quad count is the same. |
| Advanced > **Anisotropy** | 1 | Lets quads stretch along the direction the surface curves least, like long quads on a cylinder (up to about 2.3 : 1). | 0 for square quads everywhere; the result then follows the shape 25–35 % less closely. Hardly changes anything else. |
| Advanced > **Edge Scaling** | 1 | Multiplies the quad size; 2 gives about a quarter of the quads. | Leave at 1 and use Target Quads instead; it is the same thing, less clearly. |
| Advanced > **Smooth Normal** | 0° | 0: follow the model's own flat triangles. Above 0: follow a smooth surface through them, keeping edges sharper than this angle. | Only for low-poly input (big flat facets). Sculpts are already dense: leave 0. |
| Advanced > **Quad Smoothing** | Standard | How the quads are evened out after they are found. Standard moves them in 3D, which rounds off ridges, swirls and sharp edges. Keep Ridges slides them along the surface instead. | Experimental. Try Keep Ridges when ridges, swirls or vines come out softer than in the sculpt: on the test sculpts it lost a third to a half less of the shape. It can leave a few more quads folded over in deep grooves (cube with swirls: about 6 more out of 3000). |

Settings other than these defaults work, but the project's quality targets are checked at the defaults,
so expect a bit more variation.

---

## 5. Reading the result

The box under **Remesh** shows the last run. The title is **Done**, **Cancelled**, **Failed** or
**Invalid input** (with the reason under it). **Red rows are problems**; the rest is information. Clicking
another result object shows that object's own report.

| Row | Meaning | Good value |
|---|---|---|
| **Quads** | `N of T target (P %)`: quads made, the target, the ratio. | 60–150 %. |
| **Non-quads** | Triangles, pentagons and other faces that are not quads. | 0. A handful on a sculpt is common today; see [§7](#7-sculpts-what-works-and-what-struggles). |
| **Holes** | Only shown if your model was closed. Holes the remesh opened, with the number of border edges. | 0. Anything else is a defect; see [§6](#6-warnings-and-what-to-do). |
| **Handles** | Tunnels and loops through your model (the hole of a ring, a handle, an eyelet) compared with your model: `as input`, or how many were lost or added. | `as input`. Lost ones are tunnels too narrow for the quad size. |
| **Non-manifold edges** | Edges with three or more faces. | 0. |
| **Non-manifold vertices** | Points where surfaces only touch. | 0. |
| **Winding flips** | Neighbouring faces facing opposite ways. | 0. |
| **Folded faces** | Quads bent over or twisted ([§1](#1-words-used-here)). | 0. A few on sculpts is a known weakness. |
| **Input cleanup** | Only shown when the remesher fixed your *input* before starting: tiny broken triangles removed, triangles turned to face the same way as their neighbours. | Information, not a problem. |
| **Empty islands** | `E of N`: separate pieces of your model that got no quads at all. | 0 of N. |
| **Missing parts** | How far the farthest point of your model is from the result, counted in quad widths (the planned quad size). Red above 3: a part such as a vine, a limb or a spike did not come out. | none. |
| **Time** | Seconds the remesher ran. | — |

To find defects on the result: select it, **Tab** into Edit Mode, press **1** (vertex select) or **3**
(face select), then:
- holes and non-manifold spots: **Select > Select All by Trait > Non Manifold**;
- non-quads: **Select > Select All by Trait > Faces by Sides**, then in the small panel at the bottom left
  set *Number of Vertices* 4 and *Type* **Not Equal To** *(from memory, check)*.

Viewport header **Overlays** drop-down > **Statistics** shows how many are selected *(from memory, check)*.

---

## 6. Warnings and what to do

Warnings appear under the rows, with a red icon when they are a problem.

| You see | What it means | What to do |
|---|---|---|
| Dialog **"Remesh to 200,000+ quads?"** with a memory estimate | The **Target Quads guard**: a big target can use all your memory (about 2 GB per 100,000 quads) and take long. Often a typo (an extra zero). | Press Cancel unless you meant it and have the memory. |
| **"Output has N holes where the input was closed (E open edges)"**, plus a hint, and a red **Holes** row | The remesh tore the surface open. On sculpts this happens almost only at folded input ([§7](#7-sculpts-what-works-and-what-struggles)). | Try another Target Quads (a few hundred up or down often closes it). Find the holes with the hint (Non Manifold). If they sit at swirls or pinched creases, smooth those in Sculpt Mode and run again. |
| **"Output has N handles fewer than the input …"**, red **Handles** row | Tunnels or loops only a few quads wide closed up (a ring's hole, a strap's eyelet, a tiny gap in hair). | Raise Target Quads; each step up keeps more of them, but tunnels much narrower than a quad close at any count you would use. If the tunnel should not be there, ignore it. |
| **"Output has N handles more than the input …"**, red **Handles** row | Two surfaces were joined across a thin gap. Rare; seen only on badly folded or very noisy input. | Try another Target Quads, or smooth the folded spot and run again. |
| **"Folded input: N faces in K spots. …"** and a **Repair Folds and Re-run** button | Your model's surface is folded over itself in K places, on sculpts usually left by a brush. The quads there may come out messy or with holes. | Press **Repair Folds and Re-run** ([§8](#8-folded-input-and-repair-folds)). |
| **"N folded faces in K spots could not be repaired safely and were left as they were"** | After a repair: these spots were too steep, too thin or too tangled to fix without changing your shape. | Smooth those spots in Sculpt Mode and run again ([§7](#7-sculpts-what-works-and-what-struggles)). |
| Red **Non-manifold vertices** or **edges** | Points or edges that are not a clean surface. | Same as holes: another Target Quads, or clean the input at that spot. Report it with the build line if it stays. |
| **"N of M islands produced no faces"**, red **Empty islands** | A small separate piece (an eyeball, a tiny flower) got nothing. | Raise Target Quads, or remesh that piece on its own: Edit Mode, select it, **P > Selection** to make it its own object *(from memory, check)*, and remesh both. |
| **"Parts of the model are missing in the result (up to N quad widths away). …"**, red **Missing parts** | A part thinner than about two quads (a vine, a strap, a finger, a spike) was smoothed away or cut off. The model is still one piece where it was, so no other row shows it. | Raise Target Quads until the warning goes away; each doubling halves the quad width. Or remesh the thin part on its own at a higher target ([§7](#7-sculpts-what-works-and-what-struggles)). |
| **"P % of the surface (S spots) is thinner than one quad; expect folds or lost parts there. At least N target quads (an estimate) would fit a quad across about half of it, but each raise also reveals thinner parts, so it can take several raises; thickening those parts is usually the better fix"** | Parts of your model are thinner than one quad is wide (leaves, vines, a blade edge). A risk, not a defect: most runs it fires on have folds or lost parts there, but some come out clean. N is a first step, not the count that clears the warning: a higher count measures the parts more finely and finds thinner ones (on a vine-covered test sword the estimate was 1.6 to 2.1 times the count asked for at every count from 3,000 to 75,000). When no count fits, it says to thicken those parts. | Raise Target Quads to about N: the thin share shrinks (on that sword from 19 % at 3,000 to 3 % at 35,000), but it can take several raises, each costing more quads. Thickening the part is usually cheaper: Edit Mode, select it, **Alt+S** (Shrink/Fatten) and move the mouse outwards. |
| **"K islands came out with far fewer quads than planned (smallest: F of P)"** | A separate piece came out as a lump of a few quads instead of its shape, usually because it is thin or small for the target. | Raise Target Quads, or remesh that piece on its own (as for empty islands). |
| **"Could not copy data from the original: …"** | Copying materials, groups, marks or UVs failed; the result itself is fine. | Report it with the message. Untick the options under **Keep from Original** to remesh without copying. |
| **"… is scaled unevenly (N : 1), so the quads come out stretched …"** | The object (or its parent) is scaled differently along its axes, and the quads stretch the same way. | Select the original, **Ctrl+A > Scale**, and remesh again ([§3](#3-your-first-remesh)). |
| **"Blender's mesh validation changed the result"** | Blender had to repair the remesher's result when building the object. Should not happen. | Report it with the build line and the settings. |
| Title **Invalid input** | The model cannot be remeshed as it is: no faces, broken coordinates, a target of 0 or above 2,000,000. The reason is shown under it. | Fix what it says. "The mesh has no faces" means you picked a mesh of only points or edges. |
| Title **Failed** | The remesher stopped (e.g. "remeshing produced no faces"). | Try another Target Quads or Fast/Quality, then report it ([§9](#9-troubleshooting)). |
| Title **Cancelled** | You pressed Esc or Cancel, or Blender stopped it (loading another file). If the remesher does not stop within 5 seconds, the add-on kills it. | Nothing to do. |

*Planned, not yet in the add-on:* a check *before* remeshing for self-intersections and thin
parts (today thin and missing parts are reported after the run, as above).

---

## 7. Sculpts: what works and what struggles

From the project's measurements on real sculpts and on synthetic brush tests (October 2026; details in
[ENVELOPE.md](ENVELOPE.md), the project's list of supported inputs).

**Works well**
- Smooth organic sculpts: Clay, Inflate, Grab, Mask-extrude, Noise strokes come out clean.
- Dense meshes: millions of triangles are fine (6.4 million measured in about 30 s); the model's
  wireframe does not matter, only its shape.
- Dyntopo sculpts with mixed triangle sizes.
- Several pieces joined into one object, eyeballs sunk into a head, pieces that intersect each other.
- Open models (with borders) keep their borders; separate pieces stay separate.
- Small broken bits in the input (zero-size triangles, faces facing the wrong way next to each other) are
  cleaned up automatically (the **Input cleanup** row).

**Struggles, and what helps**

| Situation | What happens | What helps |
|---|---|---|
| **Thin parts** (vines, leaves, cloth, thin blades) thinner than one quad | Quads fold over the thin edge, and the count of handles and holes can come out wrong. Every remesher we compared folds there. | Raise Target Quads until the thin part is at least one or two quads across: on a ribbony test object, 12,000+ instead of 3,000 cut the "too thin" area from 46 % to 17 %. Adaptivity does not help. For flat sheets with no thickness (a leaf plane, cloth), add a **Solidify** modifier first (wrench tab > **Add Modifier > Generate > Solidify** *(from memory, check)*); the add-on uses it without applying. The panel warns when parts are thinner than one quad or missing from the result ([§6](#6-warnings-and-what-to-do)). |
| **Pleats from the Twist/Rotate brush**, creases pinched past themselves | The original surface is folded there. On our two test sculpts, about three quarters of the remaining defects sit within two quads of such spots; most of the rest are on thin parts. | In Sculpt Mode, smooth the spot (**Smooth** brush, or hold **Shift** while sculpting *(from memory, check)*) or voxel-remesh the sculpt (**Remesh** in the Sculpt Mode header, or **Ctrl+R** *(from memory, check)*) and remesh again. Or press **Repair Folds and Re-run** after the remesh ([§8](#8-folded-input-and-repair-folds)). |
| **Holes in a closed sculpt** at higher counts | On the head test sculpt, before libQEx became the extractor, holes appeared from 6,000 quads up in most runs, all at the pleats above. | As above; and the **Holes** row now shows them. |
| **Crevices narrower than a quad** (deep pinched grooves, eyelid lines) | Folded quads in the groove; at high counts sometimes an extra tunnel across the groove. | More quads, or soften the groove. |
| **Sharp tips and spikes** | A cone-like tip is cut off about one quad deep, often with a ring of small, crowded quads around it (Blender's default cone at every count tested). A needle spike gets a non-quad or a crowded pole. | More quads make the cut smaller, since it is about one quad. |
| **Hard edges** (box, cylinder or cone rims) | Rounded. On Blender's default primitives an edge loop follows about half to two thirds of a box or cylinder edge from 1,000 quads up; a cone's base rim is mostly rounded off. Rows of thin quads often line the edge. | Mark the edges Sharp and tick **Follow Marks** > Sharp: at 5,000 quads an edge loop then follows 87–99 % of a cube's or cylinder's edges and about 78 % of a cone's base rim (at 1,000 quads still almost none). Without marks, more quads make the rounding smaller. If the edges are exactly 90° (Blender's cube and cylinder), set Sharp Edge to 89°: at 5,000 quads an edge loop then follows about 90 % of a cube's edges instead of 70 %, and about 80 % of a cylinder's rims instead of 60 %. On a detailed mechanical test part with many right angles, 89° made more triangles and sometimes changed its holes or handles, so check the **Holes** and **Handles** rows. |
| **Small tunnels** (a gap thinner than about 3 quads, e.g. between an arm and the body) | It may close. | Raise Target Quads until the tunnel is several quads wide. |
| **Very low-poly input** (big flat facets) | The result follows the facets. An icosphere also gets extra poles inside its facets, with or without Subdivision Surface (a UV sphere comes out clean from about 3,000 quads). | Add Subdivision Surface first, or use Smooth Normal. |
| **A surface pushed through itself** | May fold there. Not measured yet. | Fix in Sculpt Mode. |
| **Very tiny or huge models** (under about 1 cm or over 10 km across, in Blender units) | Not tested; may misbehave. | Scale the model before remeshing (**S** in Object Mode), apply the scale (**Ctrl+A > Scale**), and scale back after. |

**Time.** Measured on a cloud machine: a 140,000-triangle test sculpt took about 12 s at
10,000 quads and 30 s at 20,000 (Quality). A laptop may be 2× slower.

---

## 8. Folded input and Repair Folds

Some sculpt brushes (Twist, Rotate, Pinch, Crease pushed too far) fold the surface over itself: a small
patch ends up upside down, like a pleat in fabric. You usually cannot see it in the viewport. The
remesher cannot lay clean quads on such a spot, so that is where messy quads, triangles and holes
appear.

Every remesh checks for these spots. If it finds any, the result shows a warning **"Folded input: N
faces in K spots"** and, below it, a **Repair Folds and Re-run** button.

1. Select the result object (the one the warning is shown for).
2. Press **Repair Folds and Re-run**. The remesher runs again, the same settings, from your original
   object, and this time first smooths out only the folded spots.
3. The new result replaces the old one (the old one is hidden). The rows show **Folds repaired: X of
   N faces** (counted in faces, because a repair can split one spot into several smaller ones); if some
   could not be fixed, a red **Folds left: F faces in S spots** row and a line saying so.

**Also Flatten Needle Spikes** (a checkbox above the button, off by default). Some folds sit on the
flank of a needle: a single vertex pulled far out of the surface. The normal repair cannot fix those
without moving the needle's tip, so it leaves them. With the box ticked, it lowers such a tip to the
middle of its base when that clears the fold, which **changes your shape** at that spot. Only tips
near a fold the normal repair left are lowered, never a spike on a clean surface. The result then
shows a **Spikes flattened** row, for example "1, tip lowered by up to 0.6 quads" (how far the
farthest tip moved, measured in quad edges at your Target Quads), and a line saying the shape
changed. On our test sculpts it lowered 1 or 2 tips per model.

What it does and does not do:
- **Your original object is never changed.** The repair works on the copy the remesher gets.
- It moves only the vertices in and right around each fold, sliding them along your surface. Spots
  that would need a real change of shape (a spike's flank, a thin flap, a fold along a hard edge) are
  left alone and counted under **Folds left**.
- It is off unless you press the button, because it does change the remeshed shape slightly at those
  spots. On most models with folds the result is cleaner; check the spots it reports.
- Folds wider than about 1 % of the part's size are not seen as folds (they look like a normal surface
  turned over). Smooth those by hand. Smooth bends are never counted, even tight ones (a thin vine curling
  back on itself): only spots where the surface flips over at an edge are.

## 9. Troubleshooting

| Problem | Try |
|---|---|
| **No Quadami tab** | Mouse over the 3D Viewport, press **N**; scroll the tabs on the sidebar's right edge. Check **Edit > Preferences > Add-ons**: Quadami ticked? "Missing Add-ons: ...quadami" means its files are gone in this Blender: install the zip again and restart Blender. Not listed at all: it was installed into another Blender version. |
| **Remesh button is grey** | Be in Object Mode, with a mesh object selected (orange outline, last clicked), and no remesh running. |
| **"Remesher executable not found"** | Preferences > Add-ons > Quadami: the line under *Remesher executable* must say *Using: ...* with a tick. Leave the field empty and reinstall the right zip for your system. |
| **Build line is red** | Add-on and remesher are from different builds: reinstall the zip. |
| **macOS refuses to start the remesher** | The program is not notarized by Apple. Untested on a real Mac: in the Terminal, `xattr -dr com.apple.quarantine <add-on folder>` (the folder that holds the `bin/autoremesher-cli` shown after *Using:* in the add-on preferences). |
| **The result is missing / my model vanished** | The original is hidden, not deleted: Outliner, click its eye icon. The result is `<name>_remesh`. |
| **"Cannot remesh the original: …"** | Remesh on a result runs on its original, which must be in the scene: not deleted, not disabled in viewports (the monitor icon in the Outliner), not in an excluded collection. Show it, or clear **Source** in the panel to remesh the selected result itself. |
| **Too few or too many quads** | Within 60–150 % of the target is normal. Thin parts and many small pieces push it around. Adjust Target Quads. |
| **Folded faces / non-quads in one area** | Look at the input there ([§7](#7-sculpts-what-works-and-what-struggles)): a pleat, a thin part, a narrow groove. Try ±10 % Target Quads; small changes in the count move defects around. |
| **It takes very long or the computer slows down** | Press Esc. Lower Target Quads or use Fast. Memory is about 2 GB per 100,000 quads. |
| **Blender closed or froze** | The remesher runs as a separate program and cannot crash Blender by itself; report it. |

**Reporting a problem.** Use the [bug report form](https://github.com/KajetanMuczynski/quadami/issues/new?template=bug-report.yml) on GitHub (you need a free
account); it asks for all of this. Send: the **build line**, the settings (Target Quads, Fast/Quality, anything
changed), a screenshot of the result box, and, if possible, start Blender from a terminal (Windows:
**Window > Toggle System Console**) and copy the text after `Quadami worker output:`. If you can share
the model, say so; models are kept private and only their numbers go into the project.
