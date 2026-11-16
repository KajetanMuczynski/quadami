#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Write the "guides" A/B set: procedural meshes with guide loops as OBJ `l` lines, drawn the way the
add-on's Draw Guide tool draws them (TODO 5.7): points on the intended curve every about two input
edges, each snapped to the nearest vertex, consecutive ones joined by the shortest edge path
(addon/quadami/guides.py). The guides therefore zigzag along input edges like a user's, unlike the
`marked` set's cuts. guides_report.py measures whether a closed output edge loop runs on each.

    python make_guides.py --out meshes/guides            # numpy only

Meshes: a UV sphere and an icosphere with a tilted ring (across the rings or on an irregular
triangulation), a torus with a ring around its tube between grid lines, a capsule with two tilted
rings, a bumpy blob with a small "eye" ring and a large ring.
"""
import argparse
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "addon", "quadami"))
import guides  # noqa: E402  (Blender-free module of the add-on)


def uv_sphere(segments, rings):
    vertices = [[0.0, 0.0, 1.0]]
    for j in range(1, rings):
        theta = math.pi * j / rings
        for i in range(segments):
            phi = 2 * math.pi * i / segments
            vertices.append([math.sin(theta) * math.cos(phi), math.sin(theta) * math.sin(phi), math.cos(theta)])
    vertices.append([0.0, 0.0, -1.0])

    def index(j, i):
        return 1 + (j - 1) * segments + i % segments

    faces = [[0, index(1, i), index(1, i + 1)] for i in range(segments)]
    for j in range(1, rings - 1):
        for i in range(segments):
            a, b, c, d = index(j, i), index(j, i + 1), index(j + 1, i + 1), index(j + 1, i)
            faces += [[a, d, c], [a, c, b]]  # one diagonal direction, as Blender's triangulation
    south = len(vertices) - 1
    faces += [[south, index(rings - 1, i + 1), index(rings - 1, i)] for i in range(segments)]
    return np.array(vertices), np.array(faces)


def icosphere(subdivisions):
    t = (1 + 5 ** 0.5) / 2
    vertices = [[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0], [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t],
                [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]]
    faces = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2],
             [10, 7, 6], [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5],
             [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]]
    vertices = [np.array(v, dtype=float) / np.linalg.norm(v) for v in vertices]
    for _ in range(subdivisions):
        midpoint, refined = {}, []

        def middle(a, b):
            key = (min(a, b), max(a, b))
            if key not in midpoint:
                p = vertices[a] + vertices[b]
                vertices.append(p / np.linalg.norm(p))
                midpoint[key] = len(vertices) - 1
            return midpoint[key]

        for a, b, c in faces:
            ab, bc, ca = middle(a, b), middle(b, c), middle(c, a)
            refined += [[a, ab, ca], [b, bc, ab], [c, ca, bc], [ab, bc, ca]]
        faces = refined
    return np.array(vertices), np.array(faces)


def torus(major, minor, segments, sides):
    vertices = []
    for i in range(segments):
        u = 2 * math.pi * i / segments
        for j in range(sides):
            v = 2 * math.pi * j / sides
            r = major + minor * math.cos(v)
            vertices.append([r * math.cos(u), r * math.sin(u), minor * math.sin(v)])
    faces = []
    for i in range(segments):
        for j in range(sides):
            a, b = i * sides + j, ((i + 1) % segments) * sides + j
            c, d = ((i + 1) % segments) * sides + (j + 1) % sides, i * sides + (j + 1) % sides
            faces += [[a, b, c], [a, c, d]]
    return np.array(vertices), np.array(faces)


def capsule(radius, half_length, segments, cap_rings, body_rings):
    """Cylinder along z with hemispherical caps, one vertex grid of segments around."""
    profile = []  # (z, r) from the north pole down
    for j in range(1, cap_rings + 1):
        a = 0.5 * math.pi * j / cap_rings
        profile.append((half_length + radius * math.cos(a), radius * math.sin(a)))
    for j in range(1, body_rings):
        profile.append((half_length - 2 * half_length * j / body_rings, radius))
    for j in range(cap_rings, 0, -1):
        a = 0.5 * math.pi * j / cap_rings
        profile.append((-half_length - radius * math.cos(a), radius * math.sin(a)))
    vertices = [[0.0, 0.0, half_length + radius]]
    for z, r in profile:
        for i in range(segments):
            phi = 2 * math.pi * i / segments
            vertices.append([r * math.cos(phi), r * math.sin(phi), z])
    vertices.append([0.0, 0.0, -half_length - radius])
    rings = len(profile)

    def index(j, i):
        return 1 + j * segments + i % segments

    faces = [[0, index(0, i), index(0, i + 1)] for i in range(segments)]
    for j in range(rings - 1):
        for i in range(segments):
            a, b, c, d = index(j, i), index(j, i + 1), index(j + 1, i + 1), index(j + 1, i)
            faces += [[a, d, c], [a, c, b]]
    south = len(vertices) - 1
    faces += [[south, index(rings - 1, i + 1), index(rings - 1, i)] for i in range(segments)]
    return np.array(vertices), np.array(faces)


def bumpy(vertices):
    """A sphere with a few smooth bumps: no symmetry for the field to lock onto."""
    d = vertices / np.linalg.norm(vertices, axis=1)[:, None]
    bumps = [((0.3, 0.8, 0.5), 0.12), ((-0.7, 0.1, 0.7), -0.08), ((0.2, -0.9, -0.3), 0.1), ((-0.4, -0.2, -0.9), 0.07)]
    radius = np.ones(len(d))
    for direction, height in bumps:
        n = np.array(direction) / np.linalg.norm(direction)
        radius += height * np.exp(-((1 - d @ n) / 0.15) ** 2)
    return d * radius[:, None]


def edges_of(faces):
    edges = set()
    for f in faces:
        for k in range(3):
            a, b = int(f[k]), int(f[(k + 1) % 3])
            edges.add((min(a, b), max(a, b)))
    return np.array(sorted(edges))


def guide_along(vertices, graph, curve):
    """Edge indices of the guide through the curve's points (N, 3, closed): nearest vertices, repeats
    dropped, joined by shortest edge paths."""
    points = []
    for p in curve:
        v = int(np.argmin(np.linalg.norm(vertices - p, axis=1)))
        if not points or points[-1] != v:
            points.append(v)
    while len(points) > 1 and points[0] == points[-1]:
        points.pop()
    path = graph.path_through(points, closed=True)
    if path is None:
        raise RuntimeError("guide crosses islands")
    return path


def circle(center, normal, radius, count):
    """count points on the circle around center in the plane of normal."""
    normal = np.asarray(normal, dtype=float)
    normal /= np.linalg.norm(normal)
    helper = np.array([1.0, 0.0, 0.0]) if abs(normal[0]) < 0.9 else np.array([0.0, 1.0, 0.0])
    u = np.cross(normal, helper)
    u /= np.linalg.norm(u)
    w = np.cross(normal, u)
    a = np.linspace(0, 2 * math.pi, count, endpoint=False)[:, None]
    return np.asarray(center) + radius * (np.cos(a) * u + np.sin(a) * w)


def on_surface(points, vertices):
    """Each point moved to the nearest vertex's distance from the origin along its direction (star-
    shaped meshes only)."""
    nearest = [int(np.argmin(np.linalg.norm(vertices - p, axis=1))) for p in points]
    radius = np.linalg.norm(vertices[nearest], axis=1)
    return points / np.linalg.norm(points, axis=1)[:, None] * radius[:, None]


def tilted_ring(theta_degrees, offset=0.0):
    n = np.array([0.0, math.sin(math.radians(theta_degrees)), math.cos(math.radians(theta_degrees))])
    r = math.sqrt(1 - offset * offset)
    return circle(offset * n, n, r, 400)


def build():
    meshes = {}
    v, f = uv_sphere(64, 32)
    meshes["guides_uvsphere_tilted"] = (v, f, [tilted_ring(30)], "UV sphere 64x32, a ring tilted 30 deg across its rings")
    v, f = icosphere(4)
    meshes["guides_icosphere_ring"] = (v, f, [tilted_ring(20, 0.3)], "icosphere (4 subdivisions), a ring off the centre")
    v, f = torus(1.0, 0.35, 96, 48)
    # Around the tube on a plane between two grid lines (u = 1.75 grid steps), tilted 15 deg.
    u = 2 * math.pi * 1.75 / 96
    axis = np.array([-math.sin(u), math.cos(u), 0.0])
    tilt = math.radians(15)
    normal = np.cos(tilt) * axis + np.sin(tilt) * np.array([0.0, 0.0, 1.0])
    ring = circle([math.cos(u), math.sin(u), 0.0], normal, 0.35, 200)
    meshes["guides_torus_tube"] = (v, f, [ring], "torus 96x48, a tilted ring around the tube between grid lines")
    v, f = capsule(0.4, 0.8, 48, 10, 24)
    rings = []
    for z, degrees in ((0.45, 20), (-0.45, -25)):
        n = np.array([0.0, math.sin(math.radians(degrees)), math.cos(math.radians(degrees))])
        rings.append(circle([0.0, 0.0, z], n, 0.4 / math.cos(math.radians(degrees)), 300))
    # Project the tilted circles onto the cylinder (an ellipse on it).
    rings = [np.column_stack([0.4 * p[:, :2] / np.linalg.norm(p[:, :2], axis=1)[:, None], p[:, 2]]) for p in rings]
    meshes["guides_capsule_rings"] = (v, f, rings, "capsule 48 around, two tilted rings on the body")
    v, f = icosphere(5)
    v = bumpy(v)
    eye_direction = np.array([0.3, 0.8, 0.5]) / np.linalg.norm([0.3, 0.8, 0.5])
    eye = on_surface(circle(eye_direction, eye_direction, 0.3, 200), v)
    belt = on_surface(tilted_ring(60, -0.3), v)
    meshes["guides_blob_eye"] = (v, f, [eye, belt], "bumpy sphere, a small ring around a bump and a large ring")
    return meshes


def write(path, vertices, faces, edges, guide_edges):
    with open(path, "w", newline="\n") as fo:
        for p in vertices:
            fo.write("v %.9g %.9g %.9g\n" % tuple(float(x) for x in p))
        for a, b, c in faces:
            fo.write("f %d %d %d\n" % (a + 1, b + 1, c + 1))
        for e in guide_edges:
            fo.write("l %d %d\n" % (edges[e][0] + 1, edges[e][1] + 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for name, (vertices, faces, curves, _category) in build().items():
        vertices = vertices.astype(np.float32).astype(np.float64)  # what Blender hands over
        edges = edges_of(faces)
        graph = guides.EdgeGraph(vertices, edges)
        guide_edges = []
        for curve in curves:
            guide_edges += guide_along(vertices, graph, curve)
        flags = np.zeros(len(edges), dtype=bool)
        flags[guide_edges] = True
        closed, open_ = guides.count_loops(edges, flags)
        write(os.path.join(a.out, name + ".obj"), vertices, faces, edges, sorted(set(guide_edges)))
        print(f"{name}: {len(vertices)} vertices, {len(faces)} triangles, {len(curves)} curves -> "
              f"{closed} closed guides, {open_} open")
    return 0


if __name__ == "__main__":
    sys.exit(main())
