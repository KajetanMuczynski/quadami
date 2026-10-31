# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""preview_report's own pole, loop and size code on meshes whose answers are known.

    python -m unittest discover -s tools/bench/tests -v      (needs numpy, scipy)
"""
import contextlib
import io
import json
import math
import os
import sys
import tempfile
import unittest

import numpy as np

BENCH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BENCH)
import preview_report  # noqa: E402


def torus(n, m, big=2.0, small=0.5):
    """n x m quad torus: every vertex valence 4, every loop closed."""
    vs = []
    for i in range(n):
        for j in range(m):
            u, v = 2 * math.pi * i / n, 2 * math.pi * j / m
            r = big + small * math.cos(v)
            vs.append((r * math.cos(u), r * math.sin(u), small * math.sin(v)))
    fs = [[i * m + j, ((i + 1) % n) * m + j, ((i + 1) % n) * m + (j + 1) % m, i * m + (j + 1) % m]
          for i in range(n) for j in range(m)]
    return vs, fs


def cube(k):
    """Cube with k x k quads per side: 8 poles of valence 3 at the corners."""
    index, vs, fs = {}, [], []

    def vid(p):
        key = tuple(round(c, 9) for c in p)
        if key not in index:
            index[key] = len(vs)
            vs.append(key)
        return index[key]

    for axis in range(3):
        for sign in (-1.0, 1.0):
            a, b = [x for x in range(3) if x != axis]
            for i in range(k):
                for j in range(k):
                    quad = []
                    for di, dj in ((0, 0), (1, 0), (1, 1), (0, 1)):
                        p = [0.0, 0.0, 0.0]
                        p[axis] = sign
                        p[a] = -1 + 2 * (i + di) / k
                        p[b] = -1 + 2 * (j + dj) / k
                        quad.append(vid(p))
                    fs.append(quad if sign > 0 else quad[::-1])
    return vs, fs


def write_obj(path, mesh):
    vs, fs = mesh
    with open(path, "w") as f:
        f.writelines("v %r %r %r\n" % v for v in vs)
        f.writelines("f " + " ".join(str(i + 1) for i in q) + "\n" for q in fs)


def write_run(root, mesh_name, cfg, rep, mesh, seconds):
    d = os.path.join(root, "corpus", mesh_name, cfg, "u%d" % rep)
    os.makedirs(d)
    write_obj(os.path.join(d, "out.obj"), mesh)
    json.dump({"rc": 0, "time": seconds}, open(os.path.join(d, "record.json"), "w"))
    stages = [{"stage": "run", "islands": 1, "voxel_ms": 1, "split_ms": 9, "build_ms": 10, "total_ms": 1000,
               "parameterize_wall_ms": 500, "retry_wall_ms": 0, "merge_ms": 10, "thin_parts_ms": 40},
              {"stage": "step", "name": "Projecting vertices", "accumulated_ms": 100},
              {"stage": "step", "name": "Solving frame field", "accumulated_ms": 50},
              {"stage": "island", "resample_ms": 300}]
    with open(os.path.join(d, "stages.jsonl"), "w") as fp:
        fp.write("\n".join(json.dumps(s) for s in stages) + "\n")


class PreviewReportTest(unittest.TestCase):
    def test_torus_loops_and_poles(self):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "t.obj")
            write_obj(path, torus(12, 8))
            q = preview_report.QuadMesh(path)
            self.assertEqual(q.quads, 96)
            self.assertEqual(len(q.poles), 0)
            closed, longest = q.loops()
            self.assertEqual(closed, 1.0)
            self.assertAlmostEqual(longest, 12 / 96)

    def test_cube_poles_and_self_agreement(self):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "c.obj")
            write_obj(path, cube(4))
            q = preview_report.QuadMesh(path)
            self.assertEqual(len(q.poles), 8)
            closed, _ = q.loops()
            # 3 x 3 rings around the cube close; the 12 cube edges end at the corner poles.
            self.assertAlmostEqual(closed, 9 / 21)
            recall, chance = preview_report.pole_recall(q, q, 1e-9, np.random.default_rng(0))
            self.assertEqual(recall, 1.0)
            self.assertLess(chance, 1.0)
            # The same cube twice as fine: same size shape after the count scaling.
            fine = os.path.join(root, "f.obj")
            write_obj(fine, cube(8))
            r2, err = preview_report.size_agreement(preview_report.QuadMesh(fine), q)
            self.assertLess(err, 0.3)

    def test_report_tables(self):
        with tempfile.TemporaryDirectory() as root:
            write_run(root, "c", "quality-t400", 0, cube(8), 4.0)
            write_run(root, "c", "quality-t400", 1, cube(8), 4.0)
            write_run(root, "c", "quality-t100", 0, cube(4), 1.0)
            out = io.StringIO()
            sys.argv = ["preview_report.py", root, "--json", os.path.join(root, "r.json")]
            with contextlib.redirect_stdout(out):
                self.assertEqual(preview_report.main(), 0)
            text = out.getvalue()
            self.assertIn("reference quality-t400", text)
            # Equal edges everywhere: no size variance, so r2 is undefined.
            self.assertIn("| quality-t400/u1 | 1 | 1.00 | 1.00 | 1.00 | 1.00 | nan | 0.00 |", text)
            self.assertIn("| quality-t100 | 1 | 0.25 |", text)
            rows = json.load(open(os.path.join(root, "r.json")))
            cost = rows["costs"][0]
            self.assertAlmostEqual(cost["plan_ms"], 1000 - 100 - 550)
            self.assertAlmostEqual(cost["flow_ms"], 350 + 100 + 50)


if __name__ == "__main__":
    unittest.main()
