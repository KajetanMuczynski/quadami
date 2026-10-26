# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""ci/output_gate.py's coverage check (TARGETS H4): a dropped island and a part collapsed into a blob
fail, the input itself passes."""
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import output_gate  # noqa: E402


def two_spheres(scale_small=1.0):
    mesh = output_gate.Mesh()
    output_gate.sphere(mesh, "big", stacks=24, slices=48)
    output_gate.sphere(mesh, "small", radius=0.35 * scale_small, center=(2.0, 0.0, 0.0), stacks=12, slices=24)
    return mesh


class Coverage(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.out = os.path.join(self.tmp.name, "out.obj")

    def tearDown(self):
        self.tmp.cleanup()

    def share(self, inp, out):
        out.write(self.out)
        return output_gate.uncovered_share(inp, self.out)

    def test_input_as_its_own_output_is_covered(self):
        self.assertEqual(self.share(two_spheres(), two_spheres()), 0.0)

    def test_dropped_island_is_uncovered(self):
        out = output_gate.Mesh()
        output_gate.sphere(out, "big", stacks=24, slices=48)
        self.assertEqual(self.share(two_spheres(), out), 1.0)

    def test_island_collapsed_into_a_blob_is_uncovered(self):
        # Same genus and island count as the input: only coverage sees it.
        self.assertGreater(self.share(two_spheres(), two_spheres(scale_small=0.2)), 0.9)


if __name__ == "__main__":
    unittest.main()
