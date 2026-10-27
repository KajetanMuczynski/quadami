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

"""Screenshots of the panel with a real report, in Blender's own window.

Background mode draws no interface, so this runs Blender with a window (on
Linux CI under Xvfb) after the zip is installed:

    xvfb-run -a -s "-screen 0 1600x900x24" blender --factory-startup \\
        --python addon/tests/blender_panel_shot.py -- --out build/panel

It remeshes a sphere with a thin horn and a one-triangle speck beside it at a
low target, so the report has missing thin parts and a missing piece, shows the
result in the sidebar's Quadami tab, and saves the 3D view at UI scale 1 and
1.5 as panel-<scale>.png.  The script quits Blender with exit status 1 on any
error; the pictures are for a person to look at (a label too wide for the
sidebar is cut off with an ellipsis, which the script cannot see).
"""

import importlib
import os
import sys
import traceback

import addon_utils
import bmesh
import bpy

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
out = os.path.abspath(args[args.index("--out") + 1]) if "--out" in args else os.getcwd()
os.makedirs(out, exist_ok=True)

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "quadami"))
from names import ID, NAME  # noqa: E402

SCALES = (1.0, 1.5)
state = {"step": 0}


def view3d():
    window = bpy.context.window_manager.windows[0]
    area = max((a for a in window.screen.areas if a.type == "VIEW_3D"), key=lambda a: a.width * a.height)
    return window, area


def setup():
    module_name = "bl_ext.user_default." + ID
    addon_utils.enable(module_name, default_set=True, handle_error=None)
    addon = sys.modules[module_name]
    worker = importlib.import_module(module_name + ".worker")
    report = importlib.import_module(module_name + ".report")

    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)
    mesh = bpy.data.meshes.new("Thin parts")
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=64, v_segments=32, radius=1.0)
    # A horn out of the north pole, 0.04 thick and 1.5 long, about a fifth of
    # a quad at 300 quads; and a one-triangle speck beside the sphere.
    cap = [face for face in bm.faces if face.calc_center_median().z > 0.99]
    for vertex in {v for face in cap for v in face.verts}:
        vertex.co.x *= 0.2
        vertex.co.y *= 0.2
    extruded = bmesh.ops.extrude_face_region(bm, geom=cap)
    horn = [element for element in extruded["geom"] if isinstance(element, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, verts=horn, vec=(0.0, 0.0, 1.5))
    bmesh.ops.delete(bm, geom=cap, context="FACES_ONLY")
    speck = [bm.verts.new(co) for co in ((0.0, 2.0, 0.0), (0.02, 2.0, 0.0), (0.0, 2.02, 0.0))]
    bm.faces.new(speck)
    bm.to_mesh(mesh)
    bm.free()
    source = bpy.data.objects.new("Thin parts", mesh)
    bpy.context.scene.collection.objects.link(source)
    bpy.context.view_layer.objects.active = source
    source.select_set(True)
    bpy.context.view_layer.update()

    positions, triangles, smooth = addon._mesh_arrays(bpy.context, source)
    settings = getattr(bpy.context.scene, ID)
    settings.target_quads = 300
    outcome = worker.remesh(worker.bundled_executable(), positions, triangles, settings.parameters())
    assert outcome.ok, (outcome.status, outcome.message, outcome.log)
    obj, final_report = addon._build_object(bpy.context, source, outcome, smooth)
    addon._show_result(bpy.context, obj, (source,))
    for label, value, problem in report.summary_rows(final_report):
        print("panel_shot: %-20s %s%s" % (label, value, "  <-" if problem else ""))
    for warning in final_report.get("warnings", []):
        print("panel_shot: warning: %s" % warning)

    window, area = view3d()
    area.spaces.active.show_region_ui = True
    for region in area.regions:
        if region.type == "UI":
            region.active_panel_category = NAME
    with bpy.context.temp_override(window=window, area=area):
        bpy.ops.view3d.view_all()


def shoot(scale):
    window, area = view3d()
    region = next(r for r in area.regions if r.type == "UI")
    print("panel_shot: UI scale %.2f, sidebar %d x %d px" % (scale, region.width, region.height))
    with bpy.context.temp_override(window=window, area=area):
        bpy.ops.screen.screenshot_area(filepath=os.path.join(out, "panel-%g.png" % scale))


def step():
    # One step per call, with time for Blender to redraw in between.
    try:
        index = state["step"]
        state["step"] += 1
        if index == 0:
            setup()
        elif index <= 2 * len(SCALES):
            scale = SCALES[(index - 1) // 2]
            if (index - 1) % 2 == 0:
                bpy.context.preferences.view.ui_scale = scale
            else:
                shoot(scale)
        else:
            bpy.ops.wm.quit_blender()
            return None
        return 1.0
    except Exception:
        traceback.print_exc()
        sys.stdout.flush()
        os._exit(1)


bpy.app.timers.register(step, first_interval=1.0)
