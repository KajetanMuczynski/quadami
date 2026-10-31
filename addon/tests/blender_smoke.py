# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Headless check of the add-on inside Blender.

From the source tree, with a remesher given by path:

    AUTOREMESHER_EXECUTABLE=build/cli/autoremesher-cli \
        blender -b --factory-startup --python-exit-code 1 --python addon/tests/blender_smoke.py

Or the installed extension zip with its bundled remesher (what CI runs):

    blender --command extension install-file -r user_default -e build/addon/quadami-...zip
    blender -b --factory-startup --python-exit-code 1 --python addon/tests/blender_smoke.py -- --installed

Background mode has no window, so the modal operator cannot run; this drives
the same steps it does: read the mesh, run the worker, build the object.
"""

import importlib
import os
import sys
import types

import addon_utils
import bpy

installed = "--installed" in (sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
if installed:
    # The id is the extension's folder name; read it from the source tree.
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "quadami"))
    from names import ID  # noqa: E402
    module_name = "bl_ext.user_default." + ID
    addon_utils.enable(module_name, default_set=True, handle_error=None)
    addon = sys.modules[module_name]
    names = importlib.import_module(module_name + ".names")
    density_module = importlib.import_module(module_name + ".density")
    report = importlib.import_module(module_name + ".report")
    worker = importlib.import_module(module_name + ".worker")
else:
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    import quadami as addon  # noqa: E402
    from quadami import density as density_module  # noqa: E402
    from quadami import names, report, worker  # noqa: E402

    addon.register()
try:
    shown = addon.MAIN_PARAMETERS + addon.ADVANCED_PARAMETERS
    assert sorted(shown) == sorted(worker.PARAMETER_FLAGS), (shown, worker.PARAMETER_FLAGS)

    # The source: smooth shaded, in its own collection, parented to a rotated
    # and scaled empty, with a delta transform, so the result's placement is
    # checked against everything that makes up a world matrix.
    bpy.ops.object.empty_add(location=(0.0, 0.0, 1.0), rotation=(0.3, 0.0, 0.5))
    rig = bpy.context.active_object
    rig.scale = (2.0, 2.0, 2.0)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=128, ring_count=64)
    source = bpy.context.active_object
    source.data.shade_smooth()
    parts = bpy.data.collections.new("Parts")
    bpy.context.scene.collection.children.link(parts)
    for collection in source.users_collection:
        collection.objects.unlink(source)
    parts.objects.link(source)
    source.parent = rig
    source.matrix_parent_inverse = rig.matrix_world.inverted()
    source.location = (1.0, 2.0, 3.0)
    source.rotation_mode = "XYZ"
    source.rotation_euler = (0.1, 0.2, 0.3)
    source.delta_location = (0.0, 0.5, 0.0)
    # Data the result copies (transfer.py): two materials split at the
    # equator, which is also marked sharp, crease and seam; a vertex group
    # and a colour attribute that grow with x; the sphere's own UV map.
    mesh = source.data
    for name in ("Top", "Bottom"):
        mesh.materials.append(bpy.data.materials.new(name))
    for polygon in mesh.polygons:
        polygon.material_index = int(polygon.center.z < 0)
    weights = source.vertex_groups.new(name="Weights")
    for vertex in mesh.vertices:
        weights.add([vertex.index], max(vertex.co.x, 0.0), "REPLACE")
    density = mesh.color_attributes.new("Density", "FLOAT_COLOR", "POINT")
    for vertex in mesh.vertices:
        density.data[vertex.index].color = (max(vertex.co.x, 0.0), 0.0, 0.0, 1.0)
    # Each layer written right after it is looked up: adding a layer (the
    # first use_seam adds one) can move the others in Blender 4.2.
    equator = [all(abs(mesh.vertices[v].co.z) < 1e-4 for v in edge.vertices) for edge in mesh.edges]
    mesh.edges.foreach_set("use_seam", equator)
    mesh.attributes.new("sharp_edge", "BOOLEAN", "EDGE").data.foreach_set("value", equator)
    mesh.attributes.new("crease_edge", "FLOAT", "EDGE").data.foreach_set("value", [0.5 * e for e in equator])
    bpy.context.view_layer.update()

    positions, triangles, smooth, marked, flooded = addon._mesh_arrays(bpy.context, source, {"SHARP"})
    assert positions.shape[1] == 3 and triangles.shape[1] == 3, (positions.shape, triangles.shape)
    assert len(triangles) == 128 * 2 + 128 * 62 * 2, len(triangles)
    assert smooth
    # Follow Marks: the equator's 128 edges, as vertex pairs; none when off.
    assert marked.shape == (128, 2) and abs(positions[marked.ravel(), 2]).max() < 1e-4, marked.shape
    assert len(addon._mesh_arrays(bpy.context, source)[3]) == 0 and not flooded
    # Sharp by default; the panel counts the marks (a small share of the edges: no flood).
    assert getattr(bpy.context.scene, names.ID).follow_marks == {"SHARP"}
    line = addon._mark_line(source, {"SHARP"})
    assert line and "Sharp 128" in line[0] and "Seam 128" in line[0] and not line[1], line
    # Every edge marked Sharp on a mesh of over 1000 edges: left out, with a warning.
    flood = bpy.data.meshes.new("Flood")
    flood.from_pydata([(x, y, 0.0) for y in range(30) for x in range(30)], [],
                      [(30 * y + x, 30 * y + x + 1, 30 * y + x + 31, 30 * y + x + 30) for y in range(29) for x in range(29)])
    flood.attributes.new("sharp_edge", "BOOLEAN", "EDGE").data.foreach_set("value", [True] * len(flood.edges))
    assert addon._flooded_marks(flood, addon._mark_counts(flood), {"SHARP"}) == ["SHARP"], len(flood.edges)
    assert addon._flood_warning(["SHARP"]).startswith("Follow Marks left out Sharp")
    bpy.data.meshes.remove(flood)

    # Uneven scale warns (shear from a rotated parent counts too); even scale does not.
    assert addon._uneven_scale_warning(source) is None
    source.scale = (3.0, 1.0, 1.0)
    bpy.context.view_layer.update()
    assert addon._uneven_scale_warning(source) is not None
    source.scale = (1.0, 1.0, 1.0)
    bpy.context.view_layer.update()

    settings = getattr(bpy.context.scene, names.ID)
    settings.target_quads = 2000
    preferences = addon._preferences(bpy.context)
    executable = worker.find_executable(bpy.path.abspath(preferences.executable) if preferences else "")
    if installed:
        assert executable == worker.bundled_executable(), (executable, worker.bundled_executable())
    assert executable, "set AUTOREMESHER_EXECUTABLE"
    print("blender_smoke: remesher %s" % executable)
    build = worker.addon_build_id()
    print("blender_smoke: add-on build %s, remesher build %s" % (build, worker.worker_build_id(executable)))
    if installed:
        # package.py copies the bundled program's id, so the panel shows no mismatch.
        assert build != "dev" and worker.worker_build_id(executable) == build, build
    outcome = worker.remesh(executable, positions, triangles, settings.parameters(), marked_edges=marked)
    assert outcome.ok, (outcome.status, outcome.message, outcome.log)
    assert outcome.report["input"]["feature_edges"] == 128, outcome.report["input"]
    assert outcome.report["input"]["ignored_feature_edges"] == 0, outcome.report["input"]

    obj, final_report = addon._build_object(bpy.context, source, outcome, smooth, ["test warning"])
    addon._show_result(bpy.context, obj, (source,))
    bpy.context.view_layer.update()
    output = outcome.report["output"]
    quads = sum(1 for polygon in obj.data.polygons if polygon.loop_total == 4)
    assert len(obj.data.polygons) == output["faces"], (len(obj.data.polygons), output)
    assert quads == output["quads"], (quads, output)
    assert len(obj.data.vertices) == output["vertices"]
    assert all(polygon.use_smooth for polygon in obj.data.polygons)
    assert obj.parent == rig and obj.users_collection == source.users_collection == (parts,)
    assert max(abs(a - b) for row_a, row_b in zip(obj.matrix_world, source.matrix_world)
               for a, b in zip(row_a, row_b)) < 1e-5, (obj.matrix_world, source.matrix_world)
    assert addon.REPORT_PROPERTY in obj and final_report["warnings"][-1] == "test warning"
    # Remesh on the result runs on the source, which is hidden but still readable.
    assert addon._source(obj) == source and addon._source(source) is None
    assert addon._source_problem(bpy.context, source) is None
    assert not source.visible_get() and source.hide_render
    assert bpy.context.view_layer.objects.active == obj and obj.select_get()
    assert len(addon._mesh_arrays(bpy.context, source)[1]) == len(triangles)
    flat, _ = addon._build_object(bpy.context, source, outcome, False)
    assert not any(polygon.use_smooth for polygon in flat.data.polygons)

    # Keep from Original: everything, UVs included.
    settings.keep_uvs = True
    addon._keep(bpy.context, source, obj, final_report, settings)
    kept = final_report["kept"]
    assert kept["materials"] == 2 and kept["vertex_groups"] == 1 and kept["colors"] == 1, kept
    assert kept["uv_maps"] == 1 and kept["sharp"] == kept["seam"] == kept["crease"] > 0, kept
    result_mesh = obj.data
    assert [m.name for m in result_mesh.materials] == ["Top", "Bottom"]
    group = obj.vertex_groups["Weights"]
    for polygon in result_mesh.polygons:
        if abs(polygon.center.z) > 0.1:
            assert polygon.material_index == int(polygon.center.z < 0), polygon.center
    for vertex in result_mesh.vertices:
        weight = next((g.weight for g in vertex.groups if g.group == group.index), 0.0)
        assert abs(weight - max(vertex.co.x, 0.0)) < 0.02, (vertex.co, weight)
        color = result_mesh.color_attributes["Density"].data[vertex.index].color[0]
        assert abs(color - max(vertex.co.x, 0.0)) < 0.02, (vertex.co, color)
    # The equator seam comes back as one closed line: it cuts the sphere in two.
    seams = {edge.key for edge in result_mesh.edges if edge.use_seam}
    owner, parent = {}, list(range(len(result_mesh.polygons)))

    def root(i):
        while parent[i] != i:
            i = parent[i]
        return i

    for polygon in result_mesh.polygons:
        for key in polygon.edge_keys:
            if key not in seams:
                if key in owner:
                    parent[root(polygon.index)] = root(owner[key])
                owner.setdefault(key, polygon.index)
    halves = {root(i) for i in range(len(parent))}
    assert len(halves) == 2, len(halves)
    for vertex_index in {v for key in seams for v in key}:
        assert abs(result_mesh.vertices[vertex_index].co.z) < 0.1, result_mesh.vertices[vertex_index].co
    assert result_mesh.uv_layers.active.name == "UVMap"
    for label, value, problem in report.summary_rows(final_report):
        print("  %-20s %s%s" % (label, value, "  <-" if problem else ""))
    for text, first, problem in report.warning_lines(final_report):
        print("  %s%s" % ("" if first else "  ", text))
    # "Repair Folds and Re-run" runs the same worker with the repair on.
    repaired = worker.remesh(executable, positions, triangles, dict(settings.parameters(), repair_folds=True))
    assert repaired.ok and repaired.report["parameters"]["repair_folds"] is True, (repaired.status, repaired.log)
    assert not report.offers_fold_repair(repaired.report)

    # The panel cannot draw without a window; run its draw() on a layout that
    # accepts every call and records the labels, to catch errors in it.
    class Layout:
        def __init__(self, labels):
            self.labels = labels

        def __getattr__(self, name):
            def call(*args, **kwargs):
                if name == "label":
                    self.labels.append(kwargs.get("text", ""))
                return (Layout(self.labels), Layout(self.labels)) if name == "panel" else Layout(self.labels)
            return call

    def panel_labels():
        labels = []
        addon.VIEW3D_PT_remesh_quads.draw(types.SimpleNamespace(layout=Layout(labels)), bpy.context)
        return " ".join(labels)

    labels = panel_labels()
    assert "test warning" in labels and "Copied from the original" in labels, labels
    bpy.context.view_layer.objects.active = source
    source.scale = (1.0, 1.0, 3.0)
    bpy.context.view_layer.update()
    assert "scaled unevenly" in panel_labels()
    source.scale = (1.0, 1.0, 1.0)
    bpy.context.view_layer.update()

    # Density preview: the source planned, shown, and Change sets Target Quads
    # within the plan's range; another setting or a mesh edit takes it down.
    positions, triangles, smooth, marked, flooded = addon._mesh_arrays(bpy.context, source, settings.follow_marks)
    planned = worker.remesh(executable, positions, triangles, settings.parameters(), marked_edges=marked, plan=True)
    assert planned.ok and len(planned.plan.edge) == len(positions), (planned.status, planned.message, planned.log)
    assert planned.plan.quads == settings.target_quads == 2000
    density_module.show(density_module.Preview(source.name, density_module.settings_key(settings), planned.plan,
                                               positions, triangles, planned.report))
    settings.preview_change = 10.0
    assert settings.target_quads == 2200, settings.target_quads
    labels = panel_labels()
    assert "quads planned" in labels and "Quad edge" in labels, labels
    assert addon._overlay_state() == (2200, True)
    settings.target_quads = 1800
    assert abs(settings.preview_change + 10.0) < 1e-6, settings.preview_change
    settings.target_quads = 4500
    assert addon._overlay_state() is None and "outside the preview" in panel_labels()
    settings.target_quads = 2000
    adaptivity = settings.adaptivity
    settings.adaptivity = 0.7 if adaptivity != 0.7 else 0.3
    assert addon._overlay_state() is None and "Settings changed" in panel_labels()
    settings.adaptivity = adaptivity
    assert addon._overlay_state() == (2000, True)
    mesh.vertices[0].co.x += 0.01
    mesh.update()
    bpy.context.view_layer.update()
    assert density_module.current() is None, "a mesh edit drops the plan"
    print("blender_smoke: OK, Blender %s" % bpy.app.version_string)
finally:
    if installed:
        addon_utils.disable(module_name, default_set=True)
    else:
        addon.unregister()
