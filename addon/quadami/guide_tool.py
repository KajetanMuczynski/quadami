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

"""Guide loops in Blender: the Draw Guide tool, Mark/Clear Guide, and the
overlay that shows them (TODO 5.7, docs/GUIDES-AND-DENSITY.md).

A guide is a set of the mesh's own edges, flagged in the boolean edge
attribute guides.GUIDE_ATTRIBUTE, so it is saved with the .blend and survives
remeshing the same object again. Remesh sends every guide edge to the worker
as a marked edge, whatever Follow Marks says; the core keeps a row of quad
edges along it, and a guide drawn as a closed loop comes out as an edge loop
that Alt+click selects whole, unless the field needs a cone on it.

Draw Guide works on the object's own mesh (before modifiers), in Object Mode:
click points on the surface, or drag to draw; the points are joined by the
shortest path along the mesh's edges. Clicking the first point again closes
the loop.
"""

import bpy
import numpy as np
from bpy_extras import view3d_utils
from mathutils import Vector
from mathutils.bvhtree import BVHTree

from . import guides
from .names import ID

# gpu is imported where it draws: a background Blender (the smoke test) has
# no drawing context.

GUIDE = guides.GUIDE_ATTRIBUTE
# Screen distance in pixels: a drag adds a point every this far, and a click
# this close to the first point closes the loop.
DRAG_STEP = 12
CLOSE_RADIUS = 12
LINE_COLOR = (1.0, 0.55, 0.0, 1.0)
DRAFT_COLOR = (0.2, 0.85, 1.0, 1.0)


def _edge_vertices(mesh):
    edges = np.empty(len(mesh.edges) * 2, dtype=np.int32)
    mesh.edges.foreach_get("vertices", edges)
    return edges.reshape(-1, 2)


def guide_flags(mesh):
    """Per-edge guide flags of mesh (bool array), or None if it has none."""
    attribute = mesh.attributes.get(GUIDE)
    if attribute is None or attribute.domain != "EDGE" or attribute.data_type != "BOOLEAN" or not len(mesh.edges):
        return None
    values = np.zeros(len(mesh.edges), dtype=bool)
    attribute.data.foreach_get("value", values)
    return values if values.any() else None


def guide_pairs(mesh):
    """Vertex pairs (G, 2) of mesh's guide edges."""
    flags = guide_flags(mesh)
    if flags is None:
        return np.empty((0, 2), dtype=np.int32)
    return _edge_vertices(mesh)[flags]


def guide_counts(mesh):
    """(closed, open) guides of mesh."""
    flags = guide_flags(mesh)
    if flags is None:
        return 0, 0
    return guides.count_loops(_edge_vertices(mesh), flags)


def set_guide_edges(mesh, edge_indices, value=True):
    """Flag (or clear) the edges with these indices as guide edges."""
    attribute = mesh.attributes.get(GUIDE)
    if attribute is None:
        if not value:
            return
        attribute = mesh.attributes.new(GUIDE, "BOOLEAN", "EDGE")
    values = np.zeros(len(mesh.edges), dtype=bool)
    attribute.data.foreach_get("value", values)
    values[np.asarray(edge_indices, dtype=np.int64)] = value
    attribute = mesh.attributes.get(GUIDE)  # adding a layer can move the others
    attribute.data.foreach_set("value", values)
    mesh.update()
    invalidate_overlay()


def clear_guides(mesh):
    attribute = mesh.attributes.get(GUIDE)
    if attribute is not None:
        mesh.attributes.remove(attribute)
        mesh.update()
    invalidate_overlay()


# --- Overlay ---------------------------------------------------------------

# Object name -> (mesh pointer, edge count, world matrix, line coordinates).
_overlay_cache = {}
_draw_handle = None
# The running Draw Guide operator, whose draft the overlay draws too.
_active_tool = None


@bpy.app.handlers.persistent
def invalidate_overlay(*_args):
    _overlay_cache.clear()


def _guide_lines(obj):
    """World-space coordinates (2 per edge) of obj's guide edges, cached."""
    mesh = obj.data
    key = (mesh.as_pointer(), len(mesh.edges), tuple(map(tuple, obj.matrix_world)))
    cached = _overlay_cache.get(obj.name)
    if cached is not None and cached[0] == key:
        return cached[1]
    pairs = guide_pairs(mesh)
    coords = []
    if len(pairs):
        positions = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", positions)
        positions = _lifted(mesh, positions.reshape(-1, 3))
        matrix = np.array(obj.matrix_world, dtype=np.float64)
        points = positions[pairs.ravel()]
        points = points @ matrix[:3, :3].T + matrix[:3, 3]
        coords = [tuple(p) for p in points]
    _overlay_cache[obj.name] = (key, coords)
    return coords


def _lifted(mesh, positions):
    """Positions moved a little along the vertex normals, so a line on the
    surface is not hidden in it by the depth test."""
    normals = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
    mesh.vertex_normals.foreach_get("vector", normals)
    size = float(np.linalg.norm(positions.max(axis=0) - positions.min(axis=0))) if len(positions) else 0.0
    return positions + normals.reshape(-1, 3) * (0.002 * size)


def _draw_lines(coords, color, width):
    if not coords:
        return
    import gpu
    from gpu_extras.batch import batch_for_shader
    shader = gpu.shader.from_builtin("POLYLINE_UNIFORM_COLOR")
    viewport = gpu.state.viewport_get()
    shader.uniform_float("viewportSize", (viewport[2], viewport[3]))
    shader.uniform_float("lineWidth", width)
    shader.uniform_float("color", color)
    batch_for_shader(shader, "LINES", {"pos": coords}).draw(shader)


def _draw_points(coords, color, size):
    if not coords:
        return
    import gpu
    from gpu_extras.batch import batch_for_shader
    shader = gpu.shader.from_builtin("POINT_UNIFORM_COLOR")
    gpu.state.point_size_set(size)
    shader.uniform_float("color", color)
    batch_for_shader(shader, "POINTS", {"pos": coords}).draw(shader)


def _draw_overlay():
    context = bpy.context
    settings = getattr(context.scene, ID, None)
    obj = context.active_object
    tool = _active_tool
    if tool is None and (settings is None or not settings.show_guides):
        return
    import gpu
    gpu.state.depth_test_set("LESS_EQUAL")
    gpu.state.blend_set("ALPHA")
    try:
        if obj is not None and obj.type == "MESH" and obj.mode == "OBJECT":
            _draw_lines(_guide_lines(obj), LINE_COLOR, 3.0)
        if tool is not None:
            _draw_lines(tool.draft_lines(), DRAFT_COLOR, 3.0)
            _draw_points(tool.draft_points(), DRAFT_COLOR, 8.0)
    finally:
        gpu.state.depth_test_set("NONE")
        gpu.state.blend_set("NONE")


# --- Draw Guide -------------------------------------------------------------

class OBJECT_OT_quadami_draw_guide(bpy.types.Operator):
    """Draw a guide loop on the surface: the quads keep an edge loop along it.
    Click points or drag; click the first point to close the loop"""
    bl_idname = "object.%s_draw_guide" % ID
    bl_label = "Draw Guide"
    bl_options = {"REGISTER", "UNDO"}

    HINT = ("Draw Guide: click or drag on the surface | click the first point: close the loop | "
            "Enter: keep it open | Backspace: undo a point | Esc or right click: cancel")

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return (obj is not None and obj.type == "MESH" and obj.mode == "OBJECT"
                and len(obj.data.polygons) > 0 and _active_tool is None)

    def invoke(self, context, event):
        global _active_tool
        if context.area is None or context.area.type != "VIEW_3D":
            self.report({"ERROR"}, "Draw Guide works in the 3D Viewport")
            return {"CANCELLED"}
        obj = context.active_object
        mesh = obj.data
        positions = np.empty(len(mesh.vertices) * 3, dtype=np.float64)
        mesh.vertices.foreach_get("co", positions)
        self._positions = positions.reshape(-1, 3)
        self._edges = _edge_vertices(mesh)
        self._graph = guides.EdgeGraph(self._positions, self._edges)
        mesh.calc_loop_triangles()
        triangles = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int32)
        mesh.loop_triangles.foreach_get("vertices", triangles)
        self._triangles = triangles.reshape(-1, 3)
        self._tree = BVHTree.FromPolygons(self._positions.tolist(), self._triangles.tolist(), all_triangles=True)
        self._matrix = obj.matrix_world.copy()
        self._inverse = self._matrix.inverted()
        self._object = obj
        self._lifted = _lifted(mesh, self._positions)
        self._points = []      # vertex indices, in order
        self._legs = []        # edge indices of the path from each point to the next
        self._last_mouse = None
        self._dragging = False
        _active_tool = self
        context.window_manager.modal_handler_add(self)
        context.area.header_text_set(self.HINT)
        context.area.tag_redraw()
        return {"RUNNING_MODAL"}

    # Overlay data, world space.
    def _world(self, vertices):
        matrix = np.array(self._matrix, dtype=np.float64)
        points = self._lifted[np.asarray(vertices, dtype=np.int64)]
        return [tuple(p) for p in points @ matrix[:3, :3].T + matrix[:3, 3]]

    def draft_points(self):
        return self._world(self._points) if self._points else []

    def draft_lines(self):
        edges = [e for leg in self._legs for e in leg]
        if not edges:
            return []
        return self._world(self._edges[np.asarray(edges, dtype=np.int64)].ravel())

    def _vertex_under(self, context, event):
        """The mesh vertex nearest to the surface point under the mouse, or None."""
        region, view = context.region, context.region_data
        if region is None or view is None:
            return None
        coord = (event.mouse_region_x, event.mouse_region_y)
        origin = view3d_utils.region_2d_to_origin_3d(region, view, coord)
        direction = view3d_utils.region_2d_to_vector_3d(region, view, coord)
        local_origin = self._inverse @ origin
        local_direction = (self._inverse.to_3x3() @ direction).normalized()
        hit, _normal, index, _distance = self._tree.ray_cast(local_origin, local_direction)
        if hit is None or index is None:
            return None
        corners = self._triangles[index]
        distances = np.linalg.norm(self._positions[corners] - np.array(hit), axis=1)
        return int(corners[int(np.argmin(distances))])

    @staticmethod
    def _over_controls(context, event):
        """Whether the mouse is outside the viewport's drawing area (over the
        sidebar, the header) or over the navigation gizmo and its buttons
        at the top right, which the user clicks to turn the view."""
        region = context.region
        x, y = event.mouse_region_x, event.mouse_region_y
        if region is None or not (0 <= x < region.width and 0 <= y < region.height):
            return True
        space = context.space_data
        if space is None or not (space.show_gizmo and space.show_gizmo_navigate):
            return False
        preferences = context.preferences
        scale = preferences.system.ui_scale
        size = getattr(preferences.view, "gizmo_size_navigate_v3d", 80) * scale
        # The gizmo, then a column of four buttons below it.
        return x >= region.width - size - 20 * scale and y >= region.height - size - 4 * 30 * scale

    def _screen(self, context, vertex):
        point = self._matrix @ Vector(self._positions[vertex])
        return view3d_utils.location_3d_to_region_2d(context.region, context.region_data, point)

    def _add_point(self, vertex):
        if self._points and self._points[-1] == vertex:
            return True
        if self._points:
            leg = self._graph.shortest_path(self._points[-1], vertex)
            if leg is None:
                return False
            self._legs.append(leg)
        self._points.append(vertex)
        return True

    def _near_first(self, context, event):
        if len(self._points) < 3:
            return False
        first = self._screen(context, self._points[0])
        return first is not None and (first - Vector((event.mouse_region_x, event.mouse_region_y))).length <= CLOSE_RADIUS

    def _finish(self, context, closed):
        if closed:
            leg = self._graph.shortest_path(self._points[-1], self._points[0])
            if leg is None:
                self.report({"ERROR"}, "Cannot close the guide: its ends are on different parts of the mesh")
                return self._cancel(context)
            self._legs.append(leg)
        edges = guides.join_legs(self._legs)
        self._end(context)
        if not edges:
            self.report({"WARNING"}, "The guide has no edges")
            return {"CANCELLED"}
        set_guide_edges(self._object.data, edges)
        closed_count, open_count = guide_counts(self._object.data)
        self.report({"INFO"}, "Guide added (%d edges); %d closed, %d open guides on %s"
                    % (len(edges), closed_count, open_count, self._object.name))
        return {"FINISHED"}

    def _cancel(self, context):
        self._end(context)
        return {"CANCELLED"}

    def _end(self, context):
        global _active_tool
        _active_tool = None
        if context.area is not None:
            context.area.header_text_set(None)
            context.area.tag_redraw()

    def modal(self, context, event):
        try:
            gone = self._object.name not in context.view_layer.objects or self._object.mode != "OBJECT"
        except ReferenceError:  # the object was deleted
            gone = True
        if context.area is None or gone:
            return self._cancel(context)
        context.area.tag_redraw()
        if event.type in {"ESC", "RIGHTMOUSE"} and event.value == "PRESS":
            return self._cancel(context)
        if event.type in {"RET", "NUMPAD_ENTER"} and event.value == "PRESS":
            if len(self._points) >= 2:
                return self._finish(context, closed=False)
            return self._cancel(context)
        if (event.type == "BACK_SPACE" or (event.type == "Z" and event.ctrl)) and event.value == "PRESS":
            if self._points:
                self._points.pop()
                if self._legs:
                    self._legs.pop()
            return {"RUNNING_MODAL"}
        if event.type == "LEFTMOUSE":
            if event.value == "RELEASE":
                self._dragging = False
                return {"RUNNING_MODAL"}
            if event.value != "PRESS" or event.alt or self._over_controls(context, event):
                return {"PASS_THROUGH"}
            if self._near_first(context, event):
                return self._finish(context, closed=True)
            vertex = self._vertex_under(context, event)
            if vertex is None:
                # Off the mesh: the navigation gizmo, the header, empty space.
                return {"PASS_THROUGH"}
            if not self._add_point(vertex):
                self.report({"WARNING"}, "That point is on another part of the mesh")
                return {"RUNNING_MODAL"}
            self._dragging = True
            self._last_mouse = Vector((event.mouse_region_x, event.mouse_region_y))
            return {"RUNNING_MODAL"}
        if event.type == "MOUSEMOVE" and self._dragging:
            mouse = Vector((event.mouse_region_x, event.mouse_region_y))
            if self._last_mouse is None or (mouse - self._last_mouse).length >= DRAG_STEP:
                vertex = self._vertex_under(context, event)
                if vertex is not None and self._add_point(vertex):
                    self._last_mouse = mouse
            return {"RUNNING_MODAL"}
        # Navigation (middle mouse, wheel, trackpad, the gizmo) and the rest.
        return {"PASS_THROUGH"}


class OBJECT_OT_quadami_mark_guide(bpy.types.Operator):
    """Mark the selected edges as a guide (or clear them): the quads keep an edge loop along it"""
    bl_idname = "object.%s_mark_guide" % ID
    bl_label = "Mark Guide"
    bl_options = {"REGISTER", "UNDO"}

    clear: bpy.props.BoolProperty(name="Clear", default=False)

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj is not None and obj.type == "MESH" and obj.mode == "EDIT"

    def execute(self, context):
        obj = context.active_object
        # Edit Mode keeps its own copy of the mesh; write through Object Mode.
        bpy.ops.object.mode_set(mode="OBJECT")
        try:
            mesh = obj.data
            selected = np.zeros(len(mesh.edges), dtype=bool)
            mesh.edges.foreach_get("select", selected)
            if not selected.any():
                self.report({"WARNING"}, "No edges selected")
                return {"CANCELLED"}
            set_guide_edges(mesh, np.nonzero(selected)[0], value=not self.clear)
        finally:
            bpy.ops.object.mode_set(mode="EDIT")
        return {"FINISHED"}


class OBJECT_OT_quadami_clear_guides(bpy.types.Operator):
    """Remove every guide of the active object"""
    bl_idname = "object.%s_clear_guides" % ID
    bl_label = "Clear Guides"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj is not None and obj.type == "MESH" and obj.mode == "OBJECT" \
            and obj.data.attributes.get(GUIDE) is not None

    def execute(self, context):
        clear_guides(context.active_object.data)
        return {"FINISHED"}


def draw_panel(layout, context, enabled):
    """The panel's Guides section."""
    obj = context.active_object
    settings = getattr(context.scene, ID)
    column = layout.column(align=True)
    column.enabled = enabled
    row = column.row(align=True)
    row.label(text="Guides")
    row.prop(settings, "show_guides", text="", icon="HIDE_OFF" if settings.show_guides else "HIDE_ON")
    if obj is None or obj.type != "MESH":
        return
    if obj.mode == "EDIT":
        row = column.row(align=True)
        row.operator(OBJECT_OT_quadami_mark_guide.bl_idname, text="Mark Guide")
        row.operator(OBJECT_OT_quadami_mark_guide.bl_idname, text="Clear Guide").clear = True
        return
    row = column.row(align=True)
    row.operator(OBJECT_OT_quadami_draw_guide.bl_idname, icon="GREASEPENCIL")
    row.operator(OBJECT_OT_quadami_clear_guides.bl_idname, text="", icon="X")
    closed, open_ = guide_counts(obj.data)
    if closed or open_:
        parts = []
        if closed:
            parts.append("%d loop%s" % (closed, "" if closed == 1 else "s"))
        if open_:
            parts.append("%d open" % open_)
        column.label(text="Guides: " + ", ".join(parts))


classes = (
    OBJECT_OT_quadami_draw_guide,
    OBJECT_OT_quadami_mark_guide,
    OBJECT_OT_quadami_clear_guides,
)


def register():
    global _draw_handle
    for cls in classes:
        bpy.utils.register_class(cls)
    _draw_handle = bpy.types.SpaceView3D.draw_handler_add(_draw_overlay, (), "WINDOW", "POST_VIEW")
    bpy.app.handlers.depsgraph_update_post.append(invalidate_overlay)
    bpy.app.handlers.load_post.append(invalidate_overlay)


def unregister():
    global _draw_handle, _active_tool
    for handlers in (bpy.app.handlers.depsgraph_update_post, bpy.app.handlers.load_post):
        if invalidate_overlay in handlers:
            handlers.remove(invalidate_overlay)
    if _draw_handle is not None:
        bpy.types.SpaceView3D.draw_handler_remove(_draw_handle, "WINDOW")
        _draw_handle = None
    _active_tool = None
    _overlay_cache.clear()
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
