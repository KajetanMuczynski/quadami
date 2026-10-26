# Quadami

Quadami is an automatic quad remesher for Blender. Select a mesh (a sculpt, a scan, a dense
triangle model), set how many quads you want, press **Remesh**, and you get a new object made of
quads that follows the shape and has edge loops that run along it.

It is made for people who sculpt or model in Blender and need clean, even topology afterwards
(to animate, UV-unwrap, keep sculpting at a lower resolution, or hand over). You do not need to be
a programmer to use it: the [user guide](docs/USER-GUIDE.md) is written for Blender beginners.

> **Status: early test builds, version 0.1.0, no release yet.** It works on the sculpts we test
> with, but expect rough edges. Read [Limits and warnings](#limits-and-warnings) before you rely
> on it, and send the panel's build line with every bug report.

![Koala sculpt: input left, Quadami output right](docs/gallery/img/koala.webp)

More before/after examples, with their numbers and limits: [docs/gallery](docs/gallery/README.md).

## What it does

- **Runs outside Blender.** The remesher is a separate program that the add-on starts in the
  background. Blender stays usable while it works, **Esc** cancels it, and a crash in the remesher
  cannot take Blender down.
- **Remeshes what you see.** Modifiers (Subdivision Surface, Solidify, Mirror ...) are used at their
  viewport level; you do not need to apply them. Your original object is hidden, never changed.
- **Tells you how good the result is.** After every run the panel shows quads against the target,
  non-quads, holes, non-manifold edges and vertices, flipped and folded faces, empty pieces and the
  time, with problems in red and a warning that says what to do.
- **Finds folded sculpt input.** Twist, Rotate and Pinch brushes can fold a sculpt's surface over
  itself where you cannot see it; that is where most defects come from. Quadami detects these spots
  and offers **Repair Folds and Re-run**, which smooths only those spots in the remesher's copy.
  The warning can also appear on CAD or scanned models whose result is clean (its text still
  blames a sculpt brush); if the result looks fine, ignore it.
- **Builds quads with libQEx**, a robust quad extractor from RWTH Aachen, with our own extractor as
  a per-piece fallback.
- **Asks before a huge job.** Above 200,000 quads it shows the memory it needs (about 2 GB per
  100,000 quads) and waits for you to confirm.

## Install

There is no release download yet. What is available today:

| System | Where the zip comes from | Tested |
|---|---|---|
| **Linux** (x64, any distribution with glibc 2.28 or newer, e.g. Ubuntu 20.04+, Debian 10+, Fedora 29+, RHEL 8+) | GitHub: **Actions > ci**, a green run on `master`, **Artifacts > `addon-linux-x64`** (built in AlmaLinux 8, so it does not need a new distribution). Works with Blender from blender.org and the snap (`/snap/bin/blender`). Or build it yourself ([addon/BEGINNERS.md](addon/BEGINNERS.md)). | CI (Ubuntu 24.04, Blender 4.2, headless) and the maintainer's laptop (Ubuntu, Blender 4.5 snap) |
| **Windows** 10/11 (x64) | GitHub: **Actions > ci**, a green run on `master`, **Artifacts > `addon-windows-x64`** (you must be signed in to GitHub). | In CI only (Blender 5.2, headless); not yet on a real Windows desktop |
| **macOS** (Apple silicon, macOS 11.2+): **experimental, untested** | GitHub: **Actions > ci**, a green run on `master`, **Artifacts > `addon-macos-arm64`**. | In CI only; **never tried on a real Mac**, and the program is not notarized, so Gatekeeper may block it |
| macOS on Intel | Build it yourself ([docs/BUILDING.md](docs/BUILDING.md)). | No |

GitHub wraps each artifact in a zip of its own: unpack that one once; the `quadami-...zip` inside
is the add-on. Do not unpack the add-on zip itself.

Then, in **Blender 4.2 or newer**: **Edit > Preferences > Get Extensions**, the small **⌄** arrow
at the top right, **Install from Disk...**, pick the zip, and restart Blender. The panel is in the
3D Viewport sidebar (**N**), tab **Quadami**.

Step by step for every system, including where Blender keeps the add-on, updating, removing it,
the install scripts and the snap version of Blender on Linux: [user guide §2](docs/USER-GUIDE.md#2-install-and-update).

## Limits and warnings

- **Thin parts** (leaves, vines, cloth, blade edges) thinner than one quad fold or lose their shape,
  and the result's handles and holes can come out wrong there. The panel warns about thin parts,
  missing parts and lost handles; raise Target Quads, or thicken the part.
- **Folded sculpt input** causes nearly all folds, non-quads and holes on our test sculpts. Use
  **Repair Folds and Re-run**, or smooth those spots by hand; folds wider than about 1 % of the
  model are not detected.
- **Holes** can still appear in closed sculpts at higher quad counts; the **Holes** row shows them.
- **Hard-surface models**: sharp edges are kept above 90°, but lower angles make more triangles and
  other non-quads. Mirror symmetry, guide curves and density painting are not available yet.
- **Large inputs** are slow to hand over (the mesh goes through a text file both ways).
- Very small or very large models (under about 1 cm or over 10 km) are untested.

The full list, with what helps: [user guide §7](docs/USER-GUIDE.md#7-sculpts-what-works-and-what-struggles).
What we test and promise: [docs/TARGETS.md](docs/TARGETS.md) (quality targets) and
[docs/ENVELOPE.md](docs/ENVELOPE.md) (which inputs those targets apply to).

## How it differs from AutoRemesher

Quadami started as a fork of Jeremy Hu's AutoRemesher 1.2 (see [Origin and credits](#origin-and-credits))
and keeps its overall method: a cross field guides a quad parameterization, which is rounded to
whole numbers and turned into quads. Since then most stages have changed:

- a Blender add-on that runs the remesher in its own process, instead of a standalone app;
- quad extraction by libQEx, with closed inputs kept closed and small pieces kept present;
- a reworked rounding and untangling of the parameterization, a validity check of the map before
  extraction, and an output projected onto and relaxed along the input surface;
- input cleanup (broken triangles, inconsistent winding) and detection and optional repair of
  folded sculpt input;
- a quality report with every run, and a test harness that measures every change on the same
  meshes before it is merged.

The standalone AutoRemesher desktop app is still in this repository for development and is built
and tested by CI, but it is no longer packaged or supported as a product.

## Documentation

- [docs/USER-GUIDE.md](docs/USER-GUIDE.md): install, settings, result rows, warnings, troubleshooting.
- [docs/gallery/README.md](docs/gallery/README.md): before/after examples on public meshes, and the limits they show.
- [docs/windows-test/README.md](docs/windows-test/README.md): testing on Windows, with an install script.
- [docs/BUILDING.md](docs/BUILDING.md): building the add-on and the remesher from source on Linux,
  Windows and macOS.
- [docs/README.md](docs/README.md): index of everything else (plan, targets, technical reference).
- [CLAUDE.md](CLAUDE.md) and [addon/README.md](addon/README.md): notes for contributors (build, CI,
  pull request rules, how the add-on talks to the remesher).
- [CHANGELOGS.md](CHANGELOGS.md): what changed.

## Build from source

Short version for Linux (Ubuntu 24.04, needs CMake, a C++17 compiler and `libtbb-dev`; no Qt):

```
git clone https://github.com/KajetanMuczynski/quadami.git
cd quadami
sh addon/package.sh        # builds the remesher and prints the path of the add-on zip
```

Windows, macOS, the Qt app and the tests: [docs/BUILDING.md](docs/BUILDING.md).

## License

Quadami is licensed under the **GNU General Public License, version 3 or later**
(GPL-3.0-or-later), see [LICENSE](LICENSE).

It contains code derived from AutoRemesher, which is under the **MIT License**. That license
requires its copyright and permission notice to stay with the code, so it is kept in
[LICENSE-MIT-upstream](LICENSE-MIT-upstream) and in the headers of the files that came from
AutoRemesher. The bundled libraries keep their own licenses, listed in
[THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES); the add-on zip carries all of them in its `licenses/`
folder.

## Origin and credits

Quadami is derived from **[AutoRemesher](https://github.com/huxingyi/autoremesher)** by
**Jeremy Hu** (version 1.2.0, commit `4a49f82`), MIT License. The field-guided method, the first
versions of most of the core and the desktop app are his work, together with the people listed in
[AUTHORS](AUTHORS) and [CONTRIBUTORS](CONTRIBUTORS). AutoRemesher's own releases and history are in
its repository; its changelog is kept in [CHANGELOGS.md](CHANGELOGS.md). Quadami is a separate
project: please report Quadami problems here, not upstream.

Quadami also builds on:

- [libQEx](https://github.com/hcebke/libQEx) (Hans-Christian Ebke et al., RWTH Aachen; GPL-3.0-or-later),
  the implementation of *QEx: Robust Quad Mesh Extraction* (Ebke, Bommes, Campen, Kobbelt, SIGGRAPH Asia
  2013). Our changes are listed in `thirdparty/libQEx/PATCHES.md`.
- [OpenMesh](https://www.graphics.rwth-aachen.de/software/openmesh/) (RWTH Aachen; BSD-3-Clause), used by libQEx.
- [isotropicremesher](https://github.com/huxingyi/isotropicremesher) (Jeremy Hu; MIT).
- [Eigen](https://eigen.tuxfamily.org/) (MPL-2.0), [oneTBB](https://github.com/uxlfoundation/oneTBB) (Apache-2.0),
  [meshoptimizer](https://github.com/zeux/meshoptimizer) (Arseny Kapoulkine; MIT),
  [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) (MIT).
- The desktop app also uses Qt, QtAwesome and QtWaitingSpinner.

[ACKNOWLEDGEMENTS.html](ACKNOWLEDGEMENTS.html) is AutoRemesher's list for its desktop app, kept
as it was; it names libraries that are not in this repository. [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES)
is the list of what Quadami actually contains.
