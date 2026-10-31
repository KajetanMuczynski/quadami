# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Tests of the add-on's worker side, without Blender.

Needs numpy and a built CLI (CMakeLists.txt; the Qt app's headless mode works too):

    AUTOREMESHER_EXECUTABLE=build/cli/autoremesher-cli python3 -m unittest discover -s addon/tests -v
"""

import json
import math
import os
import re
import shutil
import stat
import sys
import tempfile
import time
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "quadami"))
import names  # noqa: E402
import raise_hint  # noqa: E402
import report  # noqa: E402
import worker  # noqa: E402

EXECUTABLE = worker.find_executable()


def uv_sphere(rings=64, segments=128):
    """Closed UV sphere: (V, 3) positions and (F, 3) triangles."""
    positions = [(0.0, 0.0, 1.0)]
    for i in range(1, rings):
        theta = math.pi * i / rings
        for j in range(segments):
            phi = 2.0 * math.pi * j / segments
            positions.append((math.sin(theta) * math.cos(phi), math.sin(theta) * math.sin(phi), math.cos(theta)))
    positions.append((0.0, 0.0, -1.0))
    south = len(positions) - 1

    def ring(i, j):
        return 1 + (i - 1) * segments + j % segments

    triangles = []
    for j in range(segments):
        triangles.append((0, ring(1, j), ring(1, j + 1)))
        triangles.append((south, ring(rings - 1, j + 1), ring(rings - 1, j)))
    for i in range(1, rings - 1):
        for j in range(segments):
            a, b, c, d = ring(i, j), ring(i, j + 1), ring(i + 1, j), ring(i + 1, j + 1)
            triangles.append((a, c, b))
            triangles.append((b, c, d))
    return np.array(positions), np.array(triangles)


class ObjTest(unittest.TestCase):
    def test_round_trip(self):
        positions, triangles = uv_sphere(4, 8)
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "mesh.obj")
            worker.write_obj(path, positions.astype(np.float32), triangles)
            read_positions, sizes, indices = worker.read_obj(path)
        # 9 digits bring every float32 coordinate back exactly.
        np.testing.assert_array_equal(read_positions.astype(np.float32), positions.astype(np.float32))
        self.assertTrue((sizes == 3).all())
        np.testing.assert_array_equal(indices.reshape(-1, 3), triangles)

    def test_marked_edges_are_lines(self):
        positions, triangles = uv_sphere(4, 8)
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "mesh.obj")
            worker.write_obj(path, positions, triangles, np.array([[1, 2], [2, 3]]))
            with open(path) as f:
                lines = [line.strip() for line in f if line.startswith("l ")]
            # read_obj reads faces only, as the CLI's output has no lines.
            _, sizes, _ = worker.read_obj(path)
        self.assertEqual(lines, ["l 2 3", "l 3 4"])
        self.assertEqual(len(sizes), len(triangles))


class NamesTest(unittest.TestCase):
    @unittest.skipIf(sys.version_info < (3, 11), "needs tomllib")
    def test_manifest_matches_names(self):
        import tomllib
        path = os.path.join(os.path.dirname(__file__), "..", "quadami", "blender_manifest.toml")
        with open(path, "rb") as f:
            manifest = tomllib.load(f)
        self.assertEqual(manifest["id"], names.ID)
        self.assertEqual(manifest["name"], names.NAME)
        self.assertEqual(manifest["license"], ["SPDX:GPL-3.0-or-later"])


class TargetLimitTest(unittest.TestCase):
    def test_limits_match_the_core(self):
        path = os.path.join(os.path.dirname(__file__), "..", "..", "src", "AutoRemesher", "remesh.h")
        with open(path, encoding="utf-8") as f:
            header = f.read()
        maximum = re.search(r"MaximumTargetQuadCount = (\d+);", header)
        per_quad = re.search(r"ApproximatePeakBytesPerQuad = ([0-9.e]+);", header)
        self.assertEqual(int(maximum.group(1)), worker.MAXIMUM_TARGET_QUADS)
        self.assertEqual(float(per_quad.group(1)), worker.PEAK_BYTES_PER_QUAD)
        self.assertLess(worker.LARGE_TARGET_QUADS, worker.MAXIMUM_TARGET_QUADS)


class ReportTest(unittest.TestCase):
    def test_rows(self):
        rows = report.summary_rows({
            "status": "ok", "parameters": {"target_quads": 2000},
            "output": {"quads": 1624, "non_quads": 2, "non_manifold_edges": 0,
                       "orientation_flips": 1, "folded_faces": 0},
            "islands": {"input": 3, "without_output": 1},
            "time": {"total_seconds": 0.795}, "warnings": ["1 of 3 islands produced no faces"]})
        self.assertIn(("Quads", "1624 of 2000 target (81%)", False), rows)
        self.assertIn(("Non-quads", "2", True), rows)
        self.assertIn(("Non-manifold edges", "0", False), rows)
        self.assertIn(("Winding flips", "1", True), rows)
        self.assertIn(("Empty islands", "1 of 3", True), rows)
        self.assertIn(("Time", "0.8 s", False), rows)
        self.assertFalse(any(label in ("Input cleanup", "Holes") for label, _, _ in rows))

    def test_missing_parts(self):
        # Kajetan's joined sword at 3k: the vines came out missing (2026-10-22).
        base = {"status": "ok", "parameters": {"target_quads": 3000}, "output": {"quads": 1979},
                "islands": {"input": 10, "without_output": 0}}
        lost = dict(base, lost_parts={"farthest_input_quad_widths": 7.6, "far_input_vertices": 812})
        self.assertIn(("Missing parts", "up to 7 quad widths", True), report.summary_rows(lost))
        kept = dict(base, lost_parts={"farthest_input_quad_widths": 0.4, "far_input_vertices": 0})
        self.assertIn(("Missing parts", "none", False), report.summary_rows(kept))
        # A worker from before the measure sends no lost_parts: no row.
        self.assertFalse(any(label == "Missing parts" for label, _, _ in report.summary_rows(base)))

    def test_warnings_are_wrapped(self):
        lines = report.warning_lines({"status": "ok", "warnings": ["1 of 3 islands produced no faces"]},
                                     width=20)
        self.assertEqual(lines, [("1 of 3 islands", True, True), ("produced no faces", False, True)])
        self.assertEqual(report.warning_lines({"status": "ok"}), [])

    def test_warnings_wrap_to_pixels(self):
        # The panel passes its width in pixels and the font's measure: here
        # 10 pixels per character, and "W" twice as wide.
        def measure(text):
            return 10 * len(text) + 10 * text.count("W")
        self.assertEqual(report.wrap("1 of 3 islands produced no faces", 200, measure),
                         ["1 of 3 islands", "produced no faces"])
        self.assertEqual(report.wrap("WWWW WWWW", 160, measure), ["WWWW", "WWWW"])
        # A word wider than the panel is split rather than cut off.
        self.assertEqual(report.wrap("abcdefghij", 40, measure), ["abcd", "efgh", "ij"])
        lines = report.warning_lines({"status": "ok", "warnings": ["Parts thinner than one quad are missing"]},
                                     150, measure)
        self.assertTrue(all(measure(text) <= 150 for text, _, _ in lines))
        self.assertEqual(" ".join(text for text, _, _ in lines), "Parts thinner than one quad are missing")

    def test_summary_columns_fit(self):
        def measure(text):
            return 10 * len(text)
        rows = [("Quads", "276 of 300 target (92%)", False), ("Non-manifold vertices", "0", False)]
        # Wide enough: the label column fits the widest label, nothing wraps.
        factor, laid = report.summary_columns(rows, 600, 8, 8, measure)
        self.assertEqual(laid, [(["Quads"], ["276 of 300 target (92%)"], False),
                                (["Non-manifold vertices"], ["0"], False)])
        self.assertGreaterEqual((600 - 8) * factor - 8, measure("Non-manifold vertices"))
        # Narrow: labels get at most 60 %, and every line fits its column.
        factor, laid = report.summary_columns(rows, 300, 8, 8, measure)
        self.assertEqual(factor, 0.6)
        for labels, values, _ in laid:
            self.assertTrue(all(measure(line) <= (300 - 8) * 0.6 - 8 for line in labels))
            self.assertTrue(all(measure(line) <= (300 - 8) * 0.4 - 8 for line in values))
        self.assertEqual(" ".join(laid[0][1]), "276 of 300 target (92%)")
        self.assertEqual(" ".join(laid[1][0]), "Non-manifold vertices")

    def test_holes_in_a_closed_input(self):
        # Kajetan's closed head at 10k quads (2026-10-21).
        holes = {
            "status": "ok", "parameters": {"target_quads": 10000},
            "output": {"quads": 10060, "non_manifold_vertices": 2},
            "islands": {"input": 1, "without_output": 0, "closed_in_input": 1, "opened": 1,
                        "holes": 2, "hole_edges": 240},
            "warnings": ["Output has 2 holes where the input was closed (240 open edges)"]}
        rows = report.summary_rows(holes)
        self.assertIn(("Holes", "2 (240 open edges)", True), rows)
        self.assertIn(("Non-manifold vertices", "2", True), rows)
        lines = report.warning_lines(holes)
        self.assertTrue(all(len(text) <= report.WARNING_WIDTH for text, _, _ in lines))
        self.assertEqual(" ".join(text for text, _, problem in lines if problem),
                         "Output has 2 holes where the input was closed (240 open edges)")
        hint = " ".join(text for text, _, problem in lines if not problem)
        self.assertIn("Select All by Trait > Non Manifold", hint)

        closed = dict(holes, islands={"input": 1, "closed_in_input": 1, "holes": 0}, warnings=[])
        self.assertIn(("Holes", "0", False), report.summary_rows(closed))
        self.assertEqual(report.warning_lines(closed), [])

    def test_handles_row(self):
        # demosthenes Quality 10k (2026-10-22): two of its three tunnels close up.
        lost = {"status": "ok", "parameters": {"target_quads": 10000}, "output": {"quads": 9940},
                "islands": {"input": 1, "handles_lost": 2, "handles_added": 0, "handles_changed": 1},
                "warnings": ["Output has 2 handles fewer than the input (tunnels or loops closed up, on 1 island): "
                             "handles only a few quads around close up. A higher Target Quads keeps more of them"]}
        self.assertIn(("Handles", "2 lost", True), report.summary_rows(lost))
        self.assertTrue(all(len(text) <= report.WARNING_WIDTH for text, _, _ in report.warning_lines(lost)))
        both = dict(lost, islands={"input": 2, "handles_lost": 1, "handles_added": 3, "handles_changed": 2})
        self.assertIn(("Handles", "1 lost, 3 added", True), report.summary_rows(both))
        same = dict(lost, islands={"input": 1, "handles_lost": 0, "handles_added": 0, "handles_changed": 0},
                    warnings=[])
        self.assertIn(("Handles", "as input", False), report.summary_rows(same))
        older = dict(lost, islands={"input": 1}, warnings=[])
        self.assertFalse(any(label == "Handles" for label, _, _ in report.summary_rows(older)))

    def test_input_cleanup_row(self):
        rows = report.summary_rows({
            "status": "ok", "parameters": {"target_quads": 5000},
            "output": {"quads": 4962}, "diagnostics": {"input_dropped_triangles": 36334}})
        self.assertIn(("Input cleanup", "36334 degenerate triangles removed", False), rows)
        rows = report.summary_rows({
            "status": "ok", "parameters": {"target_quads": 5000}, "output": {"quads": 4962},
            "diagnostics": {"input_dropped_triangles": 2, "input_flipped_triangles": 240}})
        self.assertIn(("Input cleanup", "2 degenerate triangles removed, 240 triangles turned", False), rows)
        rows = report.summary_rows({
            "status": "ok", "parameters": {"target_quads": 5000}, "output": {"quads": 4962},
            "diagnostics": {"input_welded_edges": 270, "input_duplicate_triangles": 12,
                            "input_split_vertices": 2, "input_sliver_islands": 3}})
        self.assertIn(("Input cleanup", "270 seam edges welded, 12 duplicate faces removed, "
                       "2 pinched vertices split, 3 zero-width sliver islands left out", False), rows)


    def test_fold_rows(self):
        found = {"status": "ok", "parameters": {"target_quads": 5000, "repair_folds": False},
                 "output": {"quads": 4962},
                 "diagnostics": {"input_fold_clusters": 70, "input_folded_triangles": 326}}
        warning = " ".join(text for text, _, _ in report.warning_lines(found))
        self.assertIn("Folded input: 326 faces in 70 spots,", warning)
        self.assertNotIn("brush", warning)
        self.assertTrue(report.offers_fold_repair(found))
        self.assertIn("not changed", " ".join(report.fold_offer_lines()))
        repaired = {"status": "ok", "parameters": {"target_quads": 5000, "repair_folds": True},
                    "output": {"quads": 4962},
                    "diagnostics": {"input_fold_clusters": 70, "input_fold_clusters_after": 9,
                                    "input_folded_triangles": 326, "input_folded_triangles_after": 20}}
        rows = report.summary_rows(repaired)
        self.assertIn(("Folds repaired", "306 of 326 faces", False), rows)
        self.assertIn(("Folds left", "20 faces in 9 spots", True), rows)
        self.assertFalse(report.offers_fold_repair(repaired))
        self.assertIn("20 folded faces in 9 spots could not", " ".join(t for t, _, _ in report.warning_lines(repaired)))
        # A repair that splits 3 spots into 32 smaller ones still reads sensibly.
        split = {"status": "ok", "parameters": {"target_quads": 5000, "repair_folds": True},
                 "output": {"quads": 4962},
                 "diagnostics": {"input_fold_clusters": 3, "input_fold_clusters_after": 32,
                                 "input_folded_triangles": 90, "input_folded_triangles_after": 40}}
        self.assertIn(("Folds repaired", "50 of 90 faces", False), report.summary_rows(split))
        done = {"status": "ok", "parameters": {"target_quads": 5000, "repair_folds": True},
                "output": {"quads": 4962},
                "diagnostics": {"input_fold_clusters": 3, "input_fold_clusters_after": 0,
                                "input_folded_triangles": 90, "input_folded_triangles_after": 0}}
        self.assertFalse(any(label == "Folds left" for label, _, _ in report.summary_rows(done)))
        clean = {"status": "ok", "parameters": {"target_quads": 5000}, "output": {"quads": 4962},
                 "diagnostics": {"input_fold_clusters": 0}}
        self.assertFalse(any(label in ("Folds repaired", "Folds left") for label, _, _ in report.summary_rows(clean)))
        self.assertFalse(any("Folded input" in text for text, _, _ in report.warning_lines(clean)))
        self.assertFalse(report.offers_fold_repair(clean))

    def test_thin_parts_hint(self):
        thin = {"status": "ok", "parameters": {"target_quads": 2000}, "output": {"quads": 1980},
                "diagnostics": {"thin_parts_warned": 1},
                "warnings": ["18 % of the surface (12 spots) is thinner than one quad; expect folds or "
                             "lost parts there. At least 3300 target quads (an estimate) would fit a quad "
                             "across about half of it, but each raise also reveals thinner parts, so it can "
                             "take several raises; thickening those parts is usually the better fix"]}
        lines = report.warning_lines(thin)
        self.assertTrue(all(len(text) <= report.WARNING_WIDTH for text, _, _ in lines))
        self.assertIn("At least 3300 target quads", " ".join(text for text, _, problem in lines if problem))
        self.assertIn("Alt+S", " ".join(text for text, _, problem in lines if not problem))
        clean = dict(thin, diagnostics={"thin_parts_warned": 0}, warnings=[])
        self.assertEqual(report.warning_lines(clean), [])

    def test_spike_rows(self):
        flattened = {"status": "ok", "parameters": {"target_quads": 3000, "repair_folds": True, "flatten_spikes": True},
                     "output": {"quads": 2950},
                     "diagnostics": {"input_fold_clusters": 20, "input_fold_clusters_after": 3,
                                     "input_folded_triangles": 45, "input_folded_triangles_after": 7,
                                     "input_fold_spikes": 1, "input_fold_spike_drop_quads": 0.6015}}
        self.assertIn(("Spikes flattened", "1, tip lowered by up to 0.6 quads", False), report.summary_rows(flattened))
        warning = " ".join(t for t, _, _ in report.warning_lines(flattened))
        self.assertIn("changed the shape: 1 spike tips were lowered", warning)
        none = dict(flattened, diagnostics=dict(flattened["diagnostics"], input_fold_spikes=0))
        self.assertIn(("Spikes flattened", "0", False), report.summary_rows(none))
        self.assertNotIn("changed the shape", " ".join(t for t, _, _ in report.warning_lines(none)))
        plain = dict(flattened, parameters={"target_quads": 3000, "repair_folds": True})
        self.assertFalse(any(label == "Spikes flattened" for label, _, _ in report.summary_rows(plain)))


def lossy_report(target, far, reference=None, thin=None, folds=0):
    """A finished run's report with far lost vertices, all thin unless thin
    says otherwise, and the reference block if reference = (target, far, thin)."""
    lost = {"far_input_vertices": far, "thin_vertices": far if thin is None else thin,
            "piece_vertices": 0, "open_edge_vertices": 0, "reference": None}
    if reference:
        lost["reference"] = {"target_quads": reference[0], "far_input_vertices": reference[1],
                             "thin_vertices": reference[2], "piece_vertices": 0, "open_edge_vertices": 0}
    return {"status": "ok", "parameters": {"target_quads": target}, "lost_parts": lost,
            "output": {"quads": target, "folded_faces": folds}, "islands": {"holes": 0},
            "time": {"total_seconds": 1.0}}


class RaiseHintTest(unittest.TestCase):
    MAX = worker.MAXIMUM_TARGET_QUADS

    def test_round_up(self):
        self.assertEqual([raise_hint.round_up(n) for n in (4500, 5985, 6140, 450.5, 99, 1_000_001)],
                         [4500, 6000, 6200, 460, 99, 1_100_000])

    def test_ladder(self):
        # 1.5 times the reference, then 1.33 times the last run, up to 8 times.
        steps, target = [], 3000
        while target is not None:
            steps.append(target)
            target = raise_hint.next_target(3000, target, self.MAX)
        self.assertEqual(steps, [3000, 4500, 6000, 8000, 11000, 15000, 20000, 24000])
        self.assertEqual(raise_hint.next_target(3000, 3200, self.MAX), 4500)
        self.assertIsNone(raise_hint.next_target(1_500_000, 1_500_000, self.MAX))

    def test_raisable(self):
        self.assertTrue(raise_hint.raisable({"far_input_vertices": 100, "thin_vertices": 25}))
        self.assertTrue(raise_hint.raisable({"far_input_vertices": 100, "piece_vertices": 30}))
        self.assertFalse(raise_hint.raisable({"far_input_vertices": 100, "thin_vertices": 24}))
        self.assertFalse(raise_hint.raisable({"far_input_vertices": 0}))

    def test_input_key(self):
        positions = np.zeros((4, 3), dtype=np.float32)
        triangles = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.int32)
        marked = np.empty((0, 2), dtype=np.int32)
        key = raise_hint.input_key(positions, triangles, marked, {"target_quads": 300, "adaptivity": 0.5})
        self.assertEqual(key, raise_hint.input_key(positions, triangles, marked,
                                                   {"target_quads": 900, "adaptivity": 0.5}))
        self.assertNotEqual(key, raise_hint.input_key(positions, triangles, marked,
                                                      {"target_quads": 300, "adaptivity": 0.25}))
        moved = positions.copy()
        moved[0, 0] = 1e-6
        self.assertNotEqual(key, raise_hint.input_key(moved, triangles, marked, {"target_quads": 300,
                                                                                 "adaptivity": 0.5}))
        self.assertNotEqual(key, raise_hint.input_key(positions, triangles, np.array([[0, 1]]),
                                                      {"target_quads": 300, "adaptivity": 0.5}))

    def run_ladder(self, *reports, key="k"):
        history = None
        for item in reports:
            history = raise_hint.record(history, key, item)
        return history, raise_hint.hint(history, self.MAX)

    def test_clean_run_no_hint(self):
        history, hint = self.run_ladder(lossy_report(3000, 0))
        self.assertEqual(history["reference"], 0)
        self.assertIsNone(hint)
        self.assertEqual(raise_hint.reference_target(history, "k", 4500), 0)

    def test_other_cause_no_hint(self):
        history, hint = self.run_ladder(lossy_report(3000, 100, thin=10))
        self.assertIsNone(hint)

    def test_try_then_partly_then_back(self):
        history, hint = self.run_ladder(lossy_report(3000, 308, folds=5))
        self.assertEqual((hint["state"], hint["next"]), ("try", 4500))
        self.assertIn("Try 4,500 Target Quads", raise_hint.text(hint))
        self.assertEqual(raise_hint.reference_target(history, "k", 4500), 3000)
        self.assertEqual(raise_hint.reference_target(history, "other key", 4500), 0)
        self.assertEqual(raise_hint.reference_target(history, "k", 3000), 0)

        partly = lossy_report(4500, 400, reference=(3000, 130, 130), folds=6)
        history, hint = self.run_ladder(lossy_report(3000, 308, folds=5), partly)
        self.assertEqual((hint["state"], hint["missing"], hint["next"]), ("partly", 130, 6000))
        self.assertEqual(raise_hint.text(hint), "178 of 308 points missing at 3,000 are back at 4,500; "
                                                "130 are still missing. Try 6,000.")
        # The reference stays the first lossy run, not the lossy 4,500.
        self.assertEqual(history["reference"], 3000)

        back = lossy_report(6000, 50, reference=(3000, 0, 0), folds=17)
        history, hint = self.run_ladder(lossy_report(3000, 308, folds=5), partly, back)
        self.assertEqual(hint["state"], "back")
        text = raise_hint.text(hint)
        self.assertIn("All 308 points of the original missing at 3,000 are back at 6,000.", text)
        self.assertIn("12 more folded faces", text)
        self.assertIn("finer parts", text)
        self.assertEqual([run["target"] for run in history["runs"]], [3000, 4500, 6000])

    def test_not_back_at_eight_times(self):
        history, hint = self.run_ladder(lossy_report(3000, 82), lossy_report(24000, 900, reference=(3000, 10, 10)))
        self.assertEqual(hint["state"], "not_back")
        self.assertIn("Not back at up to 24,000 (8.0 times 3,000): 10 of 82 points", raise_hint.text(hint))
        self.assertNotIn("Set", raise_hint.text(hint))

    def test_remaining_not_thin(self):
        _, hint = self.run_ladder(lossy_report(3000, 100), lossy_report(4500, 40, reference=(3000, 40, 0)))
        self.assertEqual(hint["state"], "other")
        self.assertNotIn("next", hint)

    def test_new_reference_when_lower(self):
        history, hint = self.run_ladder(lossy_report(3000, 100), lossy_report(2000, 150))
        self.assertEqual((history["reference"], history["reference_far"], hint["state"]), (2000, 150, "try"))

    def test_new_key_new_history(self):
        history = raise_hint.record(None, "a", lossy_report(3000, 100))
        history["dismissed"] = True
        history = raise_hint.record(history, "a", lossy_report(4500, 0, reference=(3000, 0, 0)))
        self.assertTrue(history["dismissed"])
        self.assertEqual(raise_hint.text(raise_hint.hint(history, self.MAX)), "")
        history = raise_hint.record(history, "b", lossy_report(4500, 0))
        self.assertFalse(history["dismissed"])
        self.assertEqual(len(history["runs"]), 1)

    def test_history_limit_and_failed_runs(self):
        history = None
        for target in range(1000, 11000, 1000):
            history = raise_hint.record(history, "k", lossy_report(target, 0))
        self.assertEqual(len(history["runs"]), raise_hint.HISTORY_LIMIT)
        self.assertIs(raise_hint.record(history, "k", {"status": "failed"}), history)

    def test_missing_reference_block_no_hint(self):
        # An older worker, or a run without the flag: nothing to judge by.
        _, hint = self.run_ladder(lossy_report(3000, 100), lossy_report(4500, 80))
        self.assertIsNone(hint)

    def test_load(self):
        self.assertIsNone(raise_hint.load(None))
        self.assertIsNone(raise_hint.load("not json"))
        self.assertIsNone(raise_hint.load("[1]"))
        self.assertEqual(raise_hint.load('{"key": "k"}'), {"key": "k"})


class CopyReportTest(unittest.TestCase):
    INFO = {"addon_build": "abc1234 2026-10-31", "worker_build": "abc1234 2026-10-31",
            "blender": "4.2.3 LTS", "system": "Linux 6.8.0 x86_64 (Ubuntu 24.04.1 LTS)",
            "settings": {"target_quads": 2000, "adaptivity": 0.5, "follow_marks": ["SHARP"], "keep_uvs": False},
            "home": "/home/mootek"}

    def test_ok_run(self):
        result = {"status": "ok", "message": "", "parameters": {"target_quads": 2000},
                  "input": {"vertices": 8066, "triangles": 16128, "feature_edges": 128, "ignored_feature_edges": 0},
                  "output": {"quads": 1624, "non_quads": 2}, "islands": {"input": 1, "without_output": 0},
                  "time": {"total_seconds": 0.8, "steps": [{"name": "remesh", "seconds": 0.5}]},
                  "warnings": ["Parts thinner than one quad are missing (up to 4 quad widths away). " * 2]}
        text = report.copy_text("ok", "", result, dict(self.INFO, input_triangles=16128,
                                                       log="reading /home/mootek/sculpt.obj\ndone"))
        lines = text.splitlines()
        self.assertEqual(lines[:6], ["Quadami report", "Result: Done", "Add-on build: abc1234 2026-10-31",
                                     "Remesher build: same", "Blender: 4.2.3 LTS",
                                     "System: Linux 6.8.0 x86_64 (Ubuntu 24.04.1 LTS)"])
        self.assertIn("Settings: adaptivity=0.5, follow_marks=SHARP, keep_uvs=False, target_quads=2000", lines)
        self.assertIn("Input: 16128 triangles, 8066 vertices, 1 islands, 128 marked edges (0 ignored)", lines)
        self.assertIn("  Quads: 1624 of 2000 target (81%)", lines)
        self.assertIn("  ! Non-quads: 2", lines)
        # A warning stays on one line, unwrapped.
        self.assertEqual(sum(1 for line in lines if line.startswith("  - Parts thinner")), 1)
        self.assertIn("Steps: remesh 0.50 s", lines)
        self.assertIn("  reading ~/sculpt.obj", lines)
        full = next(line for line in lines if line.startswith("Full report: "))
        self.assertEqual(json.loads(full[len("Full report: "):]), result)
        self.assertNotIn("mootek", text)

    def test_failed_run_without_report(self):
        log = "\n".join("line %d" % i for i in range(2000))
        text = report.copy_text("failed", "the remesher exited with code -11: line 1999", None,
                                dict(self.INFO, worker_build=None, input_triangles=500, log=log))
        self.assertIn("Result: Failed: the remesher exited with code -11: line 1999", text)
        self.assertIn("Remesher build: unknown", text)
        self.assertIn("Input: 500 triangles", text)
        self.assertNotIn("Result box:", text)
        self.assertNotIn("Full report", text)
        self.assertTrue(text.rstrip().endswith("line 1999"))
        self.assertLess(len(text), report.COPY_LOG_CHARS + 2000)

    def test_earlier_result_has_no_log(self):
        text = report.copy_text("ok", "", {"status": "ok"}, dict(self.INFO, worker_build="def5678"))
        self.assertIn("Remesher build: def5678", text)
        self.assertIn("Remesher output: not kept", text)
        self.assertNotIn("Input:", text)

    def test_redact(self):
        self.assertEqual(report.redact("/home/mootek/a /home/mootekx/b /home/ola/c", "/home/mootek"),
                         "~/a /home/<user>/b /home/<user>/c")
        self.assertEqual(report.redact("C:\\Users\\Jan Kowalski\\x C:/Users/jan/AppData", "C:\\Users\\Jan Kowalski"),
                         "~\\x C:/Users/<user>/AppData")
        self.assertEqual(report.redact("/Users/kim/Desktop", None), "/Users/<user>/Desktop")
        self.assertEqual(report.redact("no path", "/"), "no path")

    def test_system_text(self):
        self.assertTrue(report.system_text())


class EnvironmentTest(unittest.TestCase):
    def test_snap_library_paths_dropped(self):
        env = worker.worker_environment({
            "SNAP": "/snap/blender/7802", "QT_QPA_PLATFORM": "wayland",
            "LD_LIBRARY_PATH": "/snap/blender/7802/lib:/opt/x/lib:/snap/blender/7802"})
        self.assertEqual(env["QT_QPA_PLATFORM"], "offscreen")
        self.assertEqual(env["LD_LIBRARY_PATH"], "/opt/x/lib")
        env = worker.worker_environment({"SNAP": "/snap/blender/7802", "LD_LIBRARY_PATH": "/snap/blender/7802/lib"})
        self.assertNotIn("LD_LIBRARY_PATH", env)
        env = worker.worker_environment({"LD_LIBRARY_PATH": "/snap/other/lib"})
        self.assertEqual(env["LD_LIBRARY_PATH"], "/snap/other/lib")


class FakeWorkerTest(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "uses a shell script as the worker")
    def test_crash_without_report(self):
        with tempfile.TemporaryDirectory() as folder:
            script = os.path.join(folder, "crash.sh")
            with open(script, "w") as f:
                f.write("#!/bin/sh\necho 'Error: something broke'\nexit 7\n")
            os.chmod(script, os.stat(script).st_mode | stat.S_IEXEC)
            positions, triangles = uv_sphere(4, 8)
            outcome = worker.remesh(script, positions, triangles, {"target_quads": 100})
        self.assertEqual(outcome.status, "failed")
        self.assertIn("code 7", outcome.message)
        self.assertIn("something broke", outcome.message)

    @unittest.skipIf(os.name == "nt", "checks the executable bit")
    def test_bundled_executable(self):
        saved_bundled = worker.bundled_executable
        saved_env = os.environ.pop("AUTOREMESHER_EXECUTABLE", None)
        try:
            with tempfile.TemporaryDirectory() as folder:
                program = os.path.join(folder, "bin", worker.CLI_NAME)
                os.makedirs(os.path.dirname(program))
                with open(program, "w") as f:
                    f.write("#!/bin/sh\n")
                os.chmod(program, 0o644)  # as Blender's zip extraction leaves it
                worker.bundled_executable = lambda: program
                self.assertEqual(worker.find_executable(), program)
                self.assertTrue(os.access(program, os.X_OK))
                self.assertEqual(worker.find_executable(sys.executable), os.path.abspath(sys.executable))
        finally:
            worker.bundled_executable = saved_bundled
            if saved_env is not None:
                os.environ["AUTOREMESHER_EXECUTABLE"] = saved_env

    @unittest.skipIf(os.name == "nt", "uses a shell script as the worker")
    def test_worker_build_id(self):
        with tempfile.TemporaryDirectory() as folder:
            script = os.path.join(folder, "version.sh")

            def program(text):
                with open(script, "w") as f:
                    f.write("#!/bin/sh\necho '%s'\n" % text)
                os.chmod(script, 0o755)

            program("autoremesher-cli 1.2.0 (build abc1234 2026-10-21, remesh interface 1.3)")
            self.assertEqual(worker.worker_build_id(script), "abc1234 2026-10-21")
            program("autoremesher-cli 1.2.0 (remesh interface 1.3) # before build ids")
            os.utime(script, ns=(1, 1))  # a rebuilt program is asked again
            self.assertIsNone(worker.worker_build_id(script))
        self.assertIsNone(worker.worker_build_id(script))  # gone
        self.assertEqual(worker.addon_build_id(), "dev")  # source tree: no build-id.txt

    def test_unknown_parameter(self):
        job = worker.RemeshJob("unused", np.zeros((3, 3)), np.array([[0, 1, 2]]), {"mirror": True})
        with self.assertRaises(ValueError):
            job.start()


@unittest.skipIf(EXECUTABLE is None, "set AUTOREMESHER_EXECUTABLE to a built autoremesher-cli")
class CliTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.positions, cls.triangles = uv_sphere()

    def test_version_has_build_id(self):
        self.assertTrue(worker.worker_build_id(EXECUTABLE))
        self.assertGreaterEqual(worker.worker_interface(EXECUTABLE), worker.LOST_PARTS_REFERENCE_INTERFACE)

    def test_lost_parts_reference_flag(self):
        outcome = worker.remesh(EXECUTABLE, self.positions.astype(np.float32), self.triangles,
                                {"target_quads": 500, "lost_parts_reference": 300})
        self.assertEqual(outcome.status, "ok", outcome.message + "\n" + outcome.log)
        self.assertEqual(outcome.report["parameters"]["lost_parts_reference"], 300)
        reference = outcome.report["lost_parts"]["reference"]
        self.assertEqual((reference["target_quads"], reference["far_input_vertices"]), (300, 0))
        plain = worker.remesh(EXECUTABLE, self.positions.astype(np.float32), self.triangles, {"target_quads": 500})
        self.assertIsNone(plain.report["lost_parts"]["reference"])
        # Measuring only: the same output.
        self.assertTrue(np.array_equal(plain.positions, outcome.positions))

    def test_remesh(self):
        updates = []
        outcome = worker.remesh(EXECUTABLE, self.positions.astype(np.float32), self.triangles,
                                {"target_quads": 2000, "adaptivity": 1.0},
                                on_progress=lambda f, s: updates.append((f, s)))
        self.assertEqual(outcome.status, "ok", outcome.message + "\n" + outcome.log)
        out = outcome.report["output"]
        self.assertTrue(1200 <= out["quads"] <= 2400, out)
        self.assertEqual(len(outcome.face_sizes), out["faces"])
        self.assertEqual(int((outcome.face_sizes == 4).sum()), out["quads"])
        self.assertEqual(len(outcome.positions), out["vertices"])
        self.assertEqual(int(outcome.face_sizes.sum()), len(outcome.face_indices))
        self.assertLess(int(outcome.face_indices.max()), len(outcome.positions))
        self.assertEqual(outcome.report["parameters"]["target_quads"], 2000)
        fractions = [f for f, _ in updates]
        self.assertEqual(fractions, sorted(fractions))
        self.assertTrue(any(s for _, s in updates))
        # The output lies on the unit sphere.
        radii = np.linalg.norm(outcome.positions, axis=1)
        self.assertLess(float(np.abs(radii - 1.0).max()), 0.01)

    def test_fold_repair_flag(self):
        outcome = worker.remesh(EXECUTABLE, self.positions.astype(np.float32), self.triangles,
                                {"target_quads": 500, "repair_folds": True})
        self.assertEqual(outcome.status, "ok", outcome.message + "\n" + outcome.log)
        self.assertIs(outcome.report["parameters"]["repair_folds"], True)
        self.assertEqual(outcome.report["diagnostics"]["input_fold_clusters"], 0)

    def test_flatten_spikes_flag(self):
        outcome = worker.remesh(EXECUTABLE, self.positions.astype(np.float32), self.triangles,
                                {"target_quads": 500, "repair_folds": True, "flatten_spikes": True})
        self.assertEqual(outcome.status, "ok", outcome.message + "\n" + outcome.log)
        self.assertIs(outcome.report["parameters"]["flatten_spikes"], True)
        self.assertEqual(outcome.report["diagnostics"]["input_fold_spikes"], 0)

    def test_quad_smoothing_flag(self):
        outcome = worker.remesh(EXECUTABLE, self.positions.astype(np.float32), self.triangles,
                                {"target_quads": 500, "quad_smoothing": "tangential"})
        self.assertEqual(outcome.status, "ok", outcome.message + "\n" + outcome.log)
        self.assertEqual(outcome.report["parameters"]["quad_smoothing"], "tangential")

    def test_marked_edges(self):
        # The equator of uv_sphere() (64 rings, 128 segments): ring 32.
        segments = 128
        first = 1 + 31 * segments
        ring = [(first + j, first + (j + 1) % segments) for j in range(segments)]
        outcome = worker.remesh(EXECUTABLE, self.positions.astype(np.float32), self.triangles,
                                {"target_quads": 500}, marked_edges=np.array(ring + [(0, first)]))
        self.assertEqual(outcome.status, "ok", outcome.message + "\n" + outcome.log)
        # (0, first) is not an edge of the sphere: ignored and said so.
        self.assertEqual(outcome.report["input"]["feature_edges"], segments + 1)
        self.assertEqual(outcome.report["input"]["ignored_feature_edges"], 1)
        self.assertTrue(any("marked edge" in w for w in outcome.report["warnings"]), outcome.report["warnings"])

    def test_target_above_maximum(self):
        positions, triangles = uv_sphere(16, 32)
        outcome = worker.remesh(EXECUTABLE, positions, triangles,
                                {"target_quads": worker.MAXIMUM_TARGET_QUADS + 1})
        self.assertEqual(outcome.status, "invalid_input", outcome.message + "\n" + outcome.log)
        self.assertIn("maximum", outcome.message)

    def test_non_ascii_temp_folder(self):
        # A Windows user name such as "Łucja" ends up in the temporary folder's path.
        parent = tempfile.mkdtemp(prefix="quadami-test-")
        saved = tempfile.tempdir
        try:
            tempfile.tempdir = os.path.join(parent, "Łucja żółw 日本")
            os.mkdir(tempfile.tempdir)
            positions, triangles = uv_sphere(16, 32)
            outcome = worker.remesh(EXECUTABLE, positions, triangles, {"target_quads": 300})
        finally:
            tempfile.tempdir = saved
            shutil.rmtree(parent, ignore_errors=True)
        self.assertEqual(outcome.status, "ok", outcome.message + "\n" + outcome.log)
        self.assertGreater(len(outcome.face_sizes), 0)

    def _cancel_after(self, seconds, how):
        job = worker.RemeshJob(EXECUTABLE, self.positions, self.triangles, {"target_quads": 20000})
        job.start()
        try:
            started = time.monotonic()
            while time.monotonic() - started < seconds and not job.done:
                job.poll()
                time.sleep(0.05)
            self.assertFalse(job.done, "the run finished before the cancel")
            cancelled_at = time.monotonic()
            how(job)
            while not job.done:
                self.assertLess(time.monotonic() - cancelled_at, 30.0, "the run did not end after the cancel")
                job.poll()
                time.sleep(0.05)
            outcome = job.outcome()
            return outcome, time.monotonic() - cancelled_at, job
        finally:
            job.cleanup()

    def test_cancel(self):
        outcome, latency, job = self._cancel_after(1.5, lambda job: job.cancel())
        self.assertEqual(outcome.status, "cancelled")
        self.assertIsNotNone(outcome.report, "the worker should exit by itself and report")
        self.assertIsNone(outcome.positions)
        self.assertLess(latency, worker.CANCEL_GRACE_SECONDS + 1.0)
        print("\n  cancel latency %.2f s, killed: %s" % (latency, job.killed))

    def test_cancel_when_host_closes_stdin(self):
        outcome, latency, job = self._cancel_after(1.5, lambda job: job._process.stdin.close())
        self.assertEqual(outcome.status, "cancelled")
        self.assertIsNotNone(outcome.report)
        print("\n  stdin-closed latency %.2f s" % latency)

    def test_kill_after_grace(self):
        # The cancel is recorded but never reaches the worker's stdin, so the worker
        # keeps solving and only the kill after the grace can end it. The bound only
        # catches a hang: process teardown on a loaded runner takes up to ~1.5 s.
        def cancel_unheard(job):
            job.cancel_requested = True
            job._cancel_time = time.monotonic()

        grace = worker.CANCEL_GRACE_SECONDS
        worker.CANCEL_GRACE_SECONDS = 0.0
        try:
            outcome, latency, job = self._cancel_after(1.5, cancel_unheard)
        finally:
            worker.CANCEL_GRACE_SECONDS = grace
        self.assertEqual(outcome.status, "cancelled")
        self.assertTrue(job.killed, "the worker should have been killed")
        self.assertIn("killed", outcome.message)
        self.assertLess(latency, 10.0)
        print("\n  kill latency %.2f s" % latency)

    def test_invalid_input(self):
        triangles = self.triangles.copy()
        triangles[0, 0] = len(self.positions) + 10
        outcome = worker.remesh(EXECUTABLE, self.positions, triangles, {"target_quads": 2000})
        self.assertIn(outcome.status, ("invalid_input", "failed"))
        self.assertIsNone(outcome.positions)
        print("\n  bad index: %s, %r" % (outcome.status, outcome.message))


if __name__ == "__main__":
    unittest.main()
