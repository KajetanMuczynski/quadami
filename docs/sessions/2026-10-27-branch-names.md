# 2026-10-27 · cloud thread "Rename branches to type-based names" · Type-based branch names
- Repos/commits: core master, branch `docs/branch-names`
- Did: replaced the `claude/project-thread-*` branch rule in CLAUDE.md with `<type>/<slug>`, one branch
  per PR; renamed the branch names in the `ci/tests` fixtures (test data only, nothing matches on them).
- Found: no workflow, script or ruleset relies on the old prefix. A cloud session can push a branch
  outside `claude/*` (tested with `docs/branch-push-test`).
- Measured: nothing measured.
- Next: none.
