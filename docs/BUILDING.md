<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 Quadami contributors
-->

# Building Quadami from source

Most people do not need this: install a ready zip as in the [user guide §2](USER-GUIDE.md#2-install-and-update).
This page is for building the Blender add-on and its remesher yourself, on Linux, Windows or macOS.
Contributor rules (CI, pull requests, A/B runs) are in [CLAUDE.md](../CLAUDE.md); how the add-on
talks to the remesher is in [addon/README.md](../addon/README.md).

**Names.** The remesher program is still called `autoremesher-cli`, its C++ namespace `AutoRemesher`
and its CMake options `AUTOREMESHER_*`: the code keeps its upstream names so that old commits stay
comparable in the A/B harness and every core change does not touch every file. Only what users see is
called Quadami (the add-on, its panel, its zip).

## What gets built

| Part | Build system | Needs Qt | Shipped |
|---|---|---|---|
| Remesher `autoremesher-cli` (the core behind a command line) | CMake, top-level `CMakeLists.txt` | No | Yes, inside the add-on zip |
| Blender add-on zip `quadami-<version>-<platform>.zip` | `addon/package.py` | No | Yes |
| Core tests (`tests/`) | CMake, `-DAUTOREMESHER_BUILD_TESTS=ON` | No | No |
| Desktop app `autoremesher` (AutoRemesher's original GUI, with a headless mode) | qmake, `autoremesher.pro` | Yes | No (development only) |
| Benchmark drivers (`tools/bench/`) | `tools/bench/build.sh` | Partly | No |

The remesher must be built for the same operating system and CPU as the Blender that runs it.

## Linux

Checked on Ubuntu 24.04 (GCC 13.3, oneTBB 2021.11). A zip built this way links the system oneTBB unless
you ask for the bundled one, and needs the glibc of the machine it was built on (Ubuntu 24.04: 2.38) on
the machine that runs it. The zip CI ships (`addon-linux-x64`) is built for any Linux with glibc 2.28 or
newer; to build that one yourself, see "Portable build" below.

```
sudo apt install build-essential cmake git python3 libtbb-dev
git clone https://github.com/KajetanMuczynski/quadami.git
cd quadami
sh addon/package.sh
```

`addon/package.sh` builds the remesher in `build/cli` (Release, `nice -n 10`, two jobs; it refuses to
start while another build runs), installs it to `build/cli-install` and prints the zip's path,
`build/addon/quadami-<version>-linux-x64.zip`. Rebuilds are incremental. A step-by-step version for
someone new to the terminal: [addon/BEGINNERS.md](../addon/BEGINNERS.md).

The same by hand:

```
cmake -S . -B build/cli -DCMAKE_BUILD_TYPE=Release
nice -n 10 cmake --build build/cli --parallel 2
cmake --install build/cli --prefix build/cli-install
python3 addon/package.py --cli-prefix build/cli-install
```

To link oneTBB statically (so the zip does not need `libtbb.so.12` on the target machine), add
`-DAUTOREMESHER_BUNDLED_TBB=ON` to the first command; it downloads oneTBB from GitHub at a pinned
commit. That still does not make a zip portable to older distributions: glibc stays a dependency.

**Portable build** (what CI ships): with Docker installed, `sh ci/build_linux_portable.sh` builds
`build/cli` and `build/cli-install` inside AlmaLinux 8 (glibc 2.28, GCC 13 from gcc-toolset, static
oneTBB); then `python3 addon/package.py --cli-prefix build/cli-install`. `python3
ci/check_linux_libs.py build/cli-install/bin/autoremesher-cli` confirms it needs nothing newer than
glibc 2.28. It uses 4 build jobs; `JOBS=2 sh ci/build_linux_portable.sh` keeps a laptop cooler.

Blender on Linux: any 4.2 or newer from blender.org (`.tar.xz`) or the snap. **Not the Flatpak**: its
sandbox cannot start a program from your home folder.

## Windows

Windows 10 or 11, 64-bit. Needs Visual Studio 2022 or newer with the **Desktop development with C++**
workload, CMake, Git and Python 3.11+. Run in a **x64 Native Tools Command Prompt for VS**:

```
git clone https://github.com/KajetanMuczynski/quadami.git
cd quadami
cmake -S . -B build/cli
cmake --build build/cli --config Release --parallel 2
cmake --install build/cli --config Release --prefix build/cli-install
python addon/package.py --cli-prefix build/cli-install
```

On Windows oneTBB is downloaded (pinned commit) and linked statically, and the C++ runtime is linked
statically too (`/MT`), so `autoremesher-cli.exe` needs no Visual C++ Redistributable and no DLL that
Windows does not ship (CI checks its import table with `ci/check_dll_imports.py`). This is the same
configuration CI uses for the `addon-windows-x64` artifact.

## macOS

Apple silicon or Intel, macOS 11.2 or newer. Needs the Xcode Command Line Tools, CMake and Python 3:

```
xcode-select --install
brew install cmake
git clone https://github.com/KajetanMuczynski/quadami.git
cd quadami
cmake -S . -B build/cli -DCMAKE_BUILD_TYPE=Release -DAUTOREMESHER_BUNDLED_TBB=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=11.2
cmake --build build/cli --parallel 2
cmake --install build/cli --prefix build/cli-install
python3 addon/package.py --cli-prefix build/cli-install
```

These are the flags CI uses for the `addon-macos-arm64` artifact (static oneTBB, so the program needs
only macOS's own libraries). Built on your own Mac, the program is not quarantined, so Gatekeeper does
not stop it. Intel Macs are not built or tested by CI.

## Core tests

```
cmake -S . -B build/cli -DCMAKE_BUILD_TYPE=Release -DAUTOREMESHER_BUILD_TESTS=ON
cmake --build build/cli --parallel 2
ctest --test-dir build/cli --output-on-failure
```

Build all targets before `ctest`: a test whose executable was not built is reported as failed. The
add-on's own tests (worker against a built remesher, and a headless Blender check) are described in
[addon/README.md](../addon/README.md#tests).

## Desktop app (development only)

AutoRemesher's Qt GUI is kept for development: it shows the pipeline's intermediate stages, and its
headless mode is used by the benchmark tools. It is built and smoke-tested by CI on Linux but not
packaged. Running it with no arguments opens the GUI.

Linux (Ubuntu 24.04, Qt 5.15):

```
sudo apt install build-essential qt5-qmake qtbase5-dev libqt5opengl5-dev qttools5-dev-tools libqt5svg5-dev libtbb-dev libgl1-mesa-dev
mkdir -p build/app && cd build/app
qmake ../../autoremesher.pro
nice -n 10 make -j2
```

The binary is `build/app/autoremesher`. Without `libqt5opengl5-dev` qmake fails with
"Unknown module(s) in QT: opengl". Headless use, with no display needed:

```
QT_QPA_PLATFORM=offscreen build/app/autoremesher -i in.obj -o out.obj --target-quads 2000
```

Other flags: `--report`, `--edge-scaling`, `--sharp-edge`, `--smooth-normal`, `--adaptivity`,
`--anisotropy` (see `src/main.cpp`).

Windows and macOS builds of the desktop app follow AutoRemesher's original instructions (Qt 5.15 with
`qmake` and `nmake` on MSVC, Qt 6 from MSYS2, or Homebrew `qt@5` on macOS); they are not built by CI
here and may need fixes.

## Command line

The remesher can be used without Blender:

```
build/cli-install/bin/autoremesher-cli -i in.obj -o out.obj --target-quads 5000 --report-json report.json
```

`--help` lists every option (the panel's settings, plus `--fold-repair on` for the panel's Repair
Folds button, `--flatten-spikes on` for its Also Flatten Needle Spikes box, and `--extractor qex|trace`
to compare libQEx with the older extractor; the panel's Quad Smoothing is `--quad-smoothing`); `--version` prints the build id. Exit status 0 means
success, 1 a wrong command line, 2 a rejected, cancelled or empty result. The report's format is
the `autoremesher.remesh-result` schema, written in `src/AutoRemesher/remesh.cpp`; `addon/quadami/worker.py` shows how the add-on
reads it.
