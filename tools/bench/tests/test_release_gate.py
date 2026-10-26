# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""release_gate fails on a structural gate, ratchets against a baseline for 0.x, and holds --hard for 1.0.

    python -m unittest discover -s tools/bench/tests -v      (needs numpy)
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

BENCH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(quads=2000, **over):
    """A record as ab.py writes it for an all-quad run of spot at 2000, clean unless `over` says otherwise."""
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
    return {"mesh": "spot", "res": "fast", "target": 2000, "rc": 0, "metrics": m}


def write(folder, runs):
    for u, rec in enumerate(runs):
        d = os.path.join(folder, "corpus", "spot", "fast-t2000", "u%d" % u)
        os.makedirs(d)
        with open(os.path.join(d, "record.json"), "w") as f:
            json.dump(dict(rec, replicate=u), f)


class ReleaseGate(unittest.TestCase):
    def gate(self, cand, base=None, *args):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = os.path.join(tmp, "cand"), os.path.join(tmp, "base")
            write(a, cand)
            cmd = [sys.executable, os.path.join(BENCH, "release_gate.py"), a, "--set", "corpus"] + list(args)
            if base is not None:
                write(b, base)
                cmd += ["--baseline", b]
            p = subprocess.run(cmd, capture_output=True, text=True)
            return p.returncode, p.stdout + p.stderr

    def test_clean_passes_alone_and_against_itself(self):
        self.assertEqual(self.gate([run(), run()])[0], 0)
        rc, out = self.gate([run(), run()], [run(), run()])
        self.assertEqual(rc, 0, out)

    def test_each_structural_gate_fails_without_baseline(self):
        for over in ({"topology.non_manifold_edges": 1}, {"topology.inconsistently_oriented_edges": 3},
                     {"validity.islands_dropped": 1}, {"validity.holes_added": 1}, {"faces.other": 1}):
            self.assertEqual(self.gate([run(**over)])[0], 1, over)
        self.assertEqual(self.gate([run(quads=2300)])[0], 1)
        self.assertEqual(self.gate([run(quads=2200)])[0], 0)  # exactly +10 % still passes
        self.assertEqual(self.gate([dict(run(), rc=1)])[0], 1)

    def test_structural_ratchet_unless_hard(self):
        same = ([run(quads=1000), run()], [run(quads=1000), run()])
        rc, out = self.gate(*same)
        self.assertEqual(rc, 0, out)
        self.assertIn("| H6 count within ±10 % | 1 / 1 | 0 / 0 | ok |", out)
        self.assertEqual(self.gate(*same, "--hard")[0], 1)
        rc, out = self.gate([run(quads=1000), run(quads=1000)], [run(quads=1000), run()])
        self.assertEqual(rc, 1)
        self.assertIn("| H6 count within ±10 % | 1 / 2 | 0 / 1 | **worse** |", out)

    def test_structural_flip_on_some_replicates_is_noise(self):
        rc, out = self.gate([run(**{"validity.islands_dropped": 1}), run()], [run(), run()])
        self.assertEqual(rc, 0, out)
        self.assertIn("| H4 islands kept (none dropped, split or merged) | 0 / 1 | 0 / 0 | ok |", out)
        self.assertIn("| spot | fast | 2000 | u0 | H4 |", out)  # still listed

    def test_one_more_fold_is_worse(self):
        rc, out = self.gate([run(**{"quads.sj_le_0": 1}), run()], [run(), run()])
        self.assertEqual(rc, 1)
        self.assertIn("| V2 no folded quad | 2 / 1 | **worse** |", out)
        self.assertIn("| folded quads | 0.0000 / 0.2500 | **worse** |", out)
        self.assertEqual(self.gate([run()], [run(**{"quads.sj_le_0": 1})])[0], 0)  # fewer is fine


if __name__ == "__main__":
    unittest.main()
