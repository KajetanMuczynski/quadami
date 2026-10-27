#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""What happens at the sharp features of ab.py runs (made for the primitives set, make_primitives.py;
works on any set): rims and tips, which the whole-mesh metrics average away.

    python primitive_report.py --runs $AR_WORK/runs/<key> [--set primitives] [--only a,b] [--csv out.csv]

h = sqrt(input area / target), the planned quad edge. Per run:
- `bevel_p50_h` / `bevel_max_h`: distance from samples along the input's sharp edges (dihedral > 40 deg,
  metrics.py's A1 edges) to the output surface, in h: how far a rim is rounded off.
- `thin_share`: share of the output faces within 2 h of a sharp edge whose longest edge is more than 3x
  their shortest (rows of thin quads crowding a rim).
- `tip_gap_h`: largest distance, over the input's tips (vertices with an angle deficit of at least
  45 deg: an icosahedron vertex 60, a cube corner 90, the Blender cone's apex 199), from the tip to the output surface, in h:
  how much of a point is cut off.
- `tip_quarters`: the output's irregular vertices within 3 h of a tip, as quarter turns of angle
  deficit (valence 3: +1, 2: +2, 5: -1, ...), against `tip_need` = the tip's deficit in quarter turns.
- `irr_per_1k`, `crease_cov`, `hausdorff` (of the diagonal), `folded`, `non_quads`, `holes_added`,
  `quad_err` from report.json and metrics.json.
The table gives the mean over replicates and, for the counts, the range.
"""
import argparse
import csv
import glob
import json
import math
import os
import sys
from collections import Counter, defaultdict

import igl
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SHARP_DEG = 40.0
TIP_DEFICIT_DEG = 45.0


def read_obj(path):
    V, F = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                V.append([float(x) for x in line.split()[1:4]])
            elif line.startswith("f "):
                F.append([int(t.split("/")[0]) - 1 for t in line.split()[1:]])
    return np.asarray(V, float), F


def triangulate(F):
    return np.asarray([[f[0], f[k], f[k + 1]] for f in F for k in range(1, len(f) - 1)], np.int64)


class InputFeatures:
    def __init__(self, path):
        V, F = read_obj(path)
        T = triangulate(F)
        self.V, self.T = V, T
        n = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
        area2 = np.linalg.norm(n, axis=1)
        self.area = 0.5 * area2.sum()
        n = n / np.maximum(area2, 1e-300)[:, None]
        edges = defaultdict(list)
        for t, tri in enumerate(T):
            for k in range(3):
                a, b = int(tri[k]), int(tri[(k + 1) % 3])
                edges[(min(a, b), max(a, b))].append(t)
        self.sharp = []
        for (a, b), ts in edges.items():
            if len(ts) == 2:
                c = float(np.clip(n[ts[0]] @ n[ts[1]], -1, 1))
                if math.degrees(math.acos(c)) > SHARP_DEG:
                    self.sharp.append((a, b))
        angle = np.zeros(len(V))
        boundary = set(v for (a, b), ts in edges.items() if len(ts) == 1 for v in (a, b))
        for tri in T:
            for k in range(3):
                p, q, r = V[tri[k]], V[tri[(k + 1) % 3]], V[tri[(k + 2) % 3]]
                u, w = q - p, r - p
                nu, nw = np.linalg.norm(u), np.linalg.norm(w)
                if nu > 0 and nw > 0:
                    angle[tri[k]] += math.acos(float(np.clip(u @ w / (nu * nw), -1, 1)))
        deficit = np.degrees(2 * math.pi - angle)
        self.tips = [(v, deficit[v]) for v in range(len(V))
                     if v not in boundary and deficit[v] >= TIP_DEFICIT_DEG]

    def sharp_samples(self, step):
        pts = []
        for a, b in self.sharp:
            p, q = self.V[a], self.V[b]
            k = max(1, int(math.ceil(np.linalg.norm(q - p) / step)))
            for i in range(k):
                pts.append(p + (q - p) * (i + 0.5) / k)
        return np.asarray(pts).reshape(-1, 3)


def score(run_dir, feat, target):
    out = os.path.join(run_dir, "out.obj")
    if not os.path.exists(out):
        return None
    V, F = read_obj(out)
    if not F:
        return None
    T = triangulate(F)
    h = math.sqrt(feat.area / target)
    r = {}
    S = feat.sharp_samples(h / 4)
    if len(S):
        d2, _, _ = igl.point_mesh_squared_distance(S, V, T)
        d = np.sqrt(d2) / h
        r["bevel_p50_h"] = float(np.median(d))
        r["bevel_max_h"] = float(d.max())
        cent = np.asarray([V[f].mean(axis=0) for f in F])
        # nearest sharp sample per face centroid (brute force in chunks; samples are few)
        near = np.zeros(len(F), bool)
        for i in range(0, len(cent), 2048):
            c = cent[i:i + 2048]
            dd = np.min(((c[:, None, :] - S[None, :, :]) ** 2).sum(-1), axis=1)
            near[i:i + 2048] = dd < (2 * h) ** 2
        thin = []
        for f, is_near in zip(F, near):
            if is_near:
                e = [np.linalg.norm(V[f[i]] - V[f[(i + 1) % len(f)]]) for i in range(len(f))]
                thin.append(max(e) > 3 * max(min(e), 1e-300))
        r["thin_share"] = float(np.mean(thin)) if thin else 0.0
    if feat.tips:
        P = feat.V[[v for v, _ in feat.tips]]
        d2, _, _ = igl.point_mesh_squared_distance(P, V, T)
        r["tip_gap_h"] = float(np.sqrt(d2.max()) / h)
        val = Counter(v for f in F for v in f)
        quarters, need = 0, 0.0
        for (v, deficit), p in zip(feat.tips, P):
            need += deficit / 90.0
            for w, k in val.items():
                if k != 4 and np.linalg.norm(V[w] - p) < 3 * h:
                    quarters += 4 - k
        r["tip_quarters"] = quarters
        r["tip_need"] = need
    rep = json.load(open(os.path.join(run_dir, "report.json")))
    o = rep.get("output", {})
    r["quad_err"] = o.get("quads", 0) / target - 1
    r["folded"] = o.get("folded_faces")
    r["non_quads"] = o.get("non_quads")
    mp = os.path.join(run_dir, "metrics.json")
    if os.path.exists(mp):
        m = json.load(open(mp))
        r["irr_per_1k"] = (m.get("valence") or {}).get("irregular_per_1k_quads")
        r["crease_cov"] = (m.get("creases") or {}).get("coverage")
        r["hausdorff"] = (m.get("distance") or {}).get("hausdorff")
        r["holes_added"] = (m.get("validity") or {}).get("holes_added")
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", required=True, help="an ab.py run folder: $AR_WORK/runs/<key>")
    ap.add_argument("--set", default="primitives")
    ap.add_argument("--only", help="comma list of mesh ids")
    ap.add_argument("--meshes", default=os.path.join(os.environ.get("AR_WORK", os.path.expanduser("~/ar-work")), "meshes"))
    ap.add_argument("--csv")
    args = ap.parse_args()
    corpus = json.load(open(os.environ.get("AR_CORPUS") or os.path.join(HERE, "corpus.json")))
    files = {m["id"]: m["file"] for m in corpus["meshes"]}
    only = set(args.only.split(",")) if args.only else None
    rows, feats = [], {}
    for rec_path in sorted(glob.glob(os.path.join(args.runs, args.set, "*", "*", "u*", "record.json"))):
        rec = json.load(open(rec_path))
        if only and rec["mesh"] not in only:
            continue
        if rec["mesh"] not in feats:
            path = files[rec["mesh"]]
            feats[rec["mesh"]] = InputFeatures(path if os.path.isabs(path) else os.path.join(args.meshes, path))
        s = score(os.path.dirname(rec_path), feats[rec["mesh"]], rec["target"])
        row = {"mesh": rec["mesh"], "res": rec["res"], "target": rec["target"], "u": rec["replicate"], "rc": rec["rc"]}
        row.update(s or {})
        rows.append(row)
    keys = ["quad_err", "hausdorff", "crease_cov", "bevel_p50_h", "bevel_max_h", "thin_share",
            "tip_gap_h", "tip_quarters", "tip_need", "irr_per_1k", "folded", "non_quads", "holes_added"]
    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.DictWriter(f, ["mesh", "res", "target", "u", "rc"] + keys)
            w.writeheader()
            w.writerows(rows)
    groups = defaultdict(list)
    for row in rows:
        groups[(row["mesh"], row["res"], row["target"])].append(row)
    print("| mesh | target | n | " + " | ".join(keys) + " |")
    print("|---|---|---|" + "---|" * len(keys))
    for (mesh, res, target), g in sorted(groups.items()):
        cells = []
        for k in keys:
            vals = [x[k] for x in g if x.get(k) is not None]
            if not vals:
                cells.append("")
            elif k in ("tip_quarters", "folded", "non_quads", "holes_added"):
                lo, hi = min(vals), max(vals)
                cells.append("%g" % lo if lo == hi else "%g-%g" % (lo, hi))
            elif k == "tip_need":
                cells.append("%.2f" % vals[0])
            else:
                cells.append("%.3g" % (sum(vals) / len(vals)))
        print("| %s | %d | %d | %s |" % (mesh, target, len(g), " | ".join(cells)))


if __name__ == "__main__":
    main()
