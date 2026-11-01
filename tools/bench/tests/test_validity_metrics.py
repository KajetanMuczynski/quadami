# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""metrics.py's validity numbers on small meshes with a known answer.

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


def grid(n, m, wrap_u, wrap_v, point, quads=True, offset=0):
    """Vertices and faces of an n x m grid on a parametrised surface, optionally closed in u or v."""
    nu, nv = n if wrap_u else n + 1, m if wrap_v else m + 1
    V = np.array([point(i / n, j / m) for i in range(nu) for j in range(nv)])
    F = []
    for i in range(n):
        for j in range(m):
            a, b = i * nv + j, ((i + 1) % nu) * nv + j
            c, d = ((i + 1) % nu) * nv + (j + 1) % nv, i * nv + (j + 1) % nv
            F += [[a, b, c, d]] if quads else [[a, b, c], [a, c, d]]
    return V, [[x + offset for x in f] for f in F]


def torus_point(u, v, R=1.0, r=0.3, shift=0.0):
    u, v = 2 * np.pi * u, 2 * np.pi * v
    return [(R + r * np.cos(v)) * np.cos(u) + shift, (R + r * np.cos(v)) * np.sin(u), r * np.sin(v)]


def sheet(nx, ny, ly, z=0.0):
    """Quads of the rectangle [0, 1] x [0, ly] at height z, normals up."""
    x, y = np.meshgrid(np.linspace(0, 1, nx + 1), np.linspace(0, ly, ny + 1), indexing="ij")
    V = np.stack([x.ravel(), y.ravel(), np.full(x.size, z)], 1)
    return V, [[a * (ny + 1) + b, (a + 1) * (ny + 1) + b, (a + 1) * (ny + 1) + b + 1, a * (ny + 1) + b + 1]
               for a in range(nx) for b in range(ny)]


def write(path, V, F):
    with open(path, "w") as fp:
        for p in V:
            fp.write("v %.17g %.17g %.17g\n" % tuple(p))
        for f in F:
            fp.write("f " + " ".join(str(x + 1) for x in f) + "\n")


def join(*parts):
    V, F = [], []
    for Vp, Fp in parts:
        F += [[x + len(V) for x in f] for f in Fp]
        V += list(Vp)
    return np.array(V), F


class Validity(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.inp = os.path.join(self.tmp.name, "in.obj")
        self.out = os.path.join(self.tmp.name, "out.obj")

    def tearDown(self):
        self.tmp.cleanup()

    def run_metrics(self, inp, out):
        write(self.inp, *inp)
        write(self.out, *out)
        return metrics.compute(self.inp, self.out, None, samples=20000)

    def torus_in(self, **kw):
        return grid(96, 48, True, True, lambda u, v: torus_point(u, v, **kw), quads=False)

    def torus_out(self, **kw):
        return grid(40, 16, True, True, lambda u, v: torus_point(u, v, **kw))

    def test_clean_torus_is_valid(self):
        r = self.run_metrics(self.torus_in(), self.torus_out())
        v = r["validity"]
        self.assertTrue(v["islands_same_as_input"])
        self.assertEqual((v["islands_dropped"], v["holes_added"], v["handles_lost"], v["non_manifold_vertices"]),
                         (0, 0, 0, 0))
        self.assertEqual(r["flipped"]["faces_vs_input"], 0)
        self.assertLess(v["area_uncovered"] + v["area_wrong_side"], 1e-3)

    def test_turned_input_patch_is_not_counted_as_flips(self):
        V, F = self.torus_in()
        patch = list(range(960))  # a tenth of the input exported with the other winding
        for t in patch:
            F[t] = [F[t][0], F[t][2], F[t][1]]
        r = self.run_metrics((V, F), self.torus_out())
        self.assertEqual(r["flipped"]["input_reoriented_triangles"], len(patch))
        self.assertGreater(r["flipped"]["faces_vs_raw_input"], 0)
        self.assertEqual(r["flipped"]["faces_vs_input"], 0)

    def test_real_flips_are_still_counted(self):
        Vin, Fin = self.torus_in()
        for t in range(960):
            Fin[t] = [Fin[t][0], Fin[t][2], Fin[t][1]]
        V, F = self.torus_out()
        turned = [3, 4, 100, 200, 333]
        for f in turned:
            F[f] = F[f][::-1]
        r = self.run_metrics((Vin, Fin), (V, F))
        self.assertEqual(r["flipped"]["faces_vs_input"], len(turned))
        self.assertEqual(r["flipped"]["faces_off_input_folds"], len(turned))  # no input fold on a torus
        self.assertFalse(r["flipped"]["output_reversed"])

    def test_flips_over_folded_input_are_counted_apart(self):
        # A strip with a pleat narrower than input_folds' radius (the band around x = 0.5 turned over in
        # place, as a brush leaves it); the output is flat, so its faces over the pleat point against the
        # input there. One more output face, far from the pleat, is turned for real.
        V, Q = sheet(400, 40, 0.1)
        F = [t for q in Q for t in ([q[0], q[1], q[2]], [q[0], q[2], q[3]])]
        band = np.abs(V[:, 0] - 0.5) < 0.005
        V[band, 0] = 1 - V[band, 0]
        V[band, 2] = 0.002
        Vo, Fo = sheet(41, 4, 0.1, 0.002)
        Fo[21] = Fo[21][::-1]  # x = 0.13
        f = self.run_metrics((V, F), (Vo, Fo))["flipped"]
        self.assertEqual(f["faces_off_input_folds"], 1)
        self.assertEqual(f["faces_over_input_folds"], 4)  # the column of output faces at x = 0.5
        self.assertEqual(f["faces_vs_input"], 5)

    def test_globally_reversed_output_is_not_flipped(self):
        V, F = self.torus_out()
        r = self.run_metrics(self.torus_in(), (V, [f[::-1] for f in F]))
        self.assertEqual(r["flipped"]["faces_vs_input"], 0)
        self.assertTrue(r["flipped"]["output_reversed"])

    def test_dropped_island(self):
        a = self.torus_in()
        b = grid(96, 48, True, True, lambda u, v: torus_point(u, v, shift=5.0), quads=False)
        r = self.run_metrics(join(a, b), self.torus_out())
        v = r["validity"]
        self.assertEqual((v["islands_input"], v["islands_output"], v["islands_dropped"]), (2, 1, 1))
        self.assertAlmostEqual(v["area_uncovered"], 0.5, delta=0.02)
        self.assertFalse(v["islands_same_as_input"])

    def test_island_shrunk_to_a_blob_is_dropped_not_same(self):
        # The second torus comes out as a much smaller torus at its centre: one output island of the
        # same topology votes for it, but none of its area lies within one output edge of the output.
        a, b = self.torus_in(), self.torus_in(shift=5.0)
        small = grid(40, 16, True, True, lambda u, v: torus_point(u, v, R=0.3, r=0.05, shift=5.0))
        r = self.run_metrics(join(a, b), join(self.torus_out(), small))
        t, v = r["topology"], r["validity"]
        self.assertTrue(t["same_as_input"])  # the global check cannot see it
        self.assertEqual((v["islands_dropped"], v["islands_topology_changed"], v["islands_merged"]), (1, 0, 0))
        self.assertFalse(v["islands_same_as_input"])

    def test_cut_handle_is_two_holes_and_a_lost_handle(self):
        # The output is the torus cut open across the tube: a cylinder with two boundary loops.
        out = grid(40, 16, False, True, lambda u, v: torus_point(0.98 * u, v))
        v = self.run_metrics(self.torus_in(), out)["validity"]
        self.assertEqual((v["holes_added"], v["handles_lost"], v["islands_topology_changed"]), (2, 1.0, 1))

    def test_handle_swapped_between_islands(self):
        # Global Euler characteristic, components and loops match, per island they do not: a torus
        # turned into a sphere-like cap pair is not available on a grid, so swap a torus and a
        # cylinder between two islands.
        torus = self.torus_in()
        tube = grid(96, 48, False, True, lambda u, v: torus_point(0.9 * u, v, shift=5.0), quads=False)
        out_torus = grid(40, 16, True, True, lambda u, v: torus_point(u, v, shift=5.0))
        out_tube = grid(40, 16, False, True, lambda u, v: torus_point(0.9 * u, v))
        r = self.run_metrics(join(torus, tube), join(out_tube, out_torus))
        t, v = r["topology"], r["validity"]
        self.assertTrue(t["same_as_input"])  # the global check cannot see it
        self.assertEqual(v["islands_topology_changed"], 2)
        self.assertEqual((v["handles_lost"], v["handles_added"]), (1.0, 1.0))
        self.assertFalse(v["islands_same_as_input"])

    def test_non_manifold_input_island_is_not_judged(self):
        # A torus with a fin glued onto three of its edges' worth of a strip: edges of three triangles.
        V, F = self.torus_in()
        V = np.vstack([V, [[1.0, 0.0, 1.0]]])
        F = F + [[F[0][0], F[0][1], len(V) - 1]]
        out = grid(40, 16, False, True, lambda u, v: torus_point(0.98 * u, v))
        v = self.run_metrics((V, F), out)["validity"]
        self.assertEqual(v["islands_input_non_manifold"], 1)
        self.assertEqual((v["holes_added"], v["handles_lost"], v["islands_topology_changed"]), (0, 0.0, 0))

    def test_collapsed_thin_part_is_on_the_wrong_side(self):
        # A closed slab thinner than a quad, remeshed as one sheet facing up.
        def slab(u, v):
            a, b = 2 * np.pi * u, np.pi * (v - 0.5)
            return [np.cos(b) * np.cos(a), np.cos(b) * np.sin(a), 0.002 * np.sin(b)]
        Vin, Fin = grid(96, 48, True, False, slab, quads=False)
        sheet = grid(20, 20, False, False, lambda u, v: [2 * u - 1, 2 * v - 1, 0.0])
        v = self.run_metrics((Vin, Fin), sheet)["validity"]
        self.assertAlmostEqual(v["area_wrong_side"], 0.5, delta=0.05)

    def test_non_manifold_vertex_and_folded_pentagon(self):
        V = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [-1, 0, 0], [-1, -1, 0], [0, -1, 0]], float)
        self.assertEqual(metrics.nonmanifold_vertices([[0, 1, 2, 3], [0, 4, 5, 6]], len(V)), 1)
        self.assertEqual(metrics.nonmanifold_vertices([[0, 1, 2, 3], [0, 3, 4, 5]], len(V)), 0)
        P = np.array([[0, 0, 0], [2, 0, 0], [2, 2, 0], [1, 0.5, 0], [0, 2, 0]], float)  # reflex corner at 3
        faces = [[0, 1, 2, 3, 4]]
        self.assertEqual(metrics.folded_faces(P, faces, np.array([5])), 1)
        self.assertEqual(metrics.folded_faces(V, [[0, 1, 2, 3]], np.array([4])), 0)


class TurnedOver(unittest.TestCase):
    def test_only_clusters_with_a_turned_edge_count_as_folds(self):
        # Two flagged clusters on a strip: one around the in-place pleat (edges turned by ~180 degrees),
        # one on smooth flat triangles (a stand-in for a tight but valid curl).
        V, Q = sheet(400, 40, 0.1)
        F = np.array([t for q in Q for t in ([q[0], q[1], q[2]], [q[0], q[2], q[3]])])
        band = np.abs(V[:, 0] - 0.5) < 0.005
        V[band, 0] = 1 - V[band, 0]
        V[band, 2] = 0.002
        C = V[F].mean(1)
        mask = (np.abs(C[:, 0] - 0.5) < 0.01) | (np.abs(C[:, 0] - 0.2) < 0.01)
        adj = metrics.face_adjacency(F)
        adj = (adj + adj.T).tocsr()
        kept = metrics.turned_over(V, F, mask, adj)
        self.assertTrue(kept[np.abs(C[:, 0] - 0.5) < 0.005].all())
        self.assertFalse(kept[np.abs(C[:, 0] - 0.2) < 0.01].any())


class OrientLikeCore(unittest.TestCase):
    def test_minority_patch_is_turned_and_consistent_input_kept(self):
        V, F = grid(20, 10, True, True, torus_point, quads=False)
        F = np.array(F)
        same, turned, bad = metrics.orient_like_core(V, F)
        self.assertIs(same, F)
        self.assertEqual((turned, bad), (0, 0))
        G = F.copy()
        G[:7] = G[:7][:, [0, 2, 1]]
        fixed, turned, bad = metrics.orient_like_core(V, G)
        self.assertEqual((turned, bad), (7, 0))
        # Same winding as the untouched triangles, up to rotation of the corners.
        canon = lambda T: {tuple(np.roll(t, -int(np.argmin(t)))) for t in T}  # noqa: E731
        self.assertEqual(canon(fixed), canon(F))


if __name__ == "__main__":
    unittest.main()
