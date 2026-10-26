<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Quadami contributors -->

# Testing the Quadami add-on on Windows

The Windows twin of the Linux command sheet (`quadami-commands.md` in the project files). For
Windows 10 or 11, 64-bit, Blender 4.2 or newer; paths below use Blender 4.5 as the example, so
replace `4.5` with the version you run. Nothing has to be compiled. The script
[`quadami-update.ps1`](quadami-update.ps1) next to this file does the install and cleanup.

**What has been checked, and what has not.** Nobody has run this on a real Windows desktop yet.
Each statement is tagged:
- *(CI)*: shown by the `addon (windows-x64)` CI job, which builds the zip, installs it into
  Blender 5.2 for Windows with the command the script uses and remeshes a sphere headless
  (log of master `bfa0397`, 2026-10-21). CI never opens Blender's window.
- *(manual)*: from the Blender manual.
- *(memory)*: from memory of Blender's and Windows' interface, not checked. Correct it here if it is wrong.
- The script was run only with PowerShell 7 on Linux against a fake Blender (dry run, refusal of
  the GitHub wrapper zip and of a Linux zip, the `y`/`N` prompt, deletions, install, a user folder
  with Polish letters). **Untested: Windows PowerShell 5.1, a real Windows Blender, real Windows
  paths, Defender and SmartScreen.**

## 1. Get the zip

1. Sign in to GitHub (artifacts download only when signed in) and open
   <https://github.com/KajetanMuczynski/quadami/actions/workflows/ci.yml?query=branch%3Amaster>.
2. Open the newest run whose job **addon (windows-x64)** has a green tick. The run as a whole may
   be red only because its macOS job never got a runner; the Windows zip is still uploaded *(CI)*.
3. Scroll to **Artifacts** at the bottom and click **addon-windows-x64**. You get
   `addon-windows-x64.zip`: GitHub's wrapper around the real add-on *(CI)*.
4. Right-click it > **Extract All...** *(memory)*. Inside is `quadami-0.1.0-windows-x64.zip`. That
   inner zip is the add-on: **do not unpack it**.

A PR's own run also has an `addon-windows-x64` artifact; its build line then reads
`pr<N> <sha>+master`. Artifacts expire after GitHub's retention period (90 days unless the repo
says otherwise, *(memory)*). A published release has its own `quadami-<version>-windows-x64.zip`
under **Assets** on the Releases page, with no outer wrapper.

Every zip carries `build-id.txt` (commit and date), e.g. `bfa0397 2026-10-21` *(CI)*. The version
in Blender's list stays 0.1.0 for every build, so the build line is the only way to tell builds apart.

## 2. Find blender.exe

- Installer from blender.org: `C:\Program Files\Blender Foundation\Blender 4.5\blender.exe` *(memory)*.
- Portable zip from blender.org: `blender.exe` in the folder you unpacked.
- Steam or Microsoft Store: the script is untested there. The Store build keeps its settings
  under `%LOCALAPPDATA%\Packages\...` instead of `%APPDATA%` *(manual, 4.0 directory layout)*;
  install by hand (section 3) instead.

Use `blender.exe`, not `blender-launcher.exe`: only `blender.exe` prints to the console *(CI for
blender.exe)*.

## 3. Install by hand (simplest)

1. Start Blender. **Edit > Preferences > Get Extensions** (left column).
2. Small **⌄** arrow at the top right of that page > **Install from Disk...** > pick
   `quadami-0.1.0-windows-x64.zip` > **Install from Disk** *(memory; same as on Linux)*. Dragging
   the zip onto the Blender window also works in 4.2+ *(memory)*.
3. **Restart Blender** if it showed "Missing Add-ons" before.

Installing the same id (`quadami`) again replaces the old copy in place (checked on Linux; the
Windows install uses the same Blender code). It lands in
`%APPDATA%\Blender Foundation\Blender\4.5\extensions\user_default\quadami`, with the remesher at
`...\quadami\bin\autoremesher-cli.exe` *(CI: `C:\Users\runneradmin\AppData\Roaming\Blender
Foundation\Blender\5.2\extensions\user_default\quadami\bin\autoremesher-cli.exe`)*. Each Blender
version (4.2, 4.5, 5.2) has its own folder, so older versions keep their old copy.

**Remove an old copy by hand:** Preferences > Add-ons, expand Quadami, **⌄** or **...** menu >
**Uninstall** *(memory)*. Or quit Blender and delete the `quadami` folder above in File Explorer
(paste `%APPDATA%\Blender Foundation\Blender` into the address bar). An old pre-extension copy
would sit in `...\4.5\scripts\addons\autoremesher_blender` *(manual: legacy add-ons live in
`scripts\addons`)*.

## 4. Install with the script (also cleans up)

Quit Blender, open **PowerShell** (Start menu, type *PowerShell*), then:

```powershell
cd $HOME\Downloads
powershell -ExecutionPolicy Bypass -File .\quadami-update.ps1 .\quadami-0.1.0-windows-x64.zip "C:\Program Files\Blender Foundation\Blender 4.5\blender.exe"
```

`-ExecutionPolicy Bypass` applies to this one run only: Windows blocks downloaded scripts by
default *(memory)*. It is a **dry run**: it prints Blender's version and extensions folder (asked
from that Blender, not guessed), what it would delete and the build it would install. Then run
the same line with `-Apply` before the zip path; it asks `Proceed? [y/N]`, deletes, installs with

    blender.exe --command extension install-file -r user_default -e <zip>      (CI)

checks that the folder exists and the add-on is enabled, and prints
`Build: <sha> <date>`. `-AllVersions` also removes Quadami from other Blender versions.

It may delete only: Quadami in that Blender's extensions folder; with `-AllVersions`, Quadami and
`autoremesher_blender` in other versions under `%APPDATA%\Blender Foundation\Blender`; worker temp
folders `%TEMP%\quadami-*` and `%TEMP%\autoremesher-*` left by a crash; old `quadami-*.zip` and
`addon-windows-x64*.zip` directly in `Downloads` (never the zip you install). It refuses GitHub's
wrapper zip, a zip for another platform and a running Blender.

## 5. Check the install

- Preferences > Add-ons > **Quadami** ticked; expanded it says *Using: ...\bin\autoremesher-cli.exe*
  with a check mark. Leave *Remesher executable* empty.
- 3D Viewport, press **N**, tab **Quadami**, scroll to the bottom: a grey line
  *Add-on build bfa0397 2026-10-21* (your build). Red with ⚠ and a second line *Remesher build ...*
  means the add-on and its remesher come from different builds: reinstall.
- **Window > Toggle System Console** opens the console where the worker's messages appear if
  something fails *(addon/README.md; memory for the menu)*.

## 6. Antivirus and SmartScreen

`autoremesher-cli.exe` is unsigned and links everything statically, so it needs no Visual C++
Redistributable and no DLL beyond Windows' own *(CI checks its import table)*.
- **SmartScreen** ("Windows protected your PC") checks files you open from Explorer that carry the
  download mark. Blender unpacks the zip itself and starts the remesher directly, so SmartScreen
  is not expected to appear (*inferred*, untested).
- **Defender** may scan or, rarely, quarantine a new unsigned program. Then the panel reports the
  remesher as not found or the run fails at once. Note the Defender message (Windows Security >
  Protection history) and report it; do not turn Defender off.
- The script itself: if PowerShell says it "is not digitally signed", the `-ExecutionPolicy Bypass`
  part was missing.

## 7. Test plan

Settings default unless given: Adaptivity 0.5, Sharp Edge 90°, Anisotropy 1, Edge Scaling 1,
Smooth Normal 0°. Object Mode, mesh selected, **Remesh**.

| # | Test | Linux reference (where from) |
|---|---|---|
| 1 | **Add > Mesh > Monkey**, Subdivision Surface level 2, Target 5000, Resolution **Fast** | cloud 4e9cdc7: 4976 quads, 0 non-quads, 2 folded, 1.8 s; Kajetan's laptop 4d15d5f: 4917 quads, 2 non-quads |
| 2 | Same, Resolution **Quality** | cloud 4e9cdc7: 5069 quads, 2 non-quads, 1 folded, 3.5 s; laptop 4d15d5f: 4972 quads, 0 non-quads |
| 3 | Target 300000 > Remesh | a "Remesh to 300,000 quads?" dialog; press Cancel |
| 4 | Target 50000 > Remesh, after a few seconds **Esc** | status bar *cancelling...*, within 5 s *Cancelled*, no new object |
| 5 | **Ctrl+Z** after a finished run | the result goes, the source is shown again |
| 6 | Private sculpts, if you have them: the **head** (Sphere) at 10000 Quality | cloud 4e9cdc7: 10060 quads, 24 non-quads, 0 non-manifold, 0 flips, 43 folded, 12.1 s; on older builds holes appeared from about 6k up in most runs |
| 7 | Same head at 3000 and 20000; the **cube** at 3000; the **sword** at 3000 | laptop 4d15d5f: head 3k 3054 q / 12 nq / 9 folds, cube 3k 2979 / 6 / 2; cloud 4e9cdc7: head 20k 19981 / 51 / 50 / 31 s; sword 3k with the needle-tip fix (#67, A/B replicates): 1933-2711 quads |
| 8 | After the runs: `%TEMP%` has no `quadami-*` folder left | the worker removes its temp folder |

The head, cube and sword are Kajetan's private models: they are **not** in the repository or in
the zip. Use them only if you already have them.

**How close should Windows be?** Not identical. On the same commit (`bfa0397`) CI's 128×64 UV
sphere at 2000 quads gave 2010 quads on Linux (Blender 4.2) and 1998 on Windows (Blender 5.2)
*(CI)*; one ulp of difference in the input or the compiler's arithmetic can move a run. Expect
quads within a few per cent of the Linux row and the same order of defects. A crash, an error,
*Empty islands* > 0, non-manifold edges or winding flips > 0, or a time more than about 2× the
Linux time on a similar machine is a real finding. The references above come from older builds;
for an exact comparison ask a cloud thread to run the same commit on Linux.

## 8. What to report back

For every run:
- the grey **build line** (copy the text);
- Windows version (Settings > System > About), CPU, RAM; Blender version (Help > About or the splash);
- Resolution and Target Quads;
- every panel row: Quads, Non-quads, **Holes** (shown for closed inputs; e.g. `2 (240 open edges)`),
  Non-manifold edges, **Non-manifold vertices**, Winding flips, Folded faces, Input cleanup if
  shown, Empty islands, Time;
- any warning lines under the rows (red), including the hint how to see holes;
- a screenshot of the panel and one of the result in the viewport (**Win+Shift+S**);
- if anything fails: the System Console text after *Quadami worker output*, the status line, and
  the script's output if you used it.
