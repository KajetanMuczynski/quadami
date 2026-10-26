# Website and release downloads: design note

Status 2026-10-22: **the website is built** (Kajetan asked for it on 2026-10-22, before the first
release): [tools/site/README.md](../tools/site/README.md) describes it, `pages.yml` deploys it. It
follows the proposal below with three changes: our own small builder instead of Jekyll (the
whitelist, link rewrite and section pages need it), a Releases page and a Community page, and a home
page that showed "no release yet" until 0.1.0 (decision 1 below, answered by making the empty
state honest rather than waiting). The rest of this note is the original proposal; the release
questions at the end are settled in [RELEASING.md](RELEASING.md) (version check against the manifest,
draft-only releases, `SHA256SUMS.txt`, build attestations, a portable Linux zip). The release
workflow itself is being built by the "Release workflow for 0.1" thread; this note covers the web
page, how the gallery ([gallery/](gallery/README.md)) feeds both, and what to decide first.

## Web page: proposal

- **Host:** GitHub Pages of the fork (`kajetanmuczynski.github.io/quadami`), deployed by a small
  Actions workflow (`actions/upload-pages-artifact` + `actions/deploy-pages`, pinned by SHA like the
  rest of CI) on push to master when its sources change.
- **Content:** one landing page (what Quadami is, one before/after image, download button to the
  latest release, Blender version support), the user guide (`docs/USER-GUIDE.md`), the gallery
  with its limits section, and the license/credits. Nothing else.
- **Source:** a whitelist the workflow copies into the Pages artifact (landing page, USER-GUIDE.md,
  gallery/), not "Pages from `docs/`". `docs/` is the working plan (TODO, REFERENCE, research
  notes, specialist questions, sessions); published as a site it would bury the guide under
  internal notes and invite readers to treat plans as promises.
- **Generator:** plain Markdown rendered by Pages' default Jekyll, or a hand-written HTML landing
  page. No theme framework or npm build until there is a reason: every dependency there falls under
  the 7-day pin rule.

## What to decide first, in order

1. **Is there a release to point at?** A page whose main button has nothing to download is worse than
   no page. Build the site after the first public release, not before.
2. **What the page promises.** The gallery's limits section exists so the page doesn't overclaim;
   decide whether the landing page shows the limits (recommended: one line and a link) or only the
   gallery's good cases.
3. **Whitelist vs whole `docs/`.** Recommended whitelist (above). Whole `docs/` is less work once and
   harder to undo once people link to internal pages.
4. **Domain.** `github.io` costs nothing; a custom domain needs DNS, renewal and an owner. Can be
   added later without breaking links if the github.io URL redirects.
5. **Who updates it.** Every push to master that touches the whitelist redeploys; the gallery
   images change only when someone reruns `tools/gallery/gallery.py`.

## How the gallery feeds the release

- The gallery records the commit it was made from (`0e9bec8` now). Images from an older core next
  to a newer download misstate both. Proposal: rerun the gallery at the release commit as a
  **manual checklist step** of the release (about 10 minutes on 4 cores, needs bpy 5.2.2 and
  `libegl1`), not a CI job: the bpy wheel is about 300 MB per run, and a render diff is not a gate
  anyone would act on.
- Release notes link the gallery's limits section, so the known limits ship with the download.

## Release decisions this note does not settle (owned by the release thread)

For completeness, the questions Kajetan should see answered there, with what I would weigh:

- **Versioning:** one source of truth. `addon/quadami/blender_manifest.toml` says `0.1.0`; the tag,
  the zip name and the panel build line should be checked against it in the workflow, so a tag
  `v0.1.1` on a `0.1.0` manifest fails instead of shipping.
- **Signing / Gatekeeper / SmartScreen:** unsigned macOS binaries inside a zip are quarantined on
  download; an unsigned Windows exe can trip SmartScreen. Without an Apple Developer ID (paid) the
  honest option is a documented workaround in the guide, tested on a real Mac (not done yet).
- **Checksums and provenance:** `SHA256SUMS` attached to the release is cheap; GitHub's build
  provenance attestation (`actions/attest-build-provenance`) ties each zip to the workflow run, also
  free. Neither helps a user who doesn't check, so the guide should show the one command.
- **Who runs it:** only Kajetan pushes release tags; the workflow should create a draft release that
  he publishes by hand, so a mistaken tag publishes nothing.
- **Linux zip:** at the time, the CI Linux zip linked the runner's libtbb and glibc. Settled: it is
  now built in AlmaLinux 8 with static oneTBB and needs glibc 2.28 (RELEASING.md).

## Privacy guard

The gallery set in `tools/bench/corpus.json` may only use public-license or procedural meshes;
`tools/bench/tests/test_gallery_set.py` (run in CI by `qt-app`) fails if an entry points anywhere
else or at a mesh flagged unclear in [gallery/README.md](gallery/README.md#sources-and-licenses).
That is the only enforcement: the private sculpts never enter the repo, and the gallery script reads
only the set.
