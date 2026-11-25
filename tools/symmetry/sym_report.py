#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Seam and symmetry numbers of ab.py run folders on a set with mirror planes (corpus.json "sym").

    sym_report.py --set symmetry RUN [RUN ...] [--json out.json]

Per run (mesh, target, replicate) and then per mesh and target over the replicates (mean, min-max):
  quads/target, non-quads, open and non-manifold edges, folded faces (metrics.json), topological
  symmetry (metrics.json: share of reflected vertices within 0.1 edge of a vertex), runtime (record wall),
  and at the plane:
  seam_vertices   vertices on the plane (|d| < 1e-6 diag); a cut-mirror output has its seam row here
  seam_poles      seam vertices of valence other than 4 (inner ones; poles on the plane)
  seam_non_quads  non-quad faces with a corner on the seam
  seam_folds      folded faces with a corner on the seam
  seam_kink       at a valence-4 seam vertex, how far the edge path across the plane turns in the tangent
                  plane (deg, 0 straight); turn_elsewhere_mean is the same turn at every other inner
                  valence-4 vertex (both paths), the mesh's own loop waviness to compare with
  loops_crossing  Alt+click edge loops (guides_report.output_loops) with vertices on both sides, and how
                  many of them are closed
Seam numbers are 0 for a normal remesh, whose vertices are not on the plane; loops crossing is for both.
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "bench"))
from guides_report import output_loops  # noqa: E402
from metrics import folded_mask  # noqa: E402


def read_obj(path):
    V, F = [], []
    for line in open(path):
        if line.startswith("v "):
            V.append([float(x) for x in line.split()[1:4]])
        elif line.startswith("f "):
            F.append([int(t.split("/")[0]) - 1 for t in line.split()[1:]])
    return np.array(V).reshape(-1, 3), F


def seam_numbers(V, F, axis, center):
    lo, hi = V.min(0), V.max(0)
    diag = float(np.linalg.norm(hi - lo))
    s = V[:, axis] - center
    on = np.abs(s) < 1e-6 * diag
    nbr = [set() for _ in range(len(V))]
    count = {}
    for f in F:
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            nbr[a].add(b)
            nbr[b].add(a)
            key = (a, b) if a < b else (b, a)
            count[key] = count.get(key, 0) + 1
    border = np.zeros(len(V), dtype=bool)
    for (a, b), c in count.items():
        if c == 1:
            border[a] = border[b] = True
    inner_seam = np.where(on & ~border)[0]
    poles = sum(1 for v in inner_seam if len(nbr[v]) != 4)
    # vertex normals (area weighted) and the faces at each vertex, for the turn of an edge path in the
    # tangent plane: at a valence-4 vertex the two opposite neighbours (no face holds both) continue a loop
    normals = np.zeros_like(V)
    faces_at = [[] for _ in range(len(V))]
    for fi, f in enumerate(F):
        n = np.zeros(3)
        for k in range(len(f)):
            n += np.cross(V[f[k]], V[f[(k + 1) % len(f)]])
        for v in f:
            normals[v] += n
            faces_at[v].append(fi)

    def turn(v, a, b):
        n = normals[v] / (np.linalg.norm(normals[v]) or 1.0)
        e1, e2 = V[a] - V[v], V[b] - V[v]
        e1, e2 = e1 - np.dot(e1, n) * n, e2 - np.dot(e2, n) * n
        c = np.dot(e1, e2) / ((np.linalg.norm(e1) * np.linalg.norm(e2)) or 1.0)
        return 180.0 - np.degrees(np.arccos(np.clip(c, -1, 1)))

    def opposite_pairs(v):
        ns = list(nbr[v])
        together = set()
        for fi in faces_at[v]:
            fs = set(F[fi])
            together |= {(a, b) for a in ns for b in ns if a < b and a in fs and b in fs}
        return [(a, b) for i, a in enumerate(ns) for b in ns[i + 1:] if (min(a, b), max(a, b)) not in together]

    kinks, all_turns = [], []
    for v in np.where(~border)[0]:
        if len(nbr[v]) != 4:
            continue
        pairs = opposite_pairs(v)
        if on[v]:
            kinks += [turn(v, a, b) for a, b in pairs if not on[a] and not on[b]]
        else:
            all_turns += [turn(v, a, b) for a, b in pairs]
    seam_non_quads = sum(1 for f in F if len(f) != 4 and on[list(f)].any())
    folded, twisted = folded_mask(V, F)
    seam_folds = int(sum(1 for i, f in enumerate(F) if (folded[i] or twisted[i]) and on[list(f)].any()))
    tol = 1e-6 * diag
    crossing = closed = 0
    for is_closed, chain in output_loops([tuple(f) for f in F]):
        ss = s[chain]
        if (ss > tol).any() and (ss < -tol).any():
            crossing += 1
            closed += bool(is_closed)
    return {"seam_vertices": int(on.sum()), "seam_poles": int(poles), "seam_non_quads": int(seam_non_quads),
            "seam_folds": seam_folds, "seam_kink_mean": float(np.mean(kinks)) if kinks else None,
            "seam_kink_p90": float(np.percentile(kinks, 90)) if kinks else None,
            "turn_elsewhere_mean": float(np.mean(all_turns)) if all_turns else None,
            "loops_crossing": crossing, "loops_crossing_closed": closed}


def runs(folder, setname):
    base = os.path.join(folder, setname)
    for mesh in sorted(os.listdir(base)):
        for cfg in sorted(os.listdir(os.path.join(base, mesh))):
            for rep in sorted(os.listdir(os.path.join(base, mesh, cfg))):
                d = os.path.join(base, mesh, cfg, rep)
                if os.path.exists(os.path.join(d, "record.json")):
                    yield mesh, cfg, rep, d


def measure(d, plane, cache=True):
    out = os.path.join(d, "sym-report.json")
    if cache and os.path.exists(out) and os.path.getmtime(out) >= os.path.getmtime(os.path.join(d, "record.json")):
        return json.load(open(out))
    rec = json.load(open(os.path.join(d, "record.json")))
    r = {"rc": rec["rc"], "wall": rec.get("wall")}
    if rec["rc"] == 0 and os.path.exists(os.path.join(d, "out.obj")):
        V, F = read_obj(os.path.join(d, "out.obj"))
        r.update(seam_numbers(V, F, "xyz".index(plane[0]), plane[1]))
        m = rec.get("metrics") or {}
        r["quads_over_target"] = m.get("faces", {}).get("quads_over_target")
        r["non_quads"] = m.get("faces", {}).get("total", 0) - m.get("faces", {}).get("quads", 0) if m else None
        r["open_edges"] = m.get("topology", {}).get("boundary_edges")
        r["non_manifold_edges"] = m.get("topology", {}).get("non_manifold_edges")
        r["folded"] = m.get("validity", {}).get("folded_or_twisted_faces")
        r["holes_added"] = m.get("validity", {}).get("holes_added")
        r["handles_lost"] = m.get("validity", {}).get("handles_lost")
        r["sym_matched"] = m.get("symmetry", {}).get("topo_matched_lt_0.1_edge")
        r["hausdorff"] = m.get("distance", {}).get("hausdorff")
        r["sj_p5"] = m.get("quads", {}).get("sj_p5")
        seam = os.path.join(d, "seam.json")
        if os.path.exists(seam):
            w = json.load(open(seam))["weld"]
            r["border_off_plane"] = w.get("border_off_plane_vertices")
            r["crossed_vertices"] = w.get("crossed_vertices")
    json.dump(r, open(out, "w"), indent=1)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--set", default="symmetry")
    ap.add_argument("--corpus", default=os.environ.get("AR_CORPUS") or os.path.join(HERE, "..", "bench", "corpus.json"))
    ap.add_argument("--json")
    a = ap.parse_args()
    corpus = json.load(open(a.corpus))
    planes = {m["id"]: m["sym"] for m in corpus["meshes"] if m.get("set") == a.set and m.get("sym")}
    keys = ["quads_over_target", "non_quads", "open_edges", "non_manifold_edges", "folded", "holes_added",
            "sym_matched", "seam_vertices", "seam_poles", "seam_non_quads", "seam_folds", "seam_kink_mean", "turn_elsewhere_mean",
            "loops_crossing", "loops_crossing_closed", "border_off_plane", "hausdorff", "sj_p5", "wall"]
    table = {}
    for folder in a.runs:
        label = os.path.basename(folder.rstrip("/"))
        for mesh, cfg, rep, d in runs(folder, a.set):
            plane = list(planes[mesh])
            if plane[1] == "auto":
                src = next(m["file"] for m in corpus["meshes"] if m["id"] == mesh and m.get("set") == a.set)
                work = os.environ.get("AR_WORK", os.path.expanduser("~/ar-work"))
                xs = [float(line.split()["xyz".index(plane[0]) + 1])
                      for line in open(os.path.join(work, "meshes", src)) if line.startswith("v ")]
                plane[1] = (min(xs) + max(xs)) / 2
            table.setdefault((mesh, cfg), {}).setdefault(label, []).append(measure(d, plane))
    summary = {}
    for (mesh, cfg), sides in sorted(table.items()):
        print(f"\n{mesh} {cfg}")
        for label, rs in sides.items():
            ok = [r for r in rs if r["rc"] == 0]
            row = {"runs": len(rs), "failed": len(rs) - len(ok)}
            for k in keys:
                vals = [r.get(k) for r in ok if r.get(k) is not None]
                if vals:
                    row[k] = [float(np.mean(vals)), float(min(vals)), float(max(vals))]
            summary.setdefault(f"{mesh} {cfg}", {})[label] = row
            print(f"  {label[:28]:28s} n={len(rs)} fail={row['failed']} " + " ".join(
                f"{k}={row[k][0]:.3g}[{row[k][1]:.3g},{row[k][2]:.3g}]" for k in keys if k in row))
    if a.json:
        json.dump(summary, open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
