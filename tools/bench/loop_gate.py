#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""The closure gate for guide loops: one rule, fixed before the runs it scores (TODO 5.7).

    loop_gate.py RUN [RUN ...] --set SET [--md out.md]   every ab.py run folder, per case
    loop_gate.py --input in.obj --obj out.obj            one run, one JSON line per guide loop

A guide loop CLOSES in a run when the output has a closed Alt+click loop (guides_report.output_loops:
through interior valence-4 vertices on the opposite edge, back to its first edge) with

    d95 <= 1.0 h_loc

where d95 is the larger of the two 95th percentiles of the exact point-to-segment distance, loop
samples to path and path samples to loop; the path is the drawn guide averaged over an arc length of
the output's median edge h (guides_report.smoothed: a path of input edges zigzags); h_loc is the
median length of the output edges whose midpoint is within 3 h of the path; both lines are sampled
every h_loc / 10. The loop of smallest d95 is taken.

Reported beside it, not part of the gate: d95 of the runner-up, d50 (the same with medians), the
number of irregular vertices within 0.5 h_loc of the path, and of irregular vertices that share a
face with the loop (poles on the next row, which the row bound allows).

Why 1.0 and the smoothed path: a closed loop on the guide measures 0.2 to 0.8 h_loc from the drawn
staircase depending on the input's edge length there, and 0.5 put the same loop on both sides of the
threshold across replicates (research note 2026-11-26). The loop one row over has d50 near 1, so the
table prints the largest d50 among the passes: well under 0.5 means no pass is a neighbouring row.
"""
import argparse
import collections
import glob
import json
import os
import sys

import numpy as np
from scipy.spatial import cKDTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import guides_report as gr  # noqa: E402

TOLERANCE = 1.0   # of h_loc
CONE_REACH = 0.5  # of h_loc
SAMPLE = 0.1      # of h_loc
NEAR = 3.0        # of h: the edges h_loc is taken from
VERSION = 2


def segment_distance(points, a, b):
    """Distance of each point to the nearest of the segments a[i]-b[i] (exact)."""
    out = np.full(len(points), np.inf)
    ab = b - a
    length2 = np.maximum((ab * ab).sum(1), 1e-30)
    for i in range(0, len(points), 256):
        p = points[i:i + 256, None, :]
        t = np.clip(((p - a[None]) * ab[None]).sum(2) / length2[None], 0.0, 1.0)
        q = a[None] + t[..., None] * ab[None]
        out[i:i + 256] = np.sqrt(((p - q) ** 2).sum(2)).min(1)
    return out


def analyse(input_path, output_path):
    positions_in, _, lines = gr.load_obj(input_path)
    positions, faces, _ = gr.load_obj(output_path)
    loops, _ = gr.guide_loops(lines)
    edges = set()
    for f in faces:
        for i in range(len(f)):
            a, b = f[i], f[(i + 1) % len(f)]
            edges.add((min(a, b), max(a, b)))
    if not loops or not edges:
        return []
    edges = np.array(sorted(edges))
    lengths = np.linalg.norm(positions[edges[:, 0]] - positions[edges[:, 1]], axis=1)
    h = float(np.median(lengths))
    mids = 0.5 * (positions[edges[:, 0]] + positions[edges[:, 1]])
    valence = np.bincount(edges.ravel(), minlength=len(positions))
    irregular = np.array([v for v in np.unique(edges) if valence[v] != 4], dtype=np.int64)
    irregular_set = set(int(v) for v in irregular)
    around = collections.defaultdict(set)
    for f in faces:
        for v in f:
            around[v].update(f)
    closed = [chain for is_closed, chain in gr.output_loops(faces) if is_closed]
    rows = []
    for cycle in loops:
        path = gr.smoothed(positions_in[cycle], h)
        pa, pb = path, np.roll(path, -1, axis=0)
        near = segment_distance(mids, pa, pb) < NEAR * h
        h_loc = float(np.median(lengths[near])) if near.any() else h
        path_samples = gr.sample(path, SAMPLE * h_loc, True)
        tree = cKDTree(path_samples)
        scored = []
        for chain in closed:
            p = positions[chain]
            rough, _ = tree.query(p)
            if np.median(rough) > 4.0 * h_loc:
                continue
            forward = segment_distance(gr.sample(p, SAMPLE * h_loc, True), pa, pb)
            back = segment_distance(path_samples, p, np.roll(p, -1, axis=0))
            scored.append((max(np.percentile(forward, 95), np.percentile(back, 95)) / h_loc,
                           max(np.percentile(forward, 50), np.percentile(back, 50)) / h_loc, chain))
        scored.sort(key=lambda s: s[0])
        cones = int((segment_distance(positions[irregular], pa, pb) < CONE_REACH * h_loc).sum()) if len(irregular) else 0
        row = {"version": VERSION, "guide_edges": len(cycle), "h": round(h, 6), "h_loc": round(h_loc, 6),
               "closes": False, "d95": None, "d50": None, "loop_edges": None, "runner_up": None,
               "cones_on_path": cones, "poles_on_next_row": None}
        if scored:
            d95, d50, chain = scored[0]
            ring = set(chain)
            row.update(closes=bool(d95 <= TOLERANCE), d95=round(float(d95), 4), d50=round(float(d50), 4),
                       loop_edges=len(chain), runner_up=round(float(scored[1][0]), 4) if len(scored) > 1 else None,
                       poles_on_next_row=len({w for v in chain for w in around[v] if w in irregular_set and w not in ring}))
        rows.append(row)
    return rows


def cached(run_dir, input_path):
    path = os.path.join(run_dir, "loop_gate.json")
    obj = os.path.join(run_dir, "out.obj")
    if os.path.exists(path) and os.path.getmtime(path) >= os.path.getmtime(obj):
        with open(path) as f:
            rows = json.load(f)
        if all(r.get("version") == VERSION for r in rows):
            return rows
    rows = analyse(input_path, obj)
    with open(path, "w") as f:
        json.dump(rows, f)
    return rows


def collect(run, setname, work):
    """{(mesh, target): {replicate: rows}} of a run folder."""
    out = collections.defaultdict(dict)
    sources = gr.corpus_sources(work)
    for obj in sorted(glob.glob(os.path.join(run, setname, "*", "*", "u*", "out.obj"))):
        d = os.path.dirname(obj)
        src = gr.input_of(d, work, sources)
        if src is None:
            continue
        parts = obj.split(os.sep)
        out[(parts[-4], int(parts[-3].rsplit("-t", 1)[-1]))][parts[-2]] = cached(d, src)
    return out


def table(runs, setname, work):
    lines = ["| run | mesh | target | loop | closes | of | in every replicate | d95 per replicate | largest d50 of a pass |",
             "|---|---|---|---|---|---|---|---|---|"]
    totals = []
    for label, run in runs:
        cases = collect(run, setname, work)
        closes = loops = every = 0
        for case in sorted(cases):
            reps = sorted(cases[case])
            count = max(len(cases[case][r]) for r in reps)
            for i in range(count):
                rows = [cases[case][r][i] for r in reps if i < len(cases[case][r])]
                n = sum(1 for r in rows if r["closes"])
                closes += n
                loops += len(rows)
                every += int(n == len(rows))
                lines.append("| %s | %s | %d | %d | %d | %d | %s | %s | %s |" % (
                    label, case[0], case[1], i, n, len(rows), "yes" if n == len(rows) else "",
                    " ".join("-" if r["d95"] is None else "%.2f" % r["d95"] for r in rows),
                    ("%.2f" % max(r["d50"] for r in rows if r["closes"])) if n else ""))
        totals.append("| %s | %d | %d | %d |" % (label, closes, loops, every))
    return lines + ["", "| run | closes | of | cases closing in every replicate |", "|---|---|---|---|"] + totals


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="*", help="ab.py run folders, optionally label=folder")
    ap.add_argument("--set", default="guides")
    ap.add_argument("--work", help="ab.py work folder (meshes/, inputs/); default: the first run folder's parent's parent")
    ap.add_argument("--input")
    ap.add_argument("--obj")
    ap.add_argument("--md")
    a = ap.parse_args()
    if a.obj:
        for row in analyse(a.input, a.obj):
            print(json.dumps(row))
        return 0
    runs = [tuple(r.split("=", 1)) if "=" in r else (os.path.basename(r.rstrip("/")), r) for r in a.runs]
    work = a.work or os.path.dirname(os.path.dirname(os.path.abspath(runs[0][1])))
    text = "\n".join(table(runs, a.set, work))
    if a.md:
        with open(a.md, "w") as f:
            f.write(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
