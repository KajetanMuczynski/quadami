#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Write the "primitives" A/B set: Blender's Add > Mesh primitives with their default settings, plus
variants that switch one suspected cause off, triangulated the way the add-on hands meshes to the
CLI (evaluated mesh, loop triangles, float32 positions). Kajetan's report (2026-10-27): the default
cone and cylinder are destroyed at 5000 quads, the icosphere shows odd loops.

    python make_primitives.py --out meshes/primitives                    # pip install bpy (Python 3.13)
    blender -b --factory-startup -P make_primitives.py -- --out meshes/primitives
"""
import argparse
import os
import sys

import bpy
import numpy as np

# name -> (operator, keyword arguments[, Subdivision Surface levels]); every argument left out keeps
# Blender's default.
PRIMITIVES = {
    "cube": ("primitive_cube_add", {}),
    "uvsphere": ("primitive_uv_sphere_add", {}),
    "icosphere_s1": ("primitive_ico_sphere_add", {"subdivisions": 1}),
    "icosphere": ("primitive_ico_sphere_add", {}),
    "icosphere_s3": ("primitive_ico_sphere_add", {"subdivisions": 3}),
    "icosphere_s5": ("primitive_ico_sphere_add", {"subdivisions": 5}),
    "icosphere_subsurf1": ("primitive_ico_sphere_add", {}, 1),
    "icosphere_subsurf2": ("primitive_ico_sphere_add", {}, 2),
    "cone": ("primitive_cone_add", {}),
    "cone_v64": ("primitive_cone_add", {"vertices": 64}),
    "cone_trifan": ("primitive_cone_add", {"end_fill_type": "TRIFAN"}),
    "cone_open": ("primitive_cone_add", {"end_fill_type": "NOTHING"}),
    "cone_frustum": ("primitive_cone_add", {"radius2": 0.5}),
    "cylinder": ("primitive_cylinder_add", {}),
    "cylinder_v64": ("primitive_cylinder_add", {"vertices": 64}),
    "cylinder_trifan": ("primitive_cylinder_add", {"end_fill_type": "TRIFAN"}),
    "cylinder_open": ("primitive_cylinder_add", {"end_fill_type": "NOTHING"}),
}


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    os.makedirs(args.out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for name, (operator, kwargs, *subsurf) in PRIMITIVES.items():
        getattr(bpy.ops.mesh, operator)(**kwargs)
        obj = bpy.context.active_object
        if subsurf:
            obj.modifiers.new("Subdivision", "SUBSURF").levels = subsurf[0]
        ev = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mesh = ev.to_mesh()
        # Blender can emit a mesh's loop triangles in another order from run to run (seen on the UV
        # sphere); the triangles are the same, but the remesher's output may depend on the order.
        mesh.calc_loop_triangles()
        pos = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", pos)
        tri = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int32)
        mesh.loop_triangles.foreach_get("vertices", tri)
        ev.to_mesh_clear()
        path = os.path.join(args.out, name + ".obj")
        with open(path, "w") as f:
            f.write("".join("v %.9g %.9g %.9g\n" % tuple(p) for p in pos.reshape(-1, 3).astype(np.float64)))
            f.write("".join("f %d %d %d\n" % tuple(t) for t in tri.reshape(-1, 3) + 1))
        print(path, len(pos) // 3, "vertices", len(tri) // 3, "triangles")
        bpy.data.objects.remove(obj)


main()
