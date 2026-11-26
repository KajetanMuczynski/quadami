#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Guide rings cut into a mesh, for testing guide loops without the zigzag of a path of input edges.

    make_cut_rings.py IN.obj OUT_DIR --ring zp:20 [--ring xm:26 ...] [--set cutrings]

A ring `<axis><sign>:<radius>` is the level set "distance from the axis = radius" around the axis from
the mesh's centroid through its extreme vertex along +x/-x/+y/-y/+z/-z (xp, xm, yp, ym, zp, zm), on
the extreme vertex's side. Every triangle the level set crosses is split along it (one triangle and
two more), the cut edges are written as `l a b` lines, and of the closed cuts the one nearest the
extreme vertex is kept. Prints one line per ring and the corpus.json entries for `--set`.

The same circle walked along existing edges is `make_guides.guide_along(..., make_guides.circle(...))`.
"""
import argparse
import collections
import json
import os
import sys

import numpy as np

AXES = {"xp": (0, 1), "xm": (0, -1), "yp": (1, 1), "ym": (1, -1), "zp": (2, 1), "zm": (2, -1)}


def load(path):
    vertices, triangles = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                vertices.append([float(t) for t in line.split()[1:4]])
            elif line.startswith("f "):
                face = [int(t.split("/")[0]) - 1 for t in line.split()[1:]]
                triangles += [[face[0], face[k], face[k + 1]] for k in range(1, len(face) - 1)]
    return np.array(vertices), np.array(triangles)


def cut(vertices, triangles, apex, radius):
    """(vertices, triangles, guide edges) with the ring cut in, or None when no cut closes."""
    centre = vertices.mean(axis=0)
    axis = (apex - centre) / np.linalg.norm(apex - centre)
    offset = vertices - apex
    level = np.linalg.norm(offset - np.outer(offset @ axis, axis), axis=1) - radius
    level[np.abs(level) < 1e-7] = 1e-7
    side = (vertices - centre) @ axis > 0
    out = [tuple(v) for v in vertices]
    on_edge, faces, segments = {}, [], []

    def crossing(a, b):
        key = (min(a, b), max(a, b))
        if key not in on_edge:
            t = level[a] / (level[a] - level[b])
            on_edge[key] = len(out)
            out.append(tuple(vertices[a] + t * (vertices[b] - vertices[a])))
        return on_edge[key]

    for tri in triangles:
        above = [level[v] > 0 for v in tri]
        if all(above) or not any(above) or not all(side[v] for v in tri):
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
    points = np.array(out)
    keep = set(min(closed, key=lambda c: np.linalg.norm(points[c].mean(axis=0) - apex)))
    return points, faces, [(a, b) for a, b in segments if a in keep]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("out_dir")
    ap.add_argument("--ring", action="append", required=True, help="<xp|xm|yp|ym|zp|zm>:<radius>, repeatable")
    ap.add_argument("--set", default="cutrings")
    a = ap.parse_args()
    vertices, triangles = load(a.input)
    os.makedirs(a.out_dir, exist_ok=True)
    entries = []
    for spec in a.ring:
        name, radius = spec.split(":")
        component, sign = AXES[name]
        apex = vertices[np.argmax(sign * vertices[:, component])]
        result = cut(vertices, triangles, apex, float(radius))
        ident = "cut_%s%s" % (name, radius.replace(".", "_"))
        if result is None:
            print(ident, "no closed cut", file=sys.stderr)
            continue
        points, faces, guide = result
        lengths = np.array([np.linalg.norm(points[x] - points[y]) for x, y in guide])
        with open(os.path.join(a.out_dir, ident + ".obj"), "w") as f:
            for p in points:
                f.write("v %.9g %.9g %.9g\n" % tuple(p))
            for x, y, z in faces:
                f.write("f %d %d %d\n" % (x + 1, y + 1, z + 1))
            for x, y in guide:
                f.write("l %d %d\n" % (x + 1, y + 1))
        print("%s: %d guide edges, length %.4g, shortest %.3g" % (ident, len(guide), lengths.sum(), lengths.min()), file=sys.stderr)
        entries.append({"id": ident, "file": os.path.join(os.path.basename(os.path.normpath(a.out_dir)), ident + ".obj"),
                        "category": "guide ring cut into the mesh", "set": a.set})
    print(json.dumps(entries, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
