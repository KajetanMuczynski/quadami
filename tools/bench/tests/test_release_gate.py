# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""release_gate: per-run checks (crash, lost run, structural gates without a baseline or with --hard) and the
per-case sign test against a baseline, pooled over pairs, with Holm.

    python -m unittest discover -s tools/bench/tests -v      (needs numpy)
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

BENCH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BENCH)
import gates  # noqa: E402
import release_gate  # noqa: E402


def run(quads=2000, target=2000, **over):
    """A record as ab.py writes it for an all-quad run of spot at `target`, clean unless `over` says otherwise."""
    m = {"ok": True, "faces": {"total": quads, "quads": quads, "other": 0},
         "topology": {"non_manifold_edges": 0, "inconsistently_oriented_edges": 0, "same_as_input": True},
         "validity": {"islands_dropped": 0, "islands_split": 0, "islands_merged": 0, "holes_added": 0,
                      "holes_closed": 0, "folded_faces": 0, "non_manifold_vertices": 0, "handles_lost": 0,
                      "handles_added": 0, "islands_same_as_input": True},
         "quads": {"sj_le_0": 0}, "flipped": {"faces_vs_input": 0}, "degenerate": {"faces": 0},
         "valence": {"interior_hist": {"4": quads}}}
    for key, value in over.items():
        group, field = key.split(".")
        m[group][field] = value
    return {"mesh": "spot", "res": "fast", "target": target, "rc": 0, "metrics": m}


def write(folder, runs):
    """`runs`: the replicates of spot fast 2000, or {target: replicates} for several cases."""
    cases = runs if isinstance(runs, dict) else {2000: runs}
    for target, reps in cases.items():
        for u, rec in enumerate(reps):
            d = os.path.join(folder, "corpus", "spot", "fast-t%d" % target, "u%d" % u)
            os.makedirs(d)
            with open(os.path.join(d, "record.json"), "w") as f:
                json.dump(dict(rec, replicate=u, target=target), f)


def folds(target, n):
    """A run of case `target` with n folded quads (no out.obj: the records release-check.yml uploads)."""
    return run(quads=target, target=target, **{"quads.sj_le_0": n})


def cases(per_case, n=9, reps=2):
    """n cases (targets 1000, 1100, ...) whose replicates have per_case(i) folded quads each."""
    return {1000 + 100 * i: [folds(1000 + 100 * i, per_case(i)) for _ in range(reps)] for i in range(n)}


class ReleaseGate(unittest.TestCase):
    def gate(self, cand, base=None, *args, pairs=()):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = os.path.join(tmp, "cand"), os.path.join(tmp, "base")
            write(a, cand)
            cmd = [sys.executable, os.path.join(BENCH, "release_gate.py"), a, "--set", "corpus"] + list(args)
            if base is not None:
                write(b, base)
                cmd += ["--baseline", b]
            for i, (pc, pb) in enumerate(pairs):
                ca, cb = os.path.join(tmp, "c%d" % i), os.path.join(tmp, "b%d" % i)
                write(ca, pc)
                write(cb, pb)
                cmd += ["--pair", ca, cb]
            p = subprocess.run(cmd, capture_output=True, text=True)
            return p.returncode, p.stdout + p.stderr

    def test_sign_test_and_holm(self):
        self.assertEqual(release_gate.sign_p(0, 0), 1.0)
        self.assertAlmostEqual(release_gate.sign_p(10, 0), 1 / 1024)
        self.assertAlmostEqual(release_gate.sign_p(14, 3), 0.00636, places=5)  # #82, corpus folds pooled
        self.assertAlmostEqual(release_gate.sign_p(0, 5), 1.0)
        # Holm: 0.001 <= 0.05/3, 0.02 <= 0.05/2, then 0.06 > 0.05 stops.
        self.assertEqual(release_gate.holm([0.06, 0.001, 0.02], 0.05), [False, True, True])
        self.assertEqual(release_gate.holm([0.03, 0.001, 0.02], 0.05), [True, True, True])
        self.assertEqual(release_gate.holm([0.02, 0.02, 0.02], 0.05), [False, False, False])

    def test_clean_passes_alone_and_against_itself(self):
        self.assertEqual(self.gate([run(), run()])[0], 0)
        rc, out = self.gate([run(), run()], [run(), run()])
        self.assertEqual(rc, 0, out)
        self.assertIn("| corpus | folded quads | 0 / 0, p = 1 | ok |", out)

    def test_each_structural_gate_fails_without_baseline(self):
        for over in ({"topology.non_manifold_edges": 1}, {"topology.inconsistently_oriented_edges": 3},
                     {"validity.islands_dropped": 1}, {"validity.holes_added": 1}, {"faces.other": 1}):
            self.assertEqual(self.gate([run(**over)])[0], 1, over)
        self.assertEqual(self.gate([run(quads=2300)])[0], 1)
        self.assertEqual(self.gate([run(quads=2200)])[0], 0)  # exactly +10 % still passes
        self.assertEqual(self.gate([dict(run(), rc=1)])[0], 1)

    def test_structural_failure_in_one_case_is_not_a_verdict_unless_hard(self):
        rc, out = self.gate([run(quads=1000), run(quads=1000)], [run(), run()])
        self.assertEqual(rc, 0, out)
        self.assertIn("| corpus | H6 count within ±10 % | 1 / 0, p = 0.5 | ok |", out)
        self.assertIn("| spot | fast | 2000 | u0 | H6 |", out)  # still listed
        self.assertEqual(self.gate([run(quads=1000)], [run(quads=1000)], "--hard")[0], 1)

    def test_structural_gate_worse_in_many_cases_fails(self):
        bad = {t: [run(quads=t, target=t, **{"validity.holes_added": 1})] for t in range(1000, 1900, 100)}
        good = {t: [run(quads=t, target=t)] for t in bad}
        rc, out = self.gate(bad, good)
        self.assertEqual(rc, 1, out)
        self.assertIn("| corpus | H5 no hole added or closed | 9 / 0, p = 0.00195 | **fail** |", out)

    def test_crash_fails_with_baseline_unless_the_baseline_crashed_there(self):
        rc, out = self.gate([dict(run(), rc=-11), run()], [run(), run()])
        self.assertEqual(rc, 1, out)
        self.assertIn("**1 candidate runs crashed or timed out**", out)
        rc, out = self.gate([dict(run(), rc=-11), run()], [run(), dict(run(), rc=1)])
        self.assertEqual(rc, 0, out)
        self.assertIn("crashed in cases where the baseline crashed too", out)
        self.assertEqual(self.gate([dict(run(), rc=-11), run()], [run(), dict(run(), rc=1)], "--hard")[0], 1)

    def test_one_more_fold_is_noise_many_cases_fail(self):
        rc, out = self.gate([folds(2000, 1), folds(2000, 0)], [folds(2000, 0), folds(2000, 0)])
        self.assertEqual(rc, 0, out)
        self.assertIn("| corpus | folded quads | 1 / 0, p = 0.5 | ok |", out)
        # 6 of 6 cases worse: p = 1/64 is a lead (Holm over 12 tests needs < 0.0042), not a failure.
        rc, out = self.gate(cases(lambda i: 1, n=6), cases(lambda i: 0, n=6))
        self.assertEqual(rc, 0, out)
        self.assertIn("| corpus | folded quads | 6 / 0, p = 0.0156 | lead |", out)
        self.assertIn("**Leads: re-check", out)
        # 9 of 9: p = 1/512 fails.
        rc, out = self.gate(cases(lambda i: 1 + i), cases(lambda i: 0))
        self.assertEqual(rc, 1, out)
        self.assertIn("| corpus | folded quads | 9 / 0, p = 0.00195 | **fail** |", out)
        self.assertIn("spot fast 1800 (+18)", out)  # worst case first, summed over 2 replicates
        self.assertEqual(self.gate(cases(lambda i: 0), cases(lambda i: 1))[0], 0)  # fewer is fine
        # --decide runs reports the test but does not fail on it.
        rc, out = self.gate(cases(lambda i: 1), cases(lambda i: 0), "--decide", "runs")
        self.assertEqual(rc, 0, out)
        self.assertIn("**fail**", out)

    def test_pairs_pool_per_case(self):
        # Pair 1 alone: 9 cases worse by 1. Pair 2 (another platform) better by 2 on the same cases.
        worse, better = (cases(lambda i: 1), cases(lambda i: 0)), (cases(lambda i: 0), cases(lambda i: 2))
        rc, out = self.gate(*worse, pairs=[better])
        self.assertEqual(rc, 0, out)
        self.assertIn("| corpus | folded quads | 0 / 9, p = 1 | 9 / 0, 0.00195 | 0 / 9, 1 | ok |", out)
        rc, out = self.gate(*worse, pairs=[worse])
        self.assertEqual(rc, 1, out)

    def test_baseline_run_missing_from_the_candidate_fails(self):
        # Without this the lost replicate dropped out of every row and the check passed.
        rc, out = self.gate([run()], [run(), run()])
        self.assertEqual(rc, 1, out)
        self.assertIn("**1 baseline runs have no candidate run**", out)
        self.assertEqual(self.gate([run()], [run(), run()], "--hard")[0], 1)
        self.assertEqual(self.gate([run(), run()], [run()])[0], 0)  # a new candidate run is fine

    def test_run_without_metrics_is_not_scored_as_a_crash(self):
        # metrics.py failing (out of memory under load) once made identical outputs fail H1.
        for cand, base in (([dict(run(), metrics=None)], None), ([run()], [dict(run(), metrics=None)])):
            rc, out = self.gate(cand, base)
            self.assertEqual(rc, 2, out)
            self.assertIn("completed without metrics", out)

    def test_records_without_out_obj_score_non_quads(self):
        # release-check.yml uploads records only; the retired cluster rule needs out.obj and is left out.
        g, d = gates.gates(run(quads=1990, **{"faces.total": 2000}), "/nonexistent", "organic")
        self.assertEqual(d["non-quads"], 10)
        self.assertNotIn(gates.RETIRED_V1, g)

if __name__ == "__main__":
    unittest.main()
