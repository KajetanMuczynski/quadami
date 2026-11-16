#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Guide loops: does the output have a closed edge loop on each guide loop of the input?

    guides_report.py A B [--set guides] [--md out.md]   compare two ab.py run folders
    guides_report.py --input in.obj --obj out.obj        one run

A guide is a connected set of the input's marked edges (`l a b` lines, what the add-on's Draw Guide
tool and Follow Marks send); it is a loop when every vertex on it has two marked edges. An output
loop is what Blender's Alt+click selects: from an edge, continue through every interior valence-4
vertex on the edge opposite the incoming one, stop at any other vertex or a boundary; it is closed
when it comes back to its first edge. With h the median output edge length and both polylines
sampled every h/10, and the guide first averaged over arc length h (a path of input edges zigzags):

  guide_loops         guides that are loops
  followed            guide loops with a closed output loop on them: at least 95 % of the output
                      loop's samples within h/2 of the guide and at least 95 % of the guide's samples
                      within h/2 of the output loop
  irregular_on_guides output vertices of valence other than 4 within h/2 of a guide loop (a cone on
                      the line is what breaks the loop)

The compare table gives per case (mesh, resolution, target) the median over replicates of both
sides and how many replicates moved each way; a case counts as better or worse only when every
replicate agrees (CLAUDE.md noise rule). Each run's input is the corpus file ($AR_CORPUS as for
ab.py) or ab.py's perturbed copy of it in <work>/inputs.
"""
import argparse
import collections
import glob
import json
import os
import statistics
import sys

import numpy as np
from scipy.spatial import cKDTree

TOLERANCE = 0.5      # of h
SAMPLE = 0.1         # of h
SHARE = 0.95


def load_obj(path):
    positions, faces, lines = [], [], []
    with open(path) as f:
        for ln in f:
            if ln.startswith("v "):
                positions.append([float(t) for t in ln.split()[1:4]])
            elif ln.startswith("f "):
                faces.append([int(t.split("/")[0]) - 1 for t in ln.split()[1:]])
            elif ln.startswith("l "):
                ids = [int(t.split("/")[0]) - 1 for t in ln.split()[1:]]
                lines.extend(zip(ids, ids[1:]))
    return np.array(positions, dtype=np.float64).reshape(-1, 3), faces, lines


def guide_loops(lines):
    """Vertex cycles (lists, in order) of the marked edges' components whose vertices all have two
    marked edges."""
    neighbours = collections.defaultdict(set)
    for a, b in lines:
        if a != b:
            neighbours[a].add(b)
            neighbours[b].add(a)
    seen, loops, count = set(), [], 0
    for start in sorted(neighbours):
        if start in seen:
            continue
        count += 1
        component, stack = [], [start]
        while stack:
            v = stack.pop()
            if v in seen:
                continue
            seen.add(v)
            component.append(v)
            stack.extend(neighbours[v])
        if any(len(neighbours[v]) != 2 for v in component):
            continue
        cycle, previous, current = [start], None, start
        while True:
            following = min(n for n in neighbours[current] if n != previous)
            if following == start:
                break
            cycle.append(following)
            previous, current = current, following
        loops.append(cycle)
    return loops, count


def output_loops(faces):
    """Alt+click loops of a polygon mesh: [(closed, [vertex, ...])]."""
    face_of = {}
    for fi, f in enumerate(faces):
        for i in range(len(f)):
            face_of[(f[i], f[(i + 1) % len(f)])] = fi

    def rotate(a, b):
        fi = face_of.get((b, a))
        if fi is None:
            return None
        f = faces[fi]
        return f[(f.index(a) + 1) % len(f)]

    def opposite(previous, v):
        fan, w = [previous], previous
        for _ in range(8):
            w = rotate(v, w)
            if w is None:
                return None
            if w == previous:
                break
            fan.append(w)
        else:
            return None
        if len(fan) != 4 or len(set(fan)) != 4:
            return None
        return fan[2]

    def key(a, b):
        return (a, b) if a < b else (b, a)

    seen, loops = set(), []
    for (a, b) in face_of:
        if key(a, b) in seen:
            continue
        chain, edges, closed = [a, b], {key(a, b)}, False
        for direction in (0, 1):
            if direction:
                chain.reverse()
            while True:
                w = opposite(chain[-2], chain[-1])
                if w is None:
                    break
                if key(chain[-1], w) in edges:
                    closed = True
                    break
                edges.add(key(chain[-1], w))
                chain.append(w)
            if closed:
                break
        seen |= edges
        loops.append((closed, chain))
    return loops


def sample(points, step, closed):
    if closed:
        points = np.vstack([points, points[:1]])
    out = [points[:1]]
    for p, q in zip(points[:-1], points[1:]):
        n = max(1, int(np.ceil(np.linalg.norm(q - p) / step)))
        t = (np.arange(1, n + 1) / n)[:, None]
        out.append(p + t * (q - p))
    return np.vstack(out)


def smoothed(points, width):
    """The closed polyline points averaged over a window of arc length width centred on each point:
    the line the user meant, without the zigzag of a path of input edges (up to half an input edge
    either way), which no output loop at a coarser h can follow."""
    arc = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.roll(points, -1, axis=0) - points, axis=1))])
    total, s = arc[-1], arc[:-1]
    out = np.empty_like(points)
    for i in range(len(points)):
        d = np.abs(s - s[i])
        d = np.minimum(d, total - d)
        out[i] = points[d <= width / 2].mean(axis=0)
    return out


def analyse(input_path, output_path):
    positions_in, _, lines = load_obj(input_path)
    positions, faces, _ = load_obj(output_path)
    loops, components = guide_loops(lines)
    edges = {}
    for f in faces:
        for i in range(len(f)):
            a, b = f[i], f[(i + 1) % len(f)]
            edges[(min(a, b), max(a, b))] = np.linalg.norm(positions[a] - positions[b])
    h = float(np.median(list(edges.values()))) if edges else 0.0
    valence = collections.Counter()
    for a, b in edges:
        valence[a] += 1
        valence[b] += 1
    result = {"guides": components, "guide_loops": len(loops), "followed": 0, "irregular_on_guides": 0,
              "h": round(h, 6)}
    if not loops or h <= 0.0:
        return result
    tolerance, step = TOLERANCE * h, SAMPLE * h
    closed_loops = [(sample(positions[chain], step, True), chain) for closed, chain in output_loops(faces) if closed]
    irregular = np.array([v for v in valence if valence[v] != 4], dtype=np.int64)
    for cycle in loops:
        guide = sample(smoothed(positions_in[cycle], h), step, True)
        tree = cKDTree(guide)
        if len(irregular):
            near, _ = tree.query(positions[irregular])
            result["irregular_on_guides"] += int(np.count_nonzero(near < tolerance))
        for samples, _chain in closed_loops:
            near, _ = tree.query(samples)
            if np.mean(near < tolerance) < SHARE:
                continue
            back, _ = cKDTree(samples).query(guide)
            if np.mean(back < tolerance) >= SHARE:
                result["followed"] += 1
                break
    return result


def cached(run_dir, input_path):
    path = os.path.join(run_dir, "guides.json")
    obj = os.path.join(run_dir, "out.obj")
    if os.path.exists(path) and os.path.getmtime(path) >= os.path.getmtime(obj):
        with open(path) as f:
            return json.load(f)
    r = analyse(input_path, obj)
    with open(path, "w") as f:
        json.dump(r, f)
    return r


def input_of(run_dir, work, sources):
    """The input a run read: the corpus file for replicate 0, ab.py's perturbed copy
    (<work>/inputs/<hash of the source>-u<k>.obj) otherwise."""
    with open(os.path.join(run_dir, "record.json")) as f:
        rec = json.load(f)
    src = sources.get(rec["mesh"])
    if src is None or not os.path.exists(src):
        return None
    if not rec["replicate"]:
        return src
    path = os.path.join(work, "inputs", "%s-u%d.obj" % (_hash(src), rec["replicate"]))
    return path if os.path.exists(path) else None


_HASH = {}


def _hash(path):
    """ab.py's file_hash."""
    if path not in _HASH:
        import hashlib
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                h.update(block)
        _HASH[path] = h.hexdigest()[:16]
    return _HASH[path]


def corpus_sources(work):
    """Mesh id -> input file, from $AR_CORPUS or corpus.json next to this script (as ab.py)."""
    corpus = os.environ.get("AR_CORPUS") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus.json")
    with open(corpus) as f:
        meshes = json.load(f)["meshes"]
    return {m["id"]: os.path.join(work, "meshes", m["file"]) for m in meshes}


def collect(run, setname, work):
    out = collections.defaultdict(dict)
    sources = corpus_sources(work)
    for obj in sorted(glob.glob(os.path.join(run, setname, "*", "*", "u*", "out.obj"))):
        d = os.path.dirname(obj)
        src = input_of(d, work, sources)
        if src is None:
            continue
        parts = obj.split(os.sep)
        out[(parts[-4], parts[-3])][parts[-2]] = cached(d, src)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a", nargs="?")
    ap.add_argument("b", nargs="?")
    ap.add_argument("--set", default="guides")
    ap.add_argument("--work", help="ab.py work folder (meshes/, inputs/); default: the run folder's parent's parent")
    ap.add_argument("--input")
    ap.add_argument("--obj")
    ap.add_argument("--md")
    a = ap.parse_args()
    if a.obj:
        print(json.dumps(analyse(a.input, a.obj)))
        return 0
    work = a.work or os.path.dirname(os.path.dirname(os.path.abspath(a.a)))
    A, B = collect(a.a, a.set, work), collect(a.b, a.set, work)
    rows, better, worse, totals = [], 0, 0, [0, 0, 0]
    for case in sorted(set(A) & set(B)):
        reps = sorted(set(A[case]) & set(B[case]))
        if not reps or not A[case][reps[0]]["guide_loops"]:
            continue
        loops = A[case][reps[0]]["guide_loops"]
        line = [f"{case[0]} {case[1]}", str(len(reps)), str(loops)]
        for metric, sign in (("followed", 1), ("irregular_on_guides", -1)):
            va = [A[case][r][metric] for r in reps]
            vb = [B[case][r][metric] for r in reps]
            up = sum(1 for x, y in zip(va, vb) if sign * (y - x) > 0)
            down = sum(1 for x, y in zip(va, vb) if sign * (y - x) < 0)
            line += [f"{statistics.median(va):g}", f"{statistics.median(vb):g}", f"{up}/{down}"]
            if metric == "followed":
                better += up == len(reps)
                worse += down == len(reps)
                totals[1] += sum(va)
                totals[2] += sum(vb)
        totals[0] += loops * len(reps)
        rows.append(line)
    head = ["case", "reps", "guide loops", "followed A", "followed B", "B better/worse",
            "cones on guides A", "cones on guides B", "B better/worse"]
    md = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    md += ["| " + " | ".join(r) + " |" for r in rows]
    md.append("")
    md.append(f"Guide loops followed by a closed edge loop: A {totals[1]}/{totals[0]}, B {totals[2]}/{totals[0]} "
              f"(all replicates). B better in every replicate on {better} of {len(rows)} cases, worse in every "
              f"replicate on {worse}.")
    text = "\n".join(md)
    if a.md:
        with open(a.md, "w") as f:
            f.write(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
