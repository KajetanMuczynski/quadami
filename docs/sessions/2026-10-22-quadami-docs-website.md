# 2026-10-22 · cloud thread · Quadami docs website
- Repos/commits: quadami master `ae27301`
- Did: website for GitHub Pages (Kajetan: "a page like quadami.com, about releases, tests, other
  people's work with their acceptance; minimalistic, docs type"). Own builder `tools/site/build.py`
  (markdown-it-py, pinned with hashes) renders a whitelist (`tools/site/site.json`): home, releases
  (from the GitHub API, drafts never), install (USER-GUIDE §2), user guide, results (gallery),
  community (consent process + issue form), credits & license (README sections), building. Link
  check and rewrite (unlisted files go to GitHub), upstream-link and private-path guards, tests.
  CI job `site` (in **CI passed**), `pages.yml` deploys from master; a release event re-dispatches
  on master because the github-pages environment only takes master.
- Found: `upload-pages-artifact` v4+ drops dotfiles (no `.nojekyll` needed with Actions deploys).
  Inline code in the guide overflowed a 390 px phone screen; fixed with `overflow-wrap`.
- Measured: nothing about the remesher. Site: 8 pages, 10 images (~300 KB), every page fits 390 px
  without horizontal scroll (Chromium, light and dark).
- Next: Kajetan enables Pages (Settings > Pages > Source: GitHub Actions); domain decision.
