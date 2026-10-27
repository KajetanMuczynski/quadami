# 2026-10-26 · Claude (cloud thread "Audit second pass") · CI, tests and the thin-part advice
- Repos/commits: quadami master `ee43cf3`, then `a0fd599`; Release CLI builds of master, of master with
  one feature switched off at a time (no LTO), and of master with two extra thin-part numbers in the
  stage records. Report, scripts and outputs: research/audit-second-pass-2026-10-26.md and its folder.
- Did:
  - Read the CI workflows and scripts and the job history of the last 240 ci runs.
  - Switched off one merged feature or report counter at a time and ran the core tests, the output
    gate and the worker tests on each build.
  - Measured the thin-part advice on the joined test sword from 3,000 to 75,000 quads, with the core
    and with a separate script that measures thickness once on the input.
  - This PR: the thin-part warning, the user guide and the 0.1.0 notes now call the suggested count
    an estimate that can take several raises, with thickening as the usual fix.
- Found:
  - The suggested count chases the target: thinness is measured on T, which gets finer as the target
    grows, so each count shows thinner parts. On the sword the advice is 1.6-2.1× the count asked
    for at every count: 6,300 at 3k, 59,000 at 35k, 140,000 at 75k, so from 3k it takes about six
    raises. The old text, "Try at least N target quads", promised one. The fix (TODO 2.21 (e)) is
    for after the tag.
  - Several merged features can be switched off without any CI suite failing, the surface
    projection and the relax among them. The list, and which of them change the output, is in the
    report.
  - Kajetan's 2 holes on the head at 35k are reproducible: runs with holes 0/6 at 10k, 2/6 at 20k,
    6/6 at 35k and 50k; with Repair Folds 0 of 24.
  - CI installs pip packages without pinned versions or hashes, and the qt-app job's apt step can run
    into the 45-minute job timeout on a slow mirror. Fixes are waiting for Kajetan's go-ahead.
- Measured: sword thin share 19 % at 3k, 3 % at 35k, 1.3 % at 75k; following the advice from 59k
  and 75k (110k, 140k) cleared it (one run each, unconfirmed).
  `ab.py lane a0fd599 5d41be1`: light, 530 public runs byte-identical (u0 + one float32 replicate).
- Next: the rest of the audit in the report; the stable advice rule (TODO 2.21 (e)) after v0.1.0.
