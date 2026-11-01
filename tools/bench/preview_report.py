#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""How well a cheaper run predicts the run a user keeps (docs/PREVIEW.md).

Reads finished `ab.py run` folders and compares, mesh by mesh, a reference run (by default the set's
largest target at quality resolution, plain input) with cheaper "preview" runs of the same mesh:
fewer quads, fast resolution, or a 1-ulp replicate of the reference itself (the noise floor: what no
preview can beat). Metric code is separate from metrics.py on purpose (an independent route).

Per mesh and preview:
  time        CLI time of the preview / the reference
  poles       interior vertices of valence != 4; recall = share of reference poles with a preview pole
              within `tol`, chance = the same with as many preview vertices drawn at random (mean of
              20 draws), skill = (recall - chance) / (1 - chance); tol = 2 and 4 x the reference's
              mean edge length (the same distance for every preview, whatever its quad size)
  loops       Blender's Alt+click rule (go straight through valence-4 vertices, stop at a pole or a
              boundary, or close): share of loops that close, and the longest loop's share of all
              vertices (a spiral that runs over the whole part shows as a large share)
  cost        (single-island runs) share of the run's time spent before a plan-only preview could
              stop (the whole run minus the isotropic remesher's own steps, the parameterize and
              extract phases, retries, merge, the final surface projection and relax and the
              lost-parts pass: so input cleanup, fold check,
              split, contexts and decimation) and before a flow preview could stop (also the
              isotropic remesh, size field, frame field and singularity simplification)
  size        local edge length (mean of a vertex's edges); the preview's value at the nearest preview
              vertex, scaled by sqrt(quads_preview / quads_reference), against the reference's:
              r2 = share of the variance of log size the preview explains, err = median |log2 ratio|

    python preview_report.py $AR_WORK/runs/<key> --set corpus [--ref quality-t10000] [--json out.json]
"""
import argparse
import collections
import glob
import json
import math
import os
import sys

import numpy as np
from scipy.spatial import cKDTree


def load_obj(path):
    vs, fs = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                vs.append([float(x) for x in line.split()[1:4]])
            elif line.startswith("f "):
                fs.append([int(t.split("/")[0]) - 1 for t in line.split()[1:]])
    return np.asarray(vs, dtype=float), fs


class QuadMesh:
    def __init__(self, path):
        self.v, self.f = load_obj(path)
        self.quads = sum(1 for f in self.f if len(f) == 4)
        efaces = collections.defaultdict(list)
        for fi, f in enumerate(self.f):
            for i in range(len(f)):
                a, b = f[i], f[(i + 1) % len(f)]
                efaces[(min(a, b), max(a, b))].append(fi)
        self.efaces = efaces
        nb = collections.defaultdict(set)
        for a, b in efaces:
            nb[a].add(b)
            nb[b].add(a)
        self.nb = nb
        boundary = set()
        for (a, b), fl in efaces.items():
            if len(fl) != 2:
                boundary.update((a, b))
        self.boundary = boundary
        edges = np.asarray(list(efaces.keys()), dtype=np.int64)
        lengths = np.linalg.norm(self.v[edges[:, 0]] - self.v[edges[:, 1]], axis=1)
        self.mean_edge = float(lengths.mean()) if len(lengths) else 0.0
        total = np.zeros(len(self.v))
        count = np.zeros(len(self.v))
        np.add.at(total, edges[:, 0], lengths)
        np.add.at(total, edges[:, 1], lengths)
        np.add.at(count, edges[:, 0], 1)
        np.add.at(count, edges[:, 1], 1)
        self.used = count > 0
        self.local_edge = np.where(self.used, total / np.maximum(count, 1), np.nan)
        self.poles = np.asarray([v for v in nb if v not in boundary and len(nb[v]) != 4], dtype=np.int64)

    def loops(self):
        """Closed loops / all loops, and the longest loop's share of the used vertices (Alt+click rule)."""
        def opposite(v, u):
            if len(self.nb[v]) != 4 or v in self.boundary:
                return None
            side = set()
            for fi in self.efaces[(min(u, v), max(u, v))]:
                side.update(self.f[fi])
            c = [w for w in self.nb[v] if w != u and w not in side]
            return c[0] if len(c) == 1 else None

        seen = set()
        closed, longest, total = 0, 0, 0
        for e in self.efaces:
            if e in seen:
                continue
            a, b = e
            edges, verts, is_closed = {e}, {a, b}, False
            for s, t in ((a, b), (b, a)):
                u, v = s, t
                while True:
                    w = opposite(v, u)
                    if w is None:
                        break
                    k = (min(v, w), max(v, w))
                    if k in edges:
                        is_closed = True
                        break
                    edges.add(k)
                    verts.add(w)
                    u, v = v, w
                if is_closed:
                    break
            seen |= edges
            closed += is_closed
            total += 1
            longest = max(longest, len(verts))
        return closed / max(total, 1), longest / max(int(self.used.sum()), 1)


def pole_recall(ref, prev, tol, rng):
    if len(ref.poles) == 0:
        return None, None
    if len(prev.poles) == 0:
        return 0.0, 0.0
    d, _ = cKDTree(prev.v[prev.poles]).query(ref.v[ref.poles])
    recall = float(np.mean(d <= tol))
    used = np.flatnonzero(prev.used)
    chance = []
    for _ in range(20):
        pick = rng.choice(used, size=min(len(prev.poles), len(used)), replace=False)
        d, _ = cKDTree(prev.v[pick]).query(ref.v[ref.poles])
        chance.append(np.mean(d <= tol))
    return recall, float(np.mean(chance))


def size_agreement(ref, prev):
    rv = np.flatnonzero(ref.used)
    pv = np.flatnonzero(prev.used)
    _, j = cKDTree(prev.v[pv]).query(ref.v[rv])
    scale = math.sqrt(max(prev.quads, 1) / max(ref.quads, 1))
    lr = np.log2(ref.local_edge[rv])
    lp = np.log2(prev.local_edge[pv][j] * scale)
    resid = lr - lp
    var = float(np.var(lr))
    r2 = 1.0 - float(np.mean((resid - resid.mean()) ** 2)) / var if var > 0 else None
    return r2, float(np.median(np.abs(resid)))


ISOTROPIC_STEPS = ("Building bounding volume tree", "Splitting long edges", "Collapsing short edges",
                   "Flipping edges", "Shifting vertices", "Projecting vertices")
FLOW_STEPS = ("Computing vertex normals", "Computing scaling field", "Building surface topology",
              "Solving frame field", "Simplifying singularities")


def stage_cost(d):
    """(plan-only prefix ms, flow prefix ms, total ms) of a single-island run, else None."""
    run, steps, island, post_ms = None, {}, [], 0.0
    with open(os.path.join(d, "stages.jsonl")) as f:
        for line in f:
            r = json.loads(line)
            if r.get("stage") == "run":
                run = r
            elif r.get("stage") == "step":
                steps[r.get("name")] = r["accumulated_ms"]
            elif r.get("stage") == "island":
                island.append(r)
            elif r.get("stage") in ("surface_projection", "surface_relax"):
                post_ms += 1000.0 * r.get("seconds", 0.0)
    try:
        with open(os.path.join(d, "report.json")) as f:
            post_ms += json.load(f).get("diagnostics", {}).get("lost_parts_ms", 0.0)
    except OSError:
        pass
    if run is None or run.get("islands") != 1 or len(island) != 1:
        return None
    total = run["total_ms"]
    iso = sum(steps.get(n, 0.0) for n in ISOTROPIC_STEPS)
    after = (run.get("parameterize_wall_ms", 0.0) + run.get("retry_wall_ms", 0.0) + run.get("merge_ms", 0.0)
             + run.get("thin_parts_ms", 0.0) + post_ms)
    plan = total - iso - after
    flow = plan + iso + sum(steps.get(n, 0.0) for n in FLOW_STEPS)
    return plan, flow, total


def run_dir(root, set_name, mesh, cfg, rep):
    return os.path.join(root, set_name, mesh, cfg, "u%d" % rep)


def record(d):
    try:
        with open(os.path.join(d, "record.json")) as f:
            r = json.load(f)
    except OSError:
        return None
    if r.get("rc") != 0 or not os.path.exists(os.path.join(d, "out.obj")):
        return None
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("runs", help="an ab.py run folder: $AR_WORK/runs/<key>")
    ap.add_argument("--set", default="corpus")
    ap.add_argument("--ref", help="reference config, e.g. quality-t10000 (default: largest quality target)")
    ap.add_argument("--json", help="write per-mesh rows here")
    args = ap.parse_args()
    base = os.path.join(args.runs, args.set)
    meshes = sorted(os.path.basename(p) for p in glob.glob(os.path.join(base, "*")) if os.path.isdir(p))
    configs = sorted({os.path.basename(p) for p in glob.glob(os.path.join(base, "*", "*"))})
    ref_cfg = args.ref or max((c for c in configs if c.startswith("quality-t")),
                              key=lambda c: int(c.split("-t")[1]))
    previews = [(c, 0) for c in configs if c != ref_cfg] + [(ref_cfg, 1)]
    rng = np.random.default_rng(1)
    rows = []
    for mesh in meshes:
        rref = record(run_dir(args.runs, args.set, mesh, ref_cfg, 0))
        if rref is None:
            continue
        ref = QuadMesh(os.path.join(run_dir(args.runs, args.set, mesh, ref_cfg, 0), "out.obj"))
        ref_loops = ref.loops()
        for cfg, rep in previews:
            d = run_dir(args.runs, args.set, mesh, cfg, rep)
            rp = record(d)
            if rp is None:
                continue
            prev = QuadMesh(os.path.join(d, "out.obj"))
            skills = {}
            for k in (2, 4):
                recall, chance = pole_recall(ref, prev, k * ref.mean_edge, rng)
                skills[k] = (None if recall is None or chance >= 1.0
                             else (recall - chance) / (1.0 - chance))
            r2, err = size_agreement(ref, prev)
            loops = prev.loops()
            rows.append({
                "mesh": mesh, "preview": cfg + ("/u1" if rep else ""),
                "time_ratio": rp["time"] / rref["time"] if rref.get("time") else None,
                "quads": [prev.quads, ref.quads],
                "poles": [int(len(prev.poles)), int(len(ref.poles))],
                "pole_skill_2h": skills[2], "pole_skill_4h": skills[4],
                "closed_share": [round(loops[0], 4), round(ref_loops[0], 4)],
                "longest_loop_share": [round(loops[1], 4), round(ref_loops[1], 4)],
                "size_r2": r2, "size_err_log2": err,
            })
    costs = []
    for mesh in meshes:
        for cfg in configs:
            d = run_dir(args.runs, args.set, mesh, cfg, 0)
            if record(d) is not None:
                c = stage_cost(d)
                if c is not None:
                    costs.append({"mesh": mesh, "config": cfg, "plan_ms": c[0], "flow_ms": c[1], "total_ms": c[2]})
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"ref": ref_cfg, "set": args.set, "rows": rows, "costs": costs}, f, indent=1)

    def med(xs):
        xs = [x for x in xs if x is not None]
        return float(np.median(xs)) if xs else float("nan")

    print("set %s, reference %s (plain input), %d meshes" % (args.set, ref_cfg, len({r["mesh"] for r in rows})))
    print("| preview | n | time ratio | pole skill 2h | pole skill 4h | poles preview/ref | size r2 "
          "| size err (log2) | closed share diff | longest-loop share diff |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for cfg, rep in previews:
        name = cfg + ("/u1" if rep else "")
        rs = [r for r in rows if r["preview"] == name]
        if not rs:
            continue
        print("| %s | %d | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f |" % (
            name, len(rs), med([r["time_ratio"] for r in rs]), med([r["pole_skill_2h"] for r in rs]),
            med([r["pole_skill_4h"] for r in rs]), med([r["poles"][0] / max(r["poles"][1], 1) for r in rs]),
            med([r["size_r2"] for r in rs]), med([r["size_err_log2"] for r in rs]),
            med([abs(r["closed_share"][0] - r["closed_share"][1]) for r in rs]),
            med([abs(r["longest_loop_share"][0] - r["longest_loop_share"][1]) for r in rs])))
    print()
    print("| config (single-island runs) | n | run s (median) | plan-only prefix share | flow prefix share |")
    print("|---|---:|---:|---:|---:|")
    for cfg in configs:
        cs = [c for c in costs if c["config"] == cfg]
        if cs:
            print("| %s | %d | %.2f | %.2f (max %.2f) | %.2f (max %.2f) |" % (
                cfg, len(cs), med([c["total_ms"] / 1000 for c in cs]),
                med([c["plan_ms"] / c["total_ms"] for c in cs]), max(c["plan_ms"] / c["total_ms"] for c in cs),
                med([c["flow_ms"] / c["total_ms"] for c in cs]), max(c["flow_ms"] / c["total_ms"] for c in cs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
