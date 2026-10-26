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
    report = importlib.import_module(module_name + ".report")
    worker = importlib.import_module(module_name + ".worker")
else:
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    import quadami as addon  # noqa: E402
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
    bpy.context.view_layer.update()

    positions, triangles, smooth = addon._mesh_arrays(bpy.context, source)
    assert positions.shape[1] == 3 and triangles.shape[1] == 3, (positions.shape, triangles.shape)
    assert len(triangles) == 128 * 2 + 128 * 62 * 2, len(triangles)
    assert smooth

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
    outcome = worker.remesh(executable, positions, triangles, settings.parameters())
    assert outcome.ok, (outcome.status, outcome.message, outcome.log)

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

    assert "test warning" in panel_labels()
    bpy.context.view_layer.objects.active = source
    source.scale = (1.0, 1.0, 3.0)
    bpy.context.view_layer.update()
    assert "scaled unevenly" in panel_labels()
    print("blender_smoke: OK, Blender %s" % bpy.app.version_string)
finally:
    if installed:
        addon_utils.disable(module_name, default_set=True)
    else:
        addon.unregister()
