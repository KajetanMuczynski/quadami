<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 Quadami contributors
-->

# Website builder

Builds the Quadami website (GitHub Pages) from a **whitelist** of Markdown files in this
repository. The site is plain HTML with one stylesheet: no JavaScript, no web fonts, no theme,
light and dark by the reader's system setting.

```
python -m pip install --require-hashes --no-deps -r tools/site/requirements.txt
python tools/site/build.py --out build/site --offline   # preview; open build/site/index.html
python -m unittest discover -s tools/site/tests          # the builder's tests
```

Without `--offline` it fetches the release list from the GitHub API (set `GITHUB_TOKEN` to avoid the
anonymous rate limit); CI and the deploy always do.

## What is published

Only the pages in [site.json](site.json), each one Markdown file or some of its sections:

| Page | Source |
|---|---|
| Home | `docs/site/home.md`, plus the newest release's download link |
| Releases | `docs/site/releases.md`, plus every published GitHub release (never drafts) with files and notes |
| Install | `docs/USER-GUIDE.md`, section "2. Install and update" |
| User guide | `docs/USER-GUIDE.md` |
| Results | `docs/gallery/README.md` and its images |
| Community | `docs/site/community.md` |
| Contact | `README.md`, section "Contact" |
| Credits & license | `README.md`, sections "License" and "Origin and credits" |
| Building (not in the menu) | `docs/BUILDING.md` |

Everything else under `docs/` (plan, TODO, research, targets, sessions) is not published. A link from
a published page to any other file of the repository goes to that file on GitHub instead.

## What the build checks (and fails on)

- a relative link to a file that does not exist, or to a heading that does not exist in it;
- an image outside the `media` folders (`docs/gallery/img/`, `docs/site/img/`);
- raw HTML that carries `href` or `src` (it would get past the link check: write it in Markdown);
- a link to upstream AutoRemesher anywhere but the credits page (README rule: upstream links only
  in "Origin and credits");
- a `forbidden` string in the output (paths of the private test models).

CI runs the tests and the full build on every PR (job `site` in `ci.yml`, artifact `site-preview`
for a look before merging); `.github/workflows/pages.yml` deploys from master.

## Changing it

- **A new page:** add it to `pages` in `site.json` (`nav` puts it in the menu). Write it in Markdown;
  links between `.md` files work on GitHub and on the site alike.
- **Community entries:** `docs/site/community.md` says how; never without the author's written
  permission.
- **Own domain:** buy it, add a DNS `CNAME` record pointing to `kajetanmuczynski.github.io`, enter it
  in Settings > Pages > Custom domain (tick Enforce HTTPS once offered), and change `url` in
  `site.json`. All links inside the site are relative, so nothing else changes.
- **Why our own 300 lines instead of Jekyll, MkDocs or Hugo:** the whitelist, the link rewrite to
  GitHub and the section pages are the whole job, and each of those tools would need plugins or
  config for them plus a theme and its own update cycle. This needs one pinned pure-Python package
  (markdown-it-py, CommonMark like GitHub) and nothing else to keep up to date.
