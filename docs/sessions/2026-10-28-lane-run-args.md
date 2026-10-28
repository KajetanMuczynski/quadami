# 2026-10-28 · cloud thread · ab.py lane broken by --metrics-timeout
- Repos/commits: master 7fe3c75; follows 2026-10-28-metrics-stall.md (PR #142)
- Did: `ab.py lane` built its `run` arguments as a hand-written Namespace, which #142's new
  `--metrics-timeout` was missing, so every `lane` on master since #142 stopped with an AttributeError
  before its first run. lane now parses a `run` command line with the same parser (`parser()`), so every
  run option keeps its default; a test compares lane's arguments with a parsed `run`.
- Found: the bench tests did not cover lane's call into `cmd_run` [M: the new test fails on master].
- Measured: nothing measured (no output or metric changes).
- Next: none.
