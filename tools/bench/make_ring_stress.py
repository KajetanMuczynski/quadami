#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Write the "rings" stress sets for closed guide loops (TODO 5.7, research): guide rings where a head
sculpt has them, and rings of falling size. Guides are drawn as in make_guides.py (paths of input
edges through points on the curve). Public and procedural; stands in for the private head.

    python make_ring_stress.py --out $AR_WORK/meshes/rings          # numpy only
    AR_CORPUS=$AR_WORK/meshes/rings/corpus.json ab.py run HEAD --set rings_head --replicates 5

Sets (written to <out>/corpus.json, the shape of tools/bench/corpus.json):

  rings_head   a head stand-in (82k triangles): two eye sockets, a mouth groove, a nose, brow ridges
               and a neck, with a ring around each eye, an elongated ring around the mouth and a ring
               around the neck; smooth, and with sculpt-like noise. 1k, 3k, 6k, 12k quads.
  rings_size   one ring of radius 0.06 to 0.30 around a bump and around a dent of a bumpy sphere:
               from about 3 to 30 quad edges around at 1k and 3k quads. How small a ring can be
               before its inside has no room for the four poles a row away from it.
"""
import argparse
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_guides as mg  # noqa: E402

guides = mg.guides


def unit(v):
    v = np.asarray(v, dtype=float)
    return v / np.linalg.norm(v)


def gaussian(d, direction, width):
    return np.exp(-((1 - d @ unit(direction)) / width) ** 2)


EYES = [unit([0.38, 0.86, 0.30]), unit([-0.38, 0.86, 0.30])]
MOUTH = unit([0.0, 0.90, -0.42])
NECK_Z = -1.45


def head(vertices, noise=0.0):
    """Star-shaped head stand-in: +y is the face, -z the neck."""
    d = vertices / np.linalg.norm(vertices, axis=1)[:, None]
    radius = np.ones(len(d))
    for eye in EYES:
        radius -= 0.10 * gaussian(d, eye, 0.035)                       # sockets
        radius += 0.05 * gaussian(d, unit(eye + [0.0, 0.0, 0.22]), 0.02)  # brow
    radius += 0.20 * gaussian(d, unit([0.0, 1.0, 0.02]), 0.012)          # nose
    # Mouth: a groove elongated along x.
    across = d @ np.array([1.0, 0.0, 0.0])
    radius -= 0.06 * gaussian(d, MOUTH, 0.03) * np.exp(-(across / 0.45) ** 2) * 2.0
    radius += 0.10 * gaussian(d, unit([0.0, 0.75, -0.75]), 0.03)          # chin
    # Neck: the south cap pulled out into a tube.
    radius += 1.1 * np.exp(-((1 + d[:, 2]) / 0.16) ** 2)
    if noise > 0.0:
        rng = np.random.default_rng(7)
        waves = np.zeros(len(d))
        for _ in range(40):
            k = rng.normal(size=3) * 9.0
            waves += np.sin(d @ k + rng.uniform(0, 2 * math.pi))
        radius += noise * waves / math.sqrt(40)
    return d * radius[:, None]


def ellipse(center, normal, major_axis, a, b, count):
    normal = unit(normal)
    u = unit(np.asarray(major_axis, dtype=float) - normal * np.dot(major_axis, normal))
    w = np.cross(normal, u)
    t = np.linspace(0, 2 * math.pi, count, endpoint=False)[:, None]
    return np.asarray(center) + a * np.cos(t) * u + b * np.sin(t) * w


def neck_ring(vertices, z):
    """The level curve at height z of the neck: points around the axis at the surface radius."""
    t = np.linspace(0, 2 * math.pi, 300, endpoint=False)
    out = []
    for angle in t:
        # Star-shaped: walk along the ray of direction (r cos, r sin, z) / |.| until the surface.
        best, best_error = None, 1e9
        for r in np.linspace(0.05, 1.2, 60):
            p = np.array([r * math.cos(angle), r * math.sin(angle), z])
            v = int(np.argmin(np.linalg.norm(vertices - p, axis=1)))
            error = np.linalg.norm(vertices[v] - p)
            if error < best_error:
                best, best_error = vertices[v], error
        out.append(best)
    return np.array(out)


def build():
    meshes = {}
    base, faces = mg.icosphere(6)
    for name, noise, text in (("rings_head", 0.0, "smooth"), ("rings_head_noisy", 0.012, "with sculpt-like noise")):
        v = head(base, noise)
        curves = [mg.on_surface(mg.circle(eye, eye, 0.17, 200), v) for eye in EYES]
        curves.append(mg.on_surface(ellipse(MOUTH, MOUTH, [1.0, 0.0, 0.0], 0.34, 0.11, 240), v))
        curves.append(neck_ring(v, NECK_Z))
        meshes[name] = ("rings_head", v, faces, curves,
                        f"head stand-in, {text}: rings around both eyes, the mouth and the neck")
    base, faces = mg.icosphere(5)
    v = mg.bumpy(base)
    bump = unit([0.3, 0.8, 0.5])
    dent = unit([-0.7, 0.1, 0.7])
    for place, direction in (("bump", bump), ("dent", dent)):
        for radius in (0.06, 0.09, 0.12, 0.18, 0.24, 0.30):
            ring = mg.on_surface(mg.circle(direction, direction, radius, 200), v)
            meshes[f"rings_size_{place}_r{int(round(radius * 100)):02d}"] = (
                "rings_size", v, faces, [ring], f"bumpy sphere, one ring of radius {radius} around a {place}")
    return meshes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    folder = os.path.basename(os.path.normpath(a.out))
    corpus = {"_comment": "Generated by tools/bench/make_ring_stress.py; use with AR_CORPUS.",
              "sets": {"rings_head": {"targets": [1000, 3000, 6000, 12000], "resolutions": ["quality"]},
                       "rings_size": {"targets": [1000, 3000], "resolutions": ["quality"]}},
              "meshes": []}
    for name, (setname, vertices, faces, curves, category) in build().items():
        vertices = vertices.astype(np.float32).astype(np.float64)
        edges = mg.edges_of(faces)
        graph = guides.EdgeGraph(vertices, edges)
        guide_edges = []
        for curve in curves:
            guide_edges += mg.guide_along(vertices, graph, curve)
        flags = np.zeros(len(edges), dtype=bool)
        flags[guide_edges] = True
        closed, open_ = guides.count_loops(edges, flags)
        mg.write(os.path.join(a.out, name + ".obj"), vertices, faces, edges, sorted(set(guide_edges)))
        corpus["meshes"].append({"id": name, "file": f"{folder}/{name}.obj", "category": category, "set": setname})
        print(f"{name}: {len(vertices)} vertices, {len(faces)} triangles, {len(curves)} curves -> "
              f"{closed} closed guides, {open_} open")
    with open(os.path.join(a.out, "corpus.json"), "w") as f:
        json.dump(corpus, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
