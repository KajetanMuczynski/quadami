#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Separatrix diagnostic for a quad OBJ: do pole separatrices end at poles, or miss them by 1-2 rows?"""
import sys, json, statistics
def load(path):
    nv, faces = 0, []
    for ln in open(path):
        if ln.startswith("v "): nv += 1
        elif ln.startswith("f "): faces.append([int(t.split("/")[0]) - 1 for t in ln.split()[1:]])
    return nv, faces
def analyse(path, cap=100000):
    nv, faces = load(path)
    face_of = {}
    for fi, f in enumerate(faces):
        for i in range(len(f)): face_of[(f[i], f[(i + 1) % len(f)])] = fi
    nbrs = [[] for _ in range(nv)]
    for (a, b) in face_of: nbrs[a].append(b)
    for (a, b) in list(face_of):
        if (b, a) not in face_of: nbrs[a].append(b); nbrs[b].append(a)
    boundary = set()
    for (a, b) in face_of:
        if (b, a) not in face_of: boundary.add(a); boundary.add(b)
    val = [len(set(n)) for n in nbrs]
    def rotate(a, b):
        fi = face_of.get((b, a))
        if fi is None: return None
        f = faces[fi]; return f[(f.index(a) + 1) % len(f)]
    def fan(v, prev):
        out, w = [prev], prev
        for _ in range(8):
            w = rotate(v, w)
            if w is None: return None
            if w == prev: break
            out.append(w)
        else: return None
        return out
    regular = [val[v] == 4 and v not in boundary and fan(v, nbrs[v][0]) is not None and len(fan(v, nbrs[v][0])) == 4 for v in range(nv)]
    poles = [v for v in range(nv) if not regular[v] and v not in boundary and val[v] > 0]
    pole = set(poles)
    def opp(prev, v):
        F = fan(v, prev)
        return None if F is None or len(F) != 4 else F[2]
    seps = []
    for p in poles:
        for q in set(nbrs[p]):
            prev, v, n, miss = p, q, 1, None
            end = "cap"
            while n < cap:
                if v in pole: end = "pole"; break
                if v in boundary or not regular[v]: end = "boundary"; break
                if v == p: end = "self"; break
                if miss is None:
                    F = fan(v, prev)
                    for s in (F[1], F[3]):
                        if s in pole: miss = (1, n); break
                        if regular[s]:
                            t = opp(v, s)
                            if t is not None and t in pole: miss = (2, n); break
                w = opp(prev, v)
                prev, v, n = v, w, n + 1
            seps.append((end, n, miss))
    nq = len(faces)
    L = nq ** 0.5
    def pole_short(k): return sum(1 for e, n, m in seps if e == "pole" and n <= k * L) / max(len(seps), 1)
    r = {"faces": nq, "poles": len(poles), "seps": len(seps),
         "end_pole": sum(1 for s in seps if s[0] == "pole") / max(len(seps), 1),
         "end_pole_le_0.5L": pole_short(0.5), "median_len_over_L": statistics.median([n for e, n, m in seps]) / L if seps else 0,
         "miss1_before_end": sum(1 for e, n, m in seps if m and m[0] == 1) / max(len(seps), 1),
         "miss2_before_end": sum(1 for e, n, m in seps if m and m[0] == 2) / max(len(seps), 1),
         "miss_le_0.5L_and_long": sum(1 for e, n, m in seps if m and m[1] <= 0.5 * L and n > 2 * m[1]) / max(len(seps), 1)}
    return r
if __name__ == "__main__":
    for p in sys.argv[1:]: print(p, json.dumps({k: round(v, 3) if isinstance(v, float) else v for k, v in analyse(p).items()}))
