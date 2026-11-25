#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Guide rings: what the core recorded about each, next to guides_report.py's verdict (TODO 5.7).

    ring_probe.py RUN [RUN ...] [--set guides] [--per-run] [--md out.md]

RUN is an ab.py run folder. Per run it reads the core's stage records (stages.jsonl):

  guide_rings   (src/AutoRemesher/guiderings.h) the closed smooth chains of T's hard edges, how many
                have a closed cycle of output edges on them (`cycles`), and how many of those are an
                edge loop in Blender's sense, every vertex with one edge leaving on each side
                (`straight_after`; `straight_before` is the same before padding), `poles` on rings
                before padding, vertices found off the guide (`bridged`), sides padded.
  cover_result  rows of quads between guides and the cones next to them: how many separations the
                relaxed map has below half a row (`continuous_guide_separation_below_half`), how
                many the rounded map has (`rounded_...`: these put a pole on the ring), how many a
                bound raised (`guide_separations_raised`), cones that are guide vertices.

and compares with guides_report.py on the output:

  followed      its verdict as is (closed loop, 95 % of both polylines within h/2 of the other)
  followed_1h   the same with the drawn guide allowed up to 1 h from the output loop: a guide drawn
                as a path of input edges zigzags by up to half an input edge, which is more than
                h/2 where the input is coarser than the output (uvsphere and capsule at 3k)

The table gives sums over the replicates of each case and the totals per run folder.
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

RING_KEYS = ["loops", "cycles", "straight_before", "straight_after", "poles", "bends", "bridged", "padded_sides",
             "added_faces", "broken_loops", "open_fans"]
COVER_KEYS = ["guide_separations", "cones_on_guides", "continuous_guide_separation_below_half",
              "rounded_guide_separation_below_half", "guide_separations_raised"]


def stage_records(run_dir):
    out = collections.Counter()
    path = os.path.join(run_dir, "stages.jsonl")
    if not os.path.exists(path):
        return out
    with open(path) as f:
        for line in f:
            try:
                record = json.loads(line)
            except ValueError:
                continue
            if record.get("stage") == "guide_rings":
                for k in RING_KEYS:
                    out[k] += int(record.get(k, 0))
            elif record.get("stage") == "cover_result":
                for k in COVER_KEYS:
                    out[k] += int(record.get(k, 0))
    return out


def followed_relaxed(input_path, output_path, back=1.0):
    """guides_report.analyse's `followed` with the guide's samples allowed `back` h from the loop."""
    positions_in, _, lines = gr.load_obj(input_path)
    positions, faces, _ = gr.load_obj(output_path)
    loops, _ = gr.guide_loops(lines)
    lengths = {}
    for f in faces:
        for i in range(len(f)):
            a, b = f[i], f[(i + 1) % len(f)]
            lengths[(min(a, b), max(a, b))] = np.linalg.norm(positions[a] - positions[b])
    if not loops or not lengths:
        return 0
    h = float(np.median(list(lengths.values())))
    closed = [gr.sample(positions[chain], gr.SAMPLE * h, True) for is_closed, chain in gr.output_loops(faces) if is_closed]
    count = 0
    for cycle in loops:
        guide = gr.sample(gr.smoothed(positions_in[cycle], h), gr.SAMPLE * h, True)
        tree = cKDTree(guide)
        for samples in closed:
            near, _ = tree.query(samples)
            if np.mean(near < gr.TOLERANCE * h) < gr.SHARE:
                continue
            far, _ = cKDTree(samples).query(guide)
            if np.mean(far < back * h) >= gr.SHARE:
                count += 1
                break
    return count


def collect(run, setname, work):
    sources = gr.corpus_sources(work)
    cases = collections.defaultdict(collections.Counter)
    runs = []
    for obj in sorted(glob.glob(os.path.join(run, setname, "*", "*", "u*", "out.obj"))):
        d = os.path.dirname(obj)
        src = gr.input_of(d, work, sources)
        if src is None:
            continue
        parts = obj.split(os.sep)
        row = stage_records(d)
        report = gr.cached(d, src)
        row["guide_loops"] = report["guide_loops"]
        row["followed"] = report["followed"]
        row["followed_1h"] = followed_relaxed(src, obj)
        row["runs"] = 1
        cases[(parts[-4], parts[-3])].update(row)
        runs.append((parts[-4], parts[-3], parts[-2], row))
    return cases, runs


COLUMNS = [("runs", "runs"), ("guide_loops", "guide loops"), ("cycles", "ring is an edge cycle"),
           ("straight_before", "loop closes, unpadded"), ("straight_after", "loop closes"),
           ("followed", "followed (h/2)"), ("followed_1h", "followed (1 h)"), ("poles", "poles on rings"),
           ("rounded_guide_separation_below_half", "cones 0 rows off"), ("guide_separations_raised", "raised by bound"),
           ("cones_on_guides", "cones on guide vertices"), ("padded_sides", "padded sides"), ("added_faces", "added quads")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--set", default="guides")
    ap.add_argument("--work")
    ap.add_argument("--per-run", action="store_true")
    ap.add_argument("--md")
    a = ap.parse_args()
    text = []
    for run in a.runs:
        work = a.work or os.path.dirname(os.path.dirname(os.path.abspath(run)))
        cases, runs = collect(run, a.set, work)
        text.append(f"### {os.path.basename(os.path.normpath(run))}")
        text.append("")
        text.append("| case | " + " | ".join(title for _, title in COLUMNS) + " |")
        text.append("|---|" + "---|" * len(COLUMNS))
        total = collections.Counter()
        for case in sorted(cases):
            total.update(cases[case])
            text.append(f"| {case[0]} {case[1]} | " + " | ".join(str(cases[case][k]) for k, _ in COLUMNS) + " |")
        text.append("| **all** | " + " | ".join(str(total[k]) for k, _ in COLUMNS) + " |")
        text.append("")
        if a.per_run:
            for mesh, res, rep, row in runs:
                if row["straight_after"] < row["loops"] or row["followed"] < row["guide_loops"]:
                    text.append(f"- {mesh} {res} {rep}: " + ", ".join(f"{k} {row[k]}" for k, _ in COLUMNS[1:]))
            text.append("")
    out = "\n".join(text)
    if a.md:
        with open(a.md, "w") as f:
            f.write(out + "\n")
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
