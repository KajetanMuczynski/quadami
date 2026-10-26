# 2026-10-26 · cloud thread · Security policy and issue forms
- Repos/commits: quadami master aaa9b4e
- Did: SECURITY.md (private reporting through GitHub's "Report a vulnerability"), issue forms for
  bug reports and feature requests next to the existing community-result form, `config.yml` (blank
  issues off, links to the private report and the user guide), a Contact section in README and
  links from the website's home, guide §9 and community pages. GitHub Issues is the only public
  contact channel in the files.
- Found: issue-form changes under `.github/` are not "documentation" for ci.yml's `changes` job, so
  such a PR still runs the macOS job.
- Measured: nothing measured (site build and its tests pass offline).
- Next: Kajetan enables Private vulnerability reporting in the repository settings; contact data he
  provides later goes in once he sends it.
