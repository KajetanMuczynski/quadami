<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 Quadami contributors
-->

# Releasing the add-on

What ships is the Blender extension zip, one per platform: `linux-x64`, `windows-x64`,
`macos-arm64`. A release is a GitHub release with those three zips, `SHA256SUMS.txt` and notes. It
is made by CI from a tag and always starts as a **draft**: nothing is public until Kajetan presses
**Publish release** on GitHub.

## How it works

- Pushing a tag `v<version>` or `v<version>-<suffix>` runs the normal `ci` workflow on the tagged
  commit (`.github/workflows/ci.yml`): every check a PR gets, with all three add-on jobs, which
  build, test and upload `addon-<platform>`.
- Its last job, **release (draft)**, runs only when **CI passed** is green. `ci/release.py` checks
  that the tag matches the manifest version (`addon/quadami/blender_manifest.toml`), that the three
  zips carry that version, their own platform, the program, every license file and the same build
  id naming the tag (`v0.1.0 abc1234 2026-11-02`, shown in the panel's build line), and writes the
  notes from [releases/TEMPLATE.md](releases/TEMPLATE.md) and `releases/<version>.md`. Then it
  creates the draft release, or updates the draft of a re-run. A published release is never touched.
- Before that it attests the three zips (`actions/attest-build-provenance`, signed with Sigstore and
  stored by GitHub under the repository's **Attestations**): `gh attestation verify <zip> -R
  KajetanMuczynski/quadami` proves a download was built by this workflow from the tagged commit. The
  release notes tell users how. A checksum only proves the file matches the notes; the attestation
  also proves who built it.
- A suffix (`v0.1.0-rc.1`, `v0.1.0-test.1`) makes a **pre-release**; its notes file may be missing
  or unfinished. A plain `v0.1.0` needs `releases/0.1.0.md` with the sections *What changed*,
  *Warnings in the panel* and *Known limits* and no "TBD".
- A macOS job that GitHub drops for want of a runner turns **CI passed** red and skips the release;
  `rerun-dropped.yml` re-runs it (and the release job) up to attempt 3. After that: Actions > the
  tag's run > **Re-run failed jobs**. Any real failure leaves no release.
- The Linux zip is built in an AlmaLinux 8 container (`ci/build_linux_portable.sh`) and
  `ci/check_linux_libs.py` fails it if it needs anything newer than glibc 2.28 and GCC 8's libstdc++
  (the floor of Blender 4.2 itself), so it runs on Ubuntu 20.04+, Debian 10+, RHEL 8+ and the rest.
  Every PR builds it the same way, so what ships is what was tested.

## Versions

- The manifest version is the release version, `MAJOR.MINOR.PATCH`, and changes only in a release
  PR. While the major number is 0, the minor goes up for a release with new features or changed
  output, the patch for fixes only.
- Test builds between releases keep the last version; the build line in the panel tells them apart
  (`pr77 abc1234+master`, `abc1234 2026-11-03`, or `v0.1.0 ...` for the release itself).
- Not `0.1.0+<commit>` in the manifest: Blender would show it but every build would need its own
  manifest edit, and Blender 4.2's version check was not shown to accept build metadata. The build
  line already carries the commit.
- The extension id stays `quadami` (`bl_ext.user_default.quadami`): a new zip replaces the old one in
  place, and a renamed id would leave two add-ons installed side by side.

## Release checklist

Copy into the release PR's description and tick each line there.

1. **Gates.**
   - [ ] master CI green on the commit to tag (Actions > ci, branch master).
   - [ ] TARGETS release check ([TARGETS.md](TARGETS.md) §2.5, §5 "Releases") on all three
         platforms: **Actions > release-check > Run workflow**, `ref` = the commit to tag, `baseline`
         = the previous release's tag (for 0.1.0 master of 2026-10-22, `0e9bec8`, the default). Each
         platform builds both, runs corpus and closed sets with 5 replicates (about an hour; macOS
         longer) and `tools/bench/release_gate.py` decides: for 0.x no structural gate (H1–H6, H8)
         fails on every replicate of more cases (mesh, resolution, target) than in the baseline and no §3.2 validity number is worse (TARGETS §6,
         decision 3); tick **hard** for 1.0, where the structural gates must hold on every run. The
         job summary has the tables and `ab.py compare`; the artifact `release-check-<platform>` the
         run records. A macOS job dropped for want of a runner: re-run it from the run page.
   - [ ] The changes this release promises are merged. For 0.1.0 (Kajetan, 2026-10-22): the
         thin-parts (vines) fix and its panel warnings.
2. **Real-machine checks** (CI runs Blender without a window): install the master zip from CI on
   Kajetan's Linux laptop and remesh a sculpt; Windows and a Mac when one is at hand (unsigned
   program: SmartScreen and Gatekeeper behaviour are untested, write down what happened).
3. **Release PR** (`docs(docs): release <version>`): bump `version` in
   `addon/quadami/blender_manifest.toml`, write `docs/releases/<version>.md` (no TBD left), add the
   version's section to `CHANGELOGS.md`, point the install docs (README, USER-GUIDE §2) to the
   Releases page, and regenerate the public gallery at this commit (`tools/gallery/gallery.py`,
   once PR #79 is in; about 10 minutes, needs bpy) so its renders and limits match the release.
   Merge after CI is green.
4. **Optional dress rehearsal**: tag the merge commit `v<version>-rc.1` and check the draft
   pre-release (zips, notes, build line after installing one zip). Delete that draft and tag after.
5. **Tag**: `git tag -a v<version> -m "Quadami <version>" <merge commit>` and
   `git push origin v<version>`. Wait for the run's **release (draft)** job.
6. **Review the draft** on GitHub > Releases: three zips plus `SHA256SUMS.txt`, notes read well, the
   build line names the tag. Install one zip from the draft, and check it with
   `gh attestation verify <zip> -R KajetanMuczynski/quadami` (exits 0 and names `ci.yml` and the tag).
7. **Publish** (Kajetan only). A mistake found later: publish a new patch version; never replace
   the assets of a published release.

## Gaps (not done by the workflow)

- **What the release check does not cover**: it tests `ab.py`'s CMake build of the commit, not the
  shipped binary (the same on Windows and macOS; on Linux the zip's CLI comes from AlmaLinux 8, so
  only the source is checked there); H7 (determinism); the sculpt and brush sets and Kajetan's
  private sculpts (Linux, by hand in the cloud); anything inside Blender (step 2).
- **Structural gates fail on master today** (cloud dry run 2026-10-22, Linux, master `ae27301`
  against `0e9bec8`, byte-identical outputs): corpus 21 of 432 runs (human_neutral islands merged
  and holes, sword at 2k quads 11-22 % under target, csg_bracket u2 one hole), closed 114 of 468
  (thin ellipsoids at 150 quads drop the island or miss the count, tori over the count). Hence the
  0.x ratchet; 1.0 needs them fixed (`--hard`).

## Tag ruleset for `v*`

**Active since 2026-10-22** (set up by Kajetan): only repository admins may create, move or delete
release tags; GitHub Actions' token and future collaborators with write access may not. Checked the
same day: Kajetan's push of `v0.1.0-test.2` printed "Bypassed rule violations ... Cannot create ref
due to creations being restricted", so the rule is on and his bypass works. To set it up again
(e.g. on a fork) *(from memory; Kajetan followed it without reporting a difference)*:

1. Repository **Settings** > **Rules** > **Rulesets** > **New ruleset** > **New tag ruleset**.
2. **Ruleset name**: `release tags`. **Enforcement status**: **Active**.
3. **Bypass list** > **Add bypass** > **Repository admin** (that is Kajetan, so his own tag pushes,
   including test tags and deleting them afterwards, keep working).
4. **Target tags** > **Add target** > **Include by pattern** > `v*`.
5. **Rules**: tick **Restrict creations**, **Restrict updates**, **Restrict deletions** and
   **Block force pushes**. Leave the rest.
6. **Create**. Check: Settings > Rules > Rulesets lists `release tags` as Active, targeting `v*`;
   an admin's tag push then prints "Bypassed rule violations".

Only Kajetan has write access today, so this mostly guards against a compromised workflow or token
(the release job holds `contents: write`) and against future collaborators. It does not stop an
admin's own mistakes; the draft-only release job is what does that.
- **Unsigned programs**: no Apple notarization or Windows code signing (both need paid
  certificates). The notes say so.
- No "update available" check in the add-on (not planned for 0.x).
