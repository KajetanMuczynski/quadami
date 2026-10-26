# 2026-10-26 · Claude (cloud thread "Compare outputs across Windows, macOS, Linux") · Why the platforms give different remeshes
- Repos/commits: quadami master `7054a3c` for the study, `4018750` for this log; release-check #5
  (candidate de6ac3a, baseline 0e9bec8); research workflow runs 40348110535 and 40354910738 from
  branch commits 483e672 and ed35e28 (the research tooling, removed again before the PR). Note and scripts:
  research/platform-divergence-2026-10-26.md and its folder.
- Did: compared run #5 per set, mesh and replicate across the three platforms (bytes, stages,
  metrics, gates, time); a 22-point hash patch to find where two builds part; interventions that
  switch one channel off at a time (FMA contraction, CPU FMA paths, libm via a long-double shim and a
  double-double same-bits library, scalar Eigen, libc++ for libstdc++, a tie-broken sort, threads,
  machines, glibc 2.31-2.44); a corpus × 6 run with libc++; a tie test on the three meshes where the
  libraries disagree; a bootstrap and a replay of `release_gate.py` for the gate's noise floor; an
  independent disprove pass over the headline claims (three corrected).
- Found:
  - 0 of 1,800 run keys give the same output on two platforms; each platform is deterministic per
    build, also across Intel and AMD runners.
  - Four channels, in pipeline order: the OBJ reader on arm64 (FMA contraction), the math library
    (glibc, UCRT, Apple; CPU FMA dispatch on glibc and UCRT; three glibc classes), Eigen's vector code
    on arm64, and the C++ library's ordering (simplifier sort ties at `singularitysimplifier.cpp:532`,
    unordered containers in extraction, TODO 2.9). With all four off, the three platforms agree up to
    quad extraction.
  - The libm, FMA and parse differences act like another 1-ulp replicate; the simplifier's sort ties
    are the systematic per-case part. Tie-broken, libstdc++ and libc++ agree within noise.
  - The release gate's aggregate ratchet (zero tolerance on 5 defects and 6 validity counts) fails a
    change that only moves last bits: the same build on two platforms fails 24 of 24 replays, and the
    structural ratchet 9 of 24. A per-case sign test pooled over platforms finds #82 (corpus valence
    10/0 cases, folds 14/3) and false-alarms on 2 of 120 neutral comparisons. Replicates of a case are
    not independent: per-run p-values overstate.
  - The island health check accepts collapsed islands (no count check), related to 2.22 (d).
- Measured: run #5 numbers, CI rounds 2 and 3, local interventions; all in the note with labels.
- Next: Kajetan decides whether to replace the aggregate ratchet with the per-case test, whether
  the tie fix goes in after 0.1 (full A/B; then a tie-fixed release check to see the runners agree
  within noise), and whether byte-identical platforms are a 1.0 goal.
