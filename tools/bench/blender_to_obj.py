#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Write the meshes of a .blend or .fbx file as one triangle OBJ, the way the add-on hands a
mesh to the CLI: evaluated (modifiers applied as in the viewport), world transform applied, loop
triangles, float32 positions. Objects are joined, as Object > Join would; hidden objects and rig
widgets (names starting with WGT-) are left out unless named.

    python blender_to_obj.py in.blend out.obj                        # pip install bpy (Python 3.13)
    python blender_to_obj.py in.blend hair.obj --objects 'GEO-rain-hair*'
    blender -b --factory-startup -P blender_to_obj.py -- in.fbx out.obj

glTF goes through fetch_external.py instead: Blender's glTF import keeps vertices split at UV seams
(a 150k-triangle scan came in as 892 open pieces). Scripts stored in a .blend file are never run
(rigs that need them keep their rest pose). Prints one line per object written and the total.
"""
import argparse
import fnmatch
import sys

import bpy
import numpy as np


def load(path):
    if path.endswith(".blend"):
        bpy.ops.wm.open_mainfile(filepath=path, load_ui=False, use_scripts=False)
        return
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if path.endswith(".fbx"):
        bpy.ops.import_scene.fbx(filepath=path)
    elif path.endswith(".obj"):
        bpy.ops.wm.obj_import(filepath=path)
    else:
        sys.exit(f"unsupported input: {path}")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--objects", nargs="+", help="object name patterns (fnmatch); default: all visible meshes")
    args = ap.parse_args(argv)
    load(args.input)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    positions, triangles, base = [], [], 0
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH":
            continue
        if args.objects:
            if not any(fnmatch.fnmatchcase(obj.name, p) for p in args.objects):
                continue
        elif obj.hide_get() or obj.hide_render or obj.name.startswith("WGT-"):
            continue
        ev = obj.evaluated_get(depsgraph)
        mesh = ev.to_mesh()
        mesh.calc_loop_triangles()
        pos = np.empty(len(mesh.vertices) * 3, dtype=np.float64)
        mesh.vertices.foreach_get("co", pos)
        tri = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int64)
        mesh.loop_triangles.foreach_get("vertices", tri)
        world = np.array(obj.matrix_world, dtype=np.float64)
        ev.to_mesh_clear()
        if not len(tri):
            continue
        pos = pos.reshape(-1, 3) @ world[:3, :3].T + world[:3, 3]
        positions.append(pos.astype(np.float32))
        triangles.append(tri.reshape(-1, 3) + base)
        base += len(pos)
        print(f"{obj.name}: {len(pos)} vertices, {len(tri) // 3} triangles")
    if not positions:
        sys.exit("no mesh objects selected")
    pos = np.vstack(positions).astype(np.float64)
    tri = np.vstack(triangles)
    with open(args.output, "w") as f:
        f.write("".join("v %.9g %.9g %.9g\n" % tuple(p) for p in pos))
        f.write("".join("f %d %d %d\n" % tuple(t) for t in tri + 1))
    print(f"{args.output}: {len(pos)} vertices, {len(tri)} triangles")


main()
