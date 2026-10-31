# Quadami

Quadami is an automatic quad remesher for Blender. Select a mesh (a sculpt, a scan, a dense
triangle model), set how many quads you want, press **Remesh**, and you get a new object made of
quads that follows the shape and has edge loops that run along it.

It is made for people who sculpt or model in Blender and need clean, even topology afterwards
(to animate, UV-unwrap, keep sculpting at a lower resolution, or hand over). You do not need to be
a programmer to use it: the [user guide](docs/USER-GUIDE.md) is written for Blender beginners.

> **Status: 0.2.0, a release with known limits.** It works on the sculpts we test with, but
> expect rough edges. Read [Limits and warnings](#limits-and-warnings) before you rely
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
- **Shows the quad size before you remesh.** **Preview Density** paints the planned quad size on your
  model; drag **Change** to see what more or fewer quads would give, at once, then remesh at the
  count that keeps your details. It is a plan (on average 10-15 % off the result, less reliable on
  hard-surface parts), and it shows size only, not edge loops.
- **Asks before a huge job.** Above 200,000 quads it shows the memory it needs (about 2 GB per
  100,000 quads) and waits for you to confirm.

## Install

Download the zip for your system from the
[Releases page](https://github.com/KajetanMuczynski/quadami/releases/latest) (under **Assets**;
no GitHub account needed). Do not unpack it: Blender installs the zip itself.

| System | Zip | Tested |
|---|---|---|
| **Linux** (x64, any distribution with glibc 2.28 or newer, e.g. Ubuntu 20.04+, Debian 10+, Fedora 29+, RHEL 8+) | `quadami-<version>-linux-x64.zip` (built in AlmaLinux 8, so it does not need a new distribution). Works with Blender from blender.org and the snap (`/snap/bin/blender`). Or build it yourself ([addon/BEGINNERS.md](addon/BEGINNERS.md)). | CI (Ubuntu 24.04, Blender 4.2, headless) and the maintainer's laptop (Ubuntu, Blender 4.5 snap) |
| **Windows** 10/11 (x64) | `quadami-<version>-windows-x64.zip` | CI (Blender 5.2, headless); 0.1.0 also on one Windows 10 desktop, 0.2.0 not yet by hand. The program is not signed. |
| **macOS** (Apple silicon, macOS 11.2+): **experimental, untested** | `quadami-<version>-macos-arm64.zip` | In CI only; **never tried on a real Mac**, and the program is not notarized, so Gatekeeper may block it |
| macOS on Intel | Build it yourself ([docs/BUILDING.md](docs/BUILDING.md)). | No |

`SHA256SUMS.txt` next to the zips lets you check a download. Test builds between releases (newer,
less checked) are CI artifacts; how to get them: [user guide §2](docs/USER-GUIDE.md#test-builds-between-releases).

Then, in **Blender 4.2 or newer**: **Edit > Preferences > Get Extensions**, the small **⌄** arrow
at the top right, **Install from Disk...**, pick the zip, and restart Blender. The panel is in the
3D Viewport sidebar (**N**), tab **Quadami**.

Step by step for every system, including where Blender keeps the add-on, updating, removing it,
the install scripts and the snap version of Blender on Linux: [user guide §2](docs/USER-GUIDE.md#2-install-and-update).

## Limits and warnings

- **Thin parts** (leaves, vines, cloth, blade edges) thinner than one quad fold or lose their shape,
  and the result's handles and holes can come out wrong there. The panel warns about thin parts,
  missing parts and lost handles; raise Target Quads, or thicken the part.
- **Folded sculpt input** is where most remaining defects on our test sculpts sit. Use
  **Repair Folds and Re-run**, or smooth those spots by hand; folds wider than about 1 % of the
  model are not detected.
- **Holes** can still appear in closed sculpts at higher quad counts; the **Holes** row shows them.
- **Hard-surface models**: sharp edges come out slightly rounded (on Blender's cube an edge loop
  follows about 90 % of the edges at 5,000 quads, on the cylinder about 80 % of the rims; a cone's
  rim much less), and cone tips are cut off about one quad deep. Mark the edges to keep as Sharp:
  **Follow Marks** (on by default) then keeps quad edges along 90-99 % of a cube's or cylinder's marked
  edges at 5,000 quads. Sharp Edge below 90° makes more triangles
  and other non-quads on detailed parts. Mirror symmetry, guide curves and density painting are not
  available yet.
- **Modifiers are baked in**: a Mirror, Subdivision Surface or Armature on the original is applied
  into the result and is not on it.
- **Large inputs** are slow to hand over (the mesh goes through a text file both ways).

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
- input cleanup (broken triangles, inconsistent winding, unwelded seams, duplicate faces, pinched
  vertices) and detection and optional repair of folded sculpt input;
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

## Contact

[GitHub Issues](https://github.com/KajetanMuczynski/quadami/issues/new/choose) is the place for
everything: [bug reports](https://github.com/KajetanMuczynski/quadami/issues/new?template=bug-report.yml) (the form asks for the build line and the
settings), feature requests, questions and [community results](docs/site/community.md). Issues are
public: do not attach models you may not share. Security problems go to a private report instead,
see [SECURITY.md](SECURITY.md).

Without a GitHub account, or for something that should not be public (for example a model you can
share only privately), write to [quadami.contact@gmail.com](mailto:quadami.contact@gmail.com).

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
