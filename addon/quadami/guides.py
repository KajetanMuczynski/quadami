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

"""Guide loops: edge paths the remesher keeps a row of quad edges along.

A guide is drawn by clicking points on the surface; consecutive points are
joined by the shortest edge path of the mesh, and clicking the first point
again closes the loop. The guide's edges are stored in a boolean edge
attribute (GUIDE_ATTRIBUTE) of the mesh, so they stay in the .blend, and
go to the worker as hard marked edges next to Follow Marks' marks
(docs/GUIDES-AND-DENSITY.md section 3.3).

No Blender import here: the path search works on plain arrays, so the
tests and the bench use the same code as the drawing tool.
"""

import heapq

import numpy as np

GUIDE_ATTRIBUTE = "quadami_guide"


class EdgeGraph:
    """Vertex adjacency of a mesh's edges, for shortest edge paths.

    positions (V, 3) float, edges (E, 2) int vertex pairs."""

    def __init__(self, positions, edges):
        self.positions = np.asarray(positions, dtype=np.float64).reshape(-1, 3)
        edges = np.asarray(edges, dtype=np.int64).reshape(-1, 2)
        self.edges = edges
        count = len(self.positions)
        # CSR adjacency: neighbours of v are targets[starts[v]:starts[v + 1]],
        # reached over edge index edge_of[...] (both directions of every edge).
        source = np.concatenate([edges[:, 0], edges[:, 1]])
        target = np.concatenate([edges[:, 1], edges[:, 0]])
        edge_index = np.concatenate([np.arange(len(edges))] * 2)
        order = np.lexsort((target, source))  # by vertex, then neighbour: deterministic
        self.targets = target[order]
        self.edge_of = edge_index[order]
        self.starts = np.zeros(count + 1, dtype=np.int64)
        np.cumsum(np.bincount(source, minlength=count), out=self.starts[1:])
        self.lengths = np.linalg.norm(self.positions[edges[:, 0]] - self.positions[edges[:, 1]], axis=1)

    def shortest_path(self, start, goal, max_visits=2000000):
        """Edge indices of a shortest path from vertex start to vertex goal,
        in order, or None if goal is unreachable (another island) or the
        search visits more than max_visits vertices. A* with the straight
        line as heuristic, so a short guide segment on a big sculpt visits
        only the vertices near it. Ties go to the lower vertex index, so the
        same clicks give the same path."""
        if start == goal:
            return []
        positions, targets, edge_of, starts, lengths = (
            self.positions, self.targets, self.edge_of, self.starts, self.lengths)
        goal_point = positions[goal]
        best = {start: 0.0}
        came = {}
        heap = [(float(np.linalg.norm(positions[start] - goal_point)), start)]
        done = set()
        while heap:
            _, vertex = heapq.heappop(heap)
            if vertex in done:
                continue
            if vertex == goal:
                break
            done.add(vertex)
            if len(done) > max_visits:
                return None
            base = best[vertex]
            for slot in range(starts[vertex], starts[vertex + 1]):
                neighbour = int(targets[slot])
                if neighbour in done:
                    continue
                edge = int(edge_of[slot])
                cost = base + lengths[edge]
                if cost < best.get(neighbour, np.inf):
                    best[neighbour] = cost
                    came[neighbour] = (vertex, edge)
                    estimate = cost + float(np.linalg.norm(positions[neighbour] - goal_point))
                    heapq.heappush(heap, (estimate, neighbour))
        if goal not in came:
            return None
        path = []
        vertex = goal
        while vertex != start:
            vertex, edge = came[vertex]
            path.append(edge)
        path.reverse()
        return path

    def path_through(self, points, closed=False):
        """Sorted edge indices of the path through the vertices in points, in
        order, back to the first one if closed. None if any leg is
        unreachable. An edge the legs run over twice (a stroke that doubles
        back) cancels out, so a closed stroke gives a loop without spurs."""
        points = list(points)
        if closed and len(points) > 1:
            points.append(points[0])
        used = {}
        for start, goal in zip(points, points[1:]):
            leg = self.shortest_path(start, goal)
            if leg is None:
                return None
            for edge in leg:
                used[edge] = used.get(edge, 0) + 1
        return sorted(edge for edge, count in used.items() if count % 2)


def guide_edge_pairs(edges, guide_flags):
    """Vertex pairs (G, 2) of the edges whose guide flag is set."""
    edges = np.asarray(edges).reshape(-1, 2)
    return edges[np.asarray(guide_flags, dtype=bool)]


def count_loops(edges, guide_flags):
    """(closed, open) guides: connected sets of guide edges, closed when
    every vertex on them has two guide edges. Crossing guides count as one
    set that is neither closed nor open: they are counted as open."""
    pairs = guide_edge_pairs(edges, guide_flags)
    if not len(pairs):
        return 0, 0
    parent = {}

    def find(v):
        root = v
        while parent.get(root, root) != root:
            root = parent[root]
        while parent.get(v, v) != root:
            parent[v], v = root, parent[v]
        return root

    degree = {}
    for a, b in pairs.tolist():
        degree[a] = degree.get(a, 0) + 1
        degree[b] = degree.get(b, 0) + 1
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[max(ra, rb)] = min(ra, rb)
    components = {}
    for v, d in degree.items():
        root = find(v)
        components[root] = components.get(root, True) and d == 2
    closed = sum(1 for is_closed in components.values() if is_closed)
    return closed, len(components) - closed
