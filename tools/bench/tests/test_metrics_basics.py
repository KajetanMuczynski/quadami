# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""metrics.py's basic counts on meshes whose answer is derived by hand (TODO 1.4 audit, 2026-10-27):
faces, Euler characteristic, boundary loops, valence, scaled Jacobian, folded and degenerate faces,
orientation, non-manifold edges and vertices, crease coverage and distance on exact copies, and the
OBJ forms other exporters write. They pin today's numbers, so a change to metrics.py or to numpy,
scipy or libigl that moves one of them shows up here first. Known faults are expectedFailure: fixing
one turns its test into an unexpected success, which says to move it to the plain tests.

    python -m unittest discover -s tools/bench/tests -v      (needs numpy, scipy, libigl)
"""
import os
import sys
import tempfile
import unittest

import numpy as np

BENCH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BENCH)
import metrics  # noqa: E402


def cube(origin=(0, 0, 0)):
    """Unit cube, vertex x + 2y + 4z, six outward counter-clockwise quads."""
    V = np.array([[x, y, z] for z in (0, 1) for y in (0, 1) for x in (0, 1)], float) + origin
    return V, [[0, 2, 3, 1], [4, 5, 7, 6], [0, 1, 5, 4], [2, 6, 7, 3], [0, 4, 6, 2], [1, 3, 7, 5]]


def grid(n):
    """n x n quads on the unit square, normals up."""
    V = np.array([[i / n, j / n, 0] for j in range(n + 1) for i in range(n + 1)], float)
    return V, [[j * (n + 1) + i, j * (n + 1) + i + 1, (j + 1) * (n + 1) + i + 1, (j + 1) * (n + 1) + i]
               for j in range(n) for i in range(n)]


def tris(F):
    return [t for f in F for t in ([f[0], f[k], f[k + 1]] for k in range(1, len(f) - 1))]


def write(path, V, F, vertex_suffix="", negative=False):
    with open(path, "w") as fp:
        for p in V:
            fp.write("v %.17g %.17g %.17g%s\n" % (*p, vertex_suffix))
        for f in F:
            fp.write("f " + " ".join(str(x - len(V) if negative else x + 1) for x in f) + "\n")


class Basics(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def measure(self, inp, out, in_kw=None, out_kw=None):
        a, b = os.path.join(self.tmp.name, "in.obj"), os.path.join(self.tmp.name, "out.obj")
        write(a, *inp, **(in_kw or {}))
        write(b, *out, **(out_kw or {}))
        return metrics.compute(a, b, None, samples=20000)

    def test_cube_copy(self):
        V, Q = cube()
        m = self.measure((V, tris(Q)), (V, Q))
        t, val, q = m["topology"], m["valence"], m["quads"]
        self.assertEqual((m["faces"]["quads"], t["euler_output"], t["components"], t["boundary_loops"]), (6, 2, 1, 0))
        self.assertEqual((t["non_manifold_edges"], t["inconsistently_oriented_edges"]), (0, 0))
        self.assertTrue(t["same_as_input"])
        self.assertEqual((val["interior_vertices"], val["valence3"], val["irregular_interior"]), (8, 8, 8))
        self.assertEqual((q["sj_le_0"], q["sj_p1"], q["skew_p95"]), (0, 1.0, 0.0))
        self.assertEqual((m["flipped"]["faces_vs_input"], m["validity"]["folded_faces"], m["degenerate"]["faces"]), (0, 0, 0))
        self.assertEqual(m["creases"]["coverage"], 1.0)  # all 12 edges are 90° creases and kept
        self.assertLess(m["distance"]["hausdorff"], 1e-12)
        self.assertTrue(m["validity"]["islands_same_as_input"])

    def test_open_grid(self):
        V, Q = grid(4)
        m = self.measure((V, tris(Q)), (V, Q))
        self.assertEqual((m["topology"]["euler_output"], m["topology"]["boundary_loops"]), (1, 1))
        # 9 interior vertices of valence 4; 16 on the boundary, of which the 4 corners have valence 2
        self.assertEqual((m["valence"]["interior_vertices"], m["valence"]["irregular_interior"]), (9, 0))
        self.assertEqual(m["valence"]["boundary_hist"], {"2": 4, "3": 12})
        self.assertEqual(m["valence"]["irregular_boundary"], 4)

    def test_concave_quad_is_folded(self):
        V = np.array([[0, 0, 0], [2, 0, 0], [0.5, 0.5, 0], [0, 2, 0]], float)
        m = self.measure((V, [[0, 1, 3]]), (V, [[0, 1, 2, 3]]))
        self.assertEqual((m["quads"]["sj_le_0"], m["validity"]["folded_faces"], m["degenerate"]["faces"]), (1, 1, 0))

    def test_one_face_wound_the_other_way(self):
        V, Q = cube()
        m = self.measure((V, tris(Q)), (V, [Q[0][::-1]] + Q[1:]))
        self.assertEqual(m["topology"]["inconsistently_oriented_edges"], 4)
        self.assertEqual((m["flipped"]["faces_vs_input"], m["flipped"]["output_reversed"]), (1, False))

    def test_fin_and_pinch(self):
        V = np.array([[0, 0, 0], [0, 1, 0], [1, 0, 0], [1, 1, 0], [-1, 0, 0], [-1, 1, 0], [0, 0, 1], [0, 1, 1]], float)
        Q = [[0, 2, 3, 1], [0, 1, 5, 4], [0, 6, 7, 1]]  # three quads on the edge 0-1
        self.assertEqual(self.measure((V, tris(Q)), (V, Q))["topology"]["non_manifold_edges"], 1)
        V1, Q1 = cube()
        V2, Q2 = cube((1, 1, 1))  # corner (1,1,1) shared: vertex 7 of the first, 0 of the second
        remap = [7] + list(range(8, 15))
        V, Q = np.vstack([V1, V2[1:]]), Q1 + [[remap[i] for i in q] for q in Q2]
        m = self.measure((V, tris(Q)), (V, Q))
        self.assertEqual((m["validity"]["non_manifold_vertices"], m["topology"]["non_manifold_edges"]), (1, 0))
        self.assertEqual(m["validity"]["islands_input_non_manifold"], 1)

    def test_vertex_colours_and_negative_indices(self):
        # "v x y z r g b" (MeshLab, ZBrush, Blender with colours) was read as two vertices per line,
        # which gave a Hausdorff distance of 0.54 between a cube and itself; negative indices raised.
        V, Q = cube()
        m = self.measure((V, tris(Q)), (V, Q), in_kw={"vertex_suffix": " 0.5 0.25 1"}, out_kw={"negative": True})
        self.assertEqual((m["faces"]["quads"], m["topology"]["euler_input"], m["topology"]["euler_output"]), (6, 2, 2))
        self.assertLess(m["distance"]["hausdorff"], 1e-12)

    @unittest.expectedFailure
    def test_degenerate_quad_is_not_also_a_folded_quad(self):
        # A quad with a repeated vertex is degenerate (V4), and the CLI's folded count leaves it out,
        # but quads.sj_le_0 (V2) counts it too: one defect, two gates.
        V, Q = grid(2)
        m = self.measure((V, tris(Q)), (V, [Q[0], [Q[1][0], Q[1][1], Q[1][1], Q[1][3]]]))
        self.assertEqual((m["degenerate"]["faces"], m["validity"]["folded_faces"]), (1, 0))  # holds today
        self.assertEqual(m["quads"]["sj_le_0"], 0)

    @unittest.expectedFailure
    def test_touching_islands_copied_exactly_are_clean(self):
        # Two cubes stacked face to face, output = input: each output face takes the nearest input
        # triangle of either island, so one face of the shared plane counts as flipped and its area as
        # a collapsed thin part. Matching faces within their own island (TODO 1.13 F3) fixes both.
        V1, Q1 = cube()
        V2, Q2 = cube((0, 0, 1))
        V, Q = np.vstack([V1, V2]), Q1 + [[i + 8 for i in q] for q in Q2]
        m = self.measure((V, tris(Q)), (V, Q))
        self.assertEqual((m["flipped"]["faces_vs_input"], m["validity"]["area_wrong_side"]), (0, 0.0))


if __name__ == "__main__":
    unittest.main()
