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

"""Tests of the symmetry settings and asymmetry check (symmetry.py), without Blender."""

import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "quadami"))
import symmetry  # noqa: E402


def sphere(center, count=3000, seed=1):
    """Points on a unit sphere about center, and the distance to it."""
    rng = np.random.default_rng(seed)
    points = rng.normal(size=(count, 3))
    points /= np.linalg.norm(points, axis=1)[:, None]
    center = np.asarray(center, dtype=np.float64)

    def nearest(p):
        return abs(float(np.linalg.norm(p - center)) - 1.0)

    return points + center, nearest


class PlanesTest(unittest.TestCase):
    def test_text(self):
        self.assertEqual(symmetry.planes_text({0}, set()), "x")
        self.assertEqual(symmetry.planes_text({2, 0}, {2}), "x,-z")
        self.assertEqual(symmetry.center_text((0, 0.5, -1)), "0.0,0.5,-1.0")


class CheckTest(unittest.TestCase):
    def test_symmetric(self):
        points, nearest = sphere((0.0, 0.0, 0.0))
        rows = symmetry.check(points, {0, 1}, (0.0, 0.0, 0.0), nearest)
        self.assertEqual([r["axis"] for r in rows], [0, 1])
        for row in rows:
            self.assertLess(row["max"], 1e-9)
            self.assertIsNone(row["fit"])
        self.assertEqual(symmetry.check_lines(rows, set()),
                         [("Symmetric across X", False), ("Symmetric across Y", False)])

    def test_off_origin(self):
        # Symmetric about x = 0.3, checked about the origin: a warning, and
        # the fitted plane found.
        points, nearest = sphere((0.3, 0.0, 0.0))
        rows = symmetry.check(points, {0}, (0.0, 0.0, 0.0), nearest)
        self.assertGreater(rows[0]["max"], symmetry.WARN)
        self.assertAlmostEqual(rows[0]["fit"], 0.3, places=3)
        lines = symmetry.check_lines(rows, {0})
        self.assertTrue(lines[0][1])
        self.assertIn("-X side", lines[0][0])
        self.assertIn("Fit Plane", lines[1][0])
        self.assertEqual(symmetry.fitted_center(rows, (0.0, 0.0, 0.0))[0], rows[0]["fit"])

    def test_empty(self):
        self.assertEqual(symmetry.check(np.zeros((0, 3)), {0}, (0.0, 0.0, 0.0), lambda p: 0.0), [])


class HintTest(unittest.TestCase):
    def test_hint(self):
        self.assertIn("Mirror modifier (X)", symmetry.object_hint({0}, set(), set()))
        self.assertIn("own symmetry is on for Z", symmetry.object_hint(set(), {2}, {0}))
        self.assertIsNone(symmetry.object_hint({0}, {0}, {0}))


if __name__ == "__main__":
    unittest.main()
