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

The worker plans the run (the run up to its continuous cover solve): the
quad edge each vertex would get and each part's quads.  The plan, the mesh
arrays it was made from and a GPU batch stay in memory (plan_store.py keeps
the last few).  Changing the count only changes one number the shader
reads: every part's quads go as max(count * area share, floor), its edges
with one over their square root, so following Target Quads is a redraw and
nothing is recomputed.

Drawn on top of Blender's own shading: thin lines along the integer lines
of the plan's continuous cover (the map the run rounds into its quads,
drawn on the triangle mesh it was solved on), so they run where the quads'
edges will and are one planned quad apart; following Target Quads scales
each part's map by the square root of its quads.  If asked, a faint tint
where the plan puts smaller (orange) or larger (blue) quads than the median.

The map is the one before the rounding: its charts do not meet on whole
quads yet, so lines jump along the seams between them, and the run's
rounding still moves loops and poles.  So the lines show size and
direction; the exact loops and poles appear only in the run.  Not drawn once the object's mesh changed, a plan
setting (PLAN_SETTINGS) differs from the one it was made with, Target
Quads left the plan's band, or the object is gone.
"""

import numpy as np

# Settings that change the plan beyond the count; a change asks for a new plan.
PLAN_SETTINGS = ("adaptivity", "sharp_edge_degrees", "resolution", "anisotropy", "edge_scaling",
                 "smooth_normal_degrees", "follow_marks")

# The preview's range around the count it was planned at, in percent: the
# overlay follows Target Quads from -BAND to +BAND.  +-20 % quads moves the
# quad edge by -9 to +12 %; a region of the plan is about 4 % off the run there (about 3 % at
# the planned count, close to the 2-5 % two runs of the same mesh differ by),
# so the difference shows.  Farther out the rescaled plan drifts from a
# fresh one (about 6 % at 2x), so the panel asks for a new preview
# (docs/PREVIEW.md §4.5).
BAND = 20.0

# The overlay is pushed this many planned quad edges towards the eye, so it
# wins against its own mesh and a result that lies within a fraction of a
# quad of it, and still hides behind anything farther in front.
PUSH_EDGES = 0.25


def in_band(planned, quads):
    """Whether Target Quads `quads` lies within the preview's band around `planned`."""
    return planned * (1.0 - BAND / 100.0) - 0.5 <= quads <= planned * (1.0 + BAND / 100.0) + 0.5


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


def cover_attributes(plan):
    """Per corner of the plan's cover (three per triangle, not shared): pos,
    uv, and its island's share and floor (over edge_scaling^2) and quads at
    the plan's count, q0, so the map at N quads is uv * sqrt(max(N * share,
    floor) / q0).  Triangles of islands without quads are left out.  None
    without a cover."""
    cover = plan.cover
    if cover is None:
        return None
    scale2 = plan.edge_scaling ** 2
    q0 = plan.island_quads(plan.quads)
    keep = q0[cover.island] > 0
    island = np.repeat(cover.island[keep], 3)
    return {
        "pos": np.ascontiguousarray(cover.positions[cover.triangles[keep]].reshape(-1, 3), dtype=np.float32),
        "uv": np.ascontiguousarray(cover.uvs[keep].reshape(-1, 2), dtype=np.float32),
        "share": (plan.share / scale2)[island].astype(np.float32),
        "floorq": (plan.floor / scale2)[island].astype(np.float32),
        "q0": q0[island].astype(np.float32),
    }


def cover_scale(share, floor, q0, quads):
    """What the grid's vertex shader computes: the map's factor at `quads`."""
    q = np.maximum(quads * share.astype(np.float64), floor)
    with np.errstate(divide="ignore", invalid="ignore"):
        return np.where(q0 > 0, np.sqrt(q / q0), 0.0)


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

    def __init__(self, object_name, key, plan, positions, triangles, report, guide_pairs=None):
        self.object_name = object_name
        self.key = key
        self.plan = plan
        self.positions = np.ascontiguousarray(positions, dtype=np.float32)
        self.triangles = np.ascontiguousarray(triangles, dtype=np.int32)
        self.report = report
        self.reference = reference_edge(plan)
        # Vertex pairs of the guide edges (guide_tool.py), for guide_loops().
        self.guide_pairs = np.zeros((0, 2), np.int64) if guide_pairs is None else np.asarray(guide_pairs, np.int64)
        self.batch = None
        self.cover_batch = None
        self._median = (None, 0.0)

    def edge_range(self, quads):
        """(smallest, median, largest) planned edge at quads, left-out parts not counted."""
        edges = self.plan.edge_at(quads)
        kept = edges[edges > 0]
        if not kept.size:
            return 0.0, 0.0, 0.0
        low, mid, high = np.percentile(kept, (2, 50, 98))
        return float(low), float(mid), float(high)

    def median_edge(self, quads):
        """The median planned edge at quads (the grid's and tint's middle); cached per count."""
        if self._median[0] != quads:
            edges = self.plan.edge_at(quads)
            kept = edges[edges > 0]
            self._median = (quads, float(np.median(kept)) if kept.size else self.reference)
        return self._median[1]

    def guide_loops(self, quads):
        """[(closed, quads along)] for each connected run of guide edges, the
        longest first: its length over the planned edge along it at quads."""
        return guide_loops(self.positions, self.guide_pairs, self.plan.edge_at(quads))


def guide_loops(positions, pairs, edges):
    """[(closed, quads along)] of each connected chain of the edges `pairs`
    over a mesh with planned edge `edges` per vertex: sum of edge length
    over the mean planned edge of its two ends.  Closed: every vertex of the
    chain has two of its edges.  Longest first."""
    pairs = np.asarray(pairs, np.int64).reshape(-1, 2)
    if not len(pairs):
        return []
    vertices, inverse = np.unique(pairs, return_inverse=True)
    inverse = inverse.reshape(-1, 2)
    parent = np.arange(len(vertices))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for a, b in inverse:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
    roots = np.array([find(i) for i in range(len(vertices))])
    degree = np.bincount(inverse.ravel(), minlength=len(vertices))
    p = np.asarray(positions, np.float64)
    length = np.linalg.norm(p[pairs[:, 0]] - p[pairs[:, 1]], axis=1)
    planned = 0.5 * (np.asarray(edges, np.float64)[pairs[:, 0]] + np.asarray(edges, np.float64)[pairs[:, 1]])
    with np.errstate(divide="ignore", invalid="ignore"):
        steps = np.where(planned > 0, length / planned, 0.0)
    chain = roots[inverse[:, 0]]
    loops = []
    for root in np.unique(roots):
        members = roots == root
        loops.append((bool(np.all(degree[members] == 2)), float(steps[chain == root].sum())))
    loops.sort(key=lambda loop: -loop[1])
    return loops


# The preview on show (at most one), and the draw handler.
_preview = None
_handler = None
_shaders = None


def current():
    return _preview


def hide():
    """Stops drawing; the plan stays in plan_store for the next show()."""
    global _preview
    _preview = None
    _remove_handler()
    _redraw_all()


def show(preview):
    global _preview
    _preview = preview
    _add_handler()
    _redraw_all()


def geometry_changed(_scene, depsgraph):
    """depsgraph_update_post: stop drawing the plan once its object's mesh changed."""
    if _preview is None or depsgraph is None:
        return
    for update in depsgraph.updates:
        obj = getattr(update.id, "original", update.id)
        if getattr(obj, "name", None) == _preview.object_name and update.is_updated_geometry:
            hide()
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


# Push constants stay within the 128 bytes every GPU backend has: the
# object-to-clip matrix, the eye in object space (w = 1: a position, w = 0:
# the direction towards the viewer of an orthographic view) and four numbers,
# settings: x the preview count, y the median edge at that count, z
# PUSH_EDGES, w 1 for the grid and 2 for the tint.
TINT_VERTEX_SOURCE = """
void main()
{
    float q = max(settings.x * share, floorq);
    float e = (a > 0.0 && q > 0.0) ? a / sqrt(q) : 0.0;
    edge = e;
    vec3 toEye = eye.w > 0.5 ? normalize(eye.xyz - pos) : eye.xyz;
    gl_Position = modelViewProjection * vec4(pos + toEye * (settings.z * e), 1.0);
}
"""

# On the input mesh: with the tint, towards orange where quads are smaller
# than the median, blue where larger, over two octaves, faint.  Parts with
# no quads are greyed in either mode.
TINT_FRAGMENT_SOURCE = """
void main()
{
    if (edge <= 0.0) {
        fragColor = vec4(0.5, 0.5, 0.5, 0.45);
        return;
    }
    if (settings.w < 1.5)
        discard;
    float t = clamp(log2(edge / settings.y) * 0.5, -1.0, 1.0);
    vec3 tint = t < 0.0 ? vec3(0.95, 0.42, 0.10) : vec3(0.18, 0.45, 0.95);
    fragColor = vec4(tint, 0.45 * abs(t));
}
"""

GRID_VERTEX_SOURCE = """
void main()
{
    float q = max(settings.x * share, floorq);
    coverUv = uv * (q0 > 0.0 ? sqrt(q / q0) : 0.0);
    vec3 toEye = eye.w > 0.5 ? normalize(eye.xyz - pos) : eye.xyz;
    gl_Position = modelViewProjection * vec4(pos + toEye * (settings.z * settings.y), 1.0);
}
"""

# On the plan's cover mesh: the integer lines of u and v, about a pixel
# wide, faded where they would come closer than about three pixels.
GRID_FRAGMENT_SOURCE = """
void main()
{
    vec2 w = max(fwidth(coverUv), vec2(1e-6));
    vec2 d = abs(fract(coverUv - 0.5) - 0.5) / w;
    vec2 lines = (1.0 - clamp(d - 0.6, 0.0, 1.0)) * (1.0 - smoothstep(0.18, 0.35, w));
    float l = max(lines.x, lines.y);
    if (l <= 0.0)
        discard;
    fragColor = vec4(0.06, 0.06, 0.07, 0.85 * l);
}
"""


def _create_shader(name, inputs, outputs, vertex_source, fragment_source):
    import gpu
    interface = gpu.types.GPUStageInterfaceInfo(NAME_PREFIX + "_" + name)
    for kind, out in outputs:
        interface.smooth(kind, out)
    info = gpu.types.GPUShaderCreateInfo()
    info.push_constant("MAT4", "modelViewProjection")
    info.push_constant("VEC4", "eye")
    info.push_constant("VEC4", "settings")
    for slot, (kind, attribute) in enumerate(inputs):
        info.vertex_in(slot, kind, attribute)
    info.vertex_out(interface)
    info.fragment_out(0, "VEC4", "fragColor")
    info.vertex_source(vertex_source)
    info.fragment_source(fragment_source)
    return gpu.shader.create_from_info(info)


def _get_shaders():
    global _shaders
    if _shaders is None:
        _shaders = (
            _create_shader("density_tint", (("VEC3", "pos"), ("FLOAT", "a"), ("FLOAT", "share"), ("FLOAT", "floorq")),
                           (("FLOAT", "edge"),), TINT_VERTEX_SOURCE, TINT_FRAGMENT_SOURCE),
            _create_shader("density_grid", (("VEC3", "pos"), ("VEC2", "uv"), ("FLOAT", "share"), ("FLOAT", "floorq"),
                                            ("FLOAT", "q0")),
                           (("VEC2", "coverUv"),), GRID_VERTEX_SOURCE, GRID_FRAGMENT_SOURCE),
        )
    return _shaders


NAME_PREFIX = "quadami"
# Set by the add-on: () -> (preview count, grid on, tint on) from the scene's
# settings, or None when the preview should not be drawn now.
draw_state = None


def _draw():
    import bpy

    preview = _preview
    if preview is None or draw_state is None:
        return
    state = draw_state()
    if state is None:
        return
    obj = bpy.data.objects.get(preview.object_name)
    rv3d = bpy.context.region_data
    if obj is None or rv3d is None:
        return
    quads, grid, tint = state
    draw_preview(preview, obj.matrix_world, rv3d.view_matrix, rv3d.window_matrix, rv3d.is_perspective,
                 quads, grid, tint)


def draw_preview(preview, matrix, view_matrix, window_matrix, perspective, quads, grid=True, tint=False):
    """Draws the preview's mesh at matrix (object to world) with the planned
    quad size at `quads`, over whatever the bound framebuffer holds."""
    import gpu
    from gpu_extras.batch import batch_for_shader
    from mathutils import Vector

    if not (grid or tint):
        return
    tint_shader, grid_shader = _get_shaders()
    if preview.batch is None:
        a, share, floor = vertex_attributes(preview.plan)
        preview.batch = batch_for_shader(tint_shader, "TRIS", {
            "pos": preview.positions, "a": a, "share": share, "floorq": floor},
            indices=preview.triangles)
    if grid and preview.cover_batch is None and preview.plan.cover is not None:
        preview.cover_batch = batch_for_shader(grid_shader, "TRIS", cover_attributes(preview.plan))
    inverse = matrix.inverted_safe()
    view_inverse = view_matrix.inverted_safe()
    if perspective:
        eye = inverse @ view_inverse.translation
        eye = (eye.x, eye.y, eye.z, 1.0)
    else:
        towards = (inverse.to_3x3() @ (view_inverse.to_3x3() @ Vector((0.0, 0.0, 1.0)))).normalized()
        eye = (towards.x, towards.y, towards.z, 0.0)
    gpu.state.depth_test_set("LESS_EQUAL")
    gpu.state.depth_mask_set(False)
    gpu.state.blend_set("ALPHA")
    settings = (float(quads), preview.median_edge(quads), PUSH_EDGES,
                (1.0 if grid else 0.0) + (2.0 if tint else 0.0))
    batches = [(tint_shader, preview.batch)]
    if grid and preview.cover_batch is not None:
        batches.append((grid_shader, preview.cover_batch))
    for shader, batch in batches:
        shader.bind()
        shader.uniform_float("modelViewProjection", window_matrix @ view_matrix @ matrix)
        shader.uniform_float("eye", eye)
        shader.uniform_float("settings", settings)
        batch.draw(shader)
    gpu.state.blend_set("NONE")
    gpu.state.depth_test_set("NONE")
