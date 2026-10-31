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

"""Density preview: the planned quad size drawn over the mesh (docs/PREVIEW.md).

The worker plans the run once (autoremesher-cli --plan-only, a few percent
of a run): the quad edge each vertex would get and each part's quads.  The
plan, the mesh arrays it was made from and a GPU batch stay in memory here.
Changing the preview count only changes one number the shader reads: every
part's quads go as max(count * area share, floor), its edges with one over
their square root, so a swipe is a redraw and nothing is recomputed.

The plan is dropped when the mesh's geometry changes (edit, modifiers), and
not drawn while a setting it depends on (PLAN_SETTINGS) differs from the
one it was made with or the object is gone.
"""

import math

import numpy as np

# Settings that change the plan beyond the count; a change asks for a new plan.
PLAN_SETTINGS = ("adaptivity", "sharp_edge_degrees", "resolution", "edge_scaling",
                 "smooth_normal_degrees", "follow_marks")

# The preview's range around the count it was planned at: the swipe goes
# from count / RANGE to count * RANGE.  Within it the rescaled plan agrees
# with a fresh plan at that count (docs/PREVIEW.md §4.4).
RANGE = 1.25

# The overlay is pushed this many planned quad edges towards the eye, so it
# wins against its own mesh and a result that lies within a fraction of a
# quad of it, and still hides behind anything farther in front.
PUSH_EDGES = 0.25


def swipe_quads(planned, swipe):
    """Target Quads the preview shows for a swipe in [-1, 1]."""
    return max(1, int(round(planned * RANGE ** max(-1.0, min(1.0, swipe)))))


def settings_key(settings):
    values = []
    for name in PLAN_SETTINGS:
        value = getattr(settings, name)
        values.append(tuple(sorted(value)) if isinstance(value, (set, frozenset)) else value)
    return tuple(values)


def vertex_attributes(plan):
    """Per-vertex arrays for the shader: a = edge * sqrt(quads at the plan's
    count), the part's area share and floor (both over edge_scaling^2), so
    edge(N) = a / sqrt(max(N * share, floor)).  Left-out parts get a = 0."""
    scale2 = plan.edge_scaling ** 2
    q0 = plan.island_quads(plan.quads)
    a = plan.edge.astype(np.float64) * np.sqrt(q0[plan.island])
    share = (plan.share / scale2)[plan.island]
    floor = (plan.floor / scale2)[plan.island]
    a[plan.left_out[plan.island]] = 0.0
    return a.astype(np.float32), share.astype(np.float32), floor.astype(np.float32)


def shader_edges(a, share, floor, quads):
    """What the vertex shader computes, for tests and the legend."""
    q = np.maximum(quads * share.astype(np.float64), floor)
    with np.errstate(divide="ignore", invalid="ignore"):
        return np.where((a > 0) & (q > 0), a / np.sqrt(q), 0.0)


def reference_edge(plan):
    """The median planned edge at the plan's count: the colour scale's middle."""
    kept = plan.edge[plan.edge > 0]
    return float(np.median(kept)) if kept.size else 1.0


class Preview:
    """One planned object."""

    def __init__(self, object_name, key, plan, positions, triangles, report):
        self.object_name = object_name
        self.key = key
        self.plan = plan
        self.positions = np.ascontiguousarray(positions, dtype=np.float32)
        self.triangles = np.ascontiguousarray(triangles, dtype=np.int32)
        self.report = report
        self.reference = reference_edge(plan)
        self.batch = None

    def edge_range(self, quads):
        """(smallest, median, largest) planned edge at quads, left-out parts not counted."""
        edges = self.plan.edge_at(quads)
        kept = edges[edges > 0]
        if not kept.size:
            return 0.0, 0.0, 0.0
        low, mid, high = np.percentile(kept, (2, 50, 98))
        return float(low), float(mid), float(high)


# The preview on show (at most one), and the draw handler.
_preview = None
_handler = None
_shader = None


def current():
    return _preview


def clear():
    global _preview
    _preview = None
    _remove_handler()
    _redraw_all()


def show(preview):
    global _preview
    _preview = preview
    _add_handler()
    _redraw_all()


def geometry_changed(depsgraph):
    """depsgraph_update_post: drop the plan once its object's mesh changed."""
    if _preview is None:
        return
    for update in depsgraph.updates:
        obj = getattr(update.id, "original", update.id)
        if getattr(obj, "name", None) == _preview.object_name and update.is_updated_geometry:
            clear()
            return


def _redraw_all():
    import bpy
    for window in getattr(bpy.context.window_manager, "windows", []):
        for area in window.screen.areas:
            if area.type == "VIEW_3D":
                area.tag_redraw()


def _add_handler():
    global _handler
    if _handler is None:
        import bpy
        _handler = bpy.types.SpaceView3D.draw_handler_add(_draw, (), "WINDOW", "POST_VIEW")


def _remove_handler():
    global _handler
    if _handler is not None:
        import bpy
        bpy.types.SpaceView3D.draw_handler_remove(_handler, "WINDOW")
        _handler = None


VERTEX_SOURCE = """
void main()
{
    float q = max(quads * share, floorq);
    float e = (a > 0.0 && q > 0.0) ? a / sqrt(q) : 0.0;
    edge = e;
    objectPos = pos;
    vec4 v = modelView * vec4(pos, 1.0);
    float d = push * e;
    if (perspective > 0.5) {
        v.xyz *= max(0.0, 1.0 - d / max(length(v.xyz), 1e-12));
    }
    else {
        v.z += d;
    }
    viewPos = v.xyz;
    gl_Position = projection * v;
}
"""

# Smaller quads than the plan's median towards orange, larger towards blue,
# over two octaves; parts with no quads grey.  Cells: a 3D checker of the
# planned edge (in quarter-octave steps), so one cell is about one quad.
FRAGMENT_SOURCE = """
void main()
{
    vec3 n = normalize(cross(dFdx(viewPos), dFdy(viewPos)));
    float light = 0.45 + 0.55 * abs(n.z);
    vec3 colour = vec3(0.55);
    if (edge > 0.0) {
        float t = clamp(log2(edge / reference) * 0.5, -1.0, 1.0);
        vec3 middle = vec3(0.93, 0.91, 0.86);
        colour = t < 0.0 ? mix(middle, vec3(0.92, 0.36, 0.10), -t) : mix(middle, vec3(0.18, 0.42, 0.85), t);
        if (cells > 0.5) {
            float size = exp2(floor(log2(edge) * 4.0 + 0.5) * 0.25);
            vec3 c = floor(objectPos / size);
            colour *= mix(1.0, 0.8, mod(c.x + c.y + c.z, 2.0));
        }
    }
    fragColor = vec4(colour * light, 1.0);
}
"""


def _get_shader():
    global _shader
    if _shader is None:
        import gpu
        interface = gpu.types.GPUStageInterfaceInfo(NAME_PREFIX + "_density")
        interface.smooth("FLOAT", "edge")
        interface.smooth("VEC3", "objectPos")
        interface.smooth("VEC3", "viewPos")
        info = gpu.types.GPUShaderCreateInfo()
        info.push_constant("MAT4", "modelView")
        info.push_constant("MAT4", "projection")
        info.push_constant("FLOAT", "quads")
        info.push_constant("FLOAT", "reference")
        info.push_constant("FLOAT", "push")
        info.push_constant("FLOAT", "perspective")
        info.push_constant("FLOAT", "cells")
        info.vertex_in(0, "VEC3", "pos")
        info.vertex_in(1, "FLOAT", "a")
        info.vertex_in(2, "FLOAT", "share")
        info.vertex_in(3, "FLOAT", "floorq")
        info.vertex_out(interface)
        info.fragment_out(0, "VEC4", "fragColor")
        info.vertex_source(VERTEX_SOURCE)
        info.fragment_source(FRAGMENT_SOURCE)
        _shader = gpu.shader.create_from_info(info)
    return _shader


NAME_PREFIX = "quadami"
# Set by the add-on: () -> (preview count, show cells) from the scene's settings,
# or None when the preview should not be drawn now.
draw_state = None


def _draw():
    import bpy
    import gpu
    from gpu_extras.batch import batch_for_shader

    preview = _preview
    if preview is None or draw_state is None:
        return
    state = draw_state()
    if state is None:
        return
    quads, cells = state
    obj = bpy.data.objects.get(preview.object_name)
    rv3d = bpy.context.region_data
    if obj is None or rv3d is None:
        return
    shader = _get_shader()
    if preview.batch is None:
        a, share, floor = vertex_attributes(preview.plan)
        preview.batch = batch_for_shader(shader, "TRIS", {
            "pos": preview.positions, "a": a, "share": share, "floorq": floor},
            indices=preview.triangles)
    matrix = obj.matrix_world
    scale = sum(abs(s) for s in matrix.to_scale()) / 3.0
    gpu.state.depth_test_set("LESS_EQUAL")
    gpu.state.depth_mask_set(True)
    shader.bind()
    shader.uniform_float("modelView", rv3d.view_matrix @ matrix)
    shader.uniform_float("projection", rv3d.window_matrix)
    shader.uniform_float("quads", float(quads))
    shader.uniform_float("reference", preview.reference)
    shader.uniform_float("push", PUSH_EDGES * scale)
    shader.uniform_float("perspective", 1.0 if rv3d.is_perspective else 0.0)
    shader.uniform_float("cells", 1.0 if cells else 0.0)
    preview.batch.draw(shader)
    gpu.state.depth_mask_set(False)
    gpu.state.depth_test_set("NONE")
