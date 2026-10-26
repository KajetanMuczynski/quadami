# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""metrics.py's crease numbers (A1, A1theta, bent quads) on meshes with a known answer.

    python -m unittest discover -s tools/bench/tests -v      (needs numpy, scipy, libigl)
"""
import os
import sys
import unittest

import numpy as np

BENCH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BENCH)
import metrics  # noqa: E402


def box(sx, sy, sz, n, R=np.eye(3)):
    """Closed quad box [0, sx] x [0, sy] x [0, sz] with n cells per side, its vertices shared, rotated by R."""
    V, F, index = [], [], {}

    def vid(p):
        key = tuple(np.round(p, 12))
        if key not in index:
            index[key] = len(V)
            V.append(p)
        return index[key]
    size = np.array([sx, sy, sz], float)
    for axis in range(3):
        u, v = (axis + 1) % 3, (axis + 2) % 3
        for side in (0.0, 1.0):
            for i in range(n):
                for j in range(n):
                    quad = []
                    for a, b in ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)):
                        p = np.zeros(3)
                        p[axis], p[u], p[v] = side, a / n, b / n
                        quad.append(vid(p * size))
                    F.append(quad if side else quad[::-1])
    return np.array(V) @ R.T, F


def rotation(degrees):
    out = np.eye(3)
    for axis, d in enumerate(np.radians(degrees)):
        c, s = np.cos(d), np.sin(d)
        u, v = (axis + 1) % 3, (axis + 2) % 3
        r = np.eye(3)
        r[u, u], r[u, v], r[v, u], r[v, v] = c, -s, s, c
        out = r @ out
    return out


def edges(faces):
    he = np.array([(f[i], f[(i + 1) % len(f)]) for f in faces for i in range(len(f))])
    return np.unique(np.sort(he, 1), axis=0)


def report(Vin, Fin_quads, V, faces, h, sharp_degrees=90.0):
    Fin = metrics.triangulate(Fin_quads)
    sharp = metrics.sharp_edges(Vin, Fin)
    angles = metrics.sharp_edge_angles(Vin, Fin, sharp)
    return sharp, angles, metrics.crease_report(Vin, sharp, angles, V, faces, edges(faces), h, sharp_degrees)


class CreaseReport(unittest.TestCase):
    def test_box_with_aligned_output(self):
        V, F = box(1, 1, 1, 4)
        sharp, angles, r = report(V, F, *box(1, 1, 1, 10), 0.1)
        self.assertEqual(len(sharp), 12 * 4)
        self.assertEqual(r["coverage_theta_all"], 1.0)
        self.assertEqual(r["resolvable_share"], 1.0)
        self.assertEqual(r["coverage_theta"], 1.0)
        self.assertEqual(r["bent_quads_per_1k"], 0.0)

    def test_right_angle_counts_on_every_rotation(self):
        # A rotated box computes some of its 90 deg dihedrals a few ulp below 90; theta - 1 deg keeps them.
        below = 0
        for degrees in ((23.0, 37.0, 11.0), (0.0, 45.0, 0.0)):
            R = rotation(degrees)
            V, F = box(1, 1, 1, 4, R)
            sharp, angles, r = report(V, F, *box(1, 1, 1, 10, R), 0.1)
            self.assertEqual(len(sharp), 12 * 4)
            below += int(np.sum(angles < 90.0))
            self.assertAlmostEqual(r["coverage_theta"], 1.0)
        self.assertGreater(below, 0)  # the case this test is for occurs

    def test_creases_below_theta_are_left_out(self):
        V, F = box(1, 1, 1, 4)
        _, _, r = report(V, F, *box(1, 1, 1, 10), 0.1, sharp_degrees=120.0)
        self.assertIsNone(r["coverage_theta_all"])
        self.assertIsNone(r["coverage_theta"])
        self.assertEqual(r["bent_quads_per_1k"], 0.0)

    def test_output_off_the_creases(self):
        # A smaller box inside: its edges run 0.07 from the creases, 0.7 h away.
        Vin, Fin = box(1, 1, 1, 4)
        V, F = box(1, 1, 1, 10)
        V = 0.05 + 0.9 * V
        _, _, r = report(Vin, Fin, V, F, 0.1)
        self.assertEqual(r["coverage_theta"], 0.0)

    def test_thin_slab_is_not_resolvable(self):
        # Creases 0.02 apart at h = 0.1 bound a strip narrower than h/2: no quad layout keeps both.
        V, F = box(1, 1, 0.02, 4)
        _, _, r = report(V, F, V, F, 0.1)
        self.assertLess(r["resolvable_share"], 0.1)
        self.assertEqual(r["coverage_theta_all"], 1.0)

    def test_quad_folded_across_a_crease(self):
        # Diamond quads on a 90 deg roof: every quad on the ridge has the ridge as its diagonal.
        def roof(x, u):
            return np.array([x, u, 0.0]) if u <= 0 else np.array([x, 0.0, u])
        s, n = 0.05, 20
        V = np.array([roof((p + q) * s, (p - q) * s) for p in range(n + 1) for q in range(n + 1)])
        F = [[p * (n + 1) + q, (p + 1) * (n + 1) + q, (p + 1) * (n + 1) + q + 1, p * (n + 1) + q + 1]
             for p in range(n) for q in range(n)]
        Vin = np.array([roof(x, u) for x in (0.0, 2.0 * n * s) for u in (-n * s, 0.0, n * s)])
        Fin = [[0, 1, 4, 3], [1, 2, 5, 4]]
        _, _, r = report(Vin, Fin, V, F, s * np.sqrt(2))
        self.assertAlmostEqual(r["bent_quads_per_1k"], 1000.0 * (n - 1) / len(F), delta=1000.0 * 2 / len(F))


if __name__ == "__main__":
    unittest.main()
