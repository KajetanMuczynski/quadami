#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Generate the stress families: inputs that map where the remesher's limits are.

    python make_stress.py --downloads meshes/downloads --out meshes/stress [--only fam1,fam2] [--no-highpoly]

Writes one OBJ per mesh, `corpus.json` (the shape of tools/bench/corpus.json, one set per family, `file`
relative to the meshes folder) and `manifest.json` (per mesh: family, parameters, and `probes`, sample
points on the feature under test, which `stress_report.py` uses to tell whether the feature survived).
Run a family with

    AR_CORPUS=$AR_WORK/meshes/stress/corpus.json ab.py run master --set stress_thin --jobs 4 --timeout 900
    python stress_report.py --runs $AR_WORK/runs/<key> --manifest $AR_WORK/meshes/stress/manifest.json

Everything is seeded; same library versions give the same files. A "body" is a unit icosphere
(20,480 triangles), so h0 = sqrt(area / target) is set mostly by the body and the feature next to it is
measured in quad edges. Families (set name stress_<family>):

  thin       capsule of radius r (0.002-0.128) and length L (0.25, 1) as its own island next to the body:
             where a separate thin part (vine, strap, hair strand) vanishes or changes genus.
  attached   the same capsule unioned into the body (a limb, a horn): where an attached part vanishes.
  ribbon     flat closed ellipsoid 1 x 0.2 x t (t 0.002-0.032) next to the body: thin sheet as an island.
  islands    body plus n (10, 100, 1000) small spheres of radius s (0.005-0.08): crumbs, many islands (E5).
  contact    two intersecting spheres, two spheres welded at one vertex, a sphere inside a sphere, a shell
             duplicated in place, two cubes sharing an edge.
  degenerate slivers (edge splits at 1e-3), a CAD-tessellated cylinder (aspect > 100), graded density with
             edge p99/p1 of about 30, 100 and 300 (E6).
  topology   fin (a triangle hanging off an edge), open sphere with holes, open hemisphere, flat disk,
             triangle soup (unwelded spot), whole mesh flipped, 10 % of faces flipped one by one.
  scale      spot scaled x1e-4 ... x1e4 and offset by 1e3, 1e5, 1e6 (E7).
  highpoly   noisy icospheres of 1.3M and 5.2M triangles (skip with --no-highpoly).
  spikes     body with 8 cones of tip half-angle 2-20 degrees, 0.5 long.
  jitter     spot with every vertex moved by sigma = 0.05-1.0 mean edge lengths (1.0 folds it).
  folds      spot with 5 local twists of 90-720 degrees (brush-like folded input, 2.20).
  coarse     icosphere of 80 and 320 triangles and a 12-triangle cube at 500-8000 quads (E3).
  selfint    sphere with a dent pushed to depth 1.0 (touching) and 2.2 R (through the far wall) (E4).
  sanitize   input defects of TODO 2.7: unwelded seams (exact and with a gap), Edge Split on a CAD part,
             duplicate faces, a double-sided shell, pinched (non-manifold) vertices, T-junctions sealed by
             zero-area triangles, a far loose vertex. Needs synthetic/csg_bracket.obj next to downloads/.
"""
import argparse
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_synthetic import read_obj, write_obj  # noqa: E402


# --- building blocks --------------------------------------------------------------------------------

def icosphere(level):
    """Unit icosphere, vectorised (level 9 = 5.2M triangles in seconds)."""
    t = (1 + 5 ** 0.5) / 2
    V = np.array([[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0], [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t],
                  [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]], float)
    F = np.array([[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2],
                  [10, 7, 6], [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5],
                  [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]], np.int64)
    V /= np.linalg.norm(V, axis=1, keepdims=True)
    for _ in range(level):
        E = np.concatenate([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]])
        E.sort(axis=1)
        uniq, inv = np.unique(E, axis=0, return_inverse=True)
        mid = V[uniq[:, 0]] + V[uniq[:, 1]]
        mid /= np.linalg.norm(mid, axis=1, keepdims=True)
        m = len(V) + inv.reshape(3, -1)
        ab, bc, ca = m[0], m[1], m[2]
        a, b, c = F[:, 0], F[:, 1], F[:, 2]
        F = np.concatenate([np.stack([a, ab, ca], 1), np.stack([b, bc, ab], 1), np.stack([c, ca, bc], 1),
                            np.stack([ab, bc, ca], 1)])
        V = np.concatenate([V, mid])
    return V, F


def body():
    return icosphere(5)


def revolve(profile, segments):
    """Closed surface of revolution about z. profile: (rho, z) from the bottom pole (rho 0) to the top
    pole (rho 0); the poles become single vertices."""
    profile = np.asarray(profile, float)
    inner = profile[1:-1]
    phi = 2 * np.pi * np.arange(segments) / segments
    ring = np.stack([inner[:, 0, None] * np.cos(phi), inner[:, 0, None] * np.sin(phi),
                     np.repeat(inner[:, 1, None], segments, 1)], -1).reshape(-1, 3)
    V = np.concatenate([[[0, 0, profile[0, 1]]], ring, [[0, 0, profile[-1, 1]]]])
    n, top = len(inner), len(V) - 1
    idx = 1 + np.arange(n * segments).reshape(n, segments)
    nxt = np.roll(idx, -1, 1)
    F = [np.stack([np.zeros(segments, np.int64), nxt[0], idx[0]], 1)]
    a, b, c, d = idx[:-1].ravel(), nxt[:-1].ravel(), nxt[1:].ravel(), idx[1:].ravel()
    F += [np.stack([a, b, c], 1), np.stack([a, c, d], 1)]
    F.append(np.stack([idx[-1], nxt[-1], np.full(segments, top)], 1))
    return V, np.concatenate(F).astype(np.int64)


def capsule(r, L, segments=16):
    """Capsule along z from 0 to L + 2r, edge length about 2 pi r / segments."""
    e = 2 * np.pi * r / segments
    cap = [(r * math.sin(a), r - r * math.cos(a)) for a in np.linspace(0, np.pi / 2, max(3, segments // 4 + 1))]
    zs = np.linspace(r, r + L, max(2, int(round(L / e)) + 1))[1:-1]
    top = [(rho, L + 2 * r - z) for rho, z in reversed(cap)]
    return revolve(cap + [(r, z) for z in zs] + top, segments)


def cone(half_angle_deg, length, base_r=None, segments=24, rings=40):
    """Closed cone along z: tip at z = length, base disk at z = 0."""
    br = base_r if base_r is not None else length * math.tan(math.radians(half_angle_deg))
    side = [(br * (1 - s), s * length) for s in np.linspace(0, 1, rings)]
    disk = [(br * s, 0.0) for s in np.linspace(0, 1, 6)[:-1]]
    return revolve(disk + side, segments)


def merge(*parts):
    Vs, Fs, off = [], [], 0
    for V, F in parts:
        Vs.append(V)
        Fs.append(F + off)
        off += len(V)
    return np.concatenate(Vs), np.concatenate(Fs)


def union(*parts):
    from manifold3d import Manifold, Mesh

    m = None
    for V, F in parts:
        p = Manifold(Mesh(vert_properties=np.asarray(V, np.float32), tri_verts=np.asarray(F, np.uint32)))
        m = p if m is None else m + p
    out = m.to_mesh()
    return np.asarray(out.vert_properties[:, :3], float), np.asarray(out.tri_verts, np.int64)


def rotate_to(V, axis):
    """Rotates z onto the unit vector axis."""
    z = np.array([0.0, 0.0, 1.0])
    axis = np.asarray(axis, float) / np.linalg.norm(axis)
    v, c = np.cross(z, axis), float(z @ axis)
    if np.linalg.norm(v) < 1e-12:
        return V if c > 0 else V * np.array([1, -1, -1])
    K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    R = np.eye(3) + K + K @ K / (1 + c)
    return V @ R.T


def surface_samples(V, F, n, rng):
    """n area-weighted random points on a triangle mesh."""
    a = np.linalg.norm(np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]]), axis=1)
    f = rng.choice(len(F), n, p=a / a.sum())
    u, v = rng.random(n), rng.random(n)
    flip = u + v > 1
    u[flip], v[flip] = 1 - u[flip], 1 - v[flip]
    return V[F[f, 0]] + u[:, None] * (V[F[f, 1]] - V[F[f, 0]]) + v[:, None] * (V[F[f, 2]] - V[F[f, 0]])


def mean_edge(V, F):
    return float(np.linalg.norm(V[F[:, 0]] - V[F[:, 1]], axis=1).mean())


def area(V, F):
    return float(0.5 * np.linalg.norm(np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]]), axis=1).sum())


def rodrigues(P, axis, angle):
    axis = axis / np.linalg.norm(axis)
    c, s = np.cos(angle)[:, None], np.sin(angle)[:, None]
    return P * c + np.cross(axis, P) * s + axis * (P @ axis)[:, None] * (1 - c)


# --- families ---------------------------------------------------------------------------------------

RADII = [0.002, 0.004, 0.008, 0.016, 0.032, 0.064, 0.128]


def fam_thin(ctx):
    for L in (0.25, 1.0):
        for r in RADII:
            Vc, Fc = capsule(r, L)
            Vc = Vc + np.array([1.5, 0.0, -(L + 2 * r) / 2])
            yield f"thin_r{r:g}_L{L:g}", merge(body(), (Vc, Fc)), {"r": r, "L": L, "feature_area": area(Vc, Fc)}, \
                surface_samples(Vc, Fc, 200, ctx.rng)


def fam_attached(ctx):
    for r in RADII:
        Vc, Fc = capsule(r, 1.0)
        Vc = rotate_to(Vc + np.array([0, 0, 0.5]), [1, 0.3, 0.2])  # starts inside the body, pokes out ~0.5+1
        V, F = union(body(), (Vc, Fc))
        out = np.linalg.norm(V, axis=1) > 1.0 + 4 * r + 0.02
        probes = surface_samples(Vc, Fc, 600, ctx.rng)
        probes = probes[np.linalg.norm(probes, axis=1) > 1.0 + 4 * r + 0.02][:200]
        yield f"attached_r{r:g}", (V, F), {"r": r, "L": 1.0, "outside_vertices": int(out.sum())}, probes


def fam_ribbon(ctx):
    for t in (0.002, 0.008, 0.032):
        Ve, Fe = icosphere(5)
        Ve = Ve * np.array([0.5, 0.1, t / 2]) + np.array([1.8, 0, 0])
        yield f"ribbon_t{t:g}", merge(body(), (Ve, Fe)), {"t": t, "w": 0.2}, surface_samples(Ve, Fe, 200, ctx.rng)


def fam_islands(ctx):
    Vs, Fs = icosphere(2)
    for n in (10, 100, 1000):
        for s in (0.005, 0.02, 0.08):
            rng = np.random.default_rng(1000 * n + int(s * 1000))
            centres, probes = [], []
            while len(centres) < n:  # non-overlapping, in a shell around the body
                c = rng.normal(size=3)
                c = c / np.linalg.norm(c) * rng.uniform(1.3, 2.5)
                if all(np.linalg.norm(c - q) > 2.5 * s for q in centres):
                    centres.append(c)
            parts = [body()] + [(Vs * s + c, Fs) for c in centres]
            probes = np.array(centres)  # each crumb's centre: kept if output lies within ~s of it
            yield f"islands_n{n}_s{s:g}", merge(*parts), {"n": n, "s": s}, probes


def fam_contact(ctx):
    B = body()
    V2 = B[0] + np.array([1.5, 0, 0])
    yield "contact_overlap", merge(B, (V2, B[1])), {"kind": "two spheres intersecting, depth 0.5"}, None
    # Touching at one welded vertex: icosphere vertex 9 is (t, 0, 1)/|.|; mirror the sphere through it.
    p = B[0][9]
    V3 = 2 * p - B[0]
    V, F = merge(B, (V3, B[1][:, ::-1]))
    yield "contact_vertex", (V, F), {"kind": "two spheres sharing one vertex (non-manifold vertex)"}, None
    yield "contact_nested", merge(B, (B[0] * 0.5, B[1])), {"kind": "sphere inside a sphere (eye in head)"}, \
        B[0][::40] * 0.5
    yield "contact_duplicate", merge(B, B), {"kind": "same shell twice, in place"}, None
    from manifold3d import Manifold
    c1 = Manifold.cube([1, 1, 1]).to_mesh()
    Vc = np.asarray(c1.vert_properties[:, :3], float)
    Fc = np.asarray(c1.tri_verts, np.int64)
    yield "contact_edge", merge((Vc, Fc), (Vc + np.array([1, 1, 0]), Fc)), \
        {"kind": "two cubes sharing an edge (welded by position)"}, None


def graded_sphere(k, level=6):
    """Icosphere with polar angle theta -> pi * (theta / pi) ** k: density grows toward the +z pole."""
    V, F = icosphere(level)
    th = np.arccos(np.clip(V[:, 2], -1, 1))
    ph = np.arctan2(V[:, 1], V[:, 0])
    th2 = np.pi * (th / np.pi) ** k
    return np.stack([np.sin(th2) * np.cos(ph), np.sin(th2) * np.sin(ph), np.cos(th2)], 1), F


def edge_ratio(V, F):
    e = np.linalg.norm(np.concatenate([V[F[:, 0]] - V[F[:, 1]], V[F[:, 1]] - V[F[:, 2]], V[F[:, 2]] - V[F[:, 0]]]),
                       axis=1)
    return float(np.percentile(e, 99) / max(np.percentile(e, 1), 1e-300))


def fam_degenerate(ctx):
    V, F = body()
    # Slivers: split 20 % of triangles at a point 1e-3 along one edge, both faces of the edge.
    rng = np.random.default_rng(7)
    E = {}
    for fi, f in enumerate(F):
        for k in range(3):
            E.setdefault(tuple(sorted((f[k], f[(k + 1) % 3]))), []).append(fi)
    keys = list(E.keys())
    touched = np.zeros(len(F), bool)
    chosen = []
    for ci in rng.permutation(len(keys)):  # edges whose two faces are still unsplit: no T-junctions
        if len(chosen) == len(F) // 10:
            break
        if not touched[E[keys[ci]]].any():
            touched[E[keys[ci]]] = True
            chosen.append(ci)
    Vl, Fl = list(V), [list(f) for f in F]
    for ci in chosen:
        a, b = keys[ci]
        m = len(Vl)
        Vl.append(V[a] + 1e-3 * (V[b] - V[a]))
        for fi in E[(a, b)]:
            f = Fl[fi]
            i = f.index(a)
            c = [x for x in f if x not in (a, b)][0]
            # keep orientation: rotate so that f = (a, ?, ?)
            f2 = f[i:] + f[:i]
            if f2[1] == b:
                Fl[fi] = [a, m, c]
                Fl.append([m, b, c])
            else:
                Fl[fi] = [a, c, m]
                Fl.append([m, c, b])
    Vs, Fs = np.array(Vl), np.array(Fl, np.int64)
    yield "degen_slivers", (Vs, Fs), {"edge_ratio_p99_p1": edge_ratio(Vs, Fs)}, None
    from manifold3d import Manifold
    cyl = Manifold.cylinder(2.0, 0.5, 0.5, 256, True).to_mesh()
    Vc, Fc = np.asarray(cyl.vert_properties[:, :3], float), np.asarray(cyl.tri_verts, np.int64)
    yield "degen_cad_cylinder", (Vc, Fc), {"edge_ratio_p99_p1": edge_ratio(Vc, Fc)}, None
    for k in (1.6, 2.2, 3.0):
        Vg, Fg = graded_sphere(k)
        yield f"degen_graded_k{k:g}", (Vg, Fg), {"k": k, "edge_ratio_p99_p1": edge_ratio(Vg, Fg)}, None


def fam_topology(ctx):
    V, F = body()
    tip = V[F[0]].mean(0) * 1.3
    e = F[0]
    yield "topo_fin", (np.concatenate([V, [tip]]), np.concatenate([F, [[e[0], e[1], len(V)]]])), \
        {"kind": "one triangle hanging off a sphere edge (non-manifold edge)"}, None
    rng = np.random.default_rng(3)
    C = V[F].mean(1)
    keep = np.ones(len(F), bool)
    for c in V[rng.choice(len(V), 6, replace=False)]:
        keep &= np.linalg.norm(C - c, axis=1) > 0.12
    yield "topo_holes", (V, F[keep]), {"kind": "sphere with 6 holes", "removed": int((~keep).sum())}, None
    yield "topo_hemisphere", (V, F[C[:, 2] > 0]), {"kind": "open hemisphere"}, None
    n = 100
    u = np.linspace(-1, 1, n)
    X, Y = np.meshgrid(u, u, indexing="ij")
    Vd = np.stack([X.ravel(), Y.ravel(), np.zeros(n * n)], 1)
    idx = np.arange(n * n).reshape(n, n)
    a, b, c, d = idx[:-1, :-1].ravel(), idx[1:, :-1].ravel(), idx[1:, 1:].ravel(), idx[:-1, 1:].ravel()
    yield "topo_flat_square", (Vd, np.concatenate([np.stack([a, b, c], 1), np.stack([a, c, d], 1)])), \
        {"kind": "flat open square"}, None
    Vs, Fs = read_obj(os.path.join(ctx.downloads, "spot.obj"))
    yield "topo_soup", (Vs[Fs].reshape(-1, 3) + 0.0, np.arange(3 * len(Fs)).reshape(-1, 3)), \
        {"kind": "spot with every triangle unwelded (written with shared positions, so welded by position)"}, None
    # A soup that position-welding cannot repair: each triangle shrunk 1e-4 towards its centroid.
    T = Vs[Fs]
    T = T + (T.mean(1, keepdims=True) - T) * 1e-4
    yield "topo_soup_gapped", (T.reshape(-1, 3), np.arange(3 * len(Fs)).reshape(-1, 3)), \
        {"kind": "spot, triangles unwelded with 1e-4 gaps (export without merge)"}, None
    yield "topo_flipped_all", (V, F[:, ::-1]), {"kind": "whole sphere inside out"}, None
    Ff = F.copy()
    sel = rng.random(len(F)) < 0.1
    Ff[sel] = Ff[sel][:, ::-1]
    yield "topo_flipped_10pct", (V, Ff), {"kind": "10 % of triangles flipped one by one"}, None


def fam_scale(ctx):
    V, F = read_obj(os.path.join(ctx.downloads, "spot.obj"))
    for s in (1e-4, 1e-3, 1e-2, 1e2, 1e3, 1e4):
        yield f"scale_x{s:g}", (V * s, F), {"scale": s}, None
    for o in (1e3, 1e5, 1e6):
        yield f"offset_{o:g}", (V + o, F), {"offset": o}, None


def fam_highpoly(ctx):
    rng = np.random.default_rng(11)
    for level in (8, 9):
        V, F = icosphere(level)
        g = rng.normal(size=(64, 3))
        bump = sum(0.02 * np.sin(V @ gi * 6 + j) for j, gi in enumerate(g)) / 8
        yield f"highpoly_{len(F) // 1000}k", (V * (1 + bump)[:, None], F), {"triangles": len(F)}, None


def fam_spikes(ctx):
    dirs = icosphere(0)[0][[0, 2, 4, 6, 8, 9, 10, 11]]
    for ang in (2, 5, 10, 20):
        parts = [body()]
        probes = []
        for d in dirs:
            Vc, Fc = cone(ang, 0.6, base_r=max(0.6 * math.tan(math.radians(ang)), 0.03))
            Vc = rotate_to(Vc + np.array([0, 0, 0.9]), d)
            parts.append((Vc, Fc))
            probes.append(d * 1.45)  # 0.05 below the tip
        yield f"spikes_a{ang}", union(*parts), {"half_angle": ang}, np.array(probes)


def fam_jitter(ctx):
    V, F = read_obj(os.path.join(ctx.downloads, "spot.obj"))
    e = mean_edge(V, F)
    for s in (0.05, 0.2, 0.5, 1.0):
        rng = np.random.default_rng(int(s * 100))
        yield f"jitter_s{s:g}", (V + rng.normal(size=V.shape) * s * e, F), {"sigma_edges": s}, None


def fam_folds(ctx):
    V, F = read_obj(os.path.join(ctx.downloads, "spot.obj"))
    from make_synthetic import vertex_normals
    N = vertex_normals(V, F)
    diag = float(np.linalg.norm(V.max(0) - V.min(0)))
    for deg in (90, 180, 360, 720):
        rng = np.random.default_rng(deg)
        W = V.copy()
        for c in rng.choice(len(V), 5, replace=False):
            R = 0.06 * diag
            d = np.linalg.norm(W - W[c], axis=1)
            sel = d < R
            w = (1 - d[sel] / R) ** 2
            W[sel] = W[c] + rodrigues(W[sel] - W[c], N[c], np.radians(deg) * w)
        yield f"folds_twist{deg}", (W, F), {"twist_deg": deg, "spots": 5}, None


def fam_coarse(ctx):
    for level in (1, 2):
        V, F = icosphere(level)
        yield f"coarse_ico{len(F)}", (V, F), {"triangles": len(F)}, None
    from manifold3d import Manifold
    c = Manifold.cube([1, 1, 1], True).to_mesh()
    yield "coarse_cube12", (np.asarray(c.vert_properties[:, :3], float), np.asarray(c.tri_verts, np.int64)), \
        {"triangles": 12}, None


def fam_selfint(ctx):
    for depth in (1.0, 2.2):
        V, F = body()
        th = np.arccos(np.clip(V[:, 2], -1, 1))
        w = np.clip(1 - th / 0.6, 0, None) ** 2
        V = V.copy()
        V[:, 2] -= depth * w
        yield f"selfint_dent{depth:g}", (V, F), {"depth": depth}, None


def split_vertices_along(V, F, cut):
    """Unweld the edges where cut[f] is True on one face and False on the other: each vertex gets one copy
    per side, at the same position (what Edge Split or an export without merge leaves)."""
    F = F.copy()
    copy = {}
    for fi in np.nonzero(cut)[0]:
        for k in range(3):
            v = F[fi, k]
            if v not in copy:
                copy[v] = len(V) + len(copy)
            F[fi, k] = copy[v]
    used_uncut = np.zeros(len(V), bool)
    used_uncut[F[~cut].ravel()] = True
    order = sorted(copy, key=copy.get)
    V2 = np.concatenate([V, V[order]])
    # Vertices that only cut faces use become unused; drop them so the mesh has no loose vertices.
    keep = np.ones(len(V2), bool)
    keep[:len(V)] = used_uncut
    remap = np.cumsum(keep) - 1
    return V2[keep], remap[F]


def fam_sanitize(ctx):
    """Input defects TODO 2.7 is about, each on spot (genus 0, 23k triangles) unless named."""
    V, F = read_obj(os.path.join(ctx.downloads, "spot.obj"))
    rng = np.random.default_rng(27)
    C = V[F].mean(1)
    # Unwelded along a closed seam (a plane cut): two open pieces whose boundaries coincide.
    Vs, Fs = split_vertices_along(V, F, C[:, 0] > 0.05)
    yield "san_seam", (Vs, Fs), {"kind": "spot unwelded along one plane cut (two coincident boundaries)"}, None
    # Unwelded along a seam with a gap of 1e-5 of the diagonal, as a float32 round trip or a sloppy merge leaves.
    diag = float(np.linalg.norm(V.max(0) - V.min(0)))
    Vg = Vs.copy()
    Vg[len(V):] += rng.normal(size=(len(Vs) - len(V), 3)) * 1e-5 * diag
    yield "san_seam_gap", (Vg, Fs), {"kind": "spot unwelded along one plane cut, 1e-5 diagonal gap"}, None
    # Edge Split by angle on a CAD part: every flat region its own island.
    Vb, Fb = read_obj(os.path.join(os.path.dirname(ctx.downloads.rstrip("/")), "synthetic", "csg_bracket.obj"))
    Vb, Fb = edge_split(Vb, Fb, 30.0)
    yield "san_edgesplit_bracket", (Vb, Fb), {"kind": "csg_bracket after Edge Split at 30 degrees"}, None
    # Duplicate faces sharing the vertices: same winding, opposite winding, and the whole shell twice.
    sel = rng.random(len(F)) < 0.05
    yield "san_dupfaces", (V, np.concatenate([F, F[sel]])), {"kind": "5 % of faces twice, same winding"}, None
    yield "san_dupfaces_flipped", (V, np.concatenate([F, F[sel][:, ::-1]])), \
        {"kind": "5 % of faces twice, opposite winding"}, None
    yield "san_double_sided", (V, np.concatenate([F, F[:, ::-1]])), \
        {"kind": "every face twice, opposite winding (zero-thickness Solidify, double-sided export)"}, None
    # Non-manifold vertices inside one surface: 3 pairs of far-apart vertices welded (pinches).
    Vp, Fp = V.copy(), F.copy()
    pairs = []
    for _ in range(3):
        a = int(rng.integers(len(V)))
        b = int(np.argmax(np.linalg.norm(V - V[a], axis=1) * (np.abs(V[:, 0] - V[a, 0]) < 0.3)))
        pairs.append((a, b))
        Vp[a] = Vp[b] = 0.5 * (V[a] + V[b])
    yield "san_pinch_moved", (Vp, Fp), {"kind": "3 pairs of vertices moved to one point, not welded"}, None
    for a, b in pairs:
        Fp[Fp == b] = a
    yield "san_pinch", (Vp, Fp), {"kind": "3 pairs of far-apart vertices welded (non-manifold vertices)"}, None
    # T-junctions sealed by zero-area triangles (exact booleans, CAD stitching).
    yield "san_tcaps", sealed_t_junctions(V, F, 0.02, rng), \
        {"kind": "2 % of edges split on one side, sealed by a zero-area triangle"}, None
    # Loose vertices far away inflate the bounding box.
    for far in (1e3, 1e5):
        yield f"san_loose_vertex_{far:g}", (np.concatenate([V, [[far, 0.0, 0.0]]]), F), \
            {"kind": f"one loose vertex at {far:g}"}, None


def edge_split(V, F, degrees):
    """Blender's Edge Split modifier by angle: unweld every edge whose faces meet at more than `degrees`."""
    N = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-300)
    E = {}
    for fi, f in enumerate(F):
        for k in range(3):
            E.setdefault(tuple(sorted((int(f[k]), int(f[(k + 1) % 3])))), []).append(fi)
    # Faces joined across smooth edges stay together; each corner is copied per smooth fan around its vertex.
    parent = {}

    def root(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    cos = math.cos(math.radians(degrees))
    for (a, b), fs in E.items():
        if len(fs) == 2 and float(N[fs[0]] @ N[fs[1]]) >= cos:
            for v in (a, b):
                ra, rb = root((fs[0], v)), root((fs[1], v))
                if ra != rb:
                    parent[ra] = rb
    newV, index, F2 = [], {}, F.copy()
    for fi, f in enumerate(F):
        for k in range(3):
            r = root((fi, int(f[k])))
            if r not in index:
                index[r] = len(newV)
                newV.append(V[f[k]])
            F2[fi, k] = index[r]
    return np.array(newV), F2


def sealed_t_junctions(V, F, share, rng):
    """Split a share of edges at their midpoint on one side only and seal the slit with a zero-area triangle."""
    E = {}
    for fi, f in enumerate(F):
        for k in range(3):
            E.setdefault((int(f[k]), int(f[(k + 1) % 3])), fi)
    Vl, Fl = list(V), [list(f) for f in F]
    touched = np.zeros(len(F), bool)
    for (a, b), fi in sorted(E.items()):
        if rng.random() >= share or (b, a) not in E or touched[fi] or touched[E[(b, a)]]:
            continue
        touched[fi] = touched[E[(b, a)]] = True
        f = Fl[fi]
        c = [x for x in f if x not in (a, b)][0]
        m = len(Vl)
        Vl.append(0.5 * (V[a] + V[b]))
        Fl[fi] = [a, m, c]       # f walks a -> b -> c
        Fl.append([m, b, c])
        Fl.append([a, b, m])     # zero area: walks m -> a and b -> m against the two halves, a -> b against b -> a
    return np.array(Vl), np.array(Fl, np.int64)


FAMILIES = {"thin":(fam_thin, [500, 2000, 8000, 32000]), "attached": (fam_attached, [500, 2000, 8000, 32000]),
            "ribbon": (fam_ribbon, [500, 2000, 8000, 32000]), "islands": (fam_islands, [2000, 8000]),
            "contact": (fam_contact, [2000, 8000]), "degenerate": (fam_degenerate, [2000, 8000]),
            "topology": (fam_topology, [2000, 8000]), "scale": (fam_scale, [2000]),
            "highpoly": (fam_highpoly, [5000, 50000]), "spikes": (fam_spikes, [2000, 8000]),
            "jitter": (fam_jitter, [2000, 10000]), "folds": (fam_folds, [2000, 10000]),
            "coarse": (fam_coarse, [500, 2000, 8000]), "selfint": (fam_selfint, [2000, 8000]),
            "sanitize": (fam_sanitize, [2000, 8000])}


class Ctx:
    def __init__(self, downloads):
        self.downloads = downloads
        self.rng = np.random.default_rng(2026)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--downloads", required=True, help="folder with spot.obj from fetch_meshes.sh")
    ap.add_argument("--out", required=True, help="meshes/stress inside the ab.py work folder")
    ap.add_argument("--only", help="comma list of families")
    ap.add_argument("--no-highpoly", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    folder = os.path.basename(os.path.normpath(a.out))
    names = a.only.split(",") if a.only else [f for f in FAMILIES if not (a.no_highpoly and f == "highpoly")]
    corpus = {"_comment": "Generated by tools/bench/make_stress.py; use with AR_CORPUS.", "sets": {}, "meshes": []}
    manifest = {}
    for fam in names:
        gen, targets = FAMILIES[fam]
        ctx = Ctx(a.downloads)
        corpus["sets"]["stress_" + fam] = {"targets": targets, "resolutions": ["quality"]}
        for mid, (V, F), params, probes in gen(ctx):
            path = write_obj(os.path.join(a.out, mid + ".obj"), V, F)
            corpus["meshes"].append({"id": mid, "file": f"{folder}/{mid}.obj", "set": "stress_" + fam,
                                     "category": "stress " + fam})
            manifest[mid] = {"family": fam, "params": params, "triangles": int(len(F)), "area": area(V, F),
                             "probes": None if probes is None else np.round(np.asarray(probes), 9).tolist()}
            print(f"{mid:28s} {len(F):9d} triangles  {os.path.getsize(path) / 1e6:7.1f} MB")
    with open(os.path.join(a.out, "corpus.json"), "w") as f:
        json.dump(corpus, f, indent=1)
    with open(os.path.join(a.out, "manifest.json"), "w") as f:
        json.dump(manifest, f)


if __name__ == "__main__":
    main()
