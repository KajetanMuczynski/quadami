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

    blender -b --factory-startup --python-expr \\
        "import bpy; bpy.ops.wm.save_as_mainfile(filepath='start.blend')"
    xvfb-run -a -s "-screen 0 1400x2400x24" blender --factory-startup start.blend \\
        --python addon/tests/blender_panel_shot.py -- --out build/panel

(a file on the command line keeps the splash screen away from the sidebar).

It remeshes a sphere with a thin horn, a one-triangle speck beside it and its
equator marked Sharp at a low target, so the report has missing thin parts, a
missing piece, followed marks and the Raise Target Quads hint, shows the
result in the sidebar, and saves the screen at UI scale 1 and 1.5 as
panel-<scale>.png and the sidebar as sidebar-<scale>.png (ImageMagick; --ocr
prints the sidebar's text, read by tesseract).
The script quits Blender with exit status 1 on any error, a blank picture or,
with --ocr, a line of our panel that Blender cut off with an ellipsis because
it was too wide for the sidebar.  The pictures are for a person to look at.
"""

import importlib
import os
import subprocess
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
    # The equator marked Sharp, so the panel shows the mark count and the
    # Follow Marks hint too.
    for edge in bm.edges:
        if all(abs(v.co.z) < 1e-6 for v in edge.verts):
            edge.smooth = False
    bm.to_mesh(mesh)
    bm.free()
    source = bpy.data.objects.new("Thin parts", mesh)
    bpy.context.scene.collection.objects.link(source)
    bpy.context.view_layer.objects.active = source
    source.select_set(True)
    bpy.context.view_layer.update()

    settings = getattr(bpy.context.scene, ID)
    settings.target_quads = 300
    positions, triangles, smooth, marked, _flooded = addon._mesh_arrays(bpy.context, source, settings.follow_marks)
    assert len(marked) > 0
    outcome = worker.remesh(worker.bundled_executable(), positions, triangles, settings.parameters(),
                            marked_edges=marked)
    assert outcome.ok, (outcome.status, outcome.message, outcome.log)
    obj, final_report = addon._build_object(bpy.context, source, outcome, smooth)
    key = addon.raise_hint.input_key(positions, triangles, marked, settings.parameters())
    addon._track(source, key, obj, final_report)
    assert final_report.get("raise", {}).get("state") == "try", final_report.get("raise")
    print("panel_shot: raise hint: %s" % addon.raise_hint.text(final_report["raise"]))
    addon._show_result(bpy.context, obj, (source,))
    for label, value, problem in report.summary_rows(final_report):
        print("panel_shot: %-20s %s%s" % (label, value, "  <-" if problem else ""))
    for warning in final_report.get("warnings", []):
        print("panel_shot: warning: %s" % warning)

    window, area = view3d()
    area.spaces.active.show_region_ui = True
    region = next(r for r in area.regions if r.type == "UI")
    try:
        region.active_panel_category = NAME
    except AttributeError:
        # Read-only in Blender 4.2: show the panel in the
        # sidebar's first tab instead, which a fresh window opens on.
        panel = addon.VIEW3D_PT_remesh_quads
        bpy.utils.unregister_class(panel)
        panel.bl_category = "Item"
        bpy.utils.register_class(panel)
    view = next(r for r in area.regions if r.type == "WINDOW")
    with bpy.context.temp_override(window=window, area=area, region=view):
        bpy.ops.view3d.view_all()


def shoot(scale):
    window, area = view3d()
    region = next(r for r in area.regions if r.type == "UI")
    print("panel_shot: UI scale %.2f, window %d x %d, sidebar %d x %d px at %d, %d"
          % (scale, window.width, window.height, region.width, region.height, region.x, region.y))
    # The room the panel reckons its text has, against what OCR reads back.
    addon = sys.modules["bl_ext.user_default." + ID]
    with bpy.context.temp_override(window=window, area=area, region=region):
        room = addon._text_room(bpy.context)
        sample = "Parts thinner than one quad are missing"
        print("panel_shot: room panel %.1f, box %.1f, icon %d, pad %d, space %d; %r is %.1f px"
              % (room.panel, room.box, room.icon, room.pad, room.space, sample, room.measure(sample)))
    path = os.path.join(out, "panel-%g.png" % scale)
    with bpy.context.temp_override(window=window, area=area):
        bpy.ops.wm.redraw_timer(type="DRAW_WIN_SWAP", iterations=1)
    # Blender's own screenshot reads 1 x 1 pixel under Xvfb; ImageMagick reads
    # the X screen, which holds what Blender drew.
    subprocess.run(["import", "-window", "root", path], check=True)
    colours = int(subprocess.run(["identify", "-format", "%k", path], check=True,
                                 capture_output=True, text=True).stdout)
    print("panel_shot: %s, %d colours" % (os.path.basename(path), colours))
    if colours < 8:
        raise RuntimeError("the screenshot is blank: nothing was drawn")
    # The sidebar alone, read back with OCR into the log: wrapped lines and
    # any label cut off with an ellipsis show there without the artifact.
    # Window coordinates count from the bottom left; the window sits at the
    # screen's top left under Xvfb with no window manager.
    crop = os.path.join(out, "sidebar-%g.png" % scale)
    top = window.height - (region.y + region.height)
    subprocess.run(["convert", path, "-crop", "%dx%d+%d+%d" % (region.width, region.height, region.x, top),
                    "+repage", crop], check=True)
    if "--ocr" in args:
        large = os.path.join(out, "ocr.png")
        subprocess.run(["convert", crop, "-resize", "300%", large], check=True)
        text = subprocess.run(["tesseract", large, "-", "--psm", "4"], check=True,
                              capture_output=True, text=True).stdout
        os.remove(large)
        ours, cut = False, []
        for line in text.splitlines():
            if line.strip():
                print("panel_ocr %g: %s" % (scale, line))
                # Blender's other panels above ours are not checked.
                ours = ours or NAME in line
                if ours and ("..." in line or "\u2026" in line):
                    cut.append(line)
        if cut:
            raise RuntimeError("labels cut off with an ellipsis at UI scale %g: %s" % (scale, cut))


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
