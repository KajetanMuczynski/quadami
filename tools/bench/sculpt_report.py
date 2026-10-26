#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a sculptor gets: non-quads and folds by where they sit, and whether the result subdivides.

    sculpt_report.py --runs DIR [--runs DIR ...] --meshes MESHES [--set sculpt[,corpus]] [--only a,b]
                     [--md out.md] [--csv out.csv]

DIR is an ab.py run folder (<key>/<set>/<mesh>/<res>-t<target>/u<k>/) or a run_compare.py folder
(<mesh>/<tool>/t<target>/out.obj); the tool name comes from the path. Per output it writes
sculpt.json next to out.obj (reused on the next call) with:

  faces       triangles, pentagons, 6+-gons; non-quad clusters (edge-connected) and the largest one
  where       share of all faces / non-quads / folded quads (SJ <= 0) in three bands of the input's
              normal spread within one quad edge h = sqrt(area / quads) around the face:
              smooth (resultant >= 0.95), curved (0.7-0.95), sharp or thin (< 0.7: creases, tips,
              wrinkles below quad size, parts thinner than a quad); and the share of non-quads that
              touch a folded quad
  subdiv      one Catmull-Clark step of the output: quads with SJ <= 0 (folds survive subdivision,
              n-gons only become poles), child faces whose normal opposes their parent face's
              (pinches), and the minimum SJ
  input_folds folded input triangles (normal against the area-weighted mean normal within 1 % of the
              bounding-box diagonal: pleats, twists, over-pinched creases), and how many non-quads and
              folded quads sit within 1.5 h of one (with the share of the input's area that is that near)
  gates       non-manifold vertices (metrics.py counts edges only), which together with metrics.json
              gives H1-H6 and V1-V6 of docs/TARGETS.md
"""
import argparse
import csv
import glob
import json
import os
import sys

import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from scipy.spatial import cKDTree

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from metrics import corners, edge_faces, face_adjacency, flat, input_folds, nonmanifold_vertices, read_obj  # noqa: E402,F401

BANDS = ("smooth", "curved", "sharp")


def read_tris(path):
    V, faces = read_obj(path)
    F = np.array([[f[0], f[k], f[k + 1]] for f in faces for k in range(1, len(f) - 1)], np.int64)
    return V, F


def newell(V, faces):
    fid, v, nx, _ = corners(faces)
    a, b = V[v], V[nx]
    N = np.zeros((len(faces), 3))
    np.add.at(N, fid, np.cross(a, b))
    return N / 2  # area vector


def scaled_jacobian(V, faces):
    """Min over corners of det(e_next, e_prev, n_hat), normalised; for quads only (others NaN)."""
    N = newell(V, faces)
    nh = N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-300)
    fid, v, nx, pv = corners(faces)
    e1, e2 = V[nx] - V[v], V[pv] - V[v]
    s = np.einsum("ij,ij->i", np.cross(e1, e2), nh[fid])
    s = s / np.maximum(np.linalg.norm(e1, axis=1) * np.linalg.norm(e2, axis=1), 1e-300)
    sj = np.full(len(faces), np.inf)
    np.minimum.at(sj, fid, s)
    sj[np.array([len(f) for f in faces]) != 4] = np.nan
    return sj


def catmull_clark(V, faces):
    """One Catmull-Clark step (n-gons and boundaries; a non-manifold edge is treated as a crease)."""
    nv = len(V)
    fid, v, nx, pv = corners(faces)
    nf = len(faces)
    cnt = np.bincount(fid, minlength=nf)[:, None]
    FP = np.zeros((nf, 3))
    np.add.at(FP, fid, V[v])
    FP /= cnt
    E, inv, efid = edge_faces(faces)
    ne = len(E)
    euse = np.bincount(inv, minlength=ne)
    efs = np.zeros((ne, 3))
    np.add.at(efs, inv, FP[efid])
    mid = (V[E[:, 0]] + V[E[:, 1]]) / 2
    interior = euse == 2
    EP = mid.copy()
    EP[interior] = (V[E[interior, 0]] + V[E[interior, 1]] + efs[interior]) / 4
    # vertex points
    val_f = np.bincount(v, minlength=nv).astype(float)
    Fsum = np.zeros((nv, 3))
    np.add.at(Fsum, v, FP[fid])
    Rsum, Rn = np.zeros((nv, 3)), np.zeros(nv)
    for k in (0, 1):
        np.add.at(Rsum, E[:, k], mid)
        np.add.at(Rn, E[:, k], 1)
    crease = ~interior
    on_crease = np.zeros(nv, int)
    Bsum = np.zeros((nv, 3))
    for k in (0, 1):
        np.add.at(on_crease, E[crease, k], 1)
        np.add.at(Bsum, E[crease, k], V[E[crease, 1 - k]])
    n = np.maximum(val_f, 1)[:, None]
    VP = (Fsum / n + 2 * Rsum / np.maximum(Rn, 1)[:, None] + (n - 3) * V) / n
    b = on_crease == 2
    VP[b] = 0.75 * V[b] + 0.125 * Bsum[b]
    corner_v = on_crease > 2
    VP[corner_v] = V[corner_v]
    # new quads: (vertex, edge to next, face, edge from previous)
    key_n = np.sort(np.stack([v, nx], 1), 1)
    key_p = np.sort(np.stack([pv, v], 1), 1)
    lookup = {tuple(e): i for i, e in enumerate(E)}
    en = np.array([lookup[tuple(k)] for k in key_n])
    ep = np.array([lookup[tuple(k)] for k in key_p])
    NV = np.concatenate([VP, EP, FP])
    Q = np.stack([v, nv + en, nv + ne + fid, nv + ep], 1)
    return NV, [list(q) for q in Q], fid


def analyse(inp, out, cache={}):
    if inp not in cache:
        cache.clear()
        Vin, Fin = read_tris(inp)
        An = np.cross(Vin[Fin[:, 1]] - Vin[Fin[:, 0]], Vin[Fin[:, 2]] - Vin[Fin[:, 0]]) / 2
        folds = input_folds(Vin, Fin)
        cache[inp] = (Vin, Fin, Vin[Fin].mean(1), An, cKDTree(Vin[Fin].mean(1)), folds,
                      cKDTree(Vin[Fin][folds].mean(1)) if folds.any() else None)
    Vin, Fin, Cin, An, tree, folds, fold_tree = cache[inp]
    V, faces = read_obj(out)
    sides = np.array([len(f) for f in faces])
    sj = scaled_jacobian(V, faces)
    folded = (sides == 4) & (sj <= 0)
    nonq = sides != 4
    area = np.linalg.norm(An, axis=1).sum()
    h = np.sqrt(area / max(1, (sides == 4).sum()))
    C = np.array([V[f].mean(0) for f in faces])
    R = np.empty(len(faces))
    for i, nb in enumerate(tree.query_ball_point(C, h)):
        a = An[nb]
        R[i] = np.linalg.norm(a.sum(0)) / max(np.linalg.norm(a, axis=1).sum(), 1e-300) if nb else 1.0
    band = np.where(R >= 0.95, 0, np.where(R >= 0.7, 1, 2))
    A = face_adjacency(faces)
    nq_idx = np.flatnonzero(nonq)
    clusters = largest = 0
    if len(nq_idx):
        _, lab = connected_components(A[nq_idx][:, nq_idx], directed=False)
        sizes = np.bincount(lab)
        clusters, largest = int(len(sizes)), int(sizes.max())
    touches_fold = int((A[nq_idx][:, np.flatnonzero(folded)].sum(1) > 0).sum()) if folded.any() else 0
    SV, sf, parent = catmull_clark(V, faces)
    ssj = scaled_jacobian(SV, sf)
    against_parent = np.einsum("ij,ij->i", newell(SV, sf), newell(V, faces)[parent]) < 0

    # Defects on folded input: within 1.5 h of a folded input face (base = share of the input's area there).
    if fold_tree is not None:
        on_fold = fold_tree.query(C, distance_upper_bound=1.5 * h)[0] < np.inf
        an = np.linalg.norm(An, axis=1)
        base = float(an[fold_tree.query(Cin, distance_upper_bound=1.5 * h)[0] < np.inf].sum() / an.sum())
    else:
        on_fold, base = np.zeros(len(faces), bool), 0.0

    def share(mask):
        tot = max(int(mask.sum()), 1)
        return {BANDS[b]: round(float((mask & (band == b)).sum()) / tot, 3) for b in range(3)}

    return {
        "faces": {"total": len(faces), "triangles": int((sides == 3).sum()), "quads": int((sides == 4).sum()),
                  "pentagons": int((sides == 5).sum()), "six_plus": int((sides >= 6).sum()),
                  "non_quad_clusters": clusters, "largest_cluster": largest},
        "folded_quads": int(folded.sum()),
        "h": float(h),
        "where": {"all": share(np.ones(len(faces), bool)), "non_quads": share(nonq), "folded": share(folded),
                  "non_quads_touching_fold": touches_fold},
        "subdiv": {"folded": int((ssj <= 0).sum()), "sj_min": float(np.nanmin(ssj)),
                   "flipped_vs_parent": int(against_parent.sum())},
        "input_folds": {"triangles": int(folds.sum()), "area_share_near": round(base, 4),
                        "non_quads_on": int((nonq & on_fold).sum()), "folded_on": int((folded & on_fold).sum())},
        "nonmanifold_vertices": nonmanifold_vertices(faces, len(V)),
        "nonmanifold_vertices_input": nonmanifold_vertices([list(f) for f in Fin], len(Vin)),
    }


def find_runs(root, sets, corpus):
    files = {m["id"]: m["file"] for m in corpus["meshes"]}
    if any(os.path.isdir(os.path.join(root, s)) for s in sets):  # ab.py
        for rec in (r for s in sets for r in glob.glob(os.path.join(root, s, "*", "*", "u*", "record.json"))):
            r = json.load(open(rec))
            yield {"mesh": r["mesh"], "tool": "ours_" + r["res"], "target": r["target"], "replicate": r["replicate"],
                   "dir": os.path.dirname(rec), "record": r, "file": files[r["mesh"]]}
    else:  # run_compare.py
        for rec in glob.glob(os.path.join(root, "*", "*", "t*", "record.json")):
            r = json.load(open(rec))
            mesh, tool = rec.split(os.sep)[-4:-2]
            if mesh in files:
                yield {"mesh": mesh, "tool": tool, "target": int(rec.split(os.sep)[-2][1:]), "replicate": 0,
                       "dir": os.path.dirname(rec), "record": r, "file": files[mesh]}


def summary(rows):
    """One row per mesh and tool, summed over targets and resolutions (replicate 0 only)."""
    groups = {}
    for r in rows:
        if r["replicate"] == 0:
            groups.setdefault((r["mesh"], r["tool"].replace("_fast", "").replace("_quality", "")), []).append(r)
    out = ["| mesh | tool | runs | failed | tris | pents | 6+ | largest cluster | non-quads in sharp band | "
           "faces in sharp band | folded | folded after CC | non-quads touching a fold | islands, genus, holes kept |",
           "|" + "---|" * 14]
    for (mesh, tool), rs in sorted(groups.items()):
        ok = [r for r in rs if r["rc"] == 0 and "quads" in r]
        nq = sum(r["tris"] + r["pents"] + r["six_plus"] for r in ok)
        topo = sum(1 for r in ok if r["islands"].split("/")[0] == r["islands"].split("/")[1]
                   and r["euler"].split("/")[0] == r["euler"].split("/")[1]
                   and r["loops"].split("/")[0] == r["loops"].split("/")[1])
        sharp = sum((r["tris"] + r["pents"] + r["six_plus"]) * r["nq_sharp"] for r in ok)
        out.append("| " + " | ".join(str(x) for x in [
            mesh, tool, len(rs), len(rs) - len(ok), sum(r["tris"] for r in ok), sum(r["pents"] for r in ok),
            sum(r["six_plus"] for r in ok), max([r["largest_cluster"] for r in ok], default=0),
            f"{sharp / nq:.0%}" if nq else "–",
            f"{sum(r['all_sharp'] for r in ok) / len(ok):.0%}" if ok else "–",
            sum(r["folded"] for r in ok), sum(r["cc_folded"] for r in ok), sum(r["nq_touch_fold"] for r in ok),
            f"{topo}/{len(ok)}"]) + " |")
    reps = {}
    for r in rows:
        reps.setdefault((r["mesh"], r["tool"], r["target"]), []).append(r)
    reps = {k: v for k, v in reps.items() if len(v) > 1}
    if reps:
        out += ["", "Replicates (u0 plain, u1-u5 float32 1-ulp copies): runs that keep islands, genus and "
                "boundary loops, and the spread of non-quads and folds.", "",
                "| mesh | tool | target | kept | non-quads | folded |", "|---|---|---|---|---|---|"]
        for (mesh, tool, target), rs in sorted(reps.items()):
            ok = [r for r in rs if r["rc"] == 0 and "quads" in r]
            kept = sum(1 for r in ok if all(r[c].split("/")[0] == r[c].split("/")[1] for c in ("islands", "euler", "loops")))
            nq = [r["tris"] + r["pents"] + r["six_plus"] for r in ok]
            fo = [r["folded"] for r in ok]
            out.append(f"| {mesh} | {tool} | {target} | {kept}/{len(rs)} | {min(nq)}-{max(nq)} | {min(fo)}-{max(fo)} |")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--runs", action="append", required=True)
    ap.add_argument("--meshes", required=True)
    ap.add_argument("--set", default="sculpt", help="ab.py set(s), comma list")
    ap.add_argument("--only", help="comma list of meshes")
    ap.add_argument("--md")
    ap.add_argument("--csv")
    a = ap.parse_args()
    corpus = json.load(open(os.environ.get("AR_CORPUS") or os.path.join(HERE, "corpus.json")))
    rows = []
    for root in a.runs:
        runs = find_runs(root, a.set.split(","), corpus)
        for run in sorted(runs, key=lambda r: (r["file"], r["tool"], r["target"], r["replicate"])):
            if a.only and run["mesh"] not in a.only.split(","):
                continue
            out = os.path.join(run["dir"], "out.obj")
            row = {k: run[k] for k in ("mesh", "tool", "target", "replicate")}
            rec = run["record"]
            if "attempts" in rec:  # run_compare.py: the attempt kept as out.obj
                att = rec["attempts"][rec["chosen"] - 1] if rec.get("chosen") else rec["attempts"][-1]
                rc, row["time"], row["peak_rss_mb"] = (0 if rec.get("ok") else att.get("exit") or "fail",
                                                       att.get("wall_s"), att.get("peak_rss_mb"))
            else:
                rc, row["time"], row["peak_rss_mb"] = rec.get("rc", 0), rec.get("time"), rec.get("peak_rss_mb")
            row["rc"] = rc
            if rc != 0 or not os.path.exists(out):
                rows.append(row)
                continue
            cache = os.path.join(run["dir"], "sculpt.json")
            if not os.path.exists(cache):
                json.dump(analyse(os.path.join(a.meshes, run["file"]), out), open(cache, "w"), indent=1)
            s = json.load(open(cache))
            mj = os.path.join(run["dir"], "metrics.json")
            if not os.path.exists(mj):  # run_compare.py runs before metrics_compare.py
                import metrics

                json.dump(metrics.compute(os.path.join(a.meshes, run["file"]), out, run["target"], samples=100000),
                          open(mj, "w"), indent=1)
            m = json.load(open(mj))
            t = m.get("topology", {})
            row.update({
                "quads": s["faces"]["quads"], "tris": s["faces"]["triangles"], "pents": s["faces"]["pentagons"],
                "six_plus": s["faces"]["six_plus"], "clusters": s["faces"]["non_quad_clusters"],
                "largest_cluster": s["faces"]["largest_cluster"],
                "nq_smooth": s["where"]["non_quads"]["smooth"], "nq_sharp": s["where"]["non_quads"]["sharp"],
                "all_sharp": s["where"]["all"]["sharp"], "folded": s["folded_quads"],
                "fold_sharp": s["where"]["folded"]["sharp"], "nq_touch_fold": s["where"]["non_quads_touching_fold"],
                "input_folded": (s.get("input_folds") or {}).get("triangles"),
                "nq_on_input_fold": (s.get("input_folds") or {}).get("non_quads_on"),
                "folded_on_input_fold": (s.get("input_folds") or {}).get("folded_on"),
                "cc_folded": s["subdiv"]["folded"], "cc_flipped": s["subdiv"]["flipped_vs_parent"],
                "nm_edges": t.get("non_manifold_edges"), "nm_verts": s["nonmanifold_vertices"],
                "inconsistent": t.get("inconsistently_oriented_edges"),
                "islands": f'{t.get("components")}/{t.get("components_input")}',
                "euler": f'{t.get("euler_output")}/{t.get("euler_input")}',
                "loops": f'{t.get("boundary_loops")}/{t.get("boundary_loops_input")}',
                "count_ratio": round(s["faces"]["quads"] / run["target"], 3),
                "flipped_vs_input": (m.get("flipped") or {}).get("faces_vs_input"),
                "p99": (m.get("distance") or {}).get("in_to_out_p99"),
            })
            rows.append(row)
    cols = list(dict.fromkeys(k for r in rows for k in r))
    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, cols)
            w.writeheader()
            w.writerows(rows)
    lines = ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
    for r in rows:
        lines.append("| " + " | ".join("" if r.get(c) is None else
                                       (f"{r[c]:.4g}" if isinstance(r[c], float) else str(r[c])) for c in cols) + " |")
    text = summary(rows) + "\n" + "\n".join(lines) + "\n"
    if a.md:
        open(a.md, "w").write(text)
    else:
        print(text)


if __name__ == "__main__":
    main()
