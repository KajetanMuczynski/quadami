#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Quality metrics for a remeshed (quad-dominant) mesh against its input.

    python metrics.py --input in.obj --output out.obj [--target-quads N]
                      [--sym-axis x --sym-center 0] [--samples 200000] [--json m.json]
                      [--input-cache DIR] [--sharp-degrees 90]

Needs numpy, scipy and libigl (python bindings, `pip install libigl`).

Reported groups (all lengths relative to the input bounding-box diagonal unless
named *_rel_edge, which are relative to the output mean edge length):
  faces       counts by size, quad fraction, achieved / requested quads
  topology    edges by use (boundary / manifold / non-manifold), inconsistently
              oriented edges, components, Euler characteristic and boundary loops
              in vs out, same_as_input (all of these match, no non-manifold edge)
  valence     histogram of interior and boundary vertex valence, irregular
              (interior valence != 4) count and per 1k quads
  quads       corner-angle deviation from 90 deg, scaled Jacobian (min over the
              4 corners, w.r.t. the quad's Newell normal; <= 0 means folded or
              non-convex) with its 1st/5th percentile (TARGETS G1), skew (per quad
              the largest |corner angle - 90|) 95th percentile (G2), edge-length
              aspect ratio
  creases     (TARGETS A1) share of the length of input edges sharper than 40 deg
              that has an output edge within 0.1 h and 10 deg, h = output mean
              edge (the remesher comparison's definition; TARGETS wants local h);
              crease_report(): the same on creases at the run's sharp-edge angle,
              on those not in a strip narrower than h/2 (A1theta), and quads bent
              across a crease per 1k quads
  degenerate  faces with a repeated vertex, near-zero area or near-zero edge
  flipped     output faces whose normal opposes the input normal at the closest
              input point (minority orientation, so a globally reversed output
              counts as reversed, not as all-flipped). The input is first
              oriented the way the core's orientInput does (each manifold-
              connected piece follows most of its area), so a patch exported
              with the other winding is not counted; faces_vs_raw_input keeps
              the old count against the file's own normals
  validity    per input island (vertex-connected part), matched to output
              islands by nearest input triangle: dropped (under half its area
              within one output edge), split, merged (covered but without an
              output island of its own),
              Euler characteristic / boundary loops changed, holes added or
              closed, handles lost or added (genus per island; a handle moved
              between islands shows here, not in same_as_input), non-manifold
              vertices, folded faces of any size (the CLI's folded_faces),
              input area farther than one output edge from the output
              (area_uncovered) or that near but under an output face facing the
              other way (area_wrong_side: thin parts collapsed into one sheet,
              pleats), islands_same_as_input (none dropped, split, merged or
              changed, output manifold). Input islands that are not manifold
              themselves have no right topology: they are counted in
              islands_input_non_manifold and left out of the topology counts
              (a dropped one still counts as dropped)
  normal_deviation_deg  angle between output face and (oriented) input normal at the closest point (D3)
  distance    one-sided mean / max and two-sided Hausdorff on area samples
  symmetry    (with --sym-axis) input reflection error, output geometric
              reflection error, and output *topological* symmetry: distance from
              each reflected output vertex to the nearest output vertex, in mean
              edge lengths (0 for a mirror-symmetric layout)
"""
import argparse
import hashlib
import json
import os
import sys

import numpy as np
from scipy.spatial import cKDTree

try:
    import igl
except ImportError:  # pragma: no cover
    sys.exit("metrics.py needs libigl: pip install libigl")


def read_obj(path):
    """Positions and polygon faces (position indices, 0-based), like tinyobj without triangulation.

    Only the first three numbers of a `v` line are the position: exporters that write vertex colours
    (MeshLab, ZBrush, Blender with colours on) add three more, which must not become extra vertices.
    Negative face indices count back from the last vertex read, as the OBJ format defines them."""
    with open(path) as f:
        lines = f.read().splitlines()
    V, faces = [], []
    for l in lines:
        t = l.split()
        if not t:
            continue
        if t[0] == "v":
            V.append(t[1:4])
        elif t[0] == "f":
            faces.append([i - 1 if i > 0 else len(V) + i for i in (int(x.split("/")[0]) for x in t[1:])])
    return (np.array(V, dtype=float) if V else np.zeros((0, 3))), faces


def read_obj_lines(path):
    """Vertex pairs (E, 2, 0-based) of an OBJ's line elements: marked edges, as the CLI reads them."""
    pairs = []
    with open(path) as f:
        for l in f:
            if l.startswith("l "):
                idx = [int(t.split("/")[0]) - 1 for t in l.split()[1:]]
                pairs.extend(zip(idx[:-1], idx[1:]))
    return np.asarray(pairs, dtype=np.int64).reshape(-1, 2)


def triangulate(faces):
    tris = []
    for f in faces:
        for k in range(1, len(f) - 1):
            tris.append((f[0], f[k], f[k + 1]))
    return np.asarray(tris, dtype=np.int64).reshape(-1, 3)


def sample_surface(V, F, n, rng, with_faces=False):
    """n area-uniform points on the triangles F; with_faces also returns each point's triangle (None
    when the mesh has no area and the vertices are returned instead)."""
    a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
    area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
    if area.sum() <= 0:
        return (V.copy(), None) if with_faces else V.copy()
    idx = rng.choice(len(F), size=n, p=area / area.sum())
    r1, r2 = rng.random(n), rng.random(n)
    s = np.sqrt(r1)
    P = (1 - s)[:, None] * a[idx] + (s * (1 - r2))[:, None] * b[idx] + (s * r2)[:, None] * c[idx]
    return (P, idx) if with_faces else P


def stats(x, prefix=""):
    x = np.asarray(x, float)
    if x.size == 0:
        return {}
    return {prefix + "mean": float(x.mean()), prefix + "p50": float(np.percentile(x, 50)),
            prefix + "p95": float(np.percentile(x, 95)), prefix + "max": float(x.max()), prefix + "min": float(x.min())}


def point_to_mesh(P, V, F, chunk=200000):
    d2 = np.empty(len(P))
    fi = np.empty(len(P), dtype=np.int64)
    for s in range(0, len(P), chunk):
        dd, ii, _ = igl.point_mesh_squared_distance(P[s:s + chunk], V, F)
        d2[s:s + chunk] = dd
        fi[s:s + chunk] = ii
    return np.sqrt(np.maximum(d2, 0)), fi


def newell(V, face):
    p = V[face]
    q = np.roll(p, -1, axis=0)
    return np.array([np.sum((p[:, 1] - q[:, 1]) * (p[:, 2] + q[:, 2])),
                     np.sum((p[:, 2] - q[:, 2]) * (p[:, 0] + q[:, 0])),
                     np.sum((p[:, 0] - q[:, 0]) * (p[:, 1] + q[:, 1]))])


def comps(n, edges):
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components

    A = coo_matrix((np.ones(len(edges)), (edges[:, 0], edges[:, 1])), shape=(n, n))
    return connected_components(A, directed=False)[1]


def loops(n, edges):
    """Boundary loops: connected components of the boundary edges (a pinched loop counts once)."""
    if not len(edges):
        return 0
    return int(len(np.unique(comps(n, edges)[np.unique(edges)])))


def flat(faces):
    if isinstance(faces, np.ndarray):  # all faces of one size, e.g. triangles
        n = np.full(len(faces), faces.shape[1])
        return n, faces.ravel(), np.repeat(np.arange(len(faces)), n)
    n = np.array([len(f) for f in faces])
    return n, np.concatenate([np.asarray(f) for f in faces]), np.repeat(np.arange(len(faces)), n)


def corners(faces):
    """Per corner: face, vertex, next vertex, previous vertex."""
    n, idx, fid = flat(faces)
    start = np.repeat(np.cumsum(n) - n, n)
    pos = np.arange(len(idx)) - start
    nxt = idx[start + (pos + 1) % np.repeat(n, n)]
    prv = idx[start + (pos - 1) % np.repeat(n, n)]
    return fid, idx, nxt, prv


def nonmanifold_vertices(faces, nv):
    """Vertices whose incident faces do not form one edge-connected fan."""
    return int(nonmanifold_vertex_mask(faces, nv).sum())


def nonmanifold_vertex_mask(faces, nv):
    """nonmanifold_vertices() as a mask over the nv vertices."""
    fid, v, nx, pv = corners(faces)
    c = np.arange(len(v))
    # corners (v, f1) and (v, f2) are linked when f1 and f2 share an edge at v
    keys = np.concatenate([np.stack([v, np.minimum(v, nx), np.maximum(v, nx)], 1),
                           np.stack([v, np.minimum(v, pv), np.maximum(v, pv)], 1)])
    cs = np.concatenate([c, c])
    uniq, inv = np.unique(keys, axis=0, return_inverse=True)
    inv = inv.ravel()
    order = np.argsort(inv, kind="stable")
    inv, cs = inv[order], cs[order]
    same = inv[1:] == inv[:-1]
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components

    A = coo_matrix((np.ones(same.sum()), (cs[:-1][same], cs[1:][same])), shape=(len(v), len(v)))
    _, lab = connected_components(A, directed=False)
    fans = np.zeros(nv, int)
    pairs = np.unique(np.stack([v, lab], 1), axis=0)
    np.add.at(fans, pairs[:, 0], 1)
    return fans > 1



def edge_faces(faces):
    fid, v, nx, _ = corners(faces)
    nv = int(max(v.max(), nx.max())) + 1 if len(v) else 1
    # one int64 per edge: much faster than np.unique(axis=0), same (lexicographic) order
    uk, inv = np.unique(np.minimum(v, nx).astype(np.int64) * nv + np.maximum(v, nx), return_inverse=True)
    return np.stack([uk // nv, uk % nv], 1), inv.ravel(), fid


def face_adjacency(faces):
    _, inv, fid = edge_faces(faces)
    order = np.argsort(inv, kind="stable")
    inv, fid = inv[order], fid[order]
    same = inv[1:] == inv[:-1]
    a, b = fid[:-1][same], fid[1:][same]
    m = len(faces)
    from scipy.sparse import coo_matrix

    return coo_matrix((np.ones(len(a)), (a, b)), shape=(m, m)).tocsr()


def input_folds(V, F, radius=None, faces=None, tree=None, adj=None):
    """Faces whose normal points against the area-weighted mean normal of the surface within geodesic
    distance `radius` of them (default 1 % of the bounding-box diagonal; distances between centroids
    of edge-adjacent faces; the faces sharing an edge always count, for triangles larger than the
    radius): folded-over input left by brush strokes (twists, over-pinched creases,
    pleats), where nearly all defects of the 2026-10-21 real-sculpt tests sat. Geodesic, so the other
    side of a thin part or a separate island nearby does not count. Returns a boolean mask over F.

    `faces` (indices) limits the test to those faces, the rest stay False: the full test costs minutes
    on a 2M-triangle scan, where metrics.py needs it only around flipped output faces. `tree` is a
    cKDTree over the face centroids and `adj` the symmetric face_adjacency(F), if the caller has them."""
    import heapq

    if radius is None:
        radius = 0.01 * np.linalg.norm(V.max(0) - V.min(0))
    C = V[F].mean(1)
    A = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]]) / 2
    area = np.linalg.norm(A, axis=1)
    n = A / np.maximum(area[:, None], 1e-300)
    out = np.zeros(len(F), bool)
    faces = np.arange(len(F)) if faces is None else np.asarray(faces, np.int64)
    if not len(faces):
        return out
    tree = cKDTree(C) if tree is None else tree
    # Euclidean candidates first (a superset on the same sheet), then the geodesic test on those only.
    # In chunks: one query over all faces holds every neighbour list at once (12-16 GB on a
    # 394k-triangle sculpt at 3k quads, research/hand-failures-2026-10-26.md F1).
    cand = []
    for s in range(0, len(faces), 1024):
        chunk = faces[s:s + 1024]
        cand += [i for i, nb in zip(chunk, tree.query_ball_point(C[chunk], radius)) if n[i] @ A[nb].sum(0) < 0]
    if not cand:
        return out
    if adj is None:
        adj = face_adjacency(F)
        adj = (adj + adj.T).tocsr()
    for i in cand:
        dist = {i: 0.0}
        heap = [(0.0, i)]
        m = np.zeros(3)
        while heap:
            d, f = heapq.heappop(heap)
            if d > dist[f]:
                continue
            m += A[f]
            for g in adj.indices[adj.indptr[f]:adj.indptr[f + 1]]:
                nd = d + float(np.linalg.norm(C[g] - C[f]))
                if (nd <= radius or f == i) and nd < dist.get(g, np.inf):  # the first ring always counts
                    dist[g] = nd
                    heapq.heappush(heap, (nd, g))
        out[i] = n[i] @ m < 0
    return out


def turned_over(V, F, mask, adj, min_deg=150.0):
    """The faces of `mask` in an edge-connected cluster (of masked faces) that has an edge where the
    surface turns over: dihedral angle above min_deg between a masked face and a neighbour.

    input_folds() alone also flags tight but valid shapes, where the surface curves back within its
    radius without a crease (human_neutral's lips and eyelids: 550 faces, 2 clusters with a turned
    edge, 78 faces); a fold left by a brush has the turned edge (brush_twist 408 of 434 faces kept,
    brush_box_relief 59 of 70)."""
    from scipy.sparse.csgraph import connected_components

    idx = np.flatnonzero(mask)
    out = np.zeros(len(F), bool)
    if not len(idx):
        return out
    N = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    n = N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-300)
    sub = adj[idx]
    rows = np.repeat(np.arange(len(idx)), np.diff(sub.indptr))
    turned = np.zeros(len(idx), bool)
    turned[rows[np.einsum("ij,ij->i", n[idx[rows]], n[sub.indices]) < np.cos(np.radians(min_deg))]] = True
    _, lab = connected_components(sub[:, idx], directed=False)
    out[idx[np.isin(lab, lab[turned])]] = True
    return out


def orient_like_core(Vin, Fin):
    """Input triangles turned the way the core's orientInput turns them (TODO 2.7, PR #51).

    Each piece connected through manifold edges takes the orientation of most of its area; ties keep
    the piece's first triangle; non-orientable pieces stay as they are. cleanInput's merge of edges
    shorter than 4 float32 ulps is not mirrored: it changes only which triangles are adjacent across a
    zero-length edge, and a consistently oriented input is returned unchanged either way.
    Returns the oriented triangles, the number turned and the non-orientable pieces.
    """
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components

    n = len(Fin)
    a = Fin.ravel()
    b = Fin[:, [1, 2, 0]].ravel()
    tri = np.repeat(np.arange(n), 3)
    keep = a != b
    a, b, tri = a[keep], b[keep], tri[keep]
    key = np.minimum(a, b) * (int(Fin.max()) + 1 if n else 1) + np.maximum(a, b)
    order = np.lexsort((tri, key))
    key, tri, fwd = key[order], tri[order], (a < b)[order]
    _, start, cnt = np.unique(key, return_index=True, return_counts=True)
    man = cnt == 2
    s = start[man]
    t1, t2 = tri[s], tri[s + 1]
    ok = t1 != t2
    t1, t2, mismatch = t1[ok], t2[ok], (fwd[s] == fwd[s + 1])[ok]
    if not mismatch.any():
        return Fin, 0, 0
    # Pieces, and a doubled graph (t, t + n = t turned) whose components give each triangle's side.
    _, piece = connected_components(coo_matrix((np.ones(len(t1)), (t1, t2)), shape=(n, n)), directed=False)
    same = ~mismatch
    rows = np.concatenate([t1[same], t1[same] + n, t1[mismatch], t1[mismatch] + n])
    cols = np.concatenate([t2[same], t2[same] + n, t2[mismatch] + n, t2[mismatch]])
    _, side = connected_components(coo_matrix((np.ones(len(rows)), (rows, cols)), shape=(2 * n, 2 * n)),
                                   directed=False)
    orientable = np.ones(piece.max() + 1, bool)
    np.logical_and.at(orientable, piece, side[:n] != side[n:])
    seed = np.full(piece.max() + 1, n)
    np.minimum.at(seed, piece, np.arange(n))
    flip = side[:n] != side[seed[piece]]
    area = 0.5 * np.linalg.norm(np.cross(Vin[Fin[:, 1]] - Vin[Fin[:, 0]], Vin[Fin[:, 2]] - Vin[Fin[:, 0]]), axis=1)
    a0 = np.bincount(piece, weights=area * ~flip, minlength=len(seed))
    a1 = np.bincount(piece, weights=area * flip, minlength=len(seed))
    turned_side = np.where(a1 > a0, 0, 1)
    turn = orientable[piece] & (flip.astype(int) == turned_side[piece])
    out = Fin.copy()
    out[turn] = out[turn][:, [0, 2, 1]]
    return out, int(turn.sum()), int((~orientable).sum())


def island_topology(nv, first_vertex, E, bnd, used, vlab, nlab):
    """Per island (vertex label vlab, 0..nlab-1): Euler characteristic and boundary loops.

    first_vertex is one vertex per face, E the unique edges, bnd the boundary edges, used the vertices
    in a face. A pinched boundary loop counts once, as in loops().
    """
    chi = (np.bincount(vlab[used], minlength=nlab) - np.bincount(vlab[E[:, 0]], minlength=nlab)
           + np.bincount(vlab[first_vertex], minlength=nlab))
    nb = np.zeros(nlab, np.int64)
    if len(bnd):
        pairs = np.unique(np.stack([vlab[bnd[:, 0]], comps(nv, bnd)[bnd[:, 0]]], 1), axis=0)
        np.add.at(nb, pairs[:, 0], 1)
    return chi.astype(np.int64), nb


def folded_faces(V, faces, sizes):
    """Faces with a corner turning against the face's area vector (scaled Jacobian <= 0), degenerate
    faces left out: the core's `folded_faces` (measureMesh in remesh.cpp), for every face size, so
    a pleated pentagon counts too (V2 tests quads only)."""
    fid, v, nx, pv = corners(faces)
    N = np.zeros((len(faces), 3))
    np.add.at(N, fid, np.cross(V[v], V[nx]))  # twice the area vector
    e1, e2 = V[nx] - V[v], V[pv] - V[v]
    longest = np.zeros(len(faces))
    np.maximum.at(longest, fid, np.sum(e1 * e1, 1))
    repeated = np.array([len(set(f)) != len(f) for f in faces])
    degenerate = repeated | (0.5 * np.linalg.norm(N, axis=1) <= 1e-12 * longest)
    bad = np.zeros(len(faces), bool)
    np.logical_or.at(bad, fid, np.einsum("ij,ij->i", np.cross(e1, e2), N[fid]) <= 0)
    return int(np.sum(bad & ~degenerate))


def validity(inp, V, faces, sizes, normals, E, bnd, used, vertex_comp, cf, reversed_out, h, in_tri, d, out_face, Ftri):
    """Validity per input island (TARGETS H2, H4, H5, V6 and the thin-part part of V3).

    Output islands are matched to input islands by the input triangle nearest to each output face's
    centroid (majority). Coverage: an area sample of the input is covered when the output is within
    h (output mean edge) of it, and on the wrong side when it is that near but the nearest output
    face faces the other way (a thin part collapsed into one sheet, or a pleat). Islands with fewer
    than 200 area samples are measured on up to 2000 of their triangles' centroids instead.
    """
    from scipy.sparse import coo_matrix

    Vin, Fin, Fo = inp["Vin"], inp["Fin"], inp["Fin_oriented"]
    tri_island, in_chi, in_loops = inp["tri_island"], inp["island_euler"], inp["island_loops"]
    nin = len(in_chi)
    _, inv = np.unique(vertex_comp[used], return_inverse=True)
    olab = np.zeros(len(V), np.int64)
    olab[used] = inv.ravel()
    nout = int(olab[used].max()) + 1
    first = np.array([f[0] for f in faces])
    out_chi, out_loops = island_topology(len(V), first, E, bnd, used, olab, nout)
    # Faces of each output island over each input island.
    C = coo_matrix((np.ones(len(faces)), (olab[first], tri_island[cf])), shape=(nout, nin)).tocsr()
    C.sum_duplicates()
    major = np.asarray(C.argmax(1)).ravel()
    k = np.bincount(major, minlength=nin)
    chi_sum = np.bincount(major, weights=out_chi, minlength=nin).astype(np.int64)
    loops_sum = np.bincount(major, weights=out_loops, minlength=nin).astype(np.int64)

    # Coverage per input island.
    nin_o = np.cross(Vin[Fo[:, 1]] - Vin[Fo[:, 0]], Vin[Fo[:, 2]] - Vin[Fo[:, 0]])
    sign = -1.0 if reversed_out else 1.0
    if in_tri is None:  # input without area
        cov = np.ones(nin)
        uncovered = wrong = 0.0
    else:
        near = d <= h
        opposite = sign * np.einsum("ij,ij->i", nin_o[in_tri], normals[out_face]) < 0
        uncovered = float(np.mean(~near))
        wrong = float(np.mean(near & opposite))
        isl = tri_island[in_tri]
        n_s = np.bincount(isl, minlength=nin)
        cov = np.bincount(isl, weights=near, minlength=nin) / np.maximum(n_s, 1)
        rng = np.random.default_rng(1)
        area = 0.5 * np.linalg.norm(nin_o, axis=1)
        for i in np.nonzero(n_s < 200)[0]:
            tris = np.nonzero(tri_island == i)[0]
            if len(tris) > 2000:
                tris = rng.choice(tris, 2000, replace=False)
            dd, _ = point_to_mesh(Vin[Fin[tris]].mean(1), V, Ftri)
            w = area[tris]
            cov[i] = float(np.sum(w * (dd <= h)) / w.sum()) if w.sum() > 0 else float(np.mean(dd <= h))
    # Dropped: under half of the island's area has output within h. Merged: no output island of its
    # own, but covered, i.e. joined to a neighbour's output island (a centroid vote alone would also
    # flag a small island that only touches a bigger one).
    dropped = cov < 0.5
    merged = int(np.sum((k == 0) & ~dropped))
    manifold_in = ~inp["island_non_manifold"]
    kept = (k > 0) & manifold_in
    changed = ~((k == 1) & (chi_sum == in_chi) & (loops_sum == in_loops)) & manifold_in
    # Twice the genus: 2 * components - chi - boundary loops (odd only for a non-orientable surface).
    g2_in = 2 - in_chi - in_loops
    g2_out = 2 * k - chi_sum - loops_sum
    res = {"non_manifold_vertices": nonmanifold_vertices(faces, len(V)),
           "folded_faces": folded_faces(V, faces, sizes),
           "islands_input": int(nin), "islands_input_non_manifold": int((~manifold_in).sum()),
           "islands_output": int(nout), "islands_dropped": int(dropped.sum()),
           "islands_split": int(np.sum((k > 1) & manifold_in)), "islands_merged": merged,
           "islands_topology_changed": int(changed.sum()),
           "holes_added": int(np.sum(np.maximum(loops_sum - in_loops, 0)[kept])),
           "holes_closed": int(np.sum(np.maximum(in_loops - loops_sum, 0)[kept])),
           "handles_lost": float(np.sum(np.maximum(g2_in - g2_out, 0)[kept & (k == 1)]) / 2),
           "handles_added": float(np.sum(np.maximum(g2_out - g2_in, 0)[kept & (k == 1)]) / 2),
           "area_uncovered": uncovered, "area_wrong_side": wrong,
           "island_coverage_min": float(cov.min()) if nin else None}
    if nin <= 32:
        res["per_island"] = [{"area_share": float(inp["island_area"][i] / max(inp["island_area"].sum(), 1e-300)),
                              "euler": [int(in_chi[i]), int(chi_sum[i])], "loops": [int(in_loops[i]), int(loops_sum[i])],
                              "output_islands": int(k[i]), "coverage": float(cov[i]),
                              "input_non_manifold": bool(not manifold_in[i])} for i in range(nin)]
    return res


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fp:
        for block in iter(lambda: fp.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def sharp_edges(Vin, Fin, sharp_deg=40.0):
    """Input edges with two faces whose normals differ by more than sharp_deg (TARGETS A1)."""
    he = np.concatenate([Fin[:, [0, 1]], Fin[:, [1, 2]], Fin[:, [2, 0]]])
    fid = np.tile(np.arange(len(Fin)), 3)
    key = he.min(1) * len(Vin) + he.max(1)
    order = np.argsort(key, kind="stable")
    he, fid = he[order], fid[order]
    _, start, cnt = np.unique(key[order], return_index=True, return_counts=True)
    man = cnt == 2
    f1, f2 = fid[start[man]], fid[start[man] + 1]
    n = np.cross(Vin[Fin[:, 1]] - Vin[Fin[:, 0]], Vin[Fin[:, 2]] - Vin[Fin[:, 0]])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-300)
    ang = np.degrees(np.arccos(np.clip(np.sum(n[f1] * n[f2], 1), -1, 1)))
    return he[start[man]][ang > sharp_deg].reshape(-1, 2)


def crease_samples(Vin, sharp, V, E_out, h, dist_tol=0.1, ang_tol=10.0):
    """Input sharp edges sampled at h/4: points, unit tangents, edge index, length weight, and whether an
    output edge follows each sample (within dist_tol h and ang_tol deg; output edges sampled at h/10)."""
    A, B = Vin[sharp[:, 0]], Vin[sharp[:, 1]]
    L = np.linalg.norm(B - A, axis=1)
    ns = np.maximum(1, np.ceil(L / (h / 4)).astype(int))
    idx = np.repeat(np.arange(len(sharp)), ns)
    t = (np.concatenate([np.arange(k) for k in ns]) + 0.5) / ns[idx]
    P = A[idx] + t[:, None] * (B - A)[idx]
    T = (B - A)[idx] / np.maximum(L[idx, None], 1e-300)
    w = (L / ns)[idx]
    OA, OB = V[E_out[:, 0]], V[E_out[:, 1]]
    OL = np.linalg.norm(OB - OA, axis=1)
    ons = np.maximum(1, np.ceil(OL / (h / 10)).astype(int))
    oidx = np.repeat(np.arange(len(E_out)), ons)
    ot = (np.concatenate([np.arange(k) for k in ons]) + 0.5) / ons[oidx]
    OP = OA[oidx] + ot[:, None] * (OB - OA)[oidx]
    OT = (OB - OA) / np.maximum(OL[:, None], 1e-300)
    _, nn = cKDTree(OP).query(P, k=min(16, len(OP)))
    nn = nn.reshape(len(P), -1)
    covered = np.zeros(len(P), bool)
    for j in range(nn.shape[1]):
        e = oidx[nn[:, j]]
        d = OB[e] - OA[e]
        tt = np.clip(np.sum((P - OA[e]) * d, 1) / np.maximum(np.sum(d * d, 1), 1e-300), 0, 1)
        dist = np.linalg.norm(P - (OA[e] + tt[:, None] * d), axis=1)
        covered |= (dist < dist_tol * h) & (np.abs(np.sum(T * OT[e], 1)) > np.cos(np.radians(ang_tol)))
    return P, T, idx, w, covered


def crease_coverage(Vin, sharp, V, E_out, h, dist_tol=0.1, ang_tol=10.0):
    """TARGETS A1: share of the sharp input edges' length that an output edge follows."""
    if len(sharp) == 0 or len(E_out) == 0:
        return {"input_length_rel_h": 0.0, "coverage": None}
    L = np.linalg.norm(Vin[sharp[:, 1]] - Vin[sharp[:, 0]], axis=1)
    _, _, _, w, covered = crease_samples(Vin, sharp, V, E_out, h, dist_tol, ang_tol)
    return {"input_length_rel_h": float(L.sum() / h), "coverage": float(w[covered].sum() / w.sum())}


def sharp_edge_angles(Vin, Fin, sharp):
    """Dihedral angle (between the face normals, degrees) of each edge sharp_edges() returned."""
    if len(sharp) == 0:
        return np.zeros(0)
    he = np.concatenate([Fin[:, [0, 1]], Fin[:, [1, 2]], Fin[:, [2, 0]]])
    fid = np.tile(np.arange(len(Fin)), 3)
    key = he.min(1) * len(Vin) + he.max(1)
    order = np.argsort(key, kind="stable")
    key, fid = key[order], fid[order]
    n = np.cross(Vin[Fin[:, 1]] - Vin[Fin[:, 0]], Vin[Fin[:, 2]] - Vin[Fin[:, 0]])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-300)
    pos = np.searchsorted(key, sharp.min(1) * len(Vin) + sharp.max(1))
    return np.degrees(np.arccos(np.clip(np.sum(n[fid[pos]] * n[fid[pos + 1]], 1), -1, 1)))


def narrow_strip_samples(P, T, idx, h):
    """Samples with another sharp edge within h/2 that runs near-parallel (within 30 deg) beside them
    (offset mostly across the tangent): the two lines bound a strip narrower than half a quad, which
    a quad layout of edge h cannot keep both of (TODO 5.3). A zigzag crease counts as a strip too."""
    narrow = np.zeros(len(P), bool)
    pairs = cKDTree(P).query_pairs(h / 2, output_type="ndarray")
    if not len(pairs):
        return narrow
    i, j = pairs[:, 0], pairs[:, 1]
    keep = idx[i] != idx[j]
    i, j = i[keep], j[keep]
    d = P[j] - P[i]
    dist = np.linalg.norm(d, axis=1)
    parallel = np.abs(np.sum(T[i] * T[j], 1)) > np.cos(np.radians(30.0))
    ok = parallel & (dist > 1e-9 * h)
    narrow[i[ok & (np.abs(np.sum(d * T[i], 1)) < 0.5 * dist)]] = True
    narrow[j[ok & (np.abs(np.sum(d * T[j], 1)) < 0.5 * dist)]] = True
    return narrow


def crease_report(Vin, sharp, angles, V, faces, E_out, h, sharp_degrees=90.0, bend_deg=30.0):
    """A1 split for crease preservation (research/crease-preservation-2026-10-26.md):
    coverage_theta_all  coverage of the creases at or above the run's sharp-edge angle (minus 1 deg, so a
                       right angle counts on every rotation), the creases the pipeline is asked to keep;
    resolvable_share   share of that length not in a strip narrower than h/2 (narrow_strip_samples);
    coverage_theta     A1theta: coverage of the resolvable part, the number a crease fix must move;
    bent_quads_per_1k  quads folded across a crease: the smaller fold over the two diagonals above
                       bend_deg, with a sharp input edge (> 40 deg) within h/2 of the quad's centre."""
    out = {"sharp_degrees": float(sharp_degrees), "coverage_theta_all": None, "resolvable_share": None,
           "coverage_theta": None, "bent_quads_per_1k": None}
    quads = np.array([f for f in faces if len(f) == 4], dtype=np.int64).reshape(-1, 4)
    if len(quads) and len(sharp):
        def fold(a, b, c, d):
            n1 = np.cross(V[b] - V[a], V[c] - V[a])
            n2 = np.cross(V[c] - V[a], V[d] - V[a])
            n1 /= np.maximum(np.linalg.norm(n1, axis=1, keepdims=True), 1e-300)
            n2 /= np.maximum(np.linalg.norm(n2, axis=1, keepdims=True), 1e-300)
            return np.degrees(np.arccos(np.clip(np.sum(n1 * n2, 1), -1, 1)))
        bend = np.minimum(fold(quads[:, 0], quads[:, 1], quads[:, 2], quads[:, 3]),
                          fold(quads[:, 1], quads[:, 2], quads[:, 3], quads[:, 0]))
        A, B = Vin[sharp[:, 0]], Vin[sharp[:, 1]]
        L = np.linalg.norm(B - A, axis=1)
        ns = np.maximum(1, np.ceil(L / (h / 4)).astype(int))
        idx = np.repeat(np.arange(len(sharp)), ns)
        t = (np.concatenate([np.arange(k) for k in ns]) + 0.5) / ns[idx]
        dist, _ = cKDTree(A[idx] + t[:, None] * (B - A)[idx]).query(V[quads].mean(1))
        out["bent_quads_per_1k"] = float(1000.0 * np.sum((bend > bend_deg) & (dist < h / 2)) / len(quads))
    keep = angles >= sharp_degrees - 1.0
    if not np.any(keep) or len(E_out) == 0 or sharp_degrees >= 180.0 or sharp_degrees <= 0.0:
        return out
    P, T, idx, w, covered = crease_samples(Vin, sharp[keep], V, E_out, h)
    out["coverage_theta_all"] = float(w[covered].sum() / w.sum())
    ok = ~narrow_strip_samples(P, T, idx, h)
    out["resolvable_share"] = float(w[ok].sum() / w.sum())
    if np.any(ok):
        out["coverage_theta"] = float(w[ok & covered].sum() / w[ok].sum())
    return out


def input_side(input_path, cache_dir=None):
    """Everything compute() derives from the input alone (on large inputs most of its time).

    With cache_dir it is stored per input content and version of this file, as arrays in an .npz, so
    a cached result is bit-identical to a fresh one; any edit of metrics.py starts a new cache.
    """
    path = None
    if cache_dir:
        path = os.path.join(cache_dir, f"{sha256_file(input_path)[:16]}-{sha256_file(__file__)[:12]}.npz")
        if os.path.exists(path):
            with np.load(path) as z:
                return {k: z[k] for k in z.files}
    Vin, fin = read_obj(input_path)
    Fin = triangulate(fin)
    used_in = np.unique(Fin)
    ein = np.unique(np.sort(np.concatenate([Fin[:, [0, 1]], Fin[:, [1, 2]], Fin[:, [2, 0]]]), axis=1), axis=0)
    ein_all = np.sort(np.concatenate([Fin[:, [0, 1]], Fin[:, [1, 2]], Fin[:, [2, 0]]]), axis=1)
    ein_u, ein_c = np.unique(ein_all, axis=0, return_counts=True)
    # Islands (vertex-connected parts) with their own Euler characteristic, boundary loops and area.
    lab = comps(len(Vin), ein)
    _, island_of_used = np.unique(lab[used_in], return_inverse=True)
    vlab = np.zeros(len(Vin), np.int64)
    vlab[used_in] = island_of_used.ravel()
    nisl = int(vlab[used_in].max()) + 1 if len(used_in) else 0
    island_euler, island_loops = island_topology(len(Vin), Fin[:, 0], ein, ein_u[ein_c == 1], used_in, vlab, nisl)
    tri_area = 0.5 * np.linalg.norm(np.cross(Vin[Fin[:, 1]] - Vin[Fin[:, 0]], Vin[Fin[:, 2]] - Vin[Fin[:, 0]]), axis=1)
    Fin_oriented, reoriented, nonorientable = orient_like_core(Vin, Fin)
    # Islands that are not a 2-manifold in the input (an edge of 3+ triangles, a pinched vertex): their
    # topology has no right answer to compare against (sword blade with a glued-on fin).
    bad_v = nonmanifold_vertex_mask(Fin, len(Vin))
    bad_v[ein_u[ein_c > 2].ravel()] = True
    island_nm = np.zeros(nisl, bool)
    island_nm[vlab[bad_v]] = True
    sharp = sharp_edges(Vin, Fin)
    d = {"Vin": Vin, "Fin": Fin, "used_in": used_in,
         "diag": np.float64(np.linalg.norm(Vin[used_in].max(0) - Vin[used_in].min(0))),
         "euler_input": np.int64(len(used_in) - len(ein) + len(Fin)),
         "components_input": np.int64(len(np.unique(comps(len(Vin), ein)[used_in]))),
         "boundary_loops_input": np.int64(loops(len(Vin), ein_u[ein_c == 1])),
         "sharp_edges": sharp, "sharp_angles": sharp_edge_angles(Vin, Fin, sharp),
         "marked_edges": read_obj_lines(input_path),
         "tri_island": vlab[Fin[:, 0]], "island_euler": island_euler, "island_loops": island_loops,
         "island_non_manifold": island_nm, "island_area": np.bincount(vlab[Fin[:, 0]], weights=tri_area, minlength=nisl),
         "Fin_oriented": Fin_oriented, "input_reoriented": np.int64(reoriented),
         "input_nonorientable_pieces": np.int64(nonorientable)}
    if path:
        os.makedirs(cache_dir, exist_ok=True)
        tmp = f"{path}.{os.getpid()}.tmp"
        with open(tmp, "wb") as fp:
            np.savez(fp, **d)
        os.replace(tmp, path)  # atomic: parallel runs on the same input write identical files
    return d


def compute(input_path, output_path, target_quads=None, sym_axis=None, sym_center=0.0, samples=200000, seed=0,
            cache_dir=None, sharp_degrees=90.0):
    rng = np.random.default_rng(seed)
    inp = input_side(input_path, cache_dir)
    Vin, Fin, used_in = inp["Vin"], inp["Fin"], inp["used_in"]
    diag = float(inp["diag"])
    V, faces = read_obj(output_path)
    out = {"input": input_path, "output": output_path, "diag": diag}
    if not faces:
        out["ok"] = False
        return out
    out["ok"] = True

    sizes = np.array([len(f) for f in faces])
    nq = int((sizes == 4).sum())
    out["faces"] = {"total": int(len(faces)), "quads": nq, "triangles": int((sizes == 3).sum()),
                    "pentagons": int((sizes == 5).sum()), "other": int((sizes > 5).sum()),
                    "quad_fraction": nq / len(faces), "vertices": int(len(V))}
    if target_quads:
        out["faces"]["target_quads"] = int(target_quads)
        out["faces"]["quads_over_target"] = nq / float(target_quads)

    # Edges and orientation.
    he = np.array([(f[i], f[(i + 1) % len(f)]) for f in faces for i in range(len(f))], dtype=np.int64)
    lo, hi = he.min(1), he.max(1)
    key = lo * len(V) + hi
    uniq, inv, counts = np.unique(key, return_inverse=True, return_counts=True)
    fwd = (he[:, 0] < he[:, 1]).astype(np.int64)
    fwd_count = np.bincount(inv, weights=fwd, minlength=len(uniq))
    manifold = counts == 2
    inconsistent = int(np.sum(manifold & (fwd_count != 1)))
    E = np.stack([uniq // len(V), uniq % len(V)], 1)
    bnd = E[counts == 1]
    used = np.unique(he[:, 0])
    out["topology"] = {"edges": int(len(uniq)), "boundary_edges": int((counts == 1).sum()),
                       "non_manifold_edges": int((counts > 2).sum()), "inconsistently_oriented_edges": inconsistent,
                       "unused_vertices": int(len(V) - len(used))}
    # Components, Euler characteristic and boundary loops (output vs input).
    lab = comps(len(V), E)
    out["topology"]["components"] = int(len(np.unique(lab[used])))
    out["topology"]["euler_output"] = int(len(used) - len(uniq) + len(faces))
    out["topology"]["euler_input"] = int(inp["euler_input"])
    out["topology"]["components_input"] = int(inp["components_input"])
    t = out["topology"]
    t["boundary_loops"] = loops(len(V), bnd)
    t["boundary_loops_input"] = int(inp["boundary_loops_input"])
    # Same surface type as the input: Euler characteristic, components and boundary loops match and
    # no edge is non-manifold. Fixes or breaks of handles and holes ("genus") show up here.
    t["same_as_input"] = (t["euler_output"] == t["euler_input"] and t["components"] == t["components_input"]
                          and t["boundary_loops"] == t["boundary_loops_input"] and t["non_manifold_edges"] == 0)

    # Valence.
    val = np.bincount(E.ravel(), minlength=len(V))
    is_b = np.zeros(len(V), bool)
    is_b[bnd.ravel()] = True
    interior = used[~is_b[used]]
    boundary = used[is_b[used]]
    hist = lambda x: {str(k): int(v) for k, v in zip(*np.unique(np.minimum(x, 9), return_counts=True))}
    irregular = int(np.sum(val[interior] != 4))
    out["valence"] = {"interior_hist": hist(val[interior]), "boundary_hist": hist(val[boundary]),
                      "interior_vertices": int(len(interior)), "irregular_interior": irregular,
                      "irregular_per_1k_quads": 1000.0 * irregular / max(nq, 1),
                      "valence3": int(np.sum(val[interior] == 3)), "valence5": int(np.sum(val[interior] == 5)),
                      "valence6plus": int(np.sum(val[interior] >= 6)),
                      "irregular_boundary": int(np.sum(val[boundary] != 3))}

    # Per-face geometry.
    normals = np.array([newell(V, f) for f in faces])
    area = 0.5 * np.linalg.norm(normals, axis=1)
    edge_len = np.linalg.norm(V[he[:, 1]] - V[he[:, 0]], axis=1)
    mean_edge = float(edge_len.mean())
    starts = np.concatenate([[0], np.cumsum(sizes)[:-1]])
    min_edge = np.minimum.reduceat(edge_len, starts)
    repeated = np.array([len(set(f)) != len(f) for f in faces])
    degenerate = repeated | (area < 1e-10 * diag * diag) | (min_edge < 1e-7 * diag)
    out["degenerate"] = {"faces": int(degenerate.sum()), "repeated_vertex": int(repeated.sum())}
    out["edge_length"] = {"mean_rel_diag": mean_edge / diag, "cv": float(edge_len.std() / mean_edge)}

    quads = np.array([f for f in faces if len(f) == 4], dtype=np.int64).reshape(-1, 4)
    if len(quads):
        P = V[quads]
        n = np.array([newell(V, q) for q in quads])
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-300)
        nxt = np.roll(P, -1, axis=1) - P
        prv = np.roll(P, 1, axis=1) - P
        ln, lp = np.linalg.norm(nxt, axis=2), np.linalg.norm(prv, axis=2)
        cosang = np.clip(np.sum(nxt * prv, axis=2) / np.maximum(ln * lp, 1e-300), -1, 1)
        ang = np.degrees(np.arccos(cosang))
        dev = np.abs(ang - 90.0)
        sj = np.sum(np.cross(nxt, prv) * n[:, None, :], axis=2) / np.maximum(ln * lp, 1e-300)
        sjmin = sj.min(1)
        aspect = ln.max(1) / np.maximum(ln.min(1), 1e-300)
        skew = dev.max(1)
        out["quads"] = {**stats(dev.ravel(), "angle_dev_"),
                        "sj_p1": float(np.percentile(sjmin, 1)), "sj_p5": float(np.percentile(sjmin, 5)),
                        "skew_p95": float(np.percentile(skew, 95)),
                        "angle_dev_gt30": float(np.mean(dev.max(1) > 30)),
                        "angle_dev_gt45": float(np.mean(dev.max(1) > 45)),
                        "min_angle": float(ang.min()), "max_angle": float(ang.max()),
                        **stats(sjmin, "sj_"),
                        "sj_lt_0.5": float(np.mean(sjmin < 0.5)), "sj_lt_0.2": float(np.mean(sjmin < 0.2)),
                        "sj_le_0": int(np.sum(sjmin <= 0)), **stats(aspect, "aspect_"),
                        "angle_hist_10deg": np.histogram(ang.ravel(), bins=18, range=(0, 180))[0].tolist(),
                        "sj_hist": np.histogram(sjmin, bins=10, range=(-1, 1))[0].tolist()}

    out["creases"] = crease_coverage(Vin, inp["sharp_edges"], V, E, mean_edge)
    out["creases"].update(crease_report(Vin, inp["sharp_edges"], inp["sharp_angles"], V, faces, E, mean_edge,
                                        sharp_degrees))
    if len(inp["marked_edges"]):
        # Marked edges (TODO 5.4): followed by an output edge (A1 on the marked set), and how far
        # the output surface lies from them (rounding), median and p90 in output mean edges.
        marked = inp["marked_edges"]
        A, B = Vin[marked[:, 0]], Vin[marked[:, 1]]
        L = np.linalg.norm(B - A, axis=1)
        ns = np.maximum(1, np.ceil(L / (mean_edge / 4)).astype(int))
        idx = np.repeat(np.arange(len(marked)), ns)
        along = (np.concatenate([np.arange(k) for k in ns]) + 0.5) / ns[idx]
        d = point_to_mesh(A[idx] + along[:, None] * (B - A)[idx], V, triangulate(faces))[0] / mean_edge
        out["marked"] = {**crease_coverage(Vin, marked, V, E, mean_edge),
                         "bevel_median": float(np.median(d)), "bevel_p90": float(np.percentile(d, 90))}

    # Flips against the input surface and distances. The input normals are those of the input as the
    # core sees it, oriented like its orientInput (TODO 1.4): an export with a turned patch would
    # otherwise count every output face over that patch as flipped (sword blade 241 -> 1861 after #51).
    Ftri = triangulate(faces)
    tri_face = np.repeat(np.arange(len(faces)), sizes - 2)
    cent = np.array([V[f].mean(0) for f in faces])
    _, cf = point_to_mesh(cent, Vin, Fin)
    Fo = inp["Fin_oriented"]

    def flips_against(Fref):
        nin = np.cross(Vin[Fref[cf, 1]] - Vin[Fref[cf, 0]], Vin[Fref[cf, 2]] - Vin[Fref[cf, 0]])
        dots = np.sum(nin * normals, axis=1)
        return nin, dots, int(np.sum(dots < 0))

    nin, dots, neg = flips_against(Fo)
    raw_neg = neg if Fo is Fin or int(inp["input_reoriented"]) == 0 else flips_against(Fin)[2]
    reversed_out = neg > len(faces) - neg
    out["flipped"] = {"faces_vs_input": min(neg, len(faces) - neg), "output_reversed": bool(reversed_out),
                      "faces_vs_raw_input": min(raw_neg, len(faces) - raw_neg),
                      "input_reoriented_triangles": int(inp["input_reoriented"]),
                      "input_nonorientable_pieces": int(inp["input_nonorientable_pieces"])}
    # Flips over folded input (TARGETS V3, 2026-10-23): a flipped face within 1.5 output edges of a folded
    # input triangle (input_folds on the oriented input, in a cluster with a turned-over edge) follows the
    # sculptor's fold, not ours; on the real sculpts 96-100 % of the defects sat there. Counted apart so
    # V3 gates only the rest. Clusters are cut at the 1.5-edge balls, so a turned edge outside them
    # is missed (counted off-fold, the safe side).
    flip_idx = np.flatnonzero(dots >= 0 if reversed_out else dots < 0)  # sums to faces_vs_input
    on_fold = np.zeros(len(flip_idx), bool)
    if len(flip_idx):
        Cin = Vin[Fo].mean(1)
        tin = cKDTree(Cin)
        balls = [np.union1d(b, [c]).astype(np.int64)
                 for b, c in zip(tin.query_ball_point(cent[flip_idx], 1.5 * mean_edge), cf[flip_idx])]
        adj = face_adjacency(Fo)
        adj = (adj + adj.T).tocsr()
        folds = input_folds(Vin, Fo, 0.01 * diag, faces=np.unique(np.concatenate(balls)), tree=tin, adj=adj)
        folds = turned_over(Vin, Fo, folds, adj)
        on_fold = np.array([folds[b].any() for b in balls])
    out["flipped"]["faces_over_input_folds"] = int(on_fold.sum())
    out["flipped"]["faces_off_input_folds"] = int(len(flip_idx) - on_fold.sum())
    # Normal deviation (TARGETS D3): angle between each output face and the input face closest to its
    # centroid, after the global orientation fix above, so flipped faces count as > 90 degrees.
    cos_in = (-dots if reversed_out else dots) / np.maximum(np.linalg.norm(nin, axis=1) * np.linalg.norm(normals, axis=1), 1e-300)
    out["normal_deviation_deg"] = stats(np.degrees(np.arccos(np.clip(cos_in, -1, 1))))
    Pout = np.concatenate([sample_surface(V, Ftri, samples, rng), V[used]])
    d_out, _ = point_to_mesh(Pout, Vin, Fin)
    Pin_area, in_tri = sample_surface(Vin, Fin, samples, rng, with_faces=True)
    Pin = np.concatenate([Pin_area, Vin[rng.choice(used_in, min(len(used_in), samples))]])
    d_in, out_tri = point_to_mesh(Pin, V, Ftri)
    out["distance"] = {"out_to_in_mean": float(d_out.mean() / diag), "out_to_in_max": float(d_out.max() / diag),
                       "in_to_out_mean": float(d_in.mean() / diag), "in_to_out_max": float(d_in.max() / diag),
                       "hausdorff": float(max(d_out.max(), d_in.max()) / diag),
                       "in_to_out_p99": float(np.percentile(d_in, 99) / diag),
                       # Output vertices (the tail of Pout): zero once they are projected onto the input.
                       "vertex_to_in_median": float(np.median(d_out[-len(used):]) / diag)}
    out["validity"] = validity(inp, V, faces, sizes, normals, E, bnd, used, lab, cf, reversed_out, mean_edge,
                               in_tri, d_in[:len(Pin_area)], tri_face[out_tri[:len(Pin_area)]], Ftri)
    # same_as_input per island: no island dropped (a part shrunk to a blob can keep its topology and
    # its own output island), changed its Euler characteristic or boundary loops, split or merged with
    # another, and the output is manifold at edges and vertices.
    vd = out["validity"]
    vd["islands_same_as_input"] = bool(vd["islands_dropped"] == 0 and vd["islands_topology_changed"] == 0
                                       and vd["islands_merged"] == 0
                                       and vd["non_manifold_vertices"] == 0 and t["non_manifold_edges"] == 0)

    if sym_axis:
        a = "xyz".index(sym_axis)

        def reflect(P):
            R = P.copy()
            R[:, a] = 2 * sym_center - R[:, a]
            return R

        d_sym_in, _ = point_to_mesh(reflect(Vin[used_in]), Vin, Fin)
        d_sym_geo, _ = point_to_mesh(reflect(V[used]), V, Ftri)
        dv, _ = cKDTree(V[used]).query(reflect(V[used]))
        dv /= mean_edge
        out["symmetry"] = {"axis": sym_axis, "center": sym_center,
                           "input_reflection_max": float(d_sym_in.max() / diag),
                           "output_reflection_mean": float(d_sym_geo.mean() / diag),
                           "output_reflection_max": float(d_sym_geo.max() / diag),
                           **stats(dv, "topo_vertex_rel_edge_"),
                           "topo_matched_lt_0.1_edge": float(np.mean(dv < 0.1))}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--output")
    ap.add_argument("--target-quads", type=int)
    ap.add_argument("--sym-axis", choices=["x", "y", "z"])
    ap.add_argument("--sym-center", type=float, default=0.0)
    ap.add_argument("--samples", type=int, default=200000)
    ap.add_argument("--sharp-degrees", type=float, default=90.0,
                    help="the run's --sharp-edge setting, for creases.coverage_theta(_all) and bent_quads_per_1k")
    ap.add_argument("--json")
    ap.add_argument("--input-cache", help="folder that caches the input's share of the work (same results)")
    ap.add_argument("--prepare-input", action="store_true", help="only fill --input-cache for --input")
    args = ap.parse_args()
    if args.prepare_input:
        if not args.input_cache:
            ap.error("--prepare-input needs --input-cache")
        input_side(args.input, args.input_cache)
        return
    if not args.output:
        ap.error("--output is required")
    res = compute(args.input, args.output, args.target_quads, args.sym_axis, args.sym_center, args.samples,
                  cache_dir=args.input_cache, sharp_degrees=args.sharp_degrees)
    text = json.dumps(res, indent=1)
    if args.json:
        with open(args.json, "w") as f:
            f.write(text)
    else:
        print(text)


if __name__ == "__main__":
    main()
