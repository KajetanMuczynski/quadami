#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Write the "marked" A/B set: Blender meshes with edges marked Sharp, as the add-on hands them to
the CLI (evaluated mesh, loop triangles, float32 positions, the marks as `l a b` lines; TODO 5.4).
Kajetan's report (2026-10-27): marked Sharp edges on a cylinder are ignored.

    python make_marked.py --out meshes/marked                    # pip install bpy (Python 3.13)
    blender -b --factory-startup -P make_marked.py -- --out meshes/marked

What each mesh marks: "angle" = every edge whose faces meet at 30 degrees or more (Edge > Select
Sharp Edges, then Mark Sharp), a plane normal = the edges on that plane through the origin only.
"""
import argparse
import math
import os
import sys

import bpy
import bmesh  # after bpy: the pip module provides it only then
import numpy as np

# name -> (operator, keyword arguments, marks, extra step)
MESHES = {
    "marked_cube": ("primitive_cube_add", {}, "angle", None),
    "marked_cylinder": ("primitive_cylinder_add", {}, "angle", None),
    "marked_cylinder_v64": ("primitive_cylinder_add", {"vertices": 64}, "angle", None),
    "marked_cone": ("primitive_cone_add", {}, "angle", None),
    "marked_cone_frustum": ("primitive_cone_add", {"radius2": 0.5}, "angle", None),
    # Marks on a smooth surface: no dihedral test can find them.
    "marked_uvsphere_equator": ("primitive_uv_sphere_add", {}, (0, 0, 1), None),
    # The same on a circle across the sphere's rings: the cross field does not follow it unless told.
    "marked_uvsphere_tilted": ("primitive_uv_sphere_add", {}, (0.5, 0, 1), "bisect"),
    # A flat design line across the cube's sides; the cube's own edges are left to the 90 deg test.
    "marked_cube_loop": ("primitive_cube_add", {}, (0, 0, 1), "bisect"),
    # A hexagon running diagonally across six sides.
    "marked_cube_diagonal": ("primitive_cube_add", {}, (1, 1, 1), "bisect"),
    # 45 deg chamfers: creases below the default Sharp Edge of 90 deg.
    "marked_cube_bevel": ("primitive_cube_add", {}, "angle", "bevel"),
    # A hole through a box: rims meeting the sides at right angles, curved.
    "marked_cube_hole": ("primitive_cube_add", {}, "angle", "hole"),
    # Many short marked lines that meet and cross.
    "marked_suzanne": ("primitive_monkey_add", {}, "angle", None),
}

ANGLE_DEGREES = 30.0


def apply_step(obj, step, marks):
    if step == "bisect":
        mesh = obj.data
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, 0, 0),
                               plane_no=marks)
        bm.to_mesh(mesh)
        bm.free()
    elif step == "bevel":
        modifier = obj.modifiers.new("Bevel", "BEVEL")
        modifier.width = 0.15
        modifier.segments = 1
    elif step == "hole":
        bpy.ops.mesh.primitive_cylinder_add(radius=0.4, depth=3.0)
        cutter = bpy.context.active_object
        cutter.hide_set(True)
        modifier = obj.modifiers.new("Hole", "BOOLEAN")
        modifier.operation = "DIFFERENCE"
        modifier.object = cutter
        bpy.context.view_layer.objects.active = obj
        return cutter
    return None


def marked_pairs(mesh, marks):
    edges = np.empty(len(mesh.edges) * 2, dtype=np.int32)
    mesh.edges.foreach_get("vertices", edges)
    edges = edges.reshape(-1, 2)
    pos = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
    mesh.vertices.foreach_get("co", pos)
    pos = pos.reshape(-1, 3)
    if marks != "angle":
        normal = np.asarray(marks, dtype=np.float64) / np.linalg.norm(marks)
        return edges[np.all(np.abs(pos[edges] @ normal) < 1e-5, axis=1)]
    normals = np.empty(len(mesh.polygons) * 3, dtype=np.float32)
    mesh.polygons.foreach_get("normal", normals)
    normals = normals.reshape(-1, 3)
    loop_edges = np.empty(len(mesh.loops), dtype=np.int32)
    mesh.loops.foreach_get("edge_index", loop_edges)
    loop_total = np.empty(len(mesh.polygons), dtype=np.int32)
    mesh.polygons.foreach_get("loop_total", loop_total)
    loop_face = np.repeat(np.arange(len(mesh.polygons)), loop_total)
    faces_of_edge = [[] for _ in range(len(edges))]
    for e, f in zip(loop_edges, loop_face):
        faces_of_edge[e].append(f)
    keep = np.zeros(len(edges), dtype=bool)
    for e, faces in enumerate(faces_of_edge):
        if len(faces) == 2:
            c = float(np.clip(np.dot(normals[faces[0]], normals[faces[1]]), -1.0, 1.0))
            keep[e] = math.degrees(math.acos(c)) >= ANGLE_DEGREES
    return edges[keep]


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    os.makedirs(args.out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for name, (operator, kwargs, marks, step) in MESHES.items():
        getattr(bpy.ops.mesh, operator)(**kwargs)
        obj = bpy.context.active_object
        helper = apply_step(obj, step, marks)
        ev = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mesh = ev.to_mesh()
        mesh.calc_loop_triangles()
        pos = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", pos)
        tri = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int32)
        mesh.loop_triangles.foreach_get("vertices", tri)
        pairs = marked_pairs(mesh, marks)
        ev.to_mesh_clear()
        path = os.path.join(args.out, name + ".obj")
        with open(path, "w") as f:
            f.write("".join("v %.9g %.9g %.9g\n" % tuple(p) for p in pos.reshape(-1, 3).astype(np.float64)))
            f.write("".join("f %d %d %d\n" % tuple(t) for t in tri.reshape(-1, 3) + 1))
            f.write("".join("l %d %d\n" % tuple(e) for e in pairs + 1))
        print(path, len(pos) // 3, "vertices", len(tri) // 3, "triangles", len(pairs), "marked edges")
        bpy.data.objects.remove(obj)
        if helper is not None:
            bpy.data.objects.remove(helper)


main()
