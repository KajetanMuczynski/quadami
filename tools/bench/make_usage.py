#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Write the "usage" A/B set: everyday inputs a new user remeshes that no other set had
(docs/USAGE-COVERAGE.md, 2026-10-27), triangulated the way the add-on hands meshes to the CLI
(evaluated mesh, loop triangles, float32 positions).

- Converted text and curves exactly as Blender's Object > Convert > Mesh leaves them (text: caps
  and side walls not joined) and after Merge by Distance.
- Suzanne + Subdivision 2 after a glTF round trip with the importer's defaults: smooth shaded (split
  along UV seams only) and flat shaded (every face its own piece), next to the welded original.
- Low-poly inputs for Smooth Normal: the default cube, UV sphere, cylinder, Suzanne.
- With --external-raw (fetch_external.py's raw/ folder): the `external` set's glTF meshes as
  Blender's glTF importer gives them with its defaults (vertices split at UV and normal seams; the
  bench's own ext_*.obj welds them), all mesh objects joined with their transforms applied, as
  `gltfimport_<id>.obj`.

    python make_usage.py --out meshes/usage [--external-raw meshes/external/raw]   # pip install bpy
    blender -b --factory-startup -P make_usage.py -- --out meshes/usage
"""
import argparse
import os
import sys

import bpy
import numpy as np

import addon_utils  # noqa: E402 - importable only after bpy
import bmesh  # noqa: E402


def text(extrude, weld):
    bpy.ops.object.text_add()
    obj = bpy.context.active_object
    obj.data.body = "Quadami"
    obj.data.extrude = extrude
    bpy.ops.object.convert(target="MESH")
    if weld:
        merge_by_distance(obj)
    return obj


def curve_tube(weld):
    bpy.ops.curve.primitive_bezier_curve_add()
    obj = bpy.context.active_object
    obj.data.bevel_depth = 0.1
    obj.data.use_fill_caps = True
    bpy.ops.object.convert(target="MESH")
    if weld:
        merge_by_distance(obj)
    return obj


def merge_by_distance(obj):
    """Mesh > Clean Up > Merge by Distance with its default distance."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bm.to_mesh(obj.data)
    bm.free()


def suzanne(shading, work):
    bpy.ops.mesh.primitive_monkey_add()
    obj = bpy.context.active_object
    obj.modifiers.new("Subdivision", "SUBSURF").levels = 2
    bpy.ops.object.modifier_apply(modifier="Subdivision")
    if shading is None:
        return obj
    getattr(obj.data, "shade_" + shading)()
    path = os.path.join(work, "suzanne_%s.glb" % shading)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB")
    bpy.data.objects.remove(obj)
    bpy.ops.import_scene.gltf(filepath=path)
    os.remove(path)
    return next(o for o in bpy.context.selected_objects if o.type == "MESH")


MESHES = {
    "text_extruded": lambda work: text(0.1, weld=False),
    "text_extruded_welded": lambda work: text(0.1, weld=True),
    "curve_tube_caps": lambda work: curve_tube(weld=False),
    "curve_tube_caps_welded": lambda work: curve_tube(weld=True),
    "suzanne_sub2": lambda work: suzanne(None, work),
    "suzanne_sub2_gltf_smooth": lambda work: suzanne("smooth", work),
    "suzanne_sub2_gltf_flat": lambda work: suzanne("flat", work),
    "lowpoly_cube": lambda work: (bpy.ops.mesh.primitive_cube_add(), bpy.context.active_object)[1],
    "lowpoly_uvsphere": lambda work: (bpy.ops.mesh.primitive_uv_sphere_add(), bpy.context.active_object)[1],
    "lowpoly_cylinder": lambda work: (bpy.ops.mesh.primitive_cylinder_add(), bpy.context.active_object)[1],
    "lowpoly_suzanne": lambda work: (bpy.ops.mesh.primitive_monkey_add(), bpy.context.active_object)[1],
}


def gltf_import(path):
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.active_object
    obj.data.transform(obj.matrix_world)
    obj.matrix_world.identity()
    return obj


def write(obj, path):
    ev = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = ev.to_mesh()
    mesh.calc_loop_triangles()
    pos = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
    mesh.vertices.foreach_get("co", pos)
    tri = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int32)
    mesh.loop_triangles.foreach_get("vertices", tri)
    ev.to_mesh_clear()
    with open(path, "w") as f:
        f.write("".join("v %.9g %.9g %.9g\n" % tuple(p) for p in pos.reshape(-1, 3).astype(np.float64)))
        f.write("".join("f %d %d %d\n" % tuple(t) for t in tri.reshape(-1, 3) + 1))
    print(path, len(pos) // 3, "vertices", len(tri) // 3, "triangles")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--external-raw", help="fetch_external.py's raw/ folder; adds gltfimport_<id>.obj")
    args = ap.parse_args(argv)
    os.makedirs(args.out, exist_ok=True)
    for name, make in MESHES.items():
        bpy.ops.wm.read_factory_settings(use_empty=True)
        addon_utils.enable("io_scene_gltf2")
        write(make(args.out), os.path.join(args.out, name + ".obj"))
    for folder in sorted(os.listdir(args.external_raw)) if args.external_raw else ():
        files = [f for f in sorted(os.listdir(os.path.join(args.external_raw, folder))) if f.endswith((".glb", ".gltf"))]
        if files:
            bpy.ops.wm.read_factory_settings(use_empty=True)
            addon_utils.enable("io_scene_gltf2")
            obj = gltf_import(os.path.join(args.external_raw, folder, files[0]))
            write(obj, os.path.join(args.out, "gltfimport_%s.obj" % folder))

main()
