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

"""The Remesh and Copy Report buttons as a person clicks them, in Blender's
own window, with the installed zip and its bundled remesher.

blender_smoke.py runs in background mode, where the modal operator cannot
run, so it calls the operator's steps one by one. This runs the operators
themselves (invoke, the timer-driven modal loop, the finish that builds and
shows the result, the clipboard), so it needs a window; on Linux CI, Xvfb:

    blender --command extension install-file -r user_default -e build/addon/quadami-...zip
    xvfb-run -a blender --factory-startup --python addon/tests/blender_operator.py

Three runs: a UV sphere at 2,000 quads; a thin torus (tube 1/20 of its
radius) at 300 quads, where quads are wider than the tube and folded or
twisted quads may come out; Remesh again on that result, which remeshes its
source; and one run cancelled from the panel's Cancel button. Each finished
run is checked for: status Done, quads within 0.6-1.5x of Target Quads, the
result in Blender matching the report, and the Copy Report text (Result,
settings, Blender version, build line naming the commit CI built, the Folded
faces row as "N (T twisted)"). Whether the torus folds is printed, not
checked: it changes with one ulp of input (see blender_checks.py).

Exits with status 1 on any failure, or when a run takes over 3 minutes.
"""

import importlib
import json
import math
import os
import re
import sys
import time
import traceback

import addon_utils
import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_checks  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "quadami"))
from names import ID  # noqa: E402

MODULE = "bl_ext.user_default." + ID
TIMEOUT = 180.0


def window_override(region_type="WINDOW"):
    window = bpy.context.window_manager.windows[0]
    area = max((a for a in window.screen.areas if a.type == "VIEW_3D"), key=lambda a: a.width * a.height)
    region = next(r for r in area.regions if r.type == region_type)
    return bpy.context.temp_override(window=window, area=area, region=region)


def make_active(obj):
    for selected in list(bpy.context.view_layer.objects.selected):
        selected.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def thin_torus(name, radius=1.0, tube=0.05, around=400, across=24):
    vertices = []
    for i in range(around):
        u = 2.0 * math.pi * i / around
        for j in range(across):
            v = 2.0 * math.pi * j / across
            ring = radius + tube * math.cos(v)
            vertices.append((ring * math.cos(u), ring * math.sin(u), tube * math.sin(v)))
    faces = [(i * across + j, ((i + 1) % around) * across + j, ((i + 1) % around) * across + (j + 1) % across,
              i * across + (j + 1) % across) for i in range(around) for j in range(across)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def press_remesh(addon):
    with window_override():
        started = getattr(bpy.ops.object, ID + "_remesh")("INVOKE_DEFAULT")
    assert started == {"RUNNING_MODAL"}, started
    assert addon._job is not None


def wait(addon):
    start = time.monotonic()
    while addon._job is not None:
        if time.monotonic() - start > TIMEOUT:
            raise RuntimeError("the remesh did not finish in %d s: %s %s"
                               % (TIMEOUT, addon._job.step, addon._job.fraction))
        yield 0.25
    print("blender_operator: finished in %.1f s, status %s" % (time.monotonic() - start, addon._last_status))


def remesh(addon, obj, source, target):
    """Press Remesh with `obj` active, wait, check the result of `source`."""
    settings = getattr(bpy.context.scene, ID)
    settings.target_quads = target
    make_active(obj)
    before = set(bpy.data.objects)
    press_remesh(addon)
    yield from wait(addon)
    assert addon._last_status == "ok", (addon._last_status, addon._last_message)
    new = set(bpy.data.objects) - before
    assert len(new) == 1, new
    result = new.pop()
    assert bpy.context.view_layer.objects.active == result and result.select_get()
    assert addon._source(result) == source and not source.visible_get()
    assert obj == source or not obj.visible_get()
    result_report = json.loads(result[addon.REPORT_PROPERTY])
    output = result_report["output"]
    quads = sum(1 for polygon in result.data.polygons if polygon.loop_total == 4)
    assert (len(result.data.polygons), quads) == (output["faces"], output["quads"]), output
    blender_checks.check_quad_count(output, target)

    with window_override():
        assert getattr(bpy.ops.object, ID + "_copy_report")() == {"FINISHED"}
    text = bpy.context.window_manager.clipboard
    assert text == addon.copy_report_text(bpy.context), "the clipboard does not hold the report: %r" % text[:200]
    assert text.startswith("Quadami report\nResult: Done\n"), text
    assert "target_quads=%d" % target in text and "Blender: " + bpy.app.version_string in text, text
    build = re.search(r"^Add-on build: (.*)$", text, re.M).group(1)
    blender_checks.check_build(build)
    assert "\nRemesher build: same\n" in text, text
    folded, twisted = blender_checks.check_copy_report(text, result_report)
    print("blender_operator: %s at %d: %d quads, %d folded or twisted faces (%d twisted)"
          % (source.name, target, quads, folded, twisted))
    return result


def script():
    addon_utils.enable(MODULE, default_set=True, handle_error=None)
    addon = sys.modules[MODULE]
    worker = importlib.import_module(MODULE + ".worker")
    preferences = addon._preferences(bpy.context)
    executable = worker.find_executable(bpy.path.abspath(preferences.executable) if preferences else "")
    assert executable == worker.bundled_executable(), (executable, worker.bundled_executable())
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)
    yield 0.5

    with window_override():
        bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32)
    sphere = bpy.context.view_layer.objects.active
    yield from remesh(addon, sphere, sphere, 2000)

    torus = thin_torus("Thin torus")
    first = yield from remesh(addon, torus, torus, 300)
    # Remesh on a result remeshes its source and hides the old result.
    yield from remesh(addon, first, torus, 400)
    assert not first.visible_get()

    # Cancel from the panel: no result, the remesher stopped.
    getattr(bpy.context.scene, ID).target_quads = 50000
    make_active(sphere)
    before = set(bpy.data.objects)
    press_remesh(addon)
    job = addon._job
    with window_override("UI"):
        assert getattr(bpy.ops.object, ID + "_cancel")() == {"FINISHED"}
    yield from wait(addon)
    assert addon._last_status == "cancelled", (addon._last_status, addon._last_message)
    assert set(bpy.data.objects) == before
    assert job._process is None or job._process.poll() is not None, "the remesher still runs after Cancel"
    assert job._workdir is None or not os.path.exists(job._workdir), job._workdir
    print("blender_operator: OK, Blender %s" % bpy.app.version_string)


steps = script()


def step():
    try:
        return next(steps)
    except StopIteration:
        sys.stdout.flush()
        bpy.ops.wm.quit_blender()
        return None
    except Exception:
        traceback.print_exc()
        sys.stdout.flush()
        os._exit(1)


bpy.app.timers.register(step, first_interval=1.0)
