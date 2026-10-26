# Quadami for Blender (add-on internals)

Quad-remeshes the active mesh with Quadami's remeshing core. The name Quadami
lives in `quadami/names.py` plus `id`/`name` in `blender_manifest.toml`
(a test checks they agree). The core and its CLI keep their upstream code names
(`autoremesher-cli`, namespace `AutoRemesher`; why: [docs/BUILDING.md](../docs/BUILDING.md)).
The remesher runs in its own process, never inside Blender, so a crash or a
stuck solve cannot take Blender down.

License: GPL-3.0-or-later, like the rest of the fork.

This file is for building and packaging. **Using** the add-on: [docs/USER-GUIDE.md](../docs/USER-GUIDE.md).

## What it does

- **Sidebar > Quadami** shows the parameters of the core's `remesh()`
  (docs/INTERFACE.md §3) and nothing else: target quads, adaptivity and sharp
  edge angle, and in a collapsed *Advanced* section resolution (Quality or
  Fast), anisotropy, edge scaling and smooth normal angle. Ranges and defaults
  are the Qt app's; the tooltips describe the measured effect of each. The
  default target is 5000 quads, not the CLI's 50000, so a first click on a
  laptop finishes quickly.
- **Remesh** reads the evaluated mesh (modifiers applied, object space), starts
  the worker and returns control to Blender. Progress shows in the status bar
  and the panel.
- **Esc** (or **Cancel** in the panel) asks the worker to stop. The core stops
  at its next cancel point, which can take a few seconds inside one solve
  (measured up to 3.5 s, INTERFACE §8), so the add-on waits 5 s before it kills
  the process.
- On success it adds `<name>_remesh` next to the source with the same
  transform and collections, hides the source and selects the result.
- The panel shows the quality report from the worker: status, quads against
  the target, non-quads, non-manifold edges, winding flips, folded faces, empty
  islands, time and warnings; non-zero defects are drawn in red. The report is
  kept on the result object (custom property `quadami_report`), so it is
  still there after saving the file. A failed or cancelled run shows its
  status and message instead; the worker's last output lines go to Blender's
  console.

## How the worker works (and why)

There is no Python binding of the C++ interface yet (TODO G1), so the worker
is a separate program, `autoremesher-cli`: the remeshing core behind a
command line, without Qt (`src/cli/`, built with CMake from the top-level
`CMakeLists.txt`). `worker.py` wraps it (plain Python + numpy, no `bpy`, so
it is testable without Blender):

```
autoremesher-cli -i input.obj -o output.obj --report-json report.json --stdin-control \
    --target-quads N --adaptivity A ...
```

| Need | How |
|---|---|
| separate process | `subprocess.Popen` of the CLI (no console window on Windows) |
| progress | the CLI's `42% done. <step>` lines on stdout, read by a thread |
| cancel | `cancel` line on stdin (`--stdin-control`); kill after 5 s |
| Blender dies | its end of the pipe closes, the worker reads EOF and cancels itself |
| result | the OBJ the CLI writes, plus `--report-json` (schema `autoremesher.remesh-result`) |
| version check | the report's `interface_version` must be 1.x |

The Qt app's headless mode (`autoremesher -i ... -o ...`) takes the same
arguments and shares the `--stdin-control` code, so it still works as a
worker, but it needs Qt at run time.

The add-on looks for the CLI in this order: *Remesher executable* in its
preferences, `$AUTOREMESHER_EXECUTABLE`, the copy bundled in the add-on's
`bin/` folder, `autoremesher-cli` on `PATH`.

This differs from INTERFACE §4, which plans a Blender-Python worker hosting a
nanobind module and exchanging `.npz` files. Swapping in that worker later
only replaces `RemeshJob` in `worker.py`; the operator and panel only use its
`start / poll / cancel / done / outcome` methods. The cost of the CLI route
that the binding removes: OBJ text both ways, slow on multi-million-triangle
inputs (REFERENCE P4). The CLI writes coordinates with 17 significant digits,
so nothing is lost on the way back.

## Windows (10 or 11, 64-bit, Blender 4.2 or newer)

CI builds the Windows package; nothing has to be compiled or installed
besides Blender.

1. **Get the zip.** `quadami-<version>-windows-x64.zip` from the
   [Releases page](https://github.com/KajetanMuczynski/quadami/releases/latest)
   (test builds between releases: user guide §2). Do not unpack it.
2. **Install it.** Blender: *Edit > Preferences > Get Extensions*, the small
   down-arrow at the top right, *Install from Disk...*, pick
   `quadami-<version>-windows-x64.zip`.
3. **Check.** *Edit > Preferences > Add-ons*, expand *Quadami*: the line
   under *Remesher executable* must say *Using: ...\bin\autoremesher-cli.exe*
   with a check mark. Leave *Remesher executable* empty.
4. Continue with the user guide, [docs/USER-GUIDE.md](../docs/USER-GUIDE.md) §3.

`autoremesher-cli.exe` links the C++ runtime and oneTBB statically, so it
needs no Visual C++ Redistributable and no other DLL beyond the ones every
Windows has (CI checks the import table: `ci/check_dll_imports.py`). Its
licenses and the source location are in the zip's `licenses/` folder.

To build it yourself instead (Visual Studio 2022 or newer with the C++
workload, CMake, Git, Python 3.11+):

```
cmake -S . -B build/cli
cmake --build build/cli --config Release --parallel 2
cmake --install build/cli --config Release --prefix build/cli-install
python addon/package.py --cli-prefix build/cli-install
```

The first configure downloads oneTBB from GitHub (pinned commit).

## macOS (Apple silicon, macOS 11.2 or newer, Blender 4.2 or newer)

Experimental and untested on a real Mac. CI builds the macOS package the same
way: `quadami-<version>-macos-arm64.zip` from the
[Releases page](https://github.com/KajetanMuczynski/quadami/releases/latest),
then *Install from Disk...* with that zip and check
*Using: .../bin/autoremesher-cli* in the add-on preferences as above.

`autoremesher-cli` links oneTBB statically and needs only macOS's own
libraries (CI checks with `otool -L`). It is only ad-hoc signed, not
notarized. Not yet tried on a real Mac: if macOS refuses to start it, run
`xattr -dr com.apple.quarantine` on the add-on's folder (shown in the add-on
preferences). Intel Macs: build it yourself as for Linux below
(`brew install cmake`).

## Install (Linux, Blender 4.2 or newer)

The remesher binary has to be built on the same machine and OS as the
Blender that uses it.

1. **Blender.** Any Blender 4.2 or newer from blender.org (the Linux `.tar.xz`,
   extracted anywhere) or the Snap. Not the Flatpak: its sandbox cannot start
   a program from your home folder. On older GPUs (Intel before Kaby Lake,
   NVIDIA before GeForce 900, e.g. the E7450's HD 5500) use 4.5 LTS: Blender
   5.0 raised the minimum GPU to those generations
   (https://www.blender.org/download/requirements/).
2. **Zip with the remesher**, from the repository (this checkout):

   ```
   sh addon/package.sh
   ```

   It builds `autoremesher-cli` with CMake in `build/cli` (Release,
   `nice -n 10`, two jobs; refuses while another build runs), installs it to
   `build/cli-install` and prints the zip's path,
   `build/addon/quadami-<version>-linux-x64.zip`, with the CLI inside.
   Build dependencies: CMake, a C++17 compiler, `libtbb-dev`. No Qt.
3. **Install the zip.** Blender: *Edit > Preferences > Get Extensions*, the
   small down-arrow at the top right, *Install from Disk...*, pick the zip.
   (Dragging the zip onto the Blender window works too.)
4. **Check.** *Edit > Preferences > Add-ons*, expand *Quadami*; the line
   below *Remesher executable* must say *Using: .../bin/autoremesher-cli*
   with a check mark. To use another build, set *Remesher executable* or
   start Blender with `AUTOREMESHER_EXECUTABLE=<path>`.
5. **Which build.** The grey line at the bottom of the *Quadami* panel (and
   in the preferences) is the build id: commit and commit date, e.g.
   `4d15d5f 2026-10-21` (`-dirty`: uncommitted changes; `pr52 abc1234+master`:
   a PR's CI zip, built from that head merged into master; `dev`: the add-on
   runs from the source tree). The manifest version stays 0.1.0, so Blender's
   extension list cannot tell builds apart. If the add-on and the remesher it
   uses come from different builds, both ids show in red.

`python3 addon/package.py` without arguments makes a zip without the CLI
(`quadami-<version>.zip`, any platform), which then needs the executable
path in the preferences.

To update after a code change: run step 2 again, then in *Get Extensions*
remove Quadami and install the new zip (or restart Blender after reinstalling).

## Using the add-on

How to use it (panel, settings, result rows, warnings, troubleshooting) is in
the user guide, [docs/USER-GUIDE.md](../docs/USER-GUIDE.md), the only
user-facing guide; keep usage instructions there, not here.

Headless check without a window:

```
AUTOREMESHER_EXECUTABLE=build/cli/autoremesher-cli <blender> -b --factory-startup --python-exit-code 1 --python addon/tests/blender_smoke.py
```

It should end with `blender_smoke: OK`. CI runs the same check on the
installed zip with its bundled CLI (`blender_smoke.py -- --installed`).

## Tests

Worker side, against a built CLI (no Blender needed):

```
AUTOREMESHER_EXECUTABLE=build/cli/autoremesher-cli python3 -m unittest discover -s addon/tests -v
```

Covers the manifest matching `names.py`, OBJ round trip, report rows, finding
the bundled CLI, a remesh of a 16k-triangle sphere (counts match the report,
progress non-decreasing), a temporary folder with non-ASCII characters in its
path, cancel by `cancel` line and by closed stdin (worker exits by itself and
reports `cancelled`), kill after the grace time, a rejected input and a
worker that crashes without a report.

Inside Blender, headless (`blender_smoke.py`): reads a sphere through the
add-on, runs the worker and builds the result object, then checks counts and
transform. Background mode has no window, so the modal operator, the panel,
Esc and the progress display can only be checked by hand in the UI.

CI (the `addon` jobs of `.github/workflows/ci.yml`) runs all of it on Ubuntu 24.04 (Blender
4.2 LTS, the oldest supported), Windows (MSVC, Blender 5.2) and macOS on Apple
silicon (Blender 5.2): CMake build,
DLL check (Windows), CLI argument checks, the worker tests, packaging,
`blender --command extension validate` and `install-file` on the zip, then
`blender_smoke.py --installed`.

## Status (2026-10-22)

- Tested here: `test_worker.py` on Ubuntu 24.04 against `autoremesher-cli`
  with the system oneTBB and with the static oneTBB build Windows and macOS use.
- CI (every PR; docs-only PRs skip macOS): the full add-on check on Linux (Blender 4.2), Windows and
  macOS Apple silicon (Blender 5.2), headless.
- In a real Blender window: only on Linux (the maintainer's laptop, Blender 4.5
  snap). Not covered anywhere: a real Windows desktop, a real Mac (Gatekeeper),
  Windows on ARM, Intel Macs.
