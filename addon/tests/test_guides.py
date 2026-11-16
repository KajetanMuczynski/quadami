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

"""Tests of the guide paths (guides.py), without Blender."""

import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "quadami"))
import guides  # noqa: E402


def grid(n):
    """An n x n vertex grid in the plane, quads split along one diagonal:
    positions and edges."""
    positions = np.array([(x, y, 0.0) for y in range(n) for x in range(n)], dtype=float)
    edges = set()
    for y in range(n - 1):
        for x in range(n - 1):
            a, b, c, d = y * n + x, y * n + x + 1, (y + 1) * n + x + 1, (y + 1) * n + x
            for e in ((a, b), (b, c), (c, d), (d, a), (a, c)):
                edges.add((min(e), max(e)))
    return positions, np.array(sorted(edges))


class EdgeGraphTest(unittest.TestCase):
    def setUp(self):
        self.n = 8
        self.positions, self.edges = grid(self.n)
        self.graph = guides.EdgeGraph(self.positions, self.edges)

    def length(self, path):
        a, b = self.edges[path, 0], self.edges[path, 1]
        return np.linalg.norm(self.positions[a] - self.positions[b], axis=1).sum()

    def test_shortest_path_takes_the_diagonal(self):
        path = self.graph.shortest_path(0, self.n * self.n - 1)
        self.assertEqual(len(path), self.n - 1)
        self.assertAlmostEqual(self.length(path), (self.n - 1) * 2 ** 0.5)

    def test_same_vertex_and_unreachable(self):
        self.assertEqual(self.graph.shortest_path(5, 5), [])
        positions = np.vstack([self.positions, [[100.0, 0.0, 0.0]]])
        graph = guides.EdgeGraph(positions, self.edges)
        self.assertIsNone(graph.shortest_path(0, len(positions) - 1))

    def test_closed_stroke_is_one_loop(self):
        n = self.n
        corners = [n + 1, n + 6, 6 * n + 6, 6 * n + 1]
        path = self.graph.path_through(corners, closed=True)
        flags = np.zeros(len(self.edges), dtype=bool)
        flags[path] = True
        self.assertEqual(guides.count_loops(self.edges, flags), (1, 0))

    def test_stroke_that_doubles_back_leaves_no_spur(self):
        # Out along the bottom row and back: every edge is used twice.
        self.assertEqual(self.graph.path_through([0, 5, 0]), [])
        # A loop with a spur drawn out and back along it keeps only the loop.
        n = self.n
        legs = [self.graph.shortest_path(a, b) for a, b in
                ((n + 1, n + 3), (n + 3, n + 5), (n + 5, n + 3), (n + 3, 3 * n + 3), (3 * n + 3, n + 1))]
        flags = np.zeros(len(self.edges), dtype=bool)
        flags[guides.join_legs(legs)] = True
        self.assertEqual(guides.count_loops(self.edges, flags), (1, 0))

    def test_count_loops_open_and_closed(self):
        n = self.n
        flags = np.zeros(len(self.edges), dtype=bool)
        flags[self.graph.path_through([0, 3])] = True
        flags[self.graph.path_through([4 * n + 1, 4 * n + 4, 6 * n + 4], closed=True)] = True
        self.assertEqual(guides.count_loops(self.edges, flags), (1, 1))
        self.assertEqual(guides.count_loops(self.edges, np.zeros(len(self.edges), dtype=bool)), (0, 0))


if __name__ == "__main__":
    unittest.main()
