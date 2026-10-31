# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""ab.py's lane verdict, the metrics it adds to runs made without them or whose measurement failed,
and what `compare` says when the two sides' metrics are missing, come from different code or inputs.

    python -m unittest discover -s tools/bench/tests -v
"""
import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import time
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


class LaneRunArgs(unittest.TestCase):
    def test_lane_runs_with_every_run_option(self):
        # lane built its run arguments by hand and missed --metrics-timeout when it was added (2026-10-28).
        seen = []
        with tempfile.TemporaryDirectory() as tmp:
            corpus = os.path.join(tmp, "corpus.json")
            with open(corpus, "w") as fp:
                json.dump({"sets": {"s": {"targets": [2000]}}}, fp)
            with mock.patch.object(ab, "CORPUS", corpus), \
                    mock.patch.object(ab, "source_key", side_effect=lambda ref, *a: ref), \
                    mock.patch.object(ab, "git", return_value="0"), \
                    mock.patch.object(ab, "load_set", return_value=({}, [])), \
                    mock.patch.object(ab, "cmd_run", side_effect=seen.append), \
                    mock.patch.object(ab, "load_runs", return_value={}), \
                    contextlib.redirect_stdout(io.StringIO()):
                ab.cmd_lane(ab.parser().parse_args(["lane", "base", "head", "--work", tmp]))
        run = vars(ab.parser().parse_args(["run", "x"]))
        self.assertEqual(len(seen), 2)
        for ra in seen:
            self.assertEqual(set(vars(ra)), set(run))
            self.assertEqual((ra.no_metrics, ra.replicates, ra.timeout, ra.work), (True, 1, 1200, tmp))


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

    def test_failed_measurement_is_retried_by_the_next_run(self):
        # metrics.py failing (out of memory under load) left "metrics": null, which every later run reused.
        with mock.patch.object(ab, "measure", side_effect=lambda rec, *a: rec.update(metrics=None)):
            ab.run_one("cli", self.mesh, "corpus", 2000, "quality", 0, self.out, [], self.work, True, 1)
        self.assertFalse(ab.has_metrics(self.d))
        rec, calls = self.run_one(metrics=True)
        self.assertEqual((calls, rec["metrics"]), (1, {"measured": True}))

    def test_timed_out_run_is_measured_again_only_with_a_longer_limit(self):
        # Measuring it again under the same limit would stall every later run for that long.
        with open(os.path.join(self.d, "record.json"), "w") as fp:
            json.dump({"mesh": "m", "rc": 0, "metrics": None, "metrics_error": "timeout", "metrics_timeout": 1800}, fp)
        for limit, expected in ((1800, 0), (600, 0), (3600, 1), (None, 1)):
            with mock.patch.object(ab, "measure") as m:
                ab.run_one("cli", self.mesh, "corpus", 2000, "quality", 0, self.out, [], self.work, True, 1,
                           metrics_timeout=limit)
            self.assertEqual(m.call_count, expected, limit)


class PeakMemory(unittest.TestCase):
    """peak_rss_mb comes from the CLI's report when it has one: wait4's figure is at least the
    harness's own peak, since Python starts the CLI with vfork and exec keeps the parent's
    high-water mark (a 60 MB run read 3.6 GB after a big case)."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.work = tmp.name
        self.out = os.path.join(self.work, "runs", "k")
        self.mesh = {"id": "m", "file": "m.obj"}

    def run_one(self, report):
        def fake_cli(cmd, log, timeout):
            if report is not None:
                with open(cmd[cmd.index("--report-json") + 1], "w") as fp:
                    json.dump(report, fp)
            return 0, 3652.0
        with mock.patch.object(ab, "call_measured", side_effect=fake_cli), \
                mock.patch.object(ab, "perturbed", side_effect=lambda src, k, work: src), \
                mock.patch.object(ab, "file_hash", return_value="0"):
            return ab.run_one("cli", self.mesh, "corpus", 2000, "quality", 0, self.out, [], self.work, False, 1)

    def test_the_cli_figure_wins(self):
        rec = self.run_one({"output": {}, "diagnostics": {"peak_rss_bytes": 62 << 20}})
        self.assertEqual((rec["peak_rss_mb"], rec["peak_rss_from"]), (62.0, "cli"))

    def test_wait4_without_a_cli_figure(self):
        rec = self.run_one(None)
        self.assertEqual((rec["peak_rss_mb"], rec["peak_rss_from"]), (3652.0, "wait4"))


class Measure(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.d = tmp.name

    def measure(self, results):
        """measure() with metrics.py replaced: each call writes the next of `results` (None = fails)."""
        it = iter(results)

        def fake(cmd, **kw):
            res = next(it)
            if res is not None:
                with open(cmd[cmd.index("--json") + 1], "w") as fp:
                    json.dump(res, fp)
            return subprocess.CompletedProcess(cmd, 0 if res is not None else 1, b"", b"" if res else b"MemoryError")
        rec = {}
        with mock.patch.object(ab.subprocess, "run", side_effect=fake) as m:
            ab.measure(rec, self.d, {"id": "m"}, 2000, "in.obj", self.d)
        return rec, m.call_count

    def test_one_retry_then_recorded_with_the_tool(self):
        rec, calls = self.measure([None, {"ok": True}])
        self.assertEqual((calls, rec["metrics"]), (2, {"ok": True}))
        self.assertNotIn("metrics_error", rec)
        self.assertEqual(rec["metrics_tool"]["metrics_py"], ab.file_hash(ab.METRICS)[:12])
        self.assertIn("numpy", rec["metrics_tool"])

    def test_two_failures_leave_none_and_the_error(self):
        rec, calls = self.measure([None, None])
        self.assertEqual((calls, rec["metrics"]), (2, None))
        self.assertIn("MemoryError", rec["metrics_error"])

    def test_timeout_is_recorded_and_not_retried(self):
        def hang(cmd, **kw):
            raise subprocess.TimeoutExpired(cmd, kw["timeout"])
        rec = {"metrics": {"old": True}}
        with mock.patch.object(ab.subprocess, "run", side_effect=hang) as m:
            ab.measure(rec, self.d, {"id": "m"}, 2000, "in.obj", self.d, timeout=5)
        self.assertEqual((m.call_count, rec["metrics"], rec["metrics_timeout"]), (1, None, 5))
        self.assertIn("timeout", rec["metrics_error"])

    def test_a_real_metrics_py_is_killed_at_the_limit(self):
        # subprocess.run kills the child on timeout; a sleeping stand-in for metrics.py must not outlive it.
        with mock.patch.object(ab, "METRICS", os.path.join(self.d, "slow.py")):
            with open(ab.METRICS, "w") as fp:
                fp.write("import time\ntime.sleep(30)\n")
            t0 = time.time()
            rec = {}
            ab.measure(rec, self.d, {"id": "m"}, 2000, "in.obj", self.d, timeout=1)
        self.assertLess(time.time() - t0, 10)
        self.assertEqual((rec["metrics"], rec["metrics_timeout"]), (None, 1))


class Compare(unittest.TestCase):
    def side(self, root, name, recs):
        for u, rec in enumerate(recs):
            d = os.path.join(root, name, "corpus", "m", "quality-t2000", "u%d" % u)
            os.makedirs(d)
            with open(os.path.join(d, "record.json"), "w") as fp:
                json.dump(dict({"mesh": "m", "res": "quality", "target": 2000, "replicate": u, "rc": 0}, **rec), fp)
        return os.path.join(root, name)

    def compare(self, a, b):
        with tempfile.TemporaryDirectory() as root:
            args = mock.Mock(a=self.side(root, "A", a), b=self.side(root, "B", b), set="corpus", work=root,
                             per_run=False)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                ab.cmd_compare(args)
        return out.getvalue()

    @staticmethod
    def rec(hausdorff, tool="t1"):
        return {"metrics": {"distance": {"hausdorff": hausdorff}}, "metrics_tool": {"metrics_py": tool}}

    def test_failed_metrics_leave_the_replicate_out_on_both_sides(self):
        # A's u1 lost its metrics: averaging A over u0 only against B over u0 and u1 made a difference.
        out = self.compare([self.rec(1.0), {"metrics": None}], [self.rec(1.0), self.rec(5.0)])
        self.assertIn("**1 replicate-runs completed but metrics.py failed on them**", out)
        self.assertIn("| hausdorff | 1 | 1 | ×1.000 | 0 / 0 |", out)

    def test_different_metrics_code_is_named(self):
        self.assertIn("measured by different metrics code", self.compare([self.rec(1.0)], [self.rec(1.0, "t2")]))
        self.assertNotIn("measured by different metrics code", self.compare([self.rec(1.0)], [self.rec(1.0)]))
        # Records from before the tool was recorded (2026-10-27) may come from any older metrics.py: a side
        # measured that way is flagged against a recorded one, not silently mixed.
        old = {"metrics": {"distance": {"hausdorff": 1}}}
        self.assertIn("different metrics code", self.compare([old], [self.rec(1.0)]))
        self.assertNotIn("different metrics code", self.compare([old], [old]))

    def runs(self, root, name, recs):
        """Several meshes per side: recs = [(mesh, replicate, record fields)]."""
        for mesh, u, rec in recs:
            d = os.path.join(root, name, "corpus", mesh, "quality-t2000", "u%d" % u)
            os.makedirs(d)
            with open(os.path.join(d, "record.json"), "w") as fp:
                json.dump(dict({"mesh": mesh, "res": "quality", "target": 2000, "replicate": u, "rc": 0}, **rec), fp)
        return os.path.join(root, name)

    def compare_runs(self, a, b):
        with tempfile.TemporaryDirectory() as root:
            args = mock.Mock(a=self.runs(root, "A", a), b=self.runs(root, "B", b), set="corpus", work=root,
                             per_run=False)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                ab.cmd_compare(args)
        return out.getvalue()

    @staticmethod
    def topo(same, **kw):
        return dict({"metrics": {"topology": {"same_as_input": same}, "validity": {"islands_same_as_input": same}},
                     "metrics_tool": {"metrics_py": "t1"}}, **kw)

    def test_one_unmeasured_run_leaves_only_itself_out_of_the_topology_verdict(self):
        # One timed-out replicate used to replace the whole verdict with "not measured in every run".
        a = [("fixed", u, self.topo(False)) for u in (0, 1)] + [("timeout", u, self.topo(True)) for u in (0, 1)]
        b = [("fixed", u, self.topo(True)) for u in (0, 1)] + [("timeout", 0, self.topo(True)),
                                                               ("timeout", 1, {"metrics": None})]
        out = self.compare_runs(a, b)
        self.assertIn("1 runs not measured in every replicate on both sides, left out: timeout quality 2000", out)
        self.assertIn("- fixed: fixed quality 2000", out)
        self.assertIn("per island: A 0.0, B 1.0 of 1 runs", out)

    def test_runs_of_another_input_version_are_left_out(self):
        # A side made before a mesh was regenerated looked like a fix.
        out = self.compare_runs([("m", 0, self.topo(False, input_sha="old"))], [("m", 0, self.topo(True, input_sha="new"))])
        self.assertIn("**Different inputs**", out)
        self.assertNotIn("- fixed: m", out)

    def test_output_without_faces_is_a_failed_run(self):
        empty = {"metrics": {"ok": False}, "output": {"quads": 0}}
        out = self.compare_runs([("m", 0, self.topo(True))], [("m", 0, empty)])
        self.assertIn("**Failed runs** (non-zero exit or no output faces in some replicate; left out below): B m", out)

    def test_negative_row_shows_the_difference(self):
        # sj_p1 < 0 (folded quads) dropped out of the geo-mean: the mean rose 0.1 -> 0.25, the ratio said x0.5.
        sj = lambda a, b: ({"metrics": {"quads": {"sj_p1": a}}}, {"metrics": {"quads": {"sj_p1": b}}})  # noqa: E731
        (a1, b1), (a2, b2) = sj(0.4, 0.2), sj(-0.2, 0.3)
        out = self.compare_runs([("m1", 0, a1), ("m2", 0, a2)], [("m1", 0, b1), ("m2", 0, b2)])
        self.assertIn("| sj_p1 | 0.1 | 0.25 | Δ +0.15 | 1 / 1 |", out)


class CliArgs(unittest.TestCase):
    def test_flag_value_in_either_form(self):
        self.assertEqual(ab.cli_value(["--sharp-edge", "60"], "--sharp-edge"), "60")
        self.assertEqual(ab.cli_value(["--adaptivity=0", "--sharp-edge=60"], "--sharp-edge"), "60")
        self.assertIsNone(ab.cli_value(["--sharp-edge-x=1", "--sharp-edge"], "--sharp-edge"))

    def test_metrics_measure_creases_at_the_runs_angle(self):
        # `--cli-arg=--sharp-edge=60` (the README's form) remeshed at 60 degrees but was measured at 90.
        with tempfile.TemporaryDirectory() as d, mock.patch.object(ab.subprocess, "run",
                                                                  side_effect=OSError("stop")) as m:
            with self.assertRaises(OSError):
                ab.measure({"cli_args": ["--sharp-edge=60"]}, d, {"id": "m"}, 2000, "in.obj", d)
        cmd = m.call_args[0][0]
        self.assertEqual(cmd[cmd.index("--sharp-degrees") + 1], "60")

    def test_count_error_of_an_empty_output(self):
        self.assertEqual(ab.value({"target": 1000, "output": {"quads": 0}}, "count_err"), 1.0)


class RemeasureGuard(unittest.TestCase):
    def test_output_is_not_measured_against_a_regenerated_input(self):
        with tempfile.TemporaryDirectory() as work:
            os.makedirs(os.path.join(work, "meshes"))
            with open(os.path.join(work, "meshes", "m.obj"), "w") as fp:
                fp.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
            out = os.path.join(work, "runs", "k")
            d = os.path.join(out, "corpus", "m", "quality-t2000", "u0")
            os.makedirs(d)
            open(os.path.join(d, "out.obj"), "w").close()
            for sha, expected in (("0123456789abcdef", 0), (ab.file_hash(os.path.join(work, "meshes", "m.obj")), 1)):
                with open(os.path.join(d, "record.json"), "w") as fp:
                    json.dump({"mesh": "m", "rc": 0, "metrics": {"old": True}, "input_sha": sha}, fp)
                with mock.patch.object(ab, "measure") as m, contextlib.redirect_stderr(io.StringIO()):
                    ab.run_one("cli", {"id": "m", "file": "m.obj"}, "corpus", 2000, "quality", 0, out, [], work,
                               True, 1, remeasure=True)
                self.assertEqual(m.call_count, expected, sha)


if __name__ == "__main__":
    unittest.main()
