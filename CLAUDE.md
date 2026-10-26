# Quadami (AutoRemesher fork)

Notes for Claude working in this repository.

## What this is
- Field-guided automatic quad remesher by Jeremy Hu (huxingyi/autoremesher, MIT), forked at `4a49f82` (v1.2.0).
- Pipeline core is Qt-free C++14 in `src/AutoRemesher/` (public headers in `include/AutoRemesher/`):
  isotropic remesh, frame field, singularity simplification, mixed-integer quad parameterization
  (Eigen `SimplicialLDLT` + own rounding), quad extraction. Parallelism via TBB.
- GUI (`src/*.cpp`, Qt 5.15 or Qt 6 + OpenGL) wraps the core; built with qmake (`autoremesher.pro`).
- Vendored in `thirdparty/`: eigen, tbb (2017-era), isotropicremesher, meshoptimizer, QtAwesome,
  QtWaitingSpinner. Upstream README mentions Geogram/libigl, but the `.pro` uses neither.
- `origin` = Kajetan's fork (github.com/KajetanMuczynski/quadami), `upstream` = huxingyi. Never push upstream.

## License policy
- The fork is **GPL-3.0-or-later**. Keep Jeremy Hu's MIT notices (`LICENSE-MIT-upstream`, file headers)
  intact; new files get a GPL-3.0-or-later header.
- **No GPL-3.0-only code** (CoMISo, igl/copyleft/comiso, quadwild-bimdf, etc.).
- **No code from repos without a LICENSE file**: re-implement from the paper or get written permission.
- Check every new dependency against the plan's `LICENSES.md` before adding it.

## Dependency updates (Kajetan, 2026-10-15)
- Bump a dependency only to a release that is at least 7 days old, pinned by commit SHA or checksum
  (actions by SHA, FetchContent by commit, downloads by SHA-256), never by a movable tag or "latest".
  Exception: a release that fixes a known CVE affecting us goes in at once.
- Dependabot does this for GitHub Actions (`.github/dependabot.yml`, `cooldown: 7 days`); by hand for
  oneTBB, vendored Eigen/meshoptimizer, Qt, Homebrew formulae and Blender.

## Build (checked 2026-10-15: Ubuntu 24.04, GCC 13.3, Qt 5.15.13, oneTBB 2021.11)
Deps: `qt5-qmake qtbase5-dev libqt5opengl5-dev qttools5-dev-tools libqt5svg5-dev libtbb-dev libgl1-mesa-dev`
(`libqt5opengl5-dev` comes only as a recommend of `qtbase5-dev`; without it qmake fails with "Unknown module(s) in QT: opengl").
README's `libqt5multimedia5-dev` does not exist on 24.04 and is not needed.

Qt app, out-of-tree (`build/` is gitignored), no extra flags (~2.5 min on 4 cores, 24 harmless warnings):

    mkdir -p build/app && cd build/app && qmake ../../autoremesher.pro && make -j4
    # -> build/app/autoremesher (links system libtbb.so.12; the vendored thirdparty/tbb is Windows-only)

Headless mode of the Qt app (what the old autoremesher-blender-bridge calls; our add-on uses `autoremesher-cli` below); `offscreen` avoids needing a display:

    QT_QPA_PLATFORM=offscreen build/app/autoremesher -i in.obj -o out.obj --target-quads 2000
    # 16k-tri UV sphere -> 1624 quads, 0 non-quads, 0.55 s. Other flags: --report, --edge-scaling,
    # --sharp-edge, --smooth-normal, --adaptivity, --anisotropy (see src/main.cpp)

Qt-free core and benchmarks: `tools/bench/` (`ar_bench` driver, corpus, metrics; see its README).
`tools/bench/build.sh <work-dir>` builds the app and the bench drivers out of tree.

Qt-free core and CLI with CMake (top-level `CMakeLists.txt`, target `autoremesher_core` + `autoremesher-cli`,
the Blender add-on's worker; Windows builds static oneTBB and /MT, so no DLLs):

    cmake -S . -B build/cli -DCMAKE_BUILD_TYPE=Release && nice -n 10 cmake --build build/cli --parallel 2

The core source list exists three times: `autoremesher.pro` (app), top-level `CMakeLists.txt`
(explicit list) and `tools/bench/bench/CMakeLists.txt` (glob, kept separate on purpose so the
bench can build other checkouts for A/B). A new core `.cpp` goes into the first two.

Core tests (`tests/`, Qt-free, link `autoremesher_core`): configure with `-DAUTOREMESHER_BUILD_TESTS=ON`,
build all targets (ctest reports a test whose executable wasn't built as failed), run
`ctest --test-dir <build> --output-on-failure`.

Blender add-on (`addon/`, Quadami): `python addon/package.py --cli-prefix <install-prefix>`
builds the extension zip with the CLI bundled; see `addon/README.md`.

CI (`.github/workflows/`):
- `ci.yml` (every PR, push to master or a tag, manual run): all checks in one workflow. Its last job,
  **CI passed**, is green only if every other job succeeded; it is the one check branch protection
  is meant to require (TODO 0.9; checked 2026-10-21: the `master` ruleset requires a PR and blocks
  force pushes and deletion, but requires no status check yet). Branch pushes without a PR get no run (use Actions > ci > Run workflow);
  a PR with merge conflicts gets no run either, so **CI passed** shows as missing.
  A PR that changes only documentation (`docs/`, Markdown at the top level or directly in `addon/`)
  skips the macOS job (`changes` job decides; GitHub's macOS runners can queue for many minutes);
  **CI passed** accepts that skip and nothing else. Other paths never skip macOS or Windows: add-on
  Python broke on Windows only before (#18), and Qt-GUI/bench-only PRs, the only other safe skip, never occurred.
  - `format`: `ci/lint.sh` (src/ and tests/) with clang-format pinned to 23.1.3 (pip); bump on purpose.
  - `qt-app (linux)`: qmake app and `ar_bench`, `ci/smoke_test.sh` on both (1,200-2,400 quads).
  - `addon (linux-x64|windows-x64|macos-arm64)`: one CMake Release build per platform, the shipped
    configuration (static oneTBB everywhere; Linux built in an AlmaLinux 8 container by
    `ci/build_linux_portable.sh`, glibc 2.28 checked by `ci/check_linux_libs.py`), with the core tests: `ctest --no-tests=error`,
    `ci/output_gate.py` on the installed CLI (procedural sphere, torus, box, open tube, two islands, sphere centred at 1e5;
    fails on exit status, quads outside 0.6-1.5x target, >2 % non-quads, any non-manifold edge,
    an input island more than 1 % farther than one output edge from the output (TARGETS H4 coverage: dropped
    or collapsed parts), report/OBJ mismatch; folded faces are reported in the job summary, not gated yet), worker tests, zip installed into a real Blender (4.2 LTS on Linux, 5.2 on Windows and
    macOS). All three zips (Linux glibc 2.28+, macOS 11.2+ Apple silicon) are uploaded as artifacts
    `addon-<platform>`. One macOS job per PR, since macOS runners are the scarce ones.
  - `site`: tests and builds the website (`tools/site/`, whitelist in `site.json`); fails on broken links,
    upstream links outside the credits page, images outside the media folders, private paths. Artifact `site-preview`.
    User-facing doc changes must reach the site: every `##` section of `README.md` and `docs/USER-GUIDE.md`
    is on a page or excluded with a reason in `site.json` `coverage`, else `site` fails.
  - `release (draft)`: only on a `v*` tag, after **CI passed**: `ci/release.py` checks the tag
    against the manifest version and the zips, and makes a **draft** GitHub release with the three
    zips, `SHA256SUMS.txt` and notes from `docs/releases/`. Publishing is Kajetan's click.
    Procedure, version rule and release checklist: `docs/RELEASING.md`.
- `pages.yml` (push to master touching `docs/`, `README.md` or `tools/site/`, a release event, manual):
  builds the site from master and deploys it to GitHub Pages (needs Settings > Pages > Source: GitHub Actions).
  Only the whitelisted pages are published; never add private models or anything from `private-sculpts`.
- `release-check.yml` (by hand only, before a release tag): the TARGETS release check on Linux,
  Windows and macOS, each building the candidate and the previous release with `tools/bench/ab.py`
  (corpus and closed sets, 5 replicates, about an hour per platform) and deciding with
  `tools/bench/release_gate.py`. Not part of **CI passed**; procedure in `docs/RELEASING.md`. A PR
  touching it, `ab.py`, `gates.py`, `metrics.py` or `release_gate.py` gets a smoke run (2 meshes).
- `rerun-dropped.yml` (when a ci run completes): GitHub drops a macOS arm64 job that waits ~15 min
  for a runner (cancelled, no runner, no steps), which turns **CI passed** red. `ci/rerun_dropped.py`
  then re-runs the failed jobs of that same run, only if every failed job never got a runner, the
  run was not cancelled by a newer push, the PR head is unchanged and the attempt is below 3; a real
  failure or timeout is never re-run. It runs master's copy only (untestable on its own PR); its
  decision is unit-tested in `qt-app` (`ci/tests`). A long GitHub capacity outage still beats it:
  then click "Re-run failed jobs" later.
- What ships is only the Blender add-on (Kajetan, 2026-10-15). The standalone Qt app (AppImage,
  Windows zip, DMG: upstream's `release.yml`) is no longer packaged; it is still built and
  smoke-tested on Linux by `qt-app`.

Running the binary with no args opens the GUI. The core needs `setTargetTriangleCount(n > 0)` or
`remesh()` fails.

## A/B runs and the noise rule (2026-10-20, gap scan item 1)
- Every quality number in a PR or research note comes from `tools/bench/ab.py` (`run <ref>`, `compare A B`;
  README "A/B a change"), so it can be re-run from the repo. No per-thread corpus runners; extend `ab.py`
  or add a set to `corpus.json` instead, and keep new mesh generators in `tools/bench/`.
- One ulp of input can flip a run's genus. Before claiming a discrete result ("fixes mesh X", genus, holes,
  a run that fails or recovers), repeat those runs on both sides with `--replicates 5` (1-ulp perturbed
  inputs) and claim only what every replicate agrees on. Aggregates over a whole set need no replicates.
- A change that can move topology also runs `--set closed`.
- Test lanes (Kajetan, 2026-10-22; tools/bench/README "Test lanes"): `ab.py lane <merge-base> HEAD` decides,
  never the PR's size or the author. **none** (nothing compiled changed): CI only. **light** (compiled code
  changed, `out.obj` byte-identical on every public set with default targets, 265 runs since the `external` set, plain + one float32 replicate): paste the
  `lane` output, A/B only the new option on its meshes. **full** (anything moved): `--replicates 5` on every
  public set, both sides, as before; no replicate shortcuts (tested on #67, they miss float32-only effects).
  Input-handling changes (cleanup, orientation, fold detection) also run `lane` on the private set (`AR_CORPUS`).
  Drift guard: master's ×6 side is deterministic, so compute it once per core merge and `compare` it with
  the previous core merge (README "Test lanes").

## PRs, branches and commits (Kajetan's rules, 2026-10-15)
- Every PR targets `master` of KajetanMuczynski/quadami. Never a feature branch, no stacked PRs
  (PRs into other branches run no CI, and a late merge there never reaches master). GitHub defaults
  a fork's PR to the parent repo: check the base repository is not huxingyi/autoremesher.
- Title: `<type>(<area>): <what it does> [TODO x.y]`, e.g. `fix(islands): keep tiny islands closed [TODO 2.14]`.
  Types: fix, perf, feat, ci, docs, refactor. Areas: islands, extraction, sizing, cross-field,
  rounding, addon, ci, docs, bench. Omit `[TODO x.y]` only when no TODO item applies.
- Body starts with `Merge order / depends on: ...` (`none` if independent).
- Body holds only what this change does, possible conflicts and dependencies (Kajetan, 2026-10-26).
  No clutter: no background, no replay of the work, no to-dos for Kajetan.
- If the PR touches a conflict-prone file (`tests/CMakeLists.txt`, `src/AutoRemesher/remesh.cpp`,
  `src/AutoRemesher/quadextractor.cpp`), say so in the body so parallel PRs can be ordered.
- A PR stays **draft** until its thread has verified everything: **CI passed** green on the current
  head, no conflicts with master, A/B or regression checks finished, description final. Then mark it
  Ready for review and reply "ready to merge" with the head SHA.
- macOS drop exception (Kajetan, 2026-10-21): if **CI passed** is red only because the macOS job was
  dropped (conclusion cancelled, no runner, no steps: it never ran) and every other job of the PR's
  own run on the current head succeeded, the PR may be marked ready after its one re-run (by
  `rerun-dropped.yml` or by hand) dropped as well, or after the first drop where no re-run is possible. Say so in the reply ("macOS dropped, not run").
  A macOS job that ran and failed, timed out or was cancelled with a runner still blocks. Nothing
  in GitHub enforces this: the `master` ruleset has no required status check (TODO 0.9), so the
  thread's check is the only gate. Kajetan merges only non-draft PRs;
  to push more work to a ready PR, convert it back to draft first.
- Waiting for CI: after opening or pushing to a PR, subscribe to its activity (`subscribe_pr_activity`).
  When CI results arrive, run the ready check and mark the PR ready in the same turn; never end a turn
  with only "waiting for CI". A PR with a merge conflict gets no CI run, so merge master first.
  Unsubscribe after the merge.
- A thread pushes to its one `claude/project-thread-*` branch and has at most one open PR; the next
  PR waits until that one is merged. Never delete branches.
- Branch protection for `master` (ruleset, up-to-date setting, why no merge queue) is planned in `docs/TODO.md` item 0.9.
- Before claiming anything about a PR or CI (base, merged or not, which checks ran, their result),
  check it on GitHub. Never report from memory.
- Commit trailer: `Co-Authored-By: Claude <noreply@anthropic.com>` (short form, Kajetan's choice).

## Design critique (Kajetan, 2026-10-19)
- When designing something new (plan, workflow, architecture, CI, feature), don't just agree with a good idea.
  Say what Kajetan may have missed or should weigh, whether the idea is good enough, and what would do it better.
- Put it in the reply, next to the work, not as a separate question. Raise real gaps only, not filler objections.

## Keep the machine cool
Kajetan's laptop has 4 cores and runs hot. The heavy part is the release build: `-O3 -flto` with
`make -j4` (or `-j$(nproc)`) holds all cores at 100 % for minutes, and the LTO link is one long
single-file job on top. For routine work:
- Build with `nice -n 10 make -j2`.
- Day-to-day app builds can drop LTO (checked: removes `-flto` from the generated Makefile):
  `qmake ../../autoremesher.pro -after "QMAKE_CXXFLAGS_RELEASE-=-flto" "QMAKE_LFLAGS_RELEASE-=-flto"`.
  Keep the full LTO build for release checks and for timings that go into the baseline.
- Rebuild incrementally in the same build folder; don't wipe it between runs.
- Don't run the full corpus or `experiments.sh` while other builds or sessions are busy, and quit
  the GUI or any leftover `autoremesher` process after testing.

## Plan and context
`docs/` holds the plan, copied from Kajetan's `../docs/` on 2026-10-15. The research reports and
`archive/` stay in `../docs/` only, so links to `research/` and `archive/` don't resolve in the repo.
- `docs/README.md` (index) and `docs/TODO.md` ("Order at a glance", critical path) first.
- `docs/REFERENCE.md`: pipeline map with line numbers, known performance and math issues, baseline.
- Log each working session as a new file in `docs/sessions/` (format in its README). Never append to a shared log: one shared file made every merge conflict with every other open PR.
