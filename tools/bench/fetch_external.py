#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Fetch the external test meshes of a manifest, check their SHA-256, and write each one as a plain
triangle OBJ (positions and faces only) with a topology check.

    python fetch_external.py --manifest external.json --out <meshes>/external
    python fetch_external.py --manifest ~/private/external-testonly.json --out ~/private/external

The repo's manifest (external.json) lists only meshes whose license allows public use (CC0, public
domain, CC BY, CC BY-SA); test-only meshes keep their manifest outside the repo. Each entry pins its
files by commit in the URL and by SHA-256. Sources are OBJ, PLY (also as a `member` of a zip) or glTF 2.0 (.glb, or .gltf with its
.bin): all mesh nodes with their world transforms, triangles only; Draco-compressed primitives need
`pip install DracoPy` (checked with 2.2.0), quantized positions (KHR_mesh_quantization) are read as is. glTF splits vertices at UV and
normal seams, so glTF positions are welded where they are bit-identical (a sculpt exported from
Blender has them joined); OBJ vertices are kept as they are. `groups` keeps only those OBJ groups
(MakeHuman's base mesh carries helper geometry next to the body). Polygons are fan-triangulated and
faces that collapse to a repeated vertex are dropped.

Writes <out>/<id>.obj, <out>/raw/ (downloads), and <out>/check.json with per mesh: triangles,
vertices, islands, closed islands, boundary edges, non-manifold edges, genus of each closed island,
and the bounding-box diagonal.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import zipfile

import numpy as np

COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
DTYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def download(url, dest):
    subprocess.run(["curl", "-sSfL", "--retry", "3", "-o", dest, url], check=True)


def read_obj(path, groups=None):
    verts, faces, keep = [], [], groups is None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("v "):
                verts.append([float(x) for x in line.split()[1:4]])
            elif line.startswith("g ") and groups is not None:
                keep = line.split(maxsplit=1)[1].strip() in groups
            elif line.startswith("f ") and keep:
                idx = [int(tok.split("/")[0]) for tok in line.split()[1:]]
                idx = [i - 1 if i > 0 else len(verts) + i for i in idx]
                faces.extend([idx[0], idx[k], idx[k + 1]] for k in range(1, len(idx) - 1))
    return np.array(verts, dtype=np.float64), np.array(faces, dtype=np.int64).reshape(-1, 3)


PLY_TYPES = {"char": "i1", "int8": "i1", "uchar": "u1", "uint8": "u1", "short": "i2", "int16": "i2",
             "ushort": "u2", "uint16": "u2", "int": "i4", "int32": "i4", "uint": "u4", "uint32": "u4",
             "float": "f4", "float32": "f4", "double": "f8", "float64": "f8"}


def read_ply(data):
    """PLY with a vertex element (x, y, z plus any fixed-size properties) and a face element whose
    only property is the index list; binary faces must all be triangles."""
    end = data.index(b"end_header") + len(b"end_header")
    end = data.index(b"\n", end) + 1
    header = data[:end].decode("ascii").split("\n")
    fmt = header[1].split()[1]
    elements = []
    for line in header:
        tok = line.split()
        if tok[:1] == ["element"]:
            elements.append({"name": tok[1], "count": int(tok[2]), "props": []})
        elif tok[:1] == ["property"]:
            elements[-1]["props"].append(tok[1:])
    if [e["name"] for e in elements] != ["vertex", "face"]:
        raise ValueError("PLY: expected exactly the elements vertex, face")
    vert, face = elements
    if fmt == "ascii":
        lines = data[end:].decode("ascii").split("\n")
        names = [p[-1] for p in vert["props"]]
        v = np.loadtxt(lines[:vert["count"]], dtype=np.float64, ndmin=2)[:, [names.index(c) for c in "xyz"]]
        f = []
        for line in lines[vert["count"]:vert["count"] + face["count"]]:
            idx = [int(x) for x in line.split()]
            f.extend([idx[1], idx[k], idx[k + 1]] for k in range(2, idx[0]))
        return v, np.array(f, dtype=np.int64)
    order = {"binary_little_endian": "<", "binary_big_endian": ">"}[fmt]
    vdt = np.dtype([(p[-1], order + PLY_TYPES[p[0]]) for p in vert["props"]])
    v = np.frombuffer(data, vdt, vert["count"], end)
    v = np.stack([v[c] for c in "xyz"], axis=1).astype(np.float64)
    (_, count_t, index_t, _), = face["props"]
    fdt = np.dtype([("n", order + PLY_TYPES[count_t]), ("i", order + PLY_TYPES[index_t], 3)])
    f = np.frombuffer(data, fdt, face["count"], end + vdt.itemsize * vert["count"])
    if (f["n"] != 3).any():
        raise ValueError("PLY: binary faces must all be triangles")
    return v, f["i"].astype(np.int64)


def read_gltf(path):
    if path.endswith(".glb"):
        with open(path, "rb") as f:
            data = f.read()
        magic, _, _ = struct.unpack_from("<III", data, 0)
        if magic != 0x46546C67:
            raise ValueError(f"{path}: not a glb file")
        off, doc, bins = 12, None, []
        while off < len(data):
            length, kind = struct.unpack_from("<II", data, off)
            chunk = data[off + 8:off + 8 + length]
            if kind == 0x4E4F534A:
                doc = json.loads(chunk)
            else:
                bins.append(chunk)
            off += 8 + length
        buffers = [bins[0] if "uri" not in b else None for b in doc["buffers"]]
    else:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        buffers = []
        for b in doc["buffers"]:
            if b["uri"].startswith("data:") or "/" in b["uri"] or "\\" in b["uri"]:
                raise ValueError(f"{path}: only a sibling .bin buffer is supported")
            with open(os.path.join(os.path.dirname(path), b["uri"]), "rb") as f:
                buffers.append(f.read())

    def accessor(i):
        a = doc["accessors"][i]
        if "sparse" in a:
            raise ValueError(f"{path}: sparse accessors are not supported")
        view = doc["bufferViews"][a["bufferView"]]
        dt = np.dtype(DTYPES[a["componentType"]])
        ncomp = COMPONENTS[a["type"]]
        stride = view.get("byteStride") or dt.itemsize * ncomp
        start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        buf = buffers[view["buffer"]]
        view_array = np.ndarray((a["count"], ncomp), dt, buf, start, (stride, dt.itemsize))
        out = view_array.astype(np.float64 if dt == np.float32 else np.int64)
        if a.get("normalized"):
            out = np.maximum(out / np.iinfo(dt).max, -1.0)
        return out if ncomp > 1 else out.reshape(-1)

    def local(node):
        if "matrix" in node:
            return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
        t = np.eye(4)
        t[:3, 3] = node.get("translation", [0, 0, 0])
        x, y, z, w = node.get("rotation", [0, 0, 0, 1])
        r = np.eye(4)
        r[:3, :3] = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        s = np.diag(list(node.get("scale", [1, 1, 1])) + [1])
        return t @ r @ s

    verts, faces = [], []
    scene = doc["scenes"][doc.get("scene", 0)]
    stack = [(n, np.eye(4)) for n in scene["nodes"]]
    while stack:
        n, parent = stack.pop()
        node = doc["nodes"][n]
        world = parent @ local(node)
        stack.extend((c, world) for c in node.get("children", []))
        if "mesh" not in node:
            continue
        for prim in doc["meshes"][node["mesh"]]["primitives"]:
            if prim.get("mode", 4) != 4:
                continue
            draco = prim.get("extensions", {}).get("KHR_draco_mesh_compression")
            if draco:
                import DracoPy
                view = doc["bufferViews"][draco["bufferView"]]
                start = view.get("byteOffset", 0)
                mesh = DracoPy.decode(buffers[view["buffer"]][start:start + view["byteLength"]])
                p = np.asarray(mesh.points, dtype=np.float32).astype(np.float64).reshape(-1, 3)
                idx = np.asarray(mesh.faces, dtype=np.int64).reshape(-1)
            else:
                p = accessor(prim["attributes"]["POSITION"])
                idx = accessor(prim["indices"]) if "indices" in prim else np.arange(len(p))
            p = p @ world[:3, :3].T + world[:3, 3]
            base = sum(len(v) for v in verts)
            verts.append(p)
            faces.append(idx.reshape(-1, 3) + base)
    v = np.vstack(verts)
    f = np.vstack(faces)
    # Weld bit-identical positions (glTF splits them at UV/normal seams).
    v, inverse = np.unique(v, axis=0, return_inverse=True)
    return v, inverse.reshape(-1)[f]


def check(v, f):
    """Islands (face connectivity through shared vertices), boundary/non-manifold edges, genus."""
    n = len(v)
    parent = np.arange(n)

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for a, b in np.vstack([f[:, [0, 1]], f[:, [1, 2]]]):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
    roots = np.array([find(a) for a in range(n)])
    used = np.zeros(n, bool)
    used[f.ravel()] = True
    edges = np.sort(np.vstack([f[:, [0, 1]], f[:, [1, 2]], f[:, [2, 0]]]), axis=1)
    uniq, counts = np.unique(edges, axis=0, return_counts=True)
    island_of_face = roots[f[:, 0]]
    labels, island_index = np.unique(island_of_face, return_inverse=True)
    vert_island = roots
    islands = []
    edge_island = vert_island[uniq[:, 0]]
    for k, lab in enumerate(labels):
        nf = int((island_index == k).sum())
        nv = int((used & (vert_island == lab)).sum())
        mask = edge_island == lab
        ne = int(mask.sum())
        nb = int((counts[mask] == 1).sum())
        nm = int((counts[mask] > 2).sum())
        chi = nv - ne + nf
        islands.append({"faces": nf, "closed": nb == 0 and nm == 0, "boundary_edges": nb,
                        "non_manifold_edges": nm, "genus": (2 - chi) // 2 if nb == 0 and nm == 0 else None})
    lo, hi = v[used].min(0), v[used].max(0)
    islands.sort(key=lambda i: -i["faces"])
    return {"triangles": int(len(f)), "vertices": int(used.sum()), "islands": len(islands),
            "closed_islands": sum(i["closed"] for i in islands),
            "boundary_edges": int((counts == 1).sum()), "non_manifold_edges": int((counts > 2).sum()),
            "genus_closed": [i["genus"] for i in islands if i["closed"]][:20],
            "largest_islands": [i["faces"] for i in islands[:5]],
            "diagonal": float(np.linalg.norm(hi - lo))}


def write_obj(path, v, f):
    with open(path, "w") as out:
        out.write("".join(f"v {x:.9g} {y:.9g} {z:.9g}\n" for x, y, z in v))
        out.write("".join(f"f {a + 1} {b + 1} {c + 1}\n" for a, b, c in f))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", help="comma-separated ids")
    args = ap.parse_args()
    with open(args.manifest, encoding="utf-8") as f:
        manifest = json.load(f)
    only = set(args.only.split(",")) if args.only else None
    raw = os.path.join(args.out, "raw")
    os.makedirs(raw, exist_ok=True)
    report_path = os.path.join(args.out, "check.json")
    report = {}
    if os.path.exists(report_path):
        with open(report_path, encoding="utf-8") as f:
            report = json.load(f)
    failed = []
    for m in manifest["meshes"]:
        if only and m["id"] not in only:
            continue
        folder = os.path.join(raw, m["id"])
        os.makedirs(folder, exist_ok=True)
        paths = []
        for src in m["files"]:
            dest = os.path.join(folder, os.path.basename(src["url"]))
            if not (os.path.exists(dest) and sha256(dest) == src["sha256"]):
                print(f"fetching {m['id']}: {src['url']}", flush=True)
                download(src["url"], dest)
            got = sha256(dest)
            if got != src["sha256"]:
                print(f"{m['id']}: SHA-256 mismatch for {dest}: {got}", file=sys.stderr)
                failed.append(m["id"])
                break
            paths.append(dest)
        else:
            main_file = paths[0]
            if "member" in m:
                with zipfile.ZipFile(main_file) as z:
                    data = z.read(m["member"])
                if not m["member"].endswith(".ply"):
                    raise ValueError(f"{m['id']}: only .ply members of a zip are supported")
                v, f = read_ply(data)
            elif main_file.endswith(".ply"):
                with open(main_file, "rb") as fh:
                    v, f = read_ply(fh.read())
            elif main_file.endswith((".glb", ".gltf")):
                v, f = read_gltf(main_file)
            else:
                v, f = read_obj(main_file, m.get("groups"))
            f = f[(f[:, 0] != f[:, 1]) & (f[:, 1] != f[:, 2]) & (f[:, 2] != f[:, 0])]
            used, f = np.unique(f, return_inverse=True)
            v, f = v[used], f.reshape(-1, 3)
            write_obj(os.path.join(args.out, m["id"] + ".obj"), v, f)
            report[m["id"]] = check(v, f)
            r = report[m["id"]]
            print(f"{m['id']}: {r['triangles']} tris, {r['islands']} islands ({r['closed_islands']} closed), "
                  f"{r['boundary_edges']} boundary / {r['non_manifold_edges']} non-manifold edges, "
                  f"genus {r['genus_closed']}", flush=True)
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1, sort_keys=True)
    if failed:
        sys.exit(f"failed: {', '.join(failed)}")


if __name__ == "__main__":
    main()
