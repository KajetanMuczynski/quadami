/*
 *  Copyright (C) 2026 Quadami contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "qexextraction.h"
#include <algorithm>
#include <exception>
#include <map>
#include <qex.h>
#include <string>
#include <tuple>
#include <utility>

namespace AutoRemesher {

namespace {

    // OpenMesh takes only manifold, consistently oriented triangles: a sculpt's
    // vine ribbons glued along an edge (three faces on it) or a fold that
    // meets itself at a vertex made it reject the whole island, which then
    // fell back to our tracing.  Each vertex gets one copy per fan of its
    // triangles, a fan being triangles joined at it through edges that exactly
    // two triangles use in opposite directions.  On a manifold, oriented
    // island every vertex is one fan, so nothing changes.  The cut edges
    // become boundary edges for QEx; the triangles keep their uvs.  Returns
    // how many copies were added.
    size_t cutAtNonManifoldVertices(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles,
        std::vector<Vector3>* cutVertices,
        std::vector<std::vector<size_t>>* cutTriangles)
    {
        std::map<std::pair<size_t, size_t>, std::vector<size_t>> edgeTriangles;
        std::vector<std::vector<size_t>> vertexTriangles(vertices.size());
        for (size_t t = 0; t < triangles.size(); ++t) {
            for (size_t i = 0; i < 3; ++i) {
                const size_t a = triangles[t][i], b = triangles[t][(i + 1) % 3];
                edgeTriangles[std::make_pair(std::min(a, b), std::max(a, b))].push_back(t);
                vertexTriangles[a].push_back(t);
            }
        }
        const auto joinedAt = [&](size_t a, size_t b) {
            const auto& users = edgeTriangles[std::make_pair(std::min(a, b), std::max(a, b))];
            if (2 != users.size())
                return false;
            // Opposite directions: one of the two triangles has b followed by a.
            const auto& other = triangles[users[1]];
            for (size_t i = 0; i < 3; ++i) {
                if (other[i] == b && other[(i + 1) % 3] == a)
                    return true;
            }
            const auto& first = triangles[users[0]];
            for (size_t i = 0; i < 3; ++i) {
                if (first[i] == b && first[(i + 1) % 3] == a)
                    return true;
            }
            return false;
        };
        *cutVertices = vertices;
        *cutTriangles = triangles;
        size_t copies = 0;
        std::map<size_t, size_t> parent;
        const auto find = [&parent](size_t x) {
            while (parent[x] != x)
                x = parent[x] = parent[parent[x]];
            return x;
        };
        for (size_t v = 0; v < vertices.size(); ++v) {
            const auto& around = vertexTriangles[v];
            if (around.size() < 2)
                continue;
            parent.clear();
            for (size_t t : around)
                parent[t] = t;
            for (size_t t : around) {
                for (size_t i = 0; i < 3; ++i) {
                    const size_t a = triangles[t][i], b = triangles[t][(i + 1) % 3];
                    if ((a != v && b != v) || !joinedAt(a, b))
                        continue;
                    const auto& users = edgeTriangles[std::make_pair(std::min(a, b), std::max(a, b))];
                    parent[find(users[0])] = find(users[1]);
                }
            }
            std::map<size_t, size_t> fanVertex;
            for (size_t t : around) {
                const size_t fan = find(t);
                auto it = fanVertex.find(fan);
                if (it == fanVertex.end()) {
                    // The first fan keeps the vertex, later ones get copies.
                    size_t index = v;
                    if (!fanVertex.empty()) {
                        index = cutVertices->size();
                        cutVertices->push_back(vertices[v]);
                        ++copies;
                    }
                    it = fanVertex.emplace(fan, index).first;
                }
                for (auto& corner : (*cutTriangles)[t]) {
                    if (corner == v)
                        corner = it->second;
                }
            }
        }
        return copies;
    }

    // Faces on an edge more than two faces use, or on a directed edge another
    // face uses too (sheets of a fold touching along an edge): QuadExtractor's
    // repair keys faces by directed edge and misses both, so they are dropped
    // and the hole filling sees an ordinary hole.  Returns how many.
    size_t dropUnrepairableFaces(std::vector<std::vector<size_t>>* polygons)
    {
        std::map<std::pair<size_t, size_t>, size_t> undirected, directed;
        for (const auto& face : *polygons) {
            for (size_t i = 0; i < face.size(); ++i) {
                const size_t a = face[i], b = face[(i + 1) % face.size()];
                ++undirected[std::make_pair(std::min(a, b), std::max(a, b))];
                ++directed[std::make_pair(a, b)];
            }
        }
        std::vector<std::vector<size_t>> kept;
        kept.reserve(polygons->size());
        for (auto& face : *polygons) {
            bool drop = false;
            for (size_t i = 0; i < face.size() && !drop; ++i) {
                const size_t a = face[i], b = face[(i + 1) % face.size()];
                drop = undirected[std::make_pair(std::min(a, b), std::max(a, b))] > 2
                    || directed[std::make_pair(a, b)] > 1;
            }
            if (!drop)
                kept.push_back(std::move(face));
        }
        const size_t dropped = polygons->size() - kept.size();
        *polygons = std::move(kept);
        return dropped;
    }
}

bool extractWithQex(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    const std::vector<std::vector<Vector2>>& triangleUvs,
    const QexLimits& limits,
    std::vector<Vector3>* outVertices,
    std::vector<std::vector<size_t>>* outPolygons,
    QexOutcome* outcome)
{
    *outcome = QexOutcome();
    outcome->attempted = true;
    outVertices->clear();
    outPolygons->clear();
    if (triangleUvs.size() != triangles.size()) {
        outcome->reason = "uv count differs from triangle count";
        return false;
    }
    std::vector<Vector3> cutVertices;
    std::vector<std::vector<size_t>> cutTriangles;
    for (const auto& tri : triangles) {
        if (tri.size() != 3) {
            outcome->reason = "a face is not a triangle";
            return false;
        }
    }
    outcome->cutVertices = cutAtNonManifoldVertices(vertices, triangles, &cutVertices, &cutTriangles);
    QEx::TriMesh mesh;
    mesh.reserve(cutVertices.size(), cutVertices.size() + cutTriangles.size(), cutTriangles.size());
    for (const auto& v : cutVertices)
        mesh.add_vertex(QEx::TriMesh::Point(v.x(), v.y(), v.z()));
    std::vector<QEx::TriMesh::FaceHandle> faces;
    faces.reserve(triangles.size());
    for (size_t t = 0; t < cutTriangles.size(); ++t) {
        const auto& tri = cutTriangles[t];
        if (triangleUvs[t].size() != 3) {
            outcome->reason = "a face is not a triangle";
            return false;
        }
        const auto fh = mesh.add_face(mesh.vertex_handle((int)tri[0]),
            mesh.vertex_handle((int)tri[1]), mesh.vertex_handle((int)tri[2]));
        if (!fh.is_valid()) {
            outcome->reason = "OpenMesh rejected a triangle (non-manifold input)";
            return false;
        }
        faces.push_back(fh);
    }
    // QEx takes one uv per halfedge: the uv of its to-vertex in its face.
    std::vector<OpenMesh::Vec2d> uvs(mesh.n_halfedges(), OpenMesh::Vec2d(0, 0));
    for (size_t t = 0; t < faces.size(); ++t) {
        for (auto h = mesh.fh_iter(faces[t]); h.is_valid(); ++h) {
            const size_t to = (size_t)mesh.to_vertex_handle(*h).idx();
            for (size_t k = 0; k < 3; ++k) {
                if (cutTriangles[t][k] == to)
                    uvs[h->idx()] = OpenMesh::Vec2d(triangleUvs[t][k].x(), triangleUvs[t][k].y());
            }
        }
    }
    QEx::QuadMesh quadMesh;
    try {
        outcome->steps = QEx::extractQuadMeshOMLimited(&mesh, &uvs, nullptr, &quadMesh,
            (unsigned long)limits.maxSteps, (unsigned long)limits.maxGridVertices);
    } catch (const std::exception& e) {
        outcome->reason = e.what();
        return false;
    }
    outVertices->reserve(quadMesh.n_vertices());
    for (auto v = quadMesh.vertices_begin(); v != quadMesh.vertices_end(); ++v) {
        const auto& p = quadMesh.point(*v);
        outVertices->emplace_back(p[0], p[1], p[2]);
    }
    outPolygons->reserve(quadMesh.n_faces());
    for (auto f = quadMesh.faces_begin(); f != quadMesh.faces_end(); ++f) {
        std::vector<size_t> polygon;
        for (auto v = quadMesh.cfv_iter(*f); v.is_valid(); ++v)
            polygon.push_back((size_t)v->idx());
        if (polygon.size() >= 3)
            outPolygons->push_back(std::move(polygon));
    }
    outcome->droppedFaces = dropUnrepairableFaces(outPolygons);
    if (outPolygons->empty()) {
        outcome->reason = "QEx returned no faces";
        return false;
    }
    outcome->faces = outPolygons->size();
    for (const auto& face : *outPolygons)
        outcome->nonQuads += face.size() != 4;
    return true;
}

long long twiceHandleCount(size_t vertexCount, const std::vector<std::vector<size_t>>& faces)
{
    // Corners (face, position) around a vertex are one fan when their faces
    // share an edge at it that exactly two faces use; each fan counts as its
    // own vertex, so a pinch or a non-manifold edge splits the surface instead
    // of adding a handle.
    std::vector<size_t> cornerBase(faces.size() + 1, 0);
    for (size_t f = 0; f < faces.size(); ++f)
        cornerBase[f + 1] = cornerBase[f] + faces[f].size();
    std::vector<size_t> fan(cornerBase.back());
    for (size_t c = 0; c < fan.size(); ++c)
        fan[c] = c;
    const auto find = [](std::vector<size_t>& parent, size_t x) {
        while (parent[x] != x)
            x = parent[x] = parent[parent[x]];
        return x;
    };
    const auto unite = [&find](std::vector<size_t>& parent, size_t a, size_t b) { parent[find(parent, a)] = find(parent, b); };
    // Every use of an undirected edge with the corners at its two ends,
    // sorted so that the uses of one edge are adjacent. One flat array: a map
    // of small vectors cost about 150 bytes per input edge at the run's peak.
    // Only counts come out below, so the order within an edge does not matter.
    struct EdgeUse {
        size_t low, high, lowCorner, highCorner;
        bool operator<(const EdgeUse& other) const
        {
            return std::tie(low, high, lowCorner, highCorner) < std::tie(other.low, other.high, other.lowCorner, other.highCorner);
        }
    };
    std::vector<EdgeUse> uses;
    uses.reserve(cornerBase.back());
    for (size_t f = 0; f < faces.size(); ++f) {
        const auto& face = faces[f];
        for (size_t i = 0; i < face.size(); ++i) {
            const size_t j = (i + 1) % face.size();
            const size_t a = face[i], b = face[j];
            if (a >= vertexCount || b >= vertexCount || a == b)
                continue;
            const size_t ca = cornerBase[f] + i, cb = cornerBase[f] + j;
            uses.push_back(a < b ? EdgeUse { a, b, ca, cb } : EdgeUse { b, a, cb, ca });
        }
    }
    std::sort(uses.begin(), uses.end());
    // Calls visit(begin, end) with the range of uses of each edge.
    const auto forEachEdge = [&uses](const auto& visit) {
        for (size_t begin = 0; begin < uses.size();) {
            size_t end = begin + 1;
            while (end < uses.size() && uses[end].low == uses[begin].low && uses[end].high == uses[begin].high)
                ++end;
            visit(begin, end);
            begin = end;
        }
    };
    forEachEdge([&](size_t begin, size_t end) {
        if (2 != end - begin)
            return;
        unite(fan, uses[begin].lowCorner, uses[begin + 1].lowCorner);
        unite(fan, uses[begin].highCorner, uses[begin + 1].highCorner);
    });
    // Faces join their fans into components; boundary edges (one face, or a
    // sheet of a non-manifold edge) join them into boundary loops.
    std::vector<size_t> component(fan.size()), loop(fan.size());
    for (size_t c = 0; c < fan.size(); ++c)
        component[c] = loop[c] = c;
    for (size_t f = 0; f < faces.size(); ++f) {
        for (size_t c = cornerBase[f] + 1; c < cornerBase[f + 1]; ++c)
            unite(component, find(fan, c), find(fan, cornerBase[f]));
    }
    long long edgeCount = 0;
    std::vector<char> onBoundary(fan.size(), 0);
    forEachEdge([&](size_t begin, size_t end) {
        const size_t useCount = end - begin;
        const bool boundary = 2 != useCount;
        edgeCount += boundary ? (long long)useCount : 1;
        if (!boundary)
            return;
        for (size_t k = begin; k < end; ++k) {
            const size_t a = find(fan, uses[k].lowCorner), b = find(fan, uses[k].highCorner);
            onBoundary[a] = onBoundary[b] = 1;
            unite(loop, a, b);
        }
    });
    long long fans = 0, components = 0, loops = 0;
    for (size_t c = 0; c < fan.size(); ++c) {
        if (find(fan, c) != c)
            continue;
        ++fans;
        components += find(component, c) == c;
        loops += onBoundary[c] && find(loop, c) == c;
    }
    const long long euler = fans - edgeCount + (long long)faces.size();
    return 2 * components - euler - loops;
}

}
