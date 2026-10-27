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
"""The external sets (external.json, external_thin.json, fetch_external.py): public licenses only,
pinned sources, and readers that turn OBJ, PLY and glb (plain, quantized, Draco) into the same
triangle mesh."""
import json
import os
import re
import struct
import sys
import tempfile
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
BENCH = os.path.dirname(HERE)
sys.path.insert(0, BENCH)
import fetch_external as fx  # noqa: E402

PUBLIC_LICENSES = ("CC0 1.0", "CC BY 3.0", "CC BY 4.0", "CC BY-SA 3.0", "Public domain")
PINNED_URL = re.compile(r"^https://raw\.githubusercontent\.com/[^/]+/[^/]+/[0-9a-f]{40}/")

# A closed tetrahedron, outward-facing.
V = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], dtype=np.float64)
F = np.array([[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]])


def glb_bytes(v, f, quantized=False):
    """One mesh node, positions and uint32 indices in one buffer. quantized: positions as
    normalized int16 with the dequantizing scale on the node (KHR_mesh_quantization)."""
    pos = (v * 32767).round().astype("<i2") if quantized else v.astype("<f4")
    pos = pos.tobytes()
    pos += b"\0" * (-len(pos) % 4)
    idx = f.astype("<u4").tobytes()
    node = {"mesh": 0, "translation": [0, 0, 0]}
    position = {"bufferView": 0, "componentType": 5126, "count": len(v), "type": "VEC3"}
    if quantized:
        node["scale"] = [2, 2, 2]
        position.update(componentType=5122, normalized=True)
    doc = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [node],
           "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
           "buffers": [{"byteLength": len(pos) + len(idx)}],
           "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(pos)},
                           {"buffer": 0, "byteOffset": len(pos), "byteLength": len(idx)}],
           "accessors": [position,
                         {"bufferView": 1, "componentType": 5125, "count": f.size, "type": "SCALAR"}]}
    return glb_wrap(doc, pos + idx)


def glb_wrap(doc, binary):
    js = json.dumps(doc).encode()
    js += b" " * (-len(js) % 4)
    binary += b"\0" * (-len(binary) % 4)
    chunks = struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(binary), 0x004E4942) + binary
    return struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks


class Manifest(unittest.TestCase):
    def test_public_and_pinned(self):
        corpus = json.load(open(os.path.join(BENCH, "corpus.json")))
        all_ids = []
        for name, setname in (("external.json", "external"), ("external_thin.json", "external_thin")):
            manifest = json.load(open(os.path.join(BENCH, name)))
            ids = [m["id"] for m in manifest["meshes"]]
            all_ids += ids
            for m in manifest["meshes"]:
                self.assertTrue(m["public"], m["id"])
                self.assertIn(m["license"], PUBLIC_LICENSES, m["id"])
                for key in ("author", "source", "category"):
                    self.assertTrue(m.get(key), f"{m['id']}: {key}")
                for src in m["files"]:
                    self.assertRegex(src["url"], PINNED_URL, m["id"])
                    self.assertRegex(src["sha256"], r"^[0-9a-f]{64}$", m["id"])
            in_set = {m["id"]: m["file"] for m in corpus["meshes"] if m.get("set") == setname}
            self.assertEqual(sorted(in_set), sorted(ids))
            for mid, f in in_set.items():
                self.assertEqual(f, f"{setname}/{mid}.obj")
        self.assertEqual(len(all_ids), len(set(all_ids)))
        # Opt-in until the thin-part work decides: no default targets, so ab.py lane skips it.
        self.assertEqual(corpus["sets"]["external_thin"]["targets"], [])


class Readers(unittest.TestCase):
    def assert_tetra(self, v, f):
        self.assertEqual(len(v), 4)
        self.assertEqual(len(f), 4)
        r = fx.check(v, f)
        self.assertEqual((r["islands"], r["closed_islands"], r["boundary_edges"]), (1, 1, 0))
        self.assertEqual(r["genus_closed"], [0])

    def test_obj_groups_and_polygons(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "m.obj")
            with open(p, "w") as fp:
                fp.write("".join(f"v {x} {y} {z}\n" for x, y, z in V))
                fp.write("g body\nf 1/1 3/2 2/3\nf 1 2 4\nf 1 4 3\nf 2 3 4\ng helper\nf 1 2 3 4\n")
            v, f = fx.read_obj(p, ["body"])
            self.assert_tetra(v, f)
            _, f_all = fx.read_obj(p)
            self.assertEqual(len(f_all), 6)   # the helper quad becomes two triangles

    def test_ply_ascii_and_binary(self):
        head = "ply\nformat {}\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\n" \
               "element face 4\nproperty list uchar int vertex_indices\nend_header\n"
        ascii_body = "".join(f"{x} {y} {z}\n" for x, y, z in V) + "".join(f"3 {a} {b} {c}\n" for a, b, c in F)
        v, f = fx.read_ply((head.format("ascii 1.0") + ascii_body).encode())
        self.assert_tetra(v, f)
        binary = V.astype(">f4").tobytes() + b"".join(struct.pack(">Biii", 3, *t) for t in F.tolist())
        v, f = fx.read_ply(head.format("binary_big_endian 1.0").encode() + binary)
        self.assert_tetra(v, f)
        np.testing.assert_array_equal(f, F)

    def test_glb_welds_seams(self):
        # Every face gets its own vertices, as a glTF exporter does at UV seams.
        v_split = V[F.ravel()]
        f_split = np.arange(12).reshape(4, 3)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "m.glb")
            with open(p, "wb") as fp:
                fp.write(glb_bytes(v_split, f_split))
            v, f = fx.read_gltf(p)
        self.assert_tetra(v, f)

    def test_glb_quantized_positions(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "m.glb")
            with open(p, "wb") as fp:
                fp.write(glb_bytes(V * 0.5, F, quantized=True))
            v, f = fx.read_gltf(p)
        self.assert_tetra(v, f)
        np.testing.assert_allclose(np.sort(v, axis=0), np.sort(V, axis=0), atol=1e-4)

    def test_glb_draco(self):
        try:
            import DracoPy
        except ImportError:
            self.skipTest("DracoPy not installed")
        data = DracoPy.encode(V[F.ravel()].astype(np.float32), np.arange(12, dtype=np.uint32))
        doc = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
               "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "extensions": {
                   "KHR_draco_mesh_compression": {"bufferView": 0, "attributes": {"POSITION": 0}}}}]}],
               "buffers": [{"byteLength": len(data) + (-len(data) % 4)}],
               "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(data)}],
               "accessors": [{"componentType": 5126, "count": 12, "type": "VEC3"},
                             {"componentType": 5125, "count": 12, "type": "SCALAR"}]}
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "m.glb")
            with open(p, "wb") as fp:
                fp.write(glb_wrap(doc, data))
            v, f = fx.read_gltf(p)
        self.assert_tetra(v, f)


if __name__ == "__main__":
    unittest.main()
