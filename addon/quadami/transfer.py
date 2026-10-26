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

"""Copy surface data from the source object onto a remesh result.

Everything is looked up on the evaluated source (what was remeshed), in
object space, which the result shares: materials per face from the nearest
source face; vertex groups and colour attributes interpolated over the
nearest source face (Blender's Data Transfer, run as a modifier and baked);
sharp, seam and crease marks as connected edge paths along the marked
lines; UVs from the one UV island under each face.

The quads do not follow material borders, seams or marked edges (TODO 5.4),
so a material border zigzags along the quads, marks run as staircases where
the quads cross the line, and faces that cross a UV seam reach past the
island's border. UV faces distorted by more than 4x in area against the
source face under them, at 5000 quads (Smart UV Project on five public
meshes, a cylinder and a UV sphere with their own maps): 0-5.6 % here,
2-14 % with Blender's Data Transfer when the source's seams are marked
(taking 7-27 s instead of under 0.5 s) and 2-56 % when they are not.
"""

import bpy
import numpy as np
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree


def _copy_materials(depsgraph, source, mesh, face_centers):
    """Material slots, and each face's index from the nearest source face."""
    materials = [slot.material for slot in source.material_slots]
    for material in materials:
        mesh.materials.append(material)
    if len(materials) < 2:
        return
    evaluated = source.evaluated_get(depsgraph)
    source_mesh = evaluated.to_mesh()
    try:
        source_indices = np.empty(len(source_mesh.polygons), dtype=np.int32)
        source_mesh.polygons.foreach_get("material_index", source_indices)
    finally:
        evaluated.to_mesh_clear()
    # Returns source polygon indices, in object space with modifiers applied.
    tree = BVHTree.FromObject(source, depsgraph)
    indices = np.zeros(len(face_centers), dtype=np.int32)
    for i, center in enumerate(face_centers.tolist()):
        index = tree.find_nearest(center)[2]
        if index is not None:
            indices[i] = source_indices[index]
    mesh.polygons.foreach_set("material_index", np.minimum(indices, len(materials) - 1))


def _data_transfer(context, source, result):
    """Vertex groups and colour attributes through a Data Transfer modifier
    on the result, evaluated and baked into its mesh. Returns
    (vertex groups, colour attributes) copied."""
    mesh = result.data
    groups = [group.name for group in source.vertex_groups]
    for name in groups:
        result.vertex_groups.new(name=name)
    source_mesh = source.data
    point_colors = [(a.name, a.data_type) for a in source_mesh.color_attributes if a.domain == "POINT"]
    corner_colors = [(a.name, a.data_type) for a in source_mesh.color_attributes if a.domain == "CORNER"]
    for name, data_type in point_colors:
        mesh.color_attributes.new(name, data_type, "POINT")
    for name, data_type in corner_colors:
        mesh.color_attributes.new(name, data_type, "CORNER")
    verts = {"VGROUP_WEIGHTS"} if groups else set()
    verts |= {"COLOR_VERTEX"} if point_colors else set()
    loops = {"COLOR_CORNER"} if corner_colors else set()
    if not verts and not loops:
        return 0, 0

    modifier = result.modifiers.new("quadami_transfer", "DATA_TRANSFER")
    try:
        modifier.object = source
        # Both meshes are in the same object space (the result's vertices
        # are the remesh of the source's), whatever the transforms do.
        modifier.use_object_transform = False
        if verts:
            modifier.use_vert_data = True
            modifier.data_types_verts = verts
            modifier.vert_mapping = "POLYINTERP_NEAREST"
            modifier.layers_vgroup_select_src = "ALL"
            modifier.layers_vgroup_select_dst = "NAME"
            modifier.layers_vcol_vert_select_src = "ALL"
            modifier.layers_vcol_vert_select_dst = "NAME"
        if loops:
            modifier.use_loop_data = True
            modifier.data_types_loops = loops
            modifier.loop_mapping = "POLYINTERP_NEAREST"
            modifier.layers_vcol_loop_select_src = "ALL"
            modifier.layers_vcol_loop_select_dst = "NAME"
        depsgraph = context.evaluated_depsgraph_get()
        depsgraph.update()
        baked = bpy.data.meshes.new_from_object(result.evaluated_get(depsgraph),
                                                preserve_all_data_layers=True, depsgraph=depsgraph)
    finally:
        result.modifiers.remove(modifier)
    baked.name = mesh.name
    result.data = baked
    bpy.data.meshes.remove(mesh)
    return len(groups), len(point_colors) + len(corner_colors)


# Marked lines are followed on the result by shortest edge paths between
# points every WAYPOINT_SPACING result edges along the line, with edges
# away from the line made dearer, so a mark stays one connected line (a
# seam must, to cut UV islands) even where the quads cross it.
WAYPOINT_SPACING = 2.0
OFF_LINE_COST = 4.0
# Marked lines shorter than this many result edges have no counterpart.
SHORTEST_MARK = 1.0
# A path between two waypoints that needs more steps than this is dropped
# (the waypoints snapped to different parts of the result).
MAX_PATH_VISITS = 400


def _marked_edges(mesh):
    """{kind: per-edge values} for the source's sharp, seam and crease
    marks; 0 is unmarked."""
    marks = {}
    sharp = mesh.attributes.get("sharp_edge")
    if sharp is not None and sharp.domain == "EDGE":
        values = np.zeros(len(mesh.edges), dtype=bool)
        sharp.data.foreach_get("value", values)
        marks["sharp"] = values.astype(np.float32)
    seams = np.zeros(len(mesh.edges), dtype=bool)
    mesh.edges.foreach_get("use_seam", seams)
    marks["seam"] = seams.astype(np.float32)
    crease = mesh.attributes.get("crease_edge")
    if crease is not None and crease.domain == "EDGE":
        values = np.zeros(len(mesh.edges), dtype=np.float32)
        crease.data.foreach_get("value", values)
        marks["crease"] = values
    return {kind: values for kind, values in marks.items() if np.any(values > 0)}


def _chains(edges, marked):
    """The marked edges as polylines of vertex indices, split where lines
    meet or end; a closed loop starts and ends at the same vertex. Each
    comes with the indices of its edges."""
    neighbours = {}
    for e in np.flatnonzero(marked):
        a, b = int(edges[e, 0]), int(edges[e, 1])
        neighbours.setdefault(a, []).append((b, e))
        neighbours.setdefault(b, []).append((a, e))
    used = set()
    chains = []

    def walk(start, link):
        vertices, chain_edges = [start], []
        vertex, e = link
        while True:
            used.add(e)
            chain_edges.append(e)
            vertices.append(vertex)
            if vertex == start or len(neighbours[vertex]) != 2:
                return vertices, chain_edges
            following = [l for l in neighbours[vertex] if l[1] not in used]
            if not following:
                return vertices, chain_edges
            vertex, e = following[0]

    for vertex, links in neighbours.items():
        if len(links) != 2:
            for link in links:
                if link[1] not in used:
                    chains.append(walk(vertex, link))
    for vertex, links in neighbours.items():
        for link in links:
            if link[1] not in used:
                chains.append(walk(vertex, link))
    return chains


def _resample(points, spacing):
    """Points along the polyline at about the given spacing, ends included."""
    lengths = np.linalg.norm(np.diff(points, axis=0), axis=1)
    cumulative = np.concatenate(([0.0], np.cumsum(lengths)))
    count = max(1, int(round(cumulative[-1] / spacing)))
    at = np.linspace(0.0, cumulative[-1], count + 1)
    return np.stack([np.interp(at, cumulative, points[:, k]) for k in range(3)], axis=1), cumulative[-1]


def _shortest_path(adjacency, positions, start, goal, line_distance, scale):
    """Edge path from start to goal, by length plus OFF_LINE_COST times the
    distance of each edge's midpoint from the marked line (in units of the
    result edge length); None if it is not found within MAX_PATH_VISITS."""
    import heapq
    best = {start: 0.0}
    previous = {}
    queue = [(0.0, start)]
    visits = 0
    while queue:
        cost, vertex = heapq.heappop(queue)
        if vertex == goal:
            path = [goal]
            while path[-1] != start:
                path.append(previous[path[-1]])
            return path[::-1]
        if cost > best.get(vertex, np.inf):
            continue
        visits += 1
        if visits > MAX_PATH_VISITS:
            return None
        for other in adjacency[vertex]:
            midpoint = 0.5 * (positions[vertex] + positions[other])
            length = float(np.linalg.norm(positions[other] - positions[vertex]))
            step = length * (1.0 + OFF_LINE_COST * line_distance(midpoint) / scale)
            if cost + step < best.get(other, np.inf):
                best[other] = cost + step
                previous[other] = vertex
                heapq.heappush(queue, (cost + step, other))
    return None


def _copy_edge_marks(depsgraph, source, mesh):
    """Sharp, seam and crease marks: each marked line of the source becomes
    a connected path of result edges along it. Returns {kind: number of
    result edges marked}."""
    evaluated = source.evaluated_get(depsgraph)
    source_mesh = evaluated.to_mesh()
    try:
        marks = _marked_edges(source_mesh)
        if not marks:
            return {}
        source_positions = np.empty(len(source_mesh.vertices) * 3, dtype=np.float64)
        source_mesh.vertices.foreach_get("co", source_positions)
        source_edges = np.empty(len(source_mesh.edges) * 2, dtype=np.int32)
        source_mesh.edges.foreach_get("vertices", source_edges)
    finally:
        evaluated.to_mesh_clear()
    source_positions = source_positions.reshape(-1, 3)
    source_edges = source_edges.reshape(-1, 2)
    positions = np.empty(len(mesh.vertices) * 3, dtype=np.float64)
    mesh.vertices.foreach_get("co", positions)
    positions = positions.reshape(-1, 3)
    edges = np.empty(len(mesh.edges) * 2, dtype=np.int32)
    mesh.edges.foreach_get("vertices", edges)
    edges = edges.reshape(-1, 2)
    scale = float(np.median(np.linalg.norm(positions[edges[:, 1]] - positions[edges[:, 0]], axis=1)))
    adjacency = [[] for _ in range(len(positions))]
    edge_index = {}
    for e, (a, b) in enumerate(edges.tolist()):
        adjacency[a].append(b)
        adjacency[b].append(a)
        edge_index[(a, b) if a < b else (b, a)] = e
    vertex_tree = KDTree(len(positions))
    for i, co in enumerate(positions.tolist()):
        vertex_tree.insert(co, i)
    vertex_tree.balance()

    counts = {}
    for kind, values in marks.items():
        result = np.zeros(len(edges), dtype=np.float32)
        for chain_vertices, chain_edges in _chains(source_edges, values > 0):
            points = source_positions[chain_vertices]
            waypoints, length = _resample(points, WAYPOINT_SPACING * scale)
            if length < SHORTEST_MARK * scale:
                continue
            line = KDTree(len(waypoints) * 8)
            for i, (a, b) in enumerate(zip(waypoints[:-1], waypoints[1:])):
                for t in np.linspace(0.0, 1.0, 8, endpoint=False):
                    line.insert(a + t * (b - a), i)
            line.insert(waypoints[-1], len(waypoints) - 1)
            line.balance()
            value = float(np.max(values[chain_edges]))
            snapped = [vertex_tree.find(w)[1] for w in waypoints]
            for start, goal in zip(snapped[:-1], snapped[1:]):
                if start == goal:
                    continue
                path = _shortest_path(adjacency, positions, start, goal, lambda p: line.find(p)[2], scale)
                for a, b in zip(path or [], (path or [])[1:]):
                    e = edge_index[(a, b) if a < b else (b, a)]
                    result[e] = max(result[e], value)
        counts[kind] = int(np.count_nonzero(result))
        if not counts[kind]:
            continue
        if kind == "seam":
            mesh.edges.foreach_set("use_seam", result > 0)
        elif kind == "sharp":
            attribute = mesh.attributes.get("sharp_edge") or mesh.attributes.new("sharp_edge", "BOOLEAN", "EDGE")
            attribute.data.foreach_set("value", result > 0)
        else:
            attribute = mesh.attributes.get("crease_edge") or mesh.attributes.new("crease_edge", "FLOAT", "EDGE")
            attribute.data.foreach_set("value", result)
    return counts


def _uv_islands(triangle_vertices, triangle_uvs):
    """Island id per source triangle: triangles sharing an edge whose two
    ends have the same UVs on both sides are in one island."""
    count = len(triangle_vertices)
    # Half-edges (lower vertex, higher vertex, triangle, uv at lower, uv at higher).
    first = np.repeat(np.arange(3)[None, :], count, axis=0)
    second = (first + 1) % 3
    v0 = np.take_along_axis(triangle_vertices, first, axis=1).ravel()
    v1 = np.take_along_axis(triangle_vertices, second, axis=1).ravel()
    uv0 = triangle_uvs[np.arange(count)[:, None], first].reshape(-1, 2)
    uv1 = triangle_uvs[np.arange(count)[:, None], second].reshape(-1, 2)
    swap = v0 > v1
    lo, hi = np.where(swap, v1, v0), np.where(swap, v0, v1)
    uv_lo, uv_hi = np.where(swap[:, None], uv1, uv0), np.where(swap[:, None], uv0, uv1)
    triangle = np.repeat(np.arange(count), 3)
    order = np.lexsort((lo, hi))
    lo, hi, triangle, uv_lo, uv_hi = lo[order], hi[order], triangle[order], uv_lo[order], uv_hi[order]
    same = (lo[1:] == lo[:-1]) & (hi[1:] == hi[:-1])
    same &= np.all(np.abs(uv_lo[1:] - uv_lo[:-1]) < 1e-6, axis=1)
    same &= np.all(np.abs(uv_hi[1:] - uv_hi[:-1]) < 1e-6, axis=1)
    pairs = np.flatnonzero(same)
    return _components(count, triangle[pairs], triangle[pairs + 1])


def _components(count, first, second):
    """Connected-component label per node for the edges first[i]-second[i]:
    hook each edge's larger root onto its smaller one, then jump pointers
    until every node points at its root (numpy, no Python loop per edge)."""
    labels = np.arange(count)
    while True:
        a, b = labels[first], labels[second]
        changed = a != b
        if not np.any(changed):
            return labels
        low, high = np.minimum(a[changed], b[changed]), np.maximum(a[changed], b[changed])
        np.minimum.at(labels, high, low)
        while True:
            jumped = labels[labels]
            if np.array_equal(jumped, labels):
                break
            labels = jumped


# A corner farther than this many result edge lengths from its face's
# island lies beyond the island's border.
OFF_ISLAND = 0.05


def _interpolate_uvs(source_positions, triangle_vertices, triangle_uvs, triangles, points):
    """UVs at points by the barycentric coordinates of their projections
    onto the given source triangles (outside a triangle: extended)."""
    a, b, c = (source_positions[triangle_vertices[triangles, k]] for k in range(3))
    v0, v1, v2 = b - a, c - a, points - a
    d00, d01, d11 = (v0 * v0).sum(1), (v0 * v1).sum(1), (v1 * v1).sum(1)
    d20, d21 = (v2 * v0).sum(1), (v2 * v1).sum(1)
    determinant = d00 * d11 - d01 * d01
    determinant = np.where(np.abs(determinant) > 0, determinant, 1.0)
    w1 = (d11 * d20 - d01 * d21) / determinant
    w2 = (d00 * d21 - d01 * d20) / determinant
    uvs = triangle_uvs[triangles]
    return (1.0 - w1 - w2)[:, None] * uvs[:, 0] + w1[:, None] * uvs[:, 1] + w2[:, None] * uvs[:, 2]


def _uv_scale(source_positions, triangle_vertices, triangle_uvs, triangles):
    """UV length per unit of surface length on the given source triangles."""
    a, b, c = (source_positions[triangle_vertices[triangles, k]] for k in range(3))
    area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
    uvs = triangle_uvs[triangles]
    e1, e2 = uvs[:, 1] - uvs[:, 0], uvs[:, 2] - uvs[:, 0]
    uv_area = 0.5 * np.abs(e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0])
    return np.sqrt(uv_area / np.where(area > 0, area, np.inf))


def _copy_uvs(depsgraph, source, mesh):
    """Each UV map of the source. A result face takes its UVs from the one
    source UV island under its centre: each corner from the nearest point
    of that island, or, where that point lies beyond the island's border or
    across a seam inside it, from the centre triangle's UV map extended. So
    no face stretches across UV space between or within islands; a face
    that crosses a seam reaches a little past its island's border in UV
    space (into the margin around it). Returns the names of the maps."""
    evaluated = source.evaluated_get(depsgraph)
    source_mesh = evaluated.to_mesh()
    try:
        names = [layer.name for layer in source_mesh.uv_layers]
        if not names:
            return []
        source_mesh.calc_loop_triangles()
        source_positions = np.empty(len(source_mesh.vertices) * 3, dtype=np.float64)
        source_mesh.vertices.foreach_get("co", source_positions)
        triangle_vertices = np.empty(len(source_mesh.loop_triangles) * 3, dtype=np.int32)
        source_mesh.loop_triangles.foreach_get("vertices", triangle_vertices)
        triangle_loops = np.empty(len(source_mesh.loop_triangles) * 3, dtype=np.int32)
        source_mesh.loop_triangles.foreach_get("loops", triangle_loops)
        loop_uvs = []
        for name in names:
            uvs = np.empty(len(source_mesh.loops) * 2, dtype=np.float64)
            source_mesh.uv_layers[name].data.foreach_get("uv", uvs)
            loop_uvs.append(uvs.reshape(-1, 2))
    finally:
        evaluated.to_mesh_clear()
    source_positions = source_positions.reshape(-1, 3)
    triangle_vertices = triangle_vertices.reshape(-1, 3)
    triangle_loops = triangle_loops.reshape(-1, 3)
    everything = BVHTree.FromPolygons(source_positions.tolist(), triangle_vertices.tolist())

    positions = np.empty(len(mesh.vertices) * 3, dtype=np.float64)
    mesh.vertices.foreach_get("co", positions)
    positions = positions.reshape(-1, 3)
    corner_vertices = np.empty(len(mesh.loops), dtype=np.int32)
    mesh.loops.foreach_get("vertex_index", corner_vertices)
    starts = np.empty(len(mesh.polygons), dtype=np.int32)
    mesh.polygons.foreach_get("loop_start", starts)
    totals = np.empty(len(mesh.polygons), dtype=np.int32)
    mesh.polygons.foreach_get("loop_total", totals)
    centers = np.empty(len(mesh.polygons) * 3, dtype=np.float64)
    mesh.polygons.foreach_get("center", centers)
    under_center = np.array([everything.find_nearest(c)[2] for c in centers.reshape(-1, 3).tolist()])
    corner_face = np.repeat(np.arange(len(starts)), totals)
    following = np.arange(len(corner_vertices)) + 1
    following[starts + totals - 1] = starts
    scale = float(np.median(np.linalg.norm(positions[corner_vertices[following]] - positions[corner_vertices], axis=1)))

    for name, uvs in zip(names, loop_uvs):
        triangle_uvs = uvs[triangle_loops]
        islands = _uv_islands(triangle_vertices, triangle_uvs)
        corner_island = islands[under_center[corner_face]]
        corner_triangle = np.zeros(len(corner_vertices), dtype=np.int64)
        corner_points = np.zeros((len(corner_vertices), 3))
        for island in np.unique(corner_island).tolist():
            members = np.flatnonzero(islands == island)
            used, local = np.unique(triangle_vertices[members], return_inverse=True)
            tree = BVHTree.FromPolygons(source_positions[used].tolist(), local.reshape(-1, 3).tolist())
            for corner in np.flatnonzero(corner_island == island).tolist():
                location, _, index, _ = tree.find_nearest(positions[corner_vertices[corner]].tolist())
                corner_triangle[corner] = members[index]
                corner_points[corner] = location
        near = _interpolate_uvs(source_positions, triangle_vertices, triangle_uvs, corner_triangle, corner_points)
        # The same corner by the UV map of the triangle under the face's
        # centre, extended beyond it. Used where the nearest point misleads:
        # the corner lies beyond the island's border (a face crossing a seam
        # keeps its shape instead of being squeezed), or the nearest point is
        # across a seam inside one island (a cylinder unwrapped with one cut:
        # its UVs jump by the island's width).
        center_triangle = under_center[corner_face]
        corner_positions = positions[corner_vertices]
        extended = _interpolate_uvs(source_positions, triangle_vertices, triangle_uvs, center_triangle,
                                    corner_positions)
        uv_scale = _uv_scale(source_positions, triangle_vertices, triangle_uvs, center_triangle)
        beyond = np.linalg.norm(corner_points - corner_positions, axis=1) > OFF_ISLAND * scale
        jump = np.linalg.norm(near - extended, axis=1) > uv_scale * scale
        use_extended = (beyond | jump) & (uv_scale > 0)
        result = np.where(use_extended[:, None], extended, near)
        layer = mesh.uv_layers.get(name) or mesh.uv_layers.new(name=name)
        layer.data.foreach_set("uv", result.astype(np.float32).ravel())
    return names


def transfer(context, source, result, uvs=False):
    """Copy materials, vertex groups, colour attributes, edge marks and, if
    uvs, UV maps from the source onto the result. Returns what was copied,
    {kind: count}, for the report."""
    depsgraph = context.evaluated_depsgraph_get()
    kept = {}
    kept["vertex_groups"], kept["colors"] = _data_transfer(context, source, result)
    mesh = result.data
    kept.update(_copy_edge_marks(depsgraph, source, mesh))
    if uvs:
        kept["uv_maps"] = len(_copy_uvs(depsgraph, source, mesh))
    centers = np.empty(len(mesh.polygons) * 3, dtype=np.float64)
    mesh.polygons.foreach_get("center", centers)
    _copy_materials(depsgraph, source, mesh, centers.reshape(-1, 3))
    kept["materials"] = len(mesh.materials)
    _copy_active_layers(source.data, mesh)
    return {kind: count for kind, count in kept.items() if count}


def transfer_uvs(context, source, result):
    """Only the UV maps; returns {"uv_maps": count}."""
    count = len(_copy_uvs(context.evaluated_depsgraph_get(), source, result.data))
    _copy_active_layers(source.data, result.data)
    return {"uv_maps": count} if count else {}


def _copy_active_layers(source_mesh, mesh):
    """The source's active UV map and active and render colour attributes."""
    active = source_mesh.uv_layers.active
    if active is not None and active.name in mesh.uv_layers:
        mesh.uv_layers.active = mesh.uv_layers[active.name]
    # Newer Blender versions keep these on color_attributes, older ones on attributes.
    source_colors, colors = source_mesh.color_attributes, mesh.color_attributes
    if not hasattr(colors, "default_color_name"):
        source_colors, colors = source_mesh.attributes, mesh.attributes
    for which in ("active_color_name", "default_color_name"):
        name = getattr(source_colors, which, "")
        if name and name in mesh.color_attributes:
            setattr(colors, which, name)
