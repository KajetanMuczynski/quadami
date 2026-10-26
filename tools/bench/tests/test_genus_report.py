# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""genus_report.py's per-island reading and classes on hand-made records.

    python -m unittest discover -s tools/bench/tests -v
"""
import os
import sys
import unittest

BENCH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BENCH)
import genus_report as gr  # noqa: E402


def record(*islands):
    return {"metrics": {"validity": {"per_island": [
        {"euler": list(e), "loops": list(b), "output_islands": k, "input_non_manifold": nm}
        for e, b, k, nm in islands]}}}


class GenusReportTest(unittest.TestCase):
    def test_changed_islands(self):
        # A torus that came out a sphere, an unchanged open disc, a split island (not ours) and
        # a non-manifold input with another Euler characteristic.
        rec = record(((0, 2), (0, 0), 1, False), ((1, 1), (1, 1), 1, False),
                     ((2, 4), (0, 0), 2, False), ((1, 3), (0, 0), 1, True))
        ch = gr.changed_islands(rec)
        self.assertEqual([c["island"] for c in ch], [0, 3])
        self.assertEqual(ch[0]["d2"], -2)
        self.assertTrue(ch[0]["warned"])
        self.assertTrue(ch[1]["non_manifold_input"])
        self.assertEqual(gr.island_d2(rec, 1), 0)
        self.assertEqual(gr.island_d2(rec, 0), -2)

    def test_open_island_is_counted_but_not_by_a_closed_only_rule(self):
        # demosthenes: genus 3 with one border, all handles lost.
        ch = gr.changed_islands(record(((-5, 1), (1, 1), 1, False)))
        self.assertEqual(ch[0]["d2"], -6)
        self.assertFalse(ch[0]["warned"])

    def test_classes(self):
        lost = {"g2_in": 2, "d2": -2, "loops_in": 0, "non_manifold_input": False}
        core = {"island": {"isotropic_euler_characteristic": 0}, "map_check": {"map_handle_loops_collapsed": 1}}
        self.assertEqual(gr.classify(lost, core, -2), "map0")
        self.assertEqual(gr.classify(lost, core, 0), "extractor")
        self.assertEqual(gr.classify(lost, core, None), "unknown")
        core2 = {"island": {"isotropic_euler_characteristic": 0},
                 "map_check": {"map_handle_loops_collapsed": 0, "map_handle_loops_thin": 1}}
        self.assertEqual(gr.classify(lost, core2, -2), "map2")
        remeshed = {"island": {"isotropic_euler_characteristic": 2}, "map_check": {}}
        self.assertEqual(gr.classify(lost, remeshed, -2), "remesh")
        self.assertEqual(gr.classify(dict(lost, non_manifold_input=True), core, -2), "input")
        added = {"g2_in": 0, "d2": 2, "loops_in": 0, "non_manifold_input": False}
        self.assertEqual(gr.classify(added, {"island": {"isotropic_euler_characteristic": 2}}, 2), "both")


if __name__ == "__main__":
    unittest.main()
