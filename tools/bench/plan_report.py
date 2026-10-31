#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""How well the core's plan (--plan-only) predicts the run (docs/PREVIEW.md gates P1-P3).

Reads two `ab.py run` folders over the same set and targets: plans (`--cli-arg=--plan-only`) and
realised runs. For every plan target N0 and run target N it rescales the plan made at N0 to N the
way the add-on's overlay does while Target Quads is dragged (each island's quads
q(N) = max(N * area share, floor), edge * sqrt(q(N0) / q(N))) and compares it with the run at N.
N0 = N is the fresh plan (gate P1); N0 != N shows how far from its plan the rescale holds.

Per run and plan:
  size     per quad of the run: sqrt(quad area) (for a quad of sides su, sv that is sqrt(su sv), the
           edge the plan gives), against the plan at the input vertex nearest the quad's centre;
           r2 = share of the variance of log2 size the plan explains (bias removed, as P1 asks),
           r2s = the same after both sides are averaged over each quad's 16 nearest quads (the
           size of a region, which is what the overlay's colours show; single quads also carry
           the extraction's quad-to-quad noise, which no plan predicts),
           bias = median log2(run / plan), err = median |log2(run / plan)| (with the bias: what a
           user reads off the overlay), errA = the same weighted by quad area (share of the
           surface), p90 = its 90th percentile
  count    run quads / planned quads at N (gate P2: within 10 %)
  time     plan seconds / run seconds at the same target (gate P3: median <= 0.15)
  replan   (N0 != N) median |log2| between the rescaled plan and the fresh plan at N, per input
           vertex: the error the rescale adds, without the run's own noise
  change   (second table) how the run's region size moves between two targets A and B, against
           how the plan rescaled from N0 says it moves: at up to 20,000 input vertices, the
           region size (sqrt of the mean area of the 16 nearest quads) of run B over run A, in
           log2, minus the planned log2 ratio; median |.| per mesh. This is what a swipe shows.

Metric code is separate from metrics.py and preview_report.py on purpose.

    python plan_report.py PLANS RUNS --set corpus [--work DIR] [--json out.json]
"""
import argparse
import json
import math
import os
import sys

import numpy as np
from scipy.spatial import cKDTree

HERE = os.path.dirname(os.path.abspath(__file__))


def read_obj(path):
    """Vertex positions (n, 3) and faces (lists of 0-based indices) of an OBJ."""
    vs, fs = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                vs.append(line.split()[1:4])
            elif line.startswith("f "):
                fs.append([int(t.split("/")[0]) - 1 for t in line.split()[1:]])
    return np.asarray(vs, dtype=float).reshape(-1, 3), fs


def read_plan(d):
    """(edge per input vertex, island per input vertex, islands, report) of a plan run folder."""
    report = json.load(open(os.path.join(d, "report.json")))
    plan = report["plan"]
    n = plan["vertices"]
    raw = np.fromfile(os.path.join(d, "plan.bin"), dtype="<u4")
    if raw.size != 2 * n:
        raise ValueError(f"{d}: plan.bin has {raw.size} words, report says 2 x {n}")
    edge = raw[:n].view("<f4").astype(float)
    island = raw[n:].astype(np.int64)
    return edge, island, plan["islands"], report


def island_quads(islands, n_target, scaling=1.0):
    """Planned quads of each island at Target Quads n_target (left-out islands: 0)."""
    share = np.array([i["area_share"] for i in islands])
    floor = np.array([i["floor_quads"] for i in islands])
    out = np.array([i["left_out"] for i in islands])
    q = np.maximum(n_target * share, floor) / (scaling * scaling)
    return np.where(out, 0.0, q)


def rescale(edge, island, islands, n0, n):
    """The plan made at n0, rescaled to n (the overlay's rule)."""
    q0, q = island_quads(islands, n0), island_quads(islands, n)
    if island.size == 0:
        return edge
    valid = island < len(islands)
    factor = np.ones(len(islands))
    ok = (q0 > 0) & (q > 0)
    factor[ok] = np.sqrt(q0[ok] / q[ok])
    out = edge.copy()
    out[valid] *= factor[island[valid]]
    return out


def quad_sizes(v, faces):
    """Centre and sqrt(area) of every quad (other faces left out)."""
    quads = np.asarray([f for f in faces if len(f) == 4], dtype=np.int64).reshape(-1, 4)
    p = v[quads]
    a = 0.5 * np.linalg.norm(np.cross(p[:, 2] - p[:, 0], p[:, 3] - p[:, 1]), axis=1)
    return p.mean(axis=1), np.sqrt(a)


SMOOTH_QUADS = 16


def explained(y, x):
    """Share of the variance of y that x explains, a constant offset removed."""
    var = float(np.var(y))
    return 1.0 - float(np.var(y - x)) / var if var > 0 else None


def size_stats(centres, run_size, plan_size):
    ok = (run_size > 0) & (plan_size > 0)
    if ok.sum() < 2 * SMOOTH_QUADS:
        return None
    centres, run_size, plan_size = centres[ok], run_size[ok], plan_size[ok]
    lr, lp = np.log2(run_size), np.log2(plan_size)
    resid = lr - lp
    _, near = cKDTree(centres).query(centres, k=SMOOTH_QUADS)
    # Region size: sqrt of the mean quad area around each quad, on both sides.
    sr = 0.5 * np.log2((run_size[near] ** 2).mean(axis=1))
    sp = 0.5 * np.log2((plan_size[near] ** 2).mean(axis=1))
    area = run_size ** 2
    return {"r2": explained(lr, lp), "r2s": explained(sr, sp),
            "bias": float(np.median(resid)), "err": float(np.median(np.abs(resid))),
            "errA": float(np.average(np.abs(resid), weights=area)),
            "p90": float(np.percentile(np.abs(resid), 90)), "n": int(ok.sum())}


def region_sizes(run_dir, points):
    """log2 region size of a run at points: sqrt of the mean area of the SMOOTH_QUADS nearest quads."""
    v, faces = read_obj(os.path.join(run_dir, "out.obj"))
    centres, size = quad_sizes(v, faces)
    ok = size > 0
    _, near = cKDTree(centres[ok]).query(points, k=SMOOTH_QUADS)
    return 0.5 * np.log2((size[ok][near] ** 2).mean(axis=1))


CHANGE_PAIRS = ((4200, 5000), (5000, 6000), (3500, 5000), (5000, 7100), (2500, 5000), (5000, 10000))


def targets_of(root, set_name, mesh):
    d = os.path.join(root, set_name, mesh)
    out = []
    for cfg in os.listdir(d) if os.path.isdir(d) else []:
        if cfg.startswith("quality-t"):
            out.append(int(cfg[len("quality-t"):]))
    return sorted(out)


def run_ok(d, name):
    try:
        r = json.load(open(os.path.join(d, "record.json")))
    except OSError:
        return None
    return r if r.get("rc") == 0 and os.path.exists(os.path.join(d, name)) else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("plans", help="ab.py run folder made with --cli-arg=--plan-only")
    ap.add_argument("runs", help="ab.py run folder of the same ref without it")
    ap.add_argument("--set", default="corpus")
    ap.add_argument("--work", default=os.environ.get("AR_WORK", os.path.expanduser("~/ar-work")))
    ap.add_argument("--json", help="write the per-mesh rows here")
    a = ap.parse_args()

    corpus = json.load(open(os.path.join(HERE, "corpus.json")))
    files = {m["id"]: m["file"] for m in corpus["meshes"] if m.get("set", "corpus") == a.set}
    rows = []
    changes = []
    for mesh in sorted(files):
        plan_targets = [t for t in targets_of(a.plans, a.set, mesh)
                        if run_ok(os.path.join(a.plans, a.set, mesh, f"quality-t{t}", "u0"), "plan.bin")]
        run_targets = [t for t in targets_of(a.runs, a.set, mesh)
                       if run_ok(os.path.join(a.runs, a.set, mesh, f"quality-t{t}", "u0"), "out.obj")]
        if not plan_targets or not run_targets:
            continue
        inp, _ = read_obj(os.path.join(a.work, "meshes", files[mesh]))
        tree = cKDTree(inp)
        plans = {t: read_plan(os.path.join(a.plans, a.set, mesh, f"quality-t{t}", "u0")) for t in plan_targets}
        for edge, _, _, _ in plans.values():
            if edge.size != len(inp):
                sys.exit(f"{mesh}: plan has {edge.size} vertices, input {len(inp)}")
        for n in run_targets:
            d = os.path.join(a.runs, a.set, mesh, f"quality-t{n}", "u0")
            v, faces = read_obj(os.path.join(d, "out.obj"))
            report = json.load(open(os.path.join(d, "report.json")))
            centres, size = quad_sizes(v, faces)
            _, nearest = tree.query(centres)
            for n0, (edge, island, islands, preport) in plans.items():
                scaled = rescale(edge, island, islands, n0, n)
                row = {"mesh": mesh, "n0": n0, "n": n, "ratio": n / n0,
                       "size": size_stats(centres, size, scaled[nearest]),
                       "planned_quads": float(island_quads(islands, n).sum()),
                       "run_quads": report["output"]["quads"]}
                row["count_ratio"] = row["run_quads"] / row["planned_quads"] if row["planned_quads"] else None
                if n0 == n:
                    row["time_ratio"] = preport["time"]["total_seconds"] / report["time"]["total_seconds"]
                    row["plan_seconds"] = preport["time"]["total_seconds"]
                    row["run_seconds"] = report["time"]["total_seconds"]
                elif n in plans:
                    fresh = plans[n][0]
                    ok = (fresh > 0) & (scaled > 0)
                    row["replan"] = float(np.median(np.abs(np.log2(scaled[ok] / fresh[ok])))) if ok.any() else None
                rows.append(row)
        n0 = min(plans, key=lambda t: abs(t - 5000))
        edge, island, islands, _ = plans[n0]
        sample = np.random.default_rng(0).choice(len(inp), min(len(inp), 20000), replace=False)
        region = {}
        for a_n, b_n in CHANGE_PAIRS:
            if a_n not in run_targets or b_n not in run_targets:
                continue
            for n in (a_n, b_n):
                if n not in region:
                    region[n] = region_sizes(os.path.join(a.runs, a.set, mesh, f"quality-t{n}", "u0"), inp[sample])
            pa = rescale(edge, island, islands, n0, a_n)[sample]
            pb = rescale(edge, island, islands, n0, b_n)[sample]
            ok = (pa > 0) & (pb > 0)
            d = (region[b_n] - region[a_n])[ok] - np.log2(pb[ok] / pa[ok])
            changes.append({"mesh": mesh, "a": a_n, "b": b_n, "planned": float(np.median(np.log2(pb[ok] / pa[ok]))),
                            "err": float(np.median(np.abs(d)))})

    if a.json:
        with open(a.json, "w") as f:
            json.dump({"rows": rows, "changes": changes}, f, indent=1)

    def med(xs):
        xs = [x for x in xs if x is not None and not (isinstance(x, float) and math.isnan(x))]
        return float(np.median(xs)) if xs else None

    def fmt(x, p=2):
        return "-" if x is None else f"{x:.{p}f}"

    ratios = sorted({round(r["ratio"], 2) for r in rows})
    print(f"set {a.set}: {len({r['mesh'] for r in rows})} meshes, medians over (mesh, N0) pairs")
    print(f"{'N/N0':>6} {'pairs':>5} {'r2':>5} {'r2s':>5} {'min r2s':>7} {'bias':>6} {'err':>5} {'errA':>5} "
          f"{'p90':>5} {'count':>6} {'in10%':>6} {'replan':>6}")
    for ratio in ratios:
        sel = [r for r in rows if round(r["ratio"], 2) == ratio and r["size"]]
        r2 = [r["size"]["r2"] for r in sel if r["size"]["r2"] is not None]
        r2s = [r["size"]["r2s"] for r in sel if r["size"]["r2s"] is not None]
        counts = [r["count_ratio"] for r in sel if r["count_ratio"]]
        within = sum(1 for c in counts if abs(c - 1) <= 0.10) / len(counts) if counts else None
        print(f"{ratio:6.2f} {len(sel):5d} {fmt(med(r2))} {fmt(med(r2s))} {fmt(min(r2s) if r2s else None):>7} "
              f"{fmt(med([r['size']['bias'] for r in sel])):>6} {fmt(med([r['size']['err'] for r in sel])):>5} "
              f"{fmt(med([r['size']['errA'] for r in sel])):>5} "
              f"{fmt(med([r['size']['p90'] for r in sel])):>5} {fmt(med(counts), 3):>6} {fmt(within):>6} "
              f"{fmt(med([r.get('replan') for r in sel]), 3):>6}")
    if changes:
        print("region size change between two runs against the plan's change (log2, median |error| per mesh)")
        for a_n, b_n in CHANGE_PAIRS:
            sel = [c for c in changes if (c["a"], c["b"]) == (a_n, b_n)]
            if sel:
                worst = max(sel, key=lambda c: c["err"])
                print(f"  {a_n:>6} -> {b_n:<6} planned {med([c['planned'] for c in sel]):+.3f}  "
                      f"error median {med([c['err'] for c in sel]):.3f}, max {worst['err']:.3f} ({worst['mesh']})")
    fresh = [r for r in rows if r["n0"] == r["n"]]
    print("fresh plans (N = N0), per mesh: r2, r2s, errA (medians over targets)")
    for mesh in sorted({r["mesh"] for r in fresh}):
        sel = [r["size"] for r in fresh if r["mesh"] == mesh and r["size"]]
        print(f"  {mesh:24s} {fmt(med([x['r2'] for x in sel]))} {fmt(med([x['r2s'] for x in sel]))} "
              f"{fmt(med([x['errA'] for x in sel]))}")
    times = [r["time_ratio"] for r in fresh if r.get("time_ratio")]
    if times:
        print(f"P3 plan time / run time at the same target: median {med(times):.3f}, max {max(times):.3f} "
              f"({len(times)} runs); plan seconds max {max(r['plan_seconds'] for r in fresh):.2f}")


if __name__ == "__main__":
    main()
