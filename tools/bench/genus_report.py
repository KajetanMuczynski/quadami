#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Wrong genus per island over ab.py runs, with the cause found by intervention.

    genus_report.py RUN --set S [--trace RUN_T] [--alt NAME=RUN ...] [--per-run] [--json out.json]

RUN is an ab.py run folder (or a ref, as `ab.py compare` takes it). For every replicate-run and every
closed or open input island whose handle count (2g = 2 - chi - loops, metrics.json -> validity ->
per_island) differs from the input, the report joins what the core recorded for that island
(stages.jsonl: T's Euler characteristic after isotropic remeshing, the map_check stage on the rounded
map, the extractor used and any fallback) and the same island in the other runs:

- `--trace RUN_T`: the same inputs with `--cli-arg=--extractor=trace` (our own tracing on the same
  map). The map does not depend on the extractor, so a handle the tracer keeps was lost by libQEx or
  by the cleanup after it, not by the map.
- `--alt NAME=RUN`: any other run of the same inputs (a candidate fix), shown as fixed/kept.

Each changed island gets one class, in this order:

  input      the input island is not a 2-manifold (an edge of 3+ triangles, a pinched vertex): genus
             has no right answer (metrics.py leaves such islands out of handles_lost/added)
  remesh     T (isotropic remesh) already has another Euler characteristic than the input
  map0       handle lost, and the rounded map has a handle loop with zero translation (H rounded to 0:
             the ring cannot exist in this map) and the tracer loses it too
  map2       handle lost, the map's narrowest handle ring is 2 quads (the 2Z^2 lattice's collapsed ring;
             ENVELOPE 3.1 band 2 <= H < 3) and the tracer loses it too
  extractor  T and the map hold the handle, or the handle is extra, and the tracer gets it right
  both       T and the map hold it (no collapsed or 2-quad ring), and the tracer gets it wrong too
  unknown    no trace run to tell

map_check records per connected component of an island, so map0/map2 say "at least one handle loop of
this island is collapsed"; an island with several handles (demosthenes) can mix classes.
"""
import argparse
import json
import os
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def g2(chi, loops, comps=1):
    return 2 * comps - chi - loops


def load(path, setname):
    import ab  # noqa: E402  (same folder)

    work = os.environ.get("AR_WORK", os.path.expanduser("~/ar-work"))
    return ab.load_runs(ab.resolve(path, work), setname), work


def stages_of(run_dir):
    p = os.path.join(run_dir, "stages.jsonl")
    if not os.path.exists(p):
        return {}
    by = defaultdict(dict)
    for line in open(p):
        if not line.strip():
            continue
        s = json.loads(line)
        if "island" in s and s.get("stage") in ("island", "map_check", "extractor"):
            if s.get("superseded"):  # an extractor record of a retry attempt the core dropped
                continue
            by[s["island"]][s["stage"]] = s
    return by


def island_triangles(rec, work):
    """Triangles per input island in metrics.py's island order, from its input cache."""
    import numpy as np
    import ab
    import metrics

    src = rec["metrics"]["input"]
    path = ab.perturbed(src, rec["replicate"], work) if rec["replicate"] else src
    inp = metrics.input_side(path, ab.metrics_cache(work))
    return np.bincount(inp["tri_island"], minlength=len(inp["island_euler"]))


def changed_islands(rec):
    v = (rec.get("metrics") or {}).get("validity") or {}
    out = []
    for i, isl in enumerate(v.get("per_island") or []):
        if isl.get("output_islands") != 1 and not isl.get("input_non_manifold"):
            continue  # dropped or split: other rows (islands_dropped / split) own these
        (ci, co), (li, lo) = isl["euler"], isl["loops"]
        d = g2(co, lo) - g2(ci, li)
        if d or (isl.get("input_non_manifold") and (ci, li) != (co, lo)):
            out.append({"island": i, "g2_in": g2(ci, li), "g2_out": g2(co, lo), "d2": d,
                        "non_manifold_input": bool(isl.get("input_non_manifold")), "loops_in": li,
                        # What a core warning per closed input island (chi in vs out, output closed) sees.
                        "warned": li == 0 and lo == 0 and ci != co})
    if not v.get("per_island") and (v.get("handles_lost") or v.get("handles_added")):
        out.append({"island": None, "g2_in": None, "g2_out": None,
                    "d2": int(2 * (v.get("handles_added", 0) - v.get("handles_lost", 0))),
                    "non_manifold_input": False, "loops_in": None, "warned": None})
    return out


def match_core(tri_counts, i, core):
    """Core island index whose input_triangles is closest to metrics island i's triangle count."""
    if i is None or not core:
        return None
    want = int(tri_counts[i])
    best = min(core, key=lambda k: abs(core[k].get("island", {}).get("input_triangles", -10**9) - want))
    return best


def island_d2(rec, i):
    for c in changed_islands(rec):
        if c["island"] == i:
            return c["d2"]
    v = (rec.get("metrics") or {}).get("validity") or {}
    return 0 if v.get("per_island") else None


def classify(c, core, trace_d2):
    if c["non_manifold_input"]:
        return "input"
    isl, mc = core.get("island", {}), core.get("map_check", {})
    chi_t = isl.get("isotropic_euler_characteristic")
    if chi_t is not None and c["g2_in"] is not None and c["loops_in"] is not None:
        if g2(chi_t, c["loops_in"]) != c["g2_in"]:
            return "remesh"
    if trace_d2 is None:
        return "unknown"
    if c["d2"] < 0 and trace_d2 < 0:
        if mc.get("map_handle_loops_collapsed", 0) > 0:
            return "map0"
        if mc.get("map_handle_loops_thin", 0) > 0:
            return "map2"
        return "both"
    if trace_d2 == 0:
        return "extractor"
    return "both"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run")
    ap.add_argument("--set", default="corpus")
    ap.add_argument("--trace")
    ap.add_argument("--alt", action="append", default=[], help="NAME=RUN")
    ap.add_argument("--per-run", action="store_true")
    ap.add_argument("--json")
    a = ap.parse_args()

    runs, work = load(a.run, a.set)
    trace = load(a.trace, a.set)[0] if a.trace else {}
    alts = [(s.split("=", 1)[0], load(s.split("=", 1)[1], a.set)[0]) for s in a.alt]
    root = os.path.join(__import__("ab").resolve(a.run, work), a.set)

    rows, n_reps, n_bad = [], 0, 0
    for key in sorted(runs):
        mesh, res, target = key
        for k, rec in sorted(runs[key].items()):
            n_reps += 1
            ch = changed_islands(rec)
            if not ch:
                continue
            n_bad += 1
            d = os.path.join(root, mesh, f"{res}-t{target}", f"u{k}")
            core = stages_of(d)
            tri = island_triangles(rec, work) if any(c["island"] is not None for c in ch) else None
            for c in ch:
                ci = match_core(tri, c["island"], core)
                cr = core.get(ci, {}) if ci is not None else {}
                t = trace.get(key, {}).get(k)
                tr = island_d2(t, c["island"]) if t else None
                mc, ex, il = cr.get("map_check", {}), cr.get("extractor", {}), cr.get("island", {})
                row = {"mesh": mesh, "res": res, "target": target, "rep": k, "island": c["island"],
                       "g_in": c["g2_in"] / 2 if c["g2_in"] is not None else None,
                       "d_handles": c["d2"] / 2, "class": classify(c, cr, tr), "warned": c["warned"],
                       "trace_d_handles": tr / 2 if tr is not None else None,
                       "extractor": ex.get("used"), "fallback": ex.get("fallback_reason") or "",
                       "map_valid": mc.get("map_valid"), "map_handle_loops": mc.get("map_handle_loops"),
                       "map_ring": mc.get("map_handle_translation"),
                       "map_collapsed": mc.get("map_handle_loops_collapsed"),
                       "map_pinched": mc.get("map_boundary_loops_pinched"),
                       "retry": il.get("retry_attempts"), "t_chi": il.get("isotropic_euler_characteristic")}
                for name, alt in alts:
                    r2 = alt.get(key, {}).get(k)
                    v = island_d2(r2, c["island"]) if r2 else None
                    row[name] = v / 2 if v is not None else None
                rows.append(row)

    print(f"## Wrong genus in {a.set}\n")
    print(f"{n_bad} of {n_reps} replicate-runs have an island with another handle count than its input; "
          f"{len(rows)} changed islands.\n")
    by = Counter((r["class"], "lost" if r["d_handles"] < 0 else "added") for r in rows)
    print("| class | lost | added |\n|---|---|---|")
    for cls in ("input", "remesh", "map0", "map2", "extractor", "both", "unknown"):
        if by[(cls, "lost")] or by[(cls, "added")]:
            print(f"| {cls} | {by[(cls, 'lost')]} | {by[(cls, 'added')]} |")
    w = Counter(r["warned"] for r in rows)
    print(f"\nA warning on chi per closed input island (ENVELOPE G2) would name {w[True]} of {len(rows)} changed "
          f"islands ({w[False]} missed: open input or output, or non-manifold input; {w[None]} not resolved per "
          f"island). One on 2g = 2C - chi - B per manifold input island names every changed island but "
          f"{sum(r['class'] == 'input' for r in rows)} (non-manifold input); neither sees a lost plus an added "
          f"handle on one island.")
    agg = defaultdict(list)
    for r in rows:
        agg[(r["mesh"], r["res"], r["target"])].append(r)
    cols = [n for n, _ in alts]
    print("\n| mesh | res | target | reps wrong | Δhandles (per island-rep) | classes | extractor (fallback) | "
          "trace Δ | " + " | ".join(cols) + (" |" if cols else "") + "\n|---|---|---|---|---|---|---|---|"
          + "---|" * len(cols))
    for key, rs in sorted(agg.items()):
        reps = len({r["rep"] for r in rs})
        dh = Counter(r["d_handles"] for r in rs)
        cl = Counter(r["class"] for r in rs)
        exs = Counter((r["extractor"] or "?") + (f" ({r['fallback'][:24]})" if r["fallback"] else "") for r in rs)
        tdh = Counter(r["trace_d_handles"] for r in rs)
        extra = [" ".join(f"{v:+g}×{n}" if v is not None else f"?×{n}" for v, n in sorted(
            Counter(r[c] for r in rs).items(), key=lambda x: (x[0] is None, x[0] or 0))) for c in cols]
        print(f"| {key[0]} | {key[1]} | {key[2]} | {reps}/{len(runs[key])} | "
              + " ".join(f"{v:+g}×{n}" for v, n in sorted(dh.items())) + " | "
              + ", ".join(f"{c} {n}" for c, n in cl.most_common()) + " | "
              + ", ".join(f"{e} {n}" for e, n in exs.most_common()) + " | "
              + " ".join(f"{v:+g}×{n}" if v is not None else f"?×{n}" for v, n in tdh.items()) + " | "
              + " | ".join(extra) + (" |" if cols else ""))
    # The core's own count (report.json, interface 1.7+) against metrics.py's, per replicate-run.
    conf, exact, seen = Counter(), 0, 0
    for key in runs:
        for k, rec in runs[key].items():
            rp = os.path.join(root, key[0], f"{key[1]}-t{key[2]}", f"u{k}", "report.json")
            if not os.path.exists(rp):
                continue
            isl = json.load(open(rp)).get("islands", {})
            if "handles_lost" not in isl:
                continue
            seen += 1
            v = (rec.get("metrics") or {}).get("validity") or {}
            truth = (v.get("handles_lost", 0) or 0, v.get("handles_added", 0) or 0)
            core = (isl["handles_lost"], isl["handles_added"])
            conf[(bool(truth[0] or truth[1]), bool(core[0] or core[1]))] += 1
            exact += truth == core
    if seen:
        print(f"\nCore warning (report.json handles_lost/added) against metrics.py over {seen} replicate-runs: "
              f"both changed {conf[(True, True)]}, both same {conf[(False, False)]}, "
              f"missed {conf[(True, False)]}, false alarm {conf[(False, True)]}; equal counts in {exact}.")
    if a.per_run:
        print("\n| mesh | res | target | rep | island | g in | Δ | class | trace Δ | extractor | map valid | "
              "ring | collapsed | pinched | retry |\n|" + "---|" * 15)
        for r in rows:
            print(f"| {r['mesh']} | {r['res']} | {r['target']} | u{r['rep']} | {r['island']} | {r['g_in']} | "
                  f"{r['d_handles']:+g} | {r['class']} | {r['trace_d_handles']} | {r['extractor']} "
                  f"{r['fallback'][:30]} | {r['map_valid']} | {r['map_ring']} | {r['map_collapsed']} | "
                  f"{r['map_pinched']} | {r['retry']} |")
    if a.json:
        json.dump({"set": a.set, "replicate_runs": n_reps, "wrong": n_bad, "rows": rows}, open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
