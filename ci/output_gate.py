#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Output gate: remesh a small procedural corpus with autoremesher-cli and check the results.

    python3 ci/output_gate.py <autoremesher-cli> <work-dir>

Every mesh is generated here (standard library only), so the gate needs no downloads. For each
case the gate fails on:
  - a non-zero exit status, a status other than "ok", or a missing output;
  - a quad count outside [MIN_RATIO, MAX_RATIO] x target;
  - non-quad faces above MAX_NON_QUAD_SHARE of all faces;
  - any non-manifold edge;
  - an input island with more than MAX_UNCOVERED of its area farther than one output edge (the
    output's mean edge length) from the output: a dropped island, or a part collapsed into a blob,
    which keeps the input's genus and so passes every topology count (TARGETS H4, Kajetan 2026-10-23);
  - an output OBJ whose own counts disagree with the report (so a broken counter can't hide a
    bad mesh).
Folded faces, boundary edges, holes in closed islands and components are reported, not gated (an
island without output fails the coverage check), until the plan sets targets for them (TODO 0.7). The thresholds are wide on purpose: this
gate catches broken output, not small quality changes. A Markdown table goes to $GITHUB_STEP_SUMMARY when set.
"""
import json
import math
import os
import subprocess
import sys
import time

MIN_RATIO, MAX_RATIO = 0.6, 1.5
MAX_NON_QUAD_SHARE = 0.02
MAX_UNCOVERED = 0.01


class Mesh:
    def __init__(self):
        self.vertices, self.triangles, self._index = [], [], {}

    def vertex(self, key, position):
        """Welded vertex: the same key returns the same index."""
        if key not in self._index:
            self._index[key] = len(self.vertices)
            self.vertices.append(position)
        return self._index[key]

    def quad(self, a, b, c, d):
        self.triangles += [(a, b, c), (a, c, d)]

    def write(self, path):
        with open(path, "w") as f:
            f.writelines("v %.6f %.6f %.6f\n" % v for v in self.vertices)
            f.writelines("f %d %d %d\n" % (a + 1, b + 1, c + 1) for a, b, c in self.triangles)


def add_uv_surface(mesh, tag, nu, nv, point, wrap_u, wrap_v):
    """Grid surface point(u, v), u, v in [0, 1]; wrapped directions are welded."""
    def key(i, j):
        return (tag, i % nu if wrap_u else i, j % nv if wrap_v else j)

    for i in range(nu):
        for j in range(nv):
            corners = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
            ids = [mesh.vertex(key(a, b), point(a / nu, b / nv)) for a, b in corners]
            mesh.quad(*ids)


def sphere(mesh, tag, radius=1.0, center=(0.0, 0.0, 0.0), stacks=48, slices=96):
    """UV sphere with welded poles."""
    def point(u, v):
        theta, phi = math.pi * v, 2.0 * math.pi * u
        return (center[0] + radius * math.sin(theta) * math.cos(phi),
                center[1] + radius * math.cos(theta),
                center[2] + radius * math.sin(theta) * math.sin(phi))

    def key(i, j):
        if j == 0 or j == stacks:
            return (tag, "pole", j)
        return (tag, i % slices, j)

    for i in range(slices):
        for j in range(stacks):
            corners = [(i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j)]
            ids = [mesh.vertex(key(a, b), point(a / slices, b / stacks)) for a, b in corners]
            if j == 0:
                mesh.triangles.append((ids[0], ids[1], ids[2]))
            elif j == stacks - 1:
                mesh.triangles.append((ids[0], ids[1], ids[3]))
            else:
                mesh.quad(*ids)


def torus(mesh, tag, major=1.0, minor=0.35, n_major=96, n_minor=48):
    def point(u, v):
        a, b = 2.0 * math.pi * u, 2.0 * math.pi * v
        r = major + minor * math.cos(b)
        return (r * math.cos(a), minor * math.sin(b), r * math.sin(a))

    add_uv_surface(mesh, tag, n_major, n_minor, point, True, True)


def box(mesh, tag, n=24):
    """Unit cube surface, n x n quads per side, with sharp 90-degree edges."""
    for axis in range(3):
        for side in (0, n):
            for i in range(n):
                for j in range(n):
                    ids = []
                    for a, b in ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)):
                        grid = [0, 0, 0]
                        grid[axis], grid[(axis + 1) % 3], grid[(axis + 2) % 3] = side, a, b
                        ids.append(mesh.vertex((tag,) + tuple(grid), tuple(g / n for g in grid)))
                    if side == 0:
                        ids.reverse()
                    mesh.quad(*ids)


def open_tube(mesh, tag, n_around=96, n_along=48):
    """Cylinder without caps: two open boundary loops."""
    def point(u, v):
        a = 2.0 * math.pi * u
        return (math.cos(a), 2.0 * v, math.sin(a))

    add_uv_surface(mesh, tag, n_around, n_along, point, True, False)


def corpus():
    def single(build):
        mesh = Mesh()
        build(mesh)
        return mesh

    def two_islands(mesh):
        sphere(mesh, "big")
        sphere(mesh, "small", radius=0.35, center=(2.0, 0.0, 0.0), stacks=24, slices=48)

    return [
        ("sphere", single(lambda m: sphere(m, "s")), 2000),
        ("torus", single(lambda m: torus(m, "t")), 2000),
        ("box", single(lambda m: box(m, "b")), 1500),
        ("open_tube", single(lambda m: open_tube(m, "o")), 1500),
        ("two_islands", single(two_islands), 2000),
        # Far from the origin: crossing-point keys once overflowed a 32-bit
        # long (MSVC) beyond about 21,475 units and tore the output on Windows.
        ("far_sphere", single(lambda m: sphere(m, "f", center=(1e5, 1e5, 1e5))), 2000),
    ]


def obj_counts(path):
    """Quads, non-quads and non-manifold edges (edges with more than two faces) of an OBJ."""
    quads = others = 0
    edge_faces = {}
    with open(path) as f:
        for line in f:
            if not line.startswith("f "):
                continue
            ids = [int(t.split("/")[0]) for t in line.split()[1:]]
            if len(ids) == 4:
                quads += 1
            else:
                others += 1
            for a, b in zip(ids, ids[1:] + ids[:1]):
                edge = (min(a, b), max(a, b))
                edge_faces[edge] = edge_faces.get(edge, 0) + 1
    return quads, others, sum(1 for c in edge_faces.values() if c > 2)


def read_obj(path):
    vertices, faces = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                vertices.append(tuple(float(t) for t in line.split()[1:4]))
            elif line.startswith("f "):
                faces.append([int(t.split("/")[0]) - 1 for t in line.split()[1:]])
    return vertices, faces


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def closest_on_triangle(p, a, b, c):
    """Closest point to p on triangle abc (Ericson, Real-Time Collision Detection, 5.1.5)."""
    ab, ac, ap = sub(b, a), sub(c, a), sub(p, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        return a
    bp = sub(p, b)
    d3, d4 = dot(ab, bp), dot(ac, bp)
    if d3 >= 0 and d4 <= d3:
        return b
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3)
        return (a[0] + v * ab[0], a[1] + v * ab[1], a[2] + v * ab[2])
    cp = sub(p, c)
    d5, d6 = dot(ab, cp), dot(ac, cp)
    if d6 >= 0 and d5 <= d6:
        return c
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2 / (d2 - d6)
        return (a[0] + w * ac[0], a[1] + w * ac[1], a[2] + w * ac[2])
    va = d3 * d6 - d5 * d4
    if va <= 0 and d4 - d3 >= 0 and d5 - d6 >= 0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return (b[0] + w * (c[0] - b[0]), b[1] + w * (c[1] - b[1]), b[2] + w * (c[2] - b[2]))
    denom = 1.0 / (va + vb + vc)
    v, w = vb * denom, vc * denom
    return (a[0] + ab[0] * v + ac[0] * w, a[1] + ab[1] * v + ac[1] * w, a[2] + ab[2] * v + ac[2] * w)


def uncovered_share(mesh, output):
    """Per input island (vertex-connected), the share of its area whose triangle centroid lies farther
    than h, the output's mean edge length, from the output surface. Returns the largest share."""
    V, faces = read_obj(output)
    tris = [(f[0], f[k], f[k + 1]) for f in faces for k in range(1, len(f) - 1)]
    edges = [math.dist(V[f[k]], V[f[k - 1]]) for f in faces for k in range(len(f))]
    if not tris or not edges:
        return 1.0
    h = sum(edges) / len(edges)

    def cell(p):
        return (math.floor(p[0] / h), math.floor(p[1] / h), math.floor(p[2] / h))

    grid = {}
    for t in tris:
        lo = cell([min(V[i][k] for i in t) for k in range(3)])
        hi = cell([max(V[i][k] for i in t) for k in range(3)])
        for x in range(lo[0], hi[0] + 1):
            for y in range(lo[1], hi[1] + 1):
                for z in range(lo[2], hi[2] + 1):
                    grid.setdefault((x, y, z), []).append(t)

    def covered(p):
        # A triangle within h of p has its closest point, inside its bounding box, in a cell next to p's.
        cx, cy, cz = cell(p)
        for x in (cx - 1, cx, cx + 1):
            for y in (cy - 1, cy, cy + 1):
                for z in (cz - 1, cz, cz + 1):
                    for t in grid.get((x, y, z), ()):
                        if math.dist(p, closest_on_triangle(p, V[t[0]], V[t[1]], V[t[2]])) <= h:
                            return True
        return False

    parent = list(range(len(mesh.vertices)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for a, b, c in mesh.triangles:
        parent[root(b)] = root(a)
        parent[root(c)] = root(a)
    area, lost = {}, {}
    P = mesh.vertices
    for a, b, c in mesh.triangles:
        k = root(a)
        s = 0.5 * math.sqrt(sum(x * x for x in cross(sub(P[b], P[a]), sub(P[c], P[a]))))
        area[k] = area.get(k, 0.0) + s
        centroid = tuple((P[a][i] + P[b][i] + P[c][i]) / 3 for i in range(3))
        if not covered(centroid):
            lost[k] = lost.get(k, 0.0) + s
    return max(lost.get(k, 0.0) / area[k] for k in area if area[k] > 0)


def run_case(cli, work, name, mesh, target):
    source = os.path.join(work, name + ".obj")
    output = os.path.join(work, name + "-quads.obj")
    report_path = os.path.join(work, name + "-report.json")
    mesh.write(source)
    start = time.monotonic()
    proc = subprocess.run([cli, "--input", source, "--output", output, "--report-json", report_path,
                           "--target-quads", str(target)], capture_output=True, text=True)
    row = {"case": name, "target": target, "seconds": time.monotonic() - start, "failures": []}
    with open(os.path.join(work, name + ".log"), "w") as f:
        f.write(proc.stdout + proc.stderr)
    if proc.returncode != 0:
        row["failures"].append("exit status %d" % proc.returncode)
    try:
        with open(report_path) as f:
            report = json.load(f)
    except (OSError, ValueError) as e:
        row["failures"].append("no report (%s)" % e)
        return row
    if report["status"] != "ok":
        row["failures"].append("status %s: %s" % (report["status"], report["message"]))
    if not os.path.isfile(output):
        row["failures"].append("no output mesh")
        return row

    out = report["output"]
    quads, others, non_manifold = obj_counts(output)
    row.update(quads=quads, non_quads=others, non_manifold=non_manifold,
               folded=out["folded_faces"], boundary=out["boundary_edges"],
               components=out["components"], empty_islands=report["islands"]["without_output"],
               holes=report["islands"]["holes"],
               islands=report["islands"]["input"])
    ratio = quads / target
    if not MIN_RATIO <= ratio <= MAX_RATIO:
        row["failures"].append("quads %.2fx target, allowed [%.2f, %.2f]" % (ratio, MIN_RATIO, MAX_RATIO))
    if others > MAX_NON_QUAD_SHARE * max(1, quads + others):
        row["failures"].append("%d non-quad faces, allowed %.0f%%" % (others, 100 * MAX_NON_QUAD_SHARE))
    if non_manifold:
        row["failures"].append("%d non-manifold edges" % non_manifold)
    row["uncovered"] = uncovered_share(mesh, output)
    if row["uncovered"] > MAX_UNCOVERED:
        row["failures"].append("%.1f%% of an input island farther than one output edge from the output, "
                               "allowed %.0f%%" % (100 * row["uncovered"], 100 * MAX_UNCOVERED))
    for key, mine in (("quads", quads), ("non_quads", others), ("non_manifold_edges", non_manifold)):
        if out[key] != mine:
            row["failures"].append("report says %s = %d, the OBJ has %d" % (key, out[key], mine))
    return row


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    cli, work = os.path.abspath(sys.argv[1]), sys.argv[2]
    os.makedirs(work, exist_ok=True)
    rows = [run_case(cli, work, name, mesh, target) for name, mesh, target in corpus()]

    columns = ["case", "target", "quads", "non_quads", "non_manifold", "folded", "empty_islands",
               "islands", "boundary", "holes", "components", "uncovered", "seconds"]
    lines = ["| " + " | ".join(columns) + " | result |", "|" + "---|" * (len(columns) + 1)]
    for row in rows:
        cells = [("%.2f" % row[c]) if c == "seconds" and c in row else
                 ("%.2g" % row[c]) if c == "uncovered" and c in row else str(row.get(c, "-")) for c in columns]
        lines.append("| " + " | ".join(cells) + " | " + ("; ".join(row["failures"]) or "pass") + " |")
    table = "\n".join(lines)
    print(table)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as f:
            f.write("### Output gate (%s)\n\n%s\n\n" % (os.environ.get("RUNNER_OS", "local"), table))
    failed = [row["case"] for row in rows if row["failures"]]
    if failed:
        sys.exit("FAIL: " + ", ".join(failed))
    print("PASS")


if __name__ == "__main__":
    main()
