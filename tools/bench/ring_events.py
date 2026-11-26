#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""What lies at the gaps of guide rings, from the core's debug dumps (research, TODO 5.7).

    ring_events.py SET_FOLDER [SET_FOLDER ...] [--reach 1.5] [--cases]

SET_FOLDER is <ab.py run folder>/<set>. The runs must have been made with

    --cli-arg=--experiment=guide_rings_debug=1 --cli-arg=--experiment=flip_dump=1
    --cli-arg=--experiment=guide_separation_debug=1

which print, per cover solve: T's guide chains, the follow step's re-rounds, the cones on and next to
guides with their rows, and the flipped triangles of the continuous, rounded and untangled map. The
solve whose three flip counts equal the run's `cover_result` record is the one the output came from.
Runs whose guide the marked-edge fallback dropped are left out.

A gap is a stretch of T's chain of at least 0.6 output edges with no output edge on it (both ends
within 0.15 edges of the chain), on a loop that fails loop_gate.py. For each kind of event the table
gives how much of the gap length and of the closed length has one within `--reach` output edges, how
many gaps have one, and how many would by chance (the same interval placed at random on its ring).
`--cases` adds one row per mesh and target: closes, drops, kinks of T's chain, re-rounds, corners.
"""
import argparse
import collections
import glob
import json
import os
import random
import re
import sys

import numpy as np
from scipy.spatial import cKDTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import guides_report as gr  # noqa: E402
import loop_gate  # noqa: E402

STAGES = ("continuous", "rounded", "untangled")
ON_CHAIN = 0.15   # of the output's median edge
MIN_GAP = 0.6
KINK = 45.0       # degrees


def parse(run_dir):
    """(chains, kept solve, dropped, corner counts) of a run."""
    solves, current, chains, corners = [], collections.defaultdict(list), {}, None
    with open(os.path.join(run_dir, "log.txt"), errors="replace") as f:
        for line in f:
            if line.startswith("guide chain: loop "):
                t = line.split()
                if int(t[3]) not in chains:
                    chains[int(t[3])] = np.array([[float(x) for x in p.split(",")] for p in t[4:]])
            elif line.startswith("guide follow: turned"):
                t = line.split()
                current["follow"].append(tuple(float(x) for x in t[7:10]))
            elif line.startswith("guide cone: index"):
                current["on_guide"].append(tuple(float(x) for x in line.split()[-3:]))
            elif " separation: cone " in line:
                t = line.split()
                current[t[0] + "_rows"].append((int(t[3]), tuple(float(x) for x in t[7:10]), float(t[13])))
            elif line.startswith("cover flips: "):
                t = line.split()
                current["flips_" + t[2]] = np.array([[float(x) for x in p.split(",")] for p in t[4:]]).reshape(-1, 3)
                if "untangled" == t[2]:
                    solves.append(current)
                    current = collections.defaultdict(list)
            elif line.startswith("guide corners:") and corners is None:
                corners = [int(x) for x in re.findall(r"\d+", line)]
    record, dropped = None, 0
    with open(os.path.join(run_dir, "stages.jsonl")) as f:
        for line in f:
            r = json.loads(line)
            if "cover_result" == r.get("stage"):
                record = r
            if "marked_chains_dropped" in r:
                dropped = r["marked_chains_dropped"]
    kept = None
    if record is not None:
        for solve in solves:
            if all(len(solve.get("flips_" + s, ())) == int(record.get(s + "_flipped", -1)) for s in STAGES):
                kept = solve
    return chains, kept, dropped, corners


def turns(chain):
    step = np.roll(chain, -1, axis=0) - chain
    unit = step / np.maximum(np.linalg.norm(step, axis=1), 1e-30)[:, None]
    return np.degrees(np.arccos(np.clip((np.roll(unit, 1, axis=0) * unit).sum(1), -1.0, 1.0)))


def coverage(chain, positions, faces, h):
    """Samples along the chain and whether an output edge lies on each."""
    lengths = np.linalg.norm(np.roll(chain, -1, axis=0) - chain, axis=1)
    start = np.concatenate([[0.0], np.cumsum(lengths)])
    total, samples, arc = start[-1], [], []
    for i in range(len(chain)):
        n = max(1, int(lengths[i] / (0.05 * h)))
        for k in range(n):
            samples.append(chain[i] + (chain[(i + 1) % len(chain)] - chain[i]) * k / n)
            arc.append(start[i] + lengths[i] * k / n)
    samples, arc = np.array(samples), np.array(arc)
    edges = set()
    for f in faces:
        for i in range(len(f)):
            a, b = f[i], f[(i + 1) % len(f)]
            edges.add((min(a, b), max(a, b)))
    edges = np.array(sorted(edges))
    distance, nearest = cKDTree(samples).query(positions)
    on = (distance[edges[:, 0]] < ON_CHAIN * h) & (distance[edges[:, 1]] < ON_CHAIN * h)
    covered = np.zeros(len(samples), bool)
    for a, b in edges[on]:
        lo, hi = sorted((arc[nearest[a]], arc[nearest[b]]))
        span = (arc >= hi) | (arc <= lo) if hi - lo > total / 2 else (arc >= lo) & (arc <= hi)
        if span.sum() * 0.05 * h <= 3 * h:
            covered |= span
    return samples, covered


def gaps_of(covered, h):
    if covered.all():
        return []
    if not covered.any():
        return [list(range(len(covered)))]
    order = np.roll(np.arange(len(covered)), -int(np.argmax(covered)))
    runs, current = [], []
    for k in order:
        if not covered[k]:
            current.append(k)
        elif current:
            runs.append(current)
            current = []
    if current:
        runs.append(current)
    return [g for g in runs if len(g) * 0.05 * h >= MIN_GAP * h]


def events(solve):
    continuous = {cone: (p, rows) for cone, p, rows in solve["continuous_rows"]}
    rounded = {cone: rows for cone, _, rows in solve["rounded_rows"]}
    pts = lambda items: np.array(list(items), dtype=float).reshape(-1, 3)
    return collections.OrderedDict([
        ("continuous flip", solve["flips_continuous"]),
        ("untangled flip", solve["flips_untangled"]),
        ("follow re-round", pts(solve["follow"])),
        ("cone on the guide", pts(solve["on_guide"])),
        ("cone under 0.5 row (continuous)", pts(p for p, rows in continuous.values() if rows < 0.5)),
        ("cone under 1 row (continuous)", pts(p for p, rows in continuous.values() if rows < 0.99)),
        ("cone moved 0.5 row or more by rounding", pts(p for c, (p, rows) in continuous.items()
                                                       if c in rounded and abs(rounded[c] - rows) >= 0.5)),
    ])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folders", nargs="+")
    ap.add_argument("--reach", type=float, default=1.5)
    ap.add_argument("--work", default=os.environ.get("AR_WORK"))
    ap.add_argument("--cases", action="store_true")
    a = ap.parse_args()
    work = a.work or os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(a.folders[0]))))
    sources = gr.corpus_sources(work)
    total, cases, rng = collections.Counter(), collections.OrderedDict(), random.Random(1)
    names, kink_turn, follow_turn = None, [], []
    for folder in a.folders:
        for run_dir in sorted(glob.glob(os.path.join(folder, "*", "*", "u*"))):
            if not os.path.exists(os.path.join(run_dir, "out.obj")):
                continue
            mesh, target = run_dir.split(os.sep)[-3], int(run_dir.split(os.sep)[-2].rsplit("-t", 1)[-1])
            case = cases.setdefault((mesh, target), collections.Counter())
            source = gr.input_of(run_dir, work, sources)
            if source is None:
                continue
            gate = loop_gate.cached(run_dir, source)
            chains, kept, dropped, corners = parse(run_dir)
            case["runs"] += 1
            case["loops"] += len(gate)
            case["closes"] += sum(1 for row in gate if row["closes"])
            case["dropped"] += bool(dropped)
            if corners:
                case["crossings"] += corners[1]
                case["kink_corners"] += corners[4]
            if dropped:
                total["runs dropped"] += 1
                continue
            if kept is None or not chains:
                total["runs without a matching dump"] += 1
                continue
            total["runs"] += 1
            case["kept"] += 1
            positions, faces, _ = gr.load_obj(os.path.join(run_dir, "out.obj"))
            h = float(np.median([np.linalg.norm(positions[f[0]] - positions[f[1]]) for f in faces[::5]]))
            positions_in, _, lines = gr.load_obj(source)
            drawn = [cKDTree(gr.sample(positions_in[c], 0.5 * h, True)) for c in gr.guide_loops(lines)[0]]
            found = events(kept)
            names = list(found)
            follow = found["follow re-round"]
            case["follow"] += len(follow)
            case["flips"] += len(found["continuous flip"])
            for chain in chains.values():
                closes = bool(gate[int(np.argmin([np.mean(t.query(chain)[0]) for t in drawn]))]["closes"])
                turn = turns(chain)
                case["kinks"] += int((turn > KINK).sum())
                kink_turn += list(turn)
                samples, covered = coverage(chain, positions, faces, h)
                gaps = [] if closes else gaps_of(covered, h)
                in_gap = np.zeros(len(samples), bool)
                for g in gaps:
                    in_gap[g] = True
                total["loops"] += 1
                total["loops closing"] += closes
                total["gaps"] += len(gaps)
                total["gap samples"] += int(in_gap.sum())
                total["closed samples"] += int((~in_gap).sum())
                tree = cKDTree(samples)
                if len(follow):
                    distance, index = cKDTree(chain).query(follow)
                    for k in np.where(distance < 1e-6 * h + 1e-9)[0]:
                        follow_turn.append(turn[index[k]])
                        total["follow at a gap"] += bool(in_gap[tree.query_ball_point(follow[k], a.reach * h)].any())
                for i in np.where(turn <= 20.0)[0]:
                    total["straight vertices"] += 1
                    total["straight at a gap"] += bool(in_gap[tree.query_ball_point(chain[i], a.reach * h)].any())
                any_event = [False] * len(gaps)
                for name, points in found.items():
                    near = cKDTree(points).query(samples)[0] < a.reach * h if len(points) else np.zeros(len(samples), bool)
                    total[name + "|gap"] += int((near & in_gap).sum())
                    total[name + "|closed"] += int((near & ~in_gap).sum())
                    for j, g in enumerate(gaps):
                        hit = bool(near[g].any())
                        total[name + "|hit"] += hit
                        any_event[j] = any_event[j] or hit
                        total[name + "|chance"] += sum(
                            bool(near[np.arange(s, s + len(g)) % len(samples)].any())
                            for s in (rng.randrange(len(samples)) for _ in range(200))) / 200.0
                total["gaps with none of these"] += sum(1 for hit in any_event if not hit)
    print("runs %d (dropped %d, no matching dump %d), loops %d, closing %d, gaps %d, reach %.1f edges" % (
        total["runs"], total["runs dropped"], total["runs without a matching dump"], total["loops"],
        total["loops closing"], total["gaps"], a.reach))
    print("\n| within reach | of gap length | of closed length | gaps with one | by chance |\n|---|---|---|---|---|")
    for name in names or ():
        print("| %s | %.0f %% | %.0f %% | %d of %d | %.1f |" % (
            name, 100.0 * total[name + "|gap"] / max(total["gap samples"], 1),
            100.0 * total[name + "|closed"] / max(total["closed samples"], 1),
            total[name + "|hit"], total["gaps"], total[name + "|chance"]))
    print("\ngaps with none of these within reach: %d of %d" % (total["gaps with none of these"], total["gaps"]))
    if follow_turn:
        kink_turn, follow_turn = np.array(kink_turn), np.array(follow_turn)
        print("chain vertices turning more than %d degrees: %.1f %%; follow re-rounds at such a vertex: %.0f %% of %d" % (
            KINK, 100.0 * np.mean(kink_turn > KINK), 100.0 * np.mean(follow_turn > KINK), len(follow_turn)))
        print("a gap within reach of a follow re-round: %d of %d; of a straight vertex (20 degrees or less): %d of %d" % (
            total["follow at a gap"], len(follow_turn), total["straight at a gap"], total["straight vertices"]))
    if a.cases:
        print("\n| mesh | target | closes | of | runs dropped | kinks in T per run | follow re-rounds per run |"
              " corners per run (crossing + kink) | continuous flips per run |\n|---|---|---|---|---|---|---|---|---|")
        for (mesh, target), c in cases.items():
            kept, runs = max(c["kept"], 1), max(c["runs"], 1)
            print("| %s | %d | %d | %d | %d | %.1f | %.1f | %.1f + %.1f | %.0f |" % (
                mesh, target, c["closes"], c["loops"], c["dropped"], c["kinks"] / kept, c["follow"] / kept,
                c["crossings"] / runs, c["kink_corners"] / runs, c["flips"] / kept))
    return 0


if __name__ == "__main__":
    sys.exit(main())
