# 2026-10-26 · Claude (cloud thread "Audit merged 0.1 features") · Audit of the code merged for 0.1
- Repos/commits: quadami master `ee43cf3` (read-only); Release and ASan+UBSan builds of the CLI. Report,
  scripts and outputs: research/audit-merged-features-2026-10-26.md and its folder.
- Did: read the core, CLI, add-on, bench (ab.py, gates.py, metrics.py, release_gate.py), CI scripts and
  workflows of the 55 merged non-docs PRs. Ran the core tests, the output gate and 140 corpus, closed,
  sculpt, brush and external runs under ASan+UBSan. Tested release_gate.py on fake run folders and
  metrics.py's fold detector against its own geodesic test. Checked the 0.1.0 notes against code and
  research data. A separate worker tried to disprove the headline findings.
- Found:
  - Two claims in the 0.1.0 notes, README, user guide and website said more than the data shows; this
    PR rewords them:
    - folded input "causes nearly all folds, non-quads and holes": on the 0.1 pipeline about three
      quarters of the defects on the two test sculpts sit near folded input, and about half of the
      folded quads;
    - the thin-part warning "a third come out clean": the source counted a run as clean below a defect
      score of 10.
  - For after the tag (TODO, parked table):
    - release_gate.py passes a candidate that crashes on 5 of 6 replicates of a case when the
      baseline is unclean there on every validity gate;
    - metrics.py's fold prefilter misses 23-28 % of folded input triangles;
    - on Windows the libQEx step limit is cast to a 32-bit `unsigned long` and wraps above about 238k
      quads per island;
    - the add-on stays busy if reading the result raises.
  - No sanitizer report.
- Measured: brush_box_relief 70 vs 91 folded triangles (prefilter vs geodesic test), brush_twist 434 vs
  ≥ 601; thin-warning prototype CSVs: 9 of 77 firings without any thin or tip defect.
- Next: the parked fixes in their own PRs after v0.1.0.
