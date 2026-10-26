# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""ab.py's lane verdict and the metrics it adds to runs made without them.

    python -m unittest discover -s tools/bench/tests -v
"""
import json
import os
import sys
import tempfile
import unittest
from unittest import mock

BENCH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BENCH)
import ab  # noqa: E402


def runs(**shas):
    """load_runs()-shaped dict: mesh -> plain run with that out_sha (None = no output, rc 1)."""
    return {(m, "quality", 2000): {0: {"rc": 0 if sha else 1, "out_sha": sha}} for m, sha in shas.items()}


class OutputDiff(unittest.TestCase):
    def test_identical(self):
        self.assertEqual(ab.output_diff(runs(a="1", b="2"), runs(a="1", b="2")), (2, [], []))

    def test_differ(self):
        same, differ, failed = ab.output_diff(runs(a="1", b="2"), runs(a="1", b="3"))
        self.assertEqual((same, differ, failed), (1, [(("b", "quality", 2000), 0)], []))

    def test_failure_or_missing_run_is_never_identical(self):
        # Both sides failing gives no output to compare, so it must not count as byte-identical.
        _, _, failed = ab.output_diff(runs(a=None), runs(a=None))
        self.assertEqual(len(failed), 1)
        _, _, failed = ab.output_diff(runs(a="1", b="2"), runs(a="1"))
        self.assertEqual([k for k, *_ in failed], [("b", "quality", 2000)])

    def test_a_replicate_that_moved_is_a_difference(self):
        # u0 identical is not enough: the #67 replay moved 28 runs only in float32 replicates.
        b, h = runs(a="1"), runs(a="1")
        b[("a", "quality", 2000)][1] = {"rc": 0, "out_sha": "x"}
        h[("a", "quality", 2000)][1] = {"rc": 0, "out_sha": "y"}
        self.assertEqual(ab.output_diff(b, h, (0, 1)), (1, [(("a", "quality", 2000), 1)], []))
        _, _, failed = ab.output_diff(b, runs(a="1"), (0, 1))
        self.assertEqual([u for _, u, *_ in failed], [1])


class MetricsAddedLater(unittest.TestCase):
    """A run made with --no-metrics (as `lane` does) gets its metrics once a run asks for them,
    instead of being reused without them."""

    def setUp(self):
        self.work = tempfile.mkdtemp()
        self.out = os.path.join(self.work, "runs", "k")
        self.d = os.path.join(self.out, "corpus", "m", "quality-t2000", "u0")
        os.makedirs(self.d)
        open(os.path.join(self.d, "out.obj"), "w").close()
        with open(os.path.join(self.d, "record.json"), "w") as fp:
            json.dump({"mesh": "m", "rc": 0}, fp)
        self.mesh = {"id": "m", "file": "m.obj"}

    def run_one(self, metrics):
        def fake_measure(rec, *args):
            rec["metrics"] = {"measured": True}
        with mock.patch.object(ab, "measure", side_effect=fake_measure) as m:
            rec = ab.run_one("cli", self.mesh, "corpus", 2000, "quality", 0, self.out, [], self.work, metrics, 1)
        return rec, m.call_count

    def test_no_metrics_run_reuses_record(self):
        rec, calls = self.run_one(metrics=False)
        self.assertEqual(calls, 0)
        self.assertNotIn("metrics", rec)

    def test_metrics_run_adds_them_once(self):
        rec, calls = self.run_one(metrics=True)
        self.assertEqual((calls, rec["metrics"]), (1, {"measured": True}))
        self.assertTrue(ab.has_metrics(self.d))
        _, calls = self.run_one(metrics=True)
        self.assertEqual(calls, 0)


if __name__ == "__main__":
    unittest.main()
