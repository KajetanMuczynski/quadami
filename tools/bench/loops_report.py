#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Edge loops of quad outputs: do they close, or spiral over the whole mesh?

    loops_report.py A B [--set S] [--md out.md]     compare two ab.py run folders
    loops_report.py --obj out.obj                   one OBJ

A loop is what Blender's Alt+click selects: from an edge, continue through every interior
valence-4 vertex on the edge opposite the incoming one (two steps around the vertex's fan) and
stop at any other vertex or a boundary. Per output (cached as loops.json next to out.obj):

  loops               number of loops (every edge lies on exactly one)
  closed              loops that come back to their first edge
  closed_edge_share   share of all edges that lie on a closed loop (higher: more rings)
  longest_vert_share  distinct vertices of the longest loop / all vertices (lower: no loop that
                      spirals over the mesh; Kajetan's "the whole mesh becomes one edge loop")

The compare table gives per case (mesh, resolution, target) the median over replicates of both
sides and how many replicates moved each way; a case counts as better or worse only when every
replicate agrees (CLAUDE.md noise rule).
"""
import argparse
import collections
import glob
import json
import os
import statistics
import sys


def load_obj(path):
    nv, faces = 0, []
    with open(path) as f:
        for ln in f:
            if ln.startswith("v "):
                nv += 1
            elif ln.startswith("f "):
                faces.append([int(t.split("/")[0]) - 1 for t in ln.split()[1:]])
    return nv, faces


def analyse(path):
    nv, faces = load_obj(path)
    face_of = {}
    for fi, f in enumerate(faces):
        for i in range(len(f)):
            face_of[(f[i], f[(i + 1) % len(f)])] = fi

    def rotate(a, b):
        # next outgoing neighbour of a after a->b, through the face of the twin b->a
        fi = face_of.get((b, a))
        if fi is None:
            return None
        f = faces[fi]
        return f[(f.index(a) + 1) % len(f)]

    def opposite(prev, v):
        fan, w = [prev], prev
        for _ in range(8):
            w = rotate(v, w)
            if w is None:
                return None
            if w == prev:
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
        e = key(a, b)
        if e in seen:
            continue
        chain, edges, closed = [a, b], {e}, False
        for direction in (0, 1):
            if direction:
                chain.reverse()
            while True:
                w = opposite(chain[-2], chain[-1])
                if w is None:
                    break
                ne = key(chain[-1], w)
                if ne in edges:
                    closed = True
                    break
                edges.add(ne)
                chain.append(w)
            if closed:
                break
        seen |= edges
        loops.append((len(edges), closed, len(set(chain))))
    total = sum(l[0] for l in loops) or 1
    longest = max(loops, default=(0, False, 0))
    return {
        "vertices": nv,
        "loops": len(loops),
        "closed": sum(1 for l in loops if l[1]),
        "closed_edge_share": round(sum(l[0] for l in loops if l[1]) / total, 4),
        "longest_vert_share": round(longest[2] / max(nv, 1), 4),
    }


def cached(obj):
    path = os.path.join(os.path.dirname(obj), "loops.json")
    if os.path.exists(path) and os.path.getmtime(path) >= os.path.getmtime(obj):
        with open(path) as f:
            return json.load(f)
    r = analyse(obj)
    with open(path, "w") as f:
        json.dump(r, f)
    return r


def collect(run, setname):
    out = collections.defaultdict(dict)
    for obj in sorted(glob.glob(os.path.join(run, setname, "*", "*", "u*", "out.obj"))):
        parts = obj.split(os.sep)
        case = (parts[-4], parts[-3])
        out[case][parts[-2]] = cached(obj)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a", nargs="?")
    ap.add_argument("b", nargs="?")
    ap.add_argument("--set", default="corpus")
    ap.add_argument("--obj")
    ap.add_argument("--md")
    a = ap.parse_args()
    if a.obj:
        print(json.dumps(analyse(a.obj)))
        return
    A, B = collect(a.a, a.set), collect(a.b, a.set)
    rows = []
    better = worse = 0
    for case in sorted(set(A) & set(B)):
        reps = sorted(set(A[case]) & set(B[case]))
        if not reps:
            continue
        line = [f"{case[0]} {case[1]}", str(len(reps))]
        for m, sign in (("closed_edge_share", 1), ("longest_vert_share", -1)):
            va = [A[case][r][m] for r in reps]
            vb = [B[case][r][m] for r in reps]
            up = sum(1 for x, y in zip(va, vb) if sign * (y - x) > 1e-9)
            down = sum(1 for x, y in zip(va, vb) if sign * (y - x) < -1e-9)
            line += [f"{statistics.median(va):.3f}", f"{statistics.median(vb):.3f}", f"{up}/{down}"]
            if m == "closed_edge_share":
                better += up == len(reps)
                worse += down == len(reps)
        rows.append(line)
    head = ["case", "reps", "closed A", "closed B", "B better/worse", "longest A", "longest B", "B better/worse"]
    md = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    md += ["| " + " | ".join(r) + " |" for r in rows]
    md.append("")
    md.append(f"closed_edge_share: B better in every replicate on {better} of {len(rows)} cases, "
              f"worse in every replicate on {worse}")
    text = "\n".join(md)
    if a.md:
        with open(a.md, "w") as f:
            f.write(text + "\n")
    print(text)


if __name__ == "__main__":
    sys.exit(main())
