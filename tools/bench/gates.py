#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""docs/TARGETS.md gates (H1-H8, V1-V6) and validity rates over one ab.py run folder.

    gates.py RUN --set corpus [--set closed ...] [--md out.md] [--work DIR]

RUN is a run folder printed by `ab.py run` (or a ref). Each gate is counted per replicate-run and
reported as the mean over replicates of the runs that pass ("61.5/72"), next to the runs that pass
in every replicate ("unanimous"), per family. Families: the corpus families of TARGETS §2.1 from
each mesh's corpus.json category (organic, hard surface, thin shell, scan, multi-island); any other
set is one family. Defects per 1k faces are sums over all replicate-runs. Needs the metrics of
metrics.py from 2026-10-22 on (`validity`) and, for V3's fold mask, from 2026-10-23 on (older runs
count every flip in V3): `ab.py run ... --remeasure`. V1 is all-quad again (Kajetan, 2026-10-23); the
retired cluster rule of 2026-10-21 is still printed for comparison.
"""
import argparse
import collections
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ab  # noqa: E402
from metrics import read_obj  # noqa: E402

FAMILIES = ["organic", "hard surface", "thin shell", "scan", "multi-island"]


def family(mesh, setname):
    if setname != "corpus":
        return setname
    c = (mesh.get("category") or "").lower()
    if c.startswith("organic"):
        return "organic"
    if c.startswith("hard"):
        return "hard surface"
    if c.startswith("thin"):
        return "thin shell"
    if "scan" in c or "noise" in c:
        return "scan"
    return "multi-island"


def largest_nonquad_cluster(path):
    """Faces in the largest edge-connected cluster of non-quads (TARGETS V1)."""
    _, faces = read_obj(path)
    bad = [i for i, f in enumerate(faces) if len(f) != 4]
    if not bad:
        return 0
    edge_faces = collections.defaultdict(list)
    for i in bad:
        f = faces[i]
        for k in range(len(f)):
            edge_faces[(min(f[k], f[k - 1]), max(f[k], f[k - 1]))].append(i)
    parent = {i: i for i in bad}

    def root(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for fs in edge_faces.values():
        for j in fs[1:]:
            a, b = root(fs[0]), root(j)
            if a != b:
                parent[a] = b
    return max(collections.Counter(root(i) for i in bad).values())


def gates(rec, run_dir, fam):
    """Pass/fail of each gate for one replicate-run, and its defect counts; None when the run completed
    but has no metrics (metrics.py failed, e.g. out of memory with several 2M-triangle inputs at once)."""
    m = rec.get("metrics") or {}
    if rec.get("rc") == 0 and not rec.get("metrics"):
        return None
    if rec.get("rc") != 0 or not m.get("ok"):
        return {"H1 completes": False}, {}
    f, t, v, q = m["faces"], m["topology"], m.get("validity") or {}, m.get("quads") or {}
    val = (m.get("valence") or {}).get("interior_hist") or {}
    v5 = sum(n for k, n in val.items() if int(k) == 2 or int(k) >= 7)
    nonquads = f["total"] - f["quads"]
    cluster = largest_nonquad_cluster(os.path.join(run_dir, "out.obj")) if nonquads else 0
    fl = m["flipped"]
    v3 = fl.get("faces_off_input_folds", fl["faces_vs_input"])  # metrics.py before 2026-10-23: no fold mask
    ex = rec.get("extractor") or {}
    g = {"H1 completes": True,
         "H2 no non-manifold edge": t["non_manifold_edges"] == 0,
         "H2 no non-manifold vertex": v.get("non_manifold_vertices") == 0,
         "H3 consistently oriented": t["inconsistently_oriented_edges"] == 0,
         "H4 islands kept (none dropped, split or merged)": (v.get("islands_dropped") == 0 and v.get("islands_split") == 0
                                                             and v.get("islands_merged") == 0),
         "H5 no hole added or closed": v.get("holes_added") == 0 and v.get("holes_closed") == 0,
         "H6 count within ±10 %": abs(f["quads"] / rec["target"] - 1) <= 0.10 + 1e-9,  # 2200/2000 - 1 > 0.1 in floats
         "H8 no 6+-gon": f["other"] == 0,
         "V1 all-quad": nonquads == 0,
         "V1 retired cluster rule of 2026-10-21 (all-quad on hard surface, else clusters ≤ 2)": (
             nonquads == 0 if fam == "hard surface" else (f["other"] == 0 and cluster <= 2)),
         "V2 no folded quad": q.get("sj_le_0", 0) == 0,
         "V2+ no folded face of any size": v.get("folded_faces") == 0,
         "V3 no face flipped vs input (off input folds)": v3 == 0,
         "V3 incl. flips over input folds": fl["faces_vs_input"] == 0,
         "V4 no degenerate face": m["degenerate"]["faces"] == 0,
         "V5 no interior valence 2 or ≥ 7": v5 == 0,
         "V6 topology same as input (global)": bool(t["same_as_input"]),
         "V6 topology same as input per island": bool(v.get("islands_same_as_input")),
         "no handle lost or added": v.get("handles_lost") == 0 and v.get("handles_added") == 0,
         "thin parts kept (wrong-side area < 0.1 %)": (v.get("area_wrong_side") or 0) < 1e-3,
         "no input area lost (uncovered < 0.1 %)": (v.get("area_uncovered") or 0) < 1e-3}
    if ex.get("islands"):  # runs with stages.jsonl from the libQEx extractor
        g["no libQEx fallback (every island extracted by libQEx)"] = ex["fallbacks"] == 0
    g["V1–V5 together"] = all(g[k] for k in (
        "V1 all-quad", "V2 no folded quad", "V3 no face flipped vs input (off input folds)",
        "V4 no degenerate face", "V5 no interior valence 2 or ≥ 7"))
    g["V1–V5 together, flips over input folds included"] = g["V1–V5 together"] and g["V3 incl. flips over input folds"]
    d = {"faces": f["total"], "non-quads": nonquads, "folded quads": q.get("sj_le_0", 0),
         "folded faces (any size)": v.get("folded_faces", 0), "flipped vs input": fl["faces_vs_input"],
         "flipped off input folds": v3, "flipped over input folds": fl.get("faces_over_input_folds", 0),
         "flipped vs raw input": fl.get("faces_vs_raw_input", 0), "valence 2 / ≥ 7": v5,
         "degenerate": m["degenerate"]["faces"], "non-manifold vertices": v.get("non_manifold_vertices", 0),
         "holes added": v.get("holes_added", 0), "handles lost": v.get("handles_lost", 0),
         "handles added": v.get("handles_added", 0), "islands dropped": v.get("islands_dropped", 0),
         "libQEx fallback islands": ex.get("fallbacks", 0)}
    return g, d


def table(path, setname, work):
    defaults, meshes = ab.load_set(setname)
    fams = {mm["id"]: family(mm, setname) for mm in meshes}
    runs = ab.load_runs(path, setname)
    order = [x for x in FAMILIES if x in set(fams.values())] or sorted(set(fams.values()))
    per = collections.defaultdict(lambda: collections.defaultdict(list))  # gate -> family -> [(key, ok)]
    defects = collections.defaultdict(collections.Counter)
    fidelity = collections.defaultdict(lambda: collections.defaultdict(list))
    reps = set()
    failed, unmeasured = [], []
    for key, by_rep in runs.items():
        fam = fams.get(key[0])
        if fam is None:
            continue
        for u, rec in by_rep.items():
            reps.add(u)
            run_dir = os.path.join(path, setname, key[0], f"{key[1]}-t{key[2]}", f"u{u}")
            res = gates(rec, run_dir, fam)
            if res is None:
                unmeasured.append(f"{key[0]} {key[1]}-t{key[2]} u{u}")
                continue
            g, d = res
            if not g["H1 completes"]:
                failed.append((key, fam))
            for name, ok in g.items():
                per[name][fam].append((key, ok))
            defects[fam].update(d)
            dist = (rec.get("metrics") or {}).get("distance") or {}
            if dist:
                fidelity["D1r in→out p99 (×10⁻³ diag)"][fam].append(dist["in_to_out_p99"] * 1e3)
                fidelity["D2 mean distance (×10⁻³ diag)"][fam].append(
                    0.5 * (dist["in_to_out_mean"] + dist["out_to_in_mean"]) * 1e3)
                fidelity["D3 normal deviation, mean (°)"][fam].append(rec["metrics"]["normal_deviation_deg"]["mean"])
                fidelity["vertex-to-input median (×10⁻³ diag)"][fam].append(dist["vertex_to_in_median"] * 1e3)
    n = len(reps)
    out = [f"### Set {setname} ({len(runs)} runs × {n} replicate{'s' if n > 1 else ''}; {path})\n",
           "Cells: runs passing, mean over replicates / runs [runs passing in every replicate].\n"]
    if unmeasured:
        out.append(f"**Left out, completed but without metrics** (`ab.py run ... --remeasure`): {', '.join(unmeasured)}\n")
    out += ["| gate | " + " | ".join(order) + " | all |", "|---|" + "---|" * (len(order) + 1)]
    for (key, fam) in failed:  # a run that did not complete fails every gate
        for name in per:
            if name != "H1 completes":
                per[name][fam].append((key, False))
    for name in per:
        cells = []
        for fam in order + [None]:
            items = [x for f2 in (order if fam is None else [fam]) for x in per[name].get(f2, [])]
            if not items:
                cells.append("–")
                continue
            by_key = collections.defaultdict(list)
            for k, ok in items:
                by_key[k].append(ok)
            mean = sum(ok for _, ok in items) / n
            unanimous = sum(all(v) and len(v) == n for v in by_key.values())
            cells.append(f"{mean:.1f}/{len(by_key)} [{unanimous}]")
        out.append(f"| {name} | " + " | ".join(cells) + " |")
    out += ["", "Defects per 1k faces (sum over replicate-runs):\n",
            "| defect | " + " | ".join(order) + " | all |", "|---|" + "---|" * (len(order) + 1)]
    total = collections.Counter()
    for fam in order:
        total.update(defects[fam])
    for k in [x for x in total if x != "faces"]:
        cells = [f"{1000 * defects[f2][k] / max(defects[f2]['faces'], 1):.3g}" for f2 in order]
        out.append(f"| {k} | " + " | ".join(cells) + f" | {1000 * total[k] / max(total['faces'], 1):.3g} |")
    out += ["", "Fidelity, median over replicate-runs:\n",
            "| metric | " + " | ".join(order) + " |", "|---|" + "---|" * len(order)]
    for k, by in fidelity.items():
        out.append(f"| {k} | " + " | ".join(f"{np.median(by[f2]):.3g}" if by.get(f2) else "–" for f2 in order) + " |")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("run")
    ap.add_argument("--set", action="append", required=True)
    ap.add_argument("--md")
    ap.add_argument("--work", default=os.environ.get("AR_WORK", os.path.expanduser("~/ar-work")))
    a = ap.parse_args()
    path = ab.resolve(a.run, os.path.abspath(a.work))
    text = "\n".join(table(path, s, a.work) for s in a.set)
    if a.md:
        with open(a.md, "w") as fp:
            fp.write(text)
    print(text)


if __name__ == "__main__":
    main()
