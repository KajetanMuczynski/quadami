#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Re-embed drawn guide loops as smooth curves cut into the mesh (research prototype, TODO 5.7).

    clean_guides.py IN.obj OUT.obj [--width W] [--band B]

A drawn guide is a path of input edges: a staircase. This replaces every closed guide loop (`l a b`
lines whose vertices all have two marked edges) by the curve it stands for: the loop is averaged over
an arc length W (default: 4 median guide edges), and the mesh is cut along the zero set of the signed
side distance to that smooth curve, measured in the surface (offset from the nearest curve point along
vertex normal x curve tangent), inside a band of B around it (default: 8 median guide edges). Every
crossed triangle is split in three; the cut edges become the new `l` lines. Open guides are dropped
with a warning. Prints one line per loop.
"""
import argparse
import collections
import os
import sys

import numpy as np
from scipy.spatial import cKDTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import guides_report as gr  # noqa: E402


def vertex_normals(points, triangles):
    t = np.asarray(triangles)
    face = np.cross(points[t[:, 1]] - points[t[:, 0]], points[t[:, 2]] - points[t[:, 0]])
    normals = np.zeros_like(points)
    for k in range(3):
        np.add.at(normals, t[:, k], face)
    return normals / np.maximum(np.linalg.norm(normals, axis=1), 1e-30)[:, None]


def cut_along(points, triangles, curve, band):
    """(points, triangles, cut edges) with the closed curve cut in, or None when the cut does not close."""
    samples = gr.sample(curve, 0.05 * band, True)[:-1]
    tangent = np.roll(samples, -1, axis=0) - np.roll(samples, 1, axis=0)
    tangent /= np.maximum(np.linalg.norm(tangent, axis=1), 1e-30)[:, None]
    distance, nearest = cKDTree(samples).query(points)
    inside = distance < band
    side = np.cross(vertex_normals(points, triangles), tangent[nearest])
    side /= np.maximum(np.linalg.norm(side, axis=1), 1e-30)[:, None]
    level = ((points - samples[nearest]) * side).sum(1)
    level[np.abs(level) < 1e-7] = 1e-7
    out = [tuple(p) for p in points]
    on_edge, faces, segments = {}, [], []

    def crossing(a, b):
        key = (min(a, b), max(a, b))
        if key not in on_edge:
            t = level[a] / (level[a] - level[b])
            on_edge[key] = len(out)
            out.append(tuple(points[a] + t * (points[b] - points[a])))
        return on_edge[key]

    for tri in triangles:
        above = [level[v] > 0 for v in tri]
        if all(above) or not any(above) or not all(inside[v] for v in tri):
            faces.append(tuple(tri))
            continue
        lone = [k for k in range(3) if above[k] != above[(k + 1) % 3] and above[k] != above[(k + 2) % 3]][0]
        p, q, r = tri[lone], tri[(lone + 1) % 3], tri[(lone + 2) % 3]
        m1, m2 = crossing(p, q), crossing(p, r)
        faces += [(p, m1, m2), (m1, q, r), (m1, r, m2)]
        segments.append((m1, m2))
    around = collections.defaultdict(list)
    for a, b in segments:
        around[a].append(b)
        around[b].append(a)
    seen, closed = set(), []
    for start in around:
        if start in seen:
            continue
        stack, component = [start], []
        while stack:
            v = stack.pop()
            if v in seen:
                continue
            seen.add(v)
            component.append(v)
            stack += around[v]
        if all(len(around[v]) == 2 for v in component):
            closed.append(component)
    if not closed:
        return None
    keep = set(max(closed, key=len))
    return np.array(out), faces, [(a, b) for a, b in segments if a in keep]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--width", type=float, help="arc length the drawn loop is averaged over")
    ap.add_argument("--band", type=float, help="distance from the curve inside which the mesh is cut")
    a = ap.parse_args()
    points, faces, lines = gr.load_obj(a.input)
    triangles = [(f[0], f[k], f[k + 1]) for f in faces for k in range(1, len(f) - 1)]
    loops, components = gr.guide_loops(lines)
    if components != len(loops):
        print("%d guide(s) that are not closed loops dropped" % (components - len(loops)), file=sys.stderr)
    curves = []
    for cycle in loops:
        drawn = points[cycle]
        edge = float(np.median(np.linalg.norm(np.roll(drawn, -1, axis=0) - drawn, axis=1)))
        curves.append((gr.smoothed(drawn, a.width or 4.0 * edge), a.band or 8.0 * edge, len(cycle)))
    guide, failed = [], 0
    for index, (curve, band, drawn_edges) in enumerate(curves):
        result = cut_along(points, triangles, curve, band)
        if result is None:
            print("loop %d: the cut does not close" % index, file=sys.stderr)
            failed += 1
            continue
        points, triangles, edges = result
        guide += edges
        lengths = np.array([np.linalg.norm(points[x] - points[y]) for x, y in edges])
        print("loop %d: %d drawn edges -> %d cut edges, length %.4g, shortest %.3g" % (
            index, drawn_edges, len(edges), lengths.sum(), lengths.min()), file=sys.stderr)
    with open(a.output, "w") as f:
        for p in points:
            f.write("v %.9g %.9g %.9g\n" % tuple(p))
        for x, y, z in triangles:
            f.write("f %d %d %d\n" % (x + 1, y + 1, z + 1))
        for x, y in guide:
            f.write("l %d %d\n" % (x + 1, y + 1))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
