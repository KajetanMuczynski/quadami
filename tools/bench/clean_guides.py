#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Re-embed drawn guide loops as smooth curves cut into the mesh (research prototype, TODO 5.7).

    clean_guides.py IN.obj OUT.obj [--width W] [--band B]

A drawn guide is a path of input edges: a staircase. This replaces every closed guide loop (`l a b`
lines whose vertices all have two marked edges) by the curve it stands for: the loop is averaged over
an arc length W (default: 4 median guide edges), and the mesh is cut along the zero set of the signed
side distance to that smooth curve, measured in the surface (offset from the nearest curve point along
vertex normal x curve tangent), inside a band of B around it (default: 8 median guide edges). The cut
runs through a vertex that is within 5 % of a median mesh edge of it, so it leaves no degenerate triangle. Every
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


def cut_along(points, triangles, curve, band, snap):
    """(points, triangles, cut edges) with the closed curve cut in, or None when the cut does not close."""
    # The signed side distance to the curve: from the closest point of the polyline (exact, on the
    # two segments at the nearest sample, so the level is continuous along the curve), measured
    # across it in the surface. A vertex closer to the curve than `snap` is taken to lie on it: the
    # cut then runs through that vertex and leaves no degenerate triangle beside it.
    samples = gr.sample(curve, 0.05 * band, True)[:-1]
    count = len(samples)
    distance, nearest = cKDTree(samples).query(points)
    inside = distance < band
    closest = samples[nearest].copy()
    along = np.roll(samples, -1, axis=0)[nearest] - np.roll(samples, 1, axis=0)[nearest]
    best = np.full(len(points), np.inf)
    for shift in (-1, 0):
        a = samples[(nearest + shift) % count]
        b = samples[(nearest + shift + 1) % count]
        ab = b - a
        t = np.clip(((points - a) * ab).sum(1) / np.maximum((ab * ab).sum(1), 1e-30), 0.0, 1.0)
        foot = a + t[:, None] * ab
        d = np.linalg.norm(points - foot, axis=1)
        closer = d < best
        best[closer] = d[closer]
        closest[closer] = foot[closer]
    along /= np.maximum(np.linalg.norm(along, axis=1), 1e-30)[:, None]
    # The surface normal at each curve sample: the vertex normals within the band's fifth of it,
    # averaged, so the side direction does not follow the relief vertex by vertex.
    normals = vertex_normals(points, triangles)
    tree = cKDTree(points)
    curve_normal = np.array([normals[near].sum(axis=0) if near else np.zeros(3)
                             for near in tree.query_ball_point(samples, 0.2 * band)])
    for _ in range(4):
        curve_normal = curve_normal + np.roll(curve_normal, 1, axis=0) + np.roll(curve_normal, -1, axis=0)
    curve_normal /= np.maximum(np.linalg.norm(curve_normal, axis=1), 1e-30)[:, None]
    side = np.cross(curve_normal[nearest], along)
    side /= np.maximum(np.linalg.norm(side, axis=1), 1e-30)[:, None]
    level = ((points - closest) * side).sum(1)
    level[np.abs(level) < snap] = 0.0
    out = [tuple(p) for p in points]
    on_edge, faces, segments = {}, [], set()

    def crossing(a, b):
        key = (min(a, b), max(a, b))
        if key not in on_edge:
            t = level[a] / (level[a] - level[b])
            on_edge[key] = len(out)
            out.append(tuple(points[a] + t * (points[b] - points[a])))
        return on_edge[key]

    def segment(a, b):
        segments.add((min(a, b), max(a, b)))

    for tri in triangles:
        sign = [int(np.sign(level[v])) for v in tri]
        zeros = sign.count(0)
        if not all(inside[v] for v in tri) or (0 == zeros and abs(sum(sign)) == 3):
            faces.append(tuple(tri))
            continue
        if 0 == zeros:
            lone = [k for k in range(3) if sign[k] != sign[(k + 1) % 3] and sign[k] != sign[(k + 2) % 3]][0]
            p, q, r = tri[lone], tri[(lone + 1) % 3], tri[(lone + 2) % 3]
            m1, m2 = crossing(p, q), crossing(p, r)
            faces += [(p, m1, m2), (m1, q, r), (m1, r, m2)]
            segment(m1, m2)
        elif 1 == zeros:
            k = sign.index(0)
            p, q, r = tri[k], tri[(k + 1) % 3], tri[(k + 2) % 3]
            if sign[(k + 1) % 3] == sign[(k + 2) % 3]:
                faces.append(tuple(tri))
                continue
            m = crossing(q, r)
            faces += [(p, q, m), (p, m, r)]
            segment(p, m)
        else:
            faces.append(tuple(tri))
            if 2 == zeros:
                k = [i for i in range(3) if 0 != sign[i]][0]
                segment(tri[(k + 1) % 3], tri[(k + 2) % 3])
    segments = sorted(segments)
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
    tri = np.asarray(triangles)
    snap = 0.05 * float(np.median(np.linalg.norm(points[tri[:, 0]] - points[tri[:, 1]], axis=1)))
    guide, failed = [], 0
    for index, (curve, band, drawn_edges) in enumerate(curves):
        result = cut_along(points, triangles, curve, band, snap)
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
