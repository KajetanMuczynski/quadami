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
#ifndef AUTO_REMESHER_SURFACE_PROJECTOR_H
#define AUTO_REMESHER_SURFACE_PROJECTOR_H

#include <AutoRemesher/Vector3>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

class AxisAlignedBoudingBox;
class AxisAlignedBoudingBoxTree;

namespace AutoRemesher {

Vector3 closestPointOnTriangle(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c);
Vector3 closestPointOnSegment(const Vector3& p, const Vector3& a, const Vector3& b);

// Moves the vertices of a polygon mesh onto a triangle surface (TODO 2.8).
//
// The extractor leaves the output on the isotropic remesh T, whose faces are
// chords of the input M, so the output sits inside convex parts of M by up to
// T's sagitta. Each vertex goes to its closest point on M; a vertex on an
// output boundary goes to the closest input boundary edge instead, so open
// borders stay on the border.
//
// A move is kept only if it is safe:
// - the closest point is searched among faces of M facing the same way as the
//   vertex, so a vertex on a thin part cannot land on the opposite sheet;
// - it is no longer than half the mean output edge (the gap is a fraction of
//   T's edge, so a longer move means the vertex was matched to something else);
// - no face around it turns by more than 60 degrees, comes to face away from
//   the input face one of its vertices lands on (having faced that way before),
//   or drops in scaled Jacobian below min(0.1, its value before), so
//   projection adds no folded or flipped faces. Of a face made worse only the
//   longest move is undone, and the faces around it are checked again;
//   rejected vertices keep their position.
// Only vertex positions change, never the connectivity, so the output's
// topology is the extractor's.
//
// relax() then evens out the quads along the surface, see there.
class SurfaceProjector {
public:
    // `triangles` index `vertices`; they may be a subset of a larger mesh (one
    // island of the input). Both must outlive the projector. An input edge
    // whose two faces meet at more than `sharpEdgeDegrees` is a crease for
    // relax(); 0 (or 180 and more) means no creases.
    SurfaceProjector(const std::vector<Vector3>* vertices,
        const std::vector<std::vector<size_t>>* triangles,
        double sharpEdgeDegrees = 0.0,
        // Vertex pairs (sorted, low first) of marked edges: creases whatever
        // their angle; those that are not an edge of `triangles` are skipped.
        const std::vector<std::pair<size_t, size_t>>* markedEdges = nullptr);
    ~SurfaceProjector();

    struct Result {
        size_t vertices = 0;
        size_t projected = 0;
        // Not moved: no facing input face (or boundary edge) within reach.
        size_t keptUnmatched = 0;
        // Not moved: the move was longer than half the mean edge.
        size_t keptFar = 0;
        // Not moved: moving would fold or turn a face around it.
        size_t keptFolding = 0;
        // Mean and largest move of the projected vertices, in mean edges.
        double meanMove = 0.0;
        double maxMove = 0.0;
    };

    Result project(std::vector<Vector3>* points, const std::vector<std::vector<size_t>>& faces) const;

    struct RelaxResult {
        // Vertices moved onto an input crease, and those whose move there
        // would have folded a face.
        size_t snapped = 0;
        size_t snapFolding = 0;
        // Vertices the smoothing may move: interior, only quads around them,
        // not on or next to a crease, no face around them turned more than
        // 45 degrees from their normal.
        size_t free = 0;
        // Over all iterations: moves made, and moves undone because a face
        // would fold or turn.
        size_t moves = 0;
        size_t keptFolding = 0;
    };

    // Run after project(). First, every interior vertex within a quarter of
    // the mean edge of an input crease goes onto it (the extractor's
    // smoothing rounds creases off, and an edge loop that runs along a
    // crease is not on it). Then `iterations` rounds of tangential smoothing:
    // each free vertex moves half way to the mean of its neighbours, without
    // the part along its normal, and lands on its closest facing input point.
    // Boundary vertices, vertices of non-quads, vertices on or next to a
    // crease and vertices with a face turned more than 45 degrees from their
    // normal (a fold of the output with no input crease, mostly quads
    // bridging a rim thinner than a quad, which smoothing would cut) stay. Every round keeps moves under half the mean edge and
    // undoes moves that make a face worse, as project() does.
    RelaxResult relax(std::vector<Vector3>* points, const std::vector<std::vector<size_t>>& faces,
        size_t iterations) const;

    // Distance from `position` to the closest point of the surface, whichever
    // way its face points; `maxDistance` when nothing is closer.
    double distance(const Vector3& position, double maxDistance) const;

private:
    struct EdgeTree {
        std::vector<std::pair<size_t, size_t>> edges;
        std::unique_ptr<std::vector<AxisAlignedBoudingBox>> boxes;
        std::unique_ptr<AxisAlignedBoudingBoxTree> tree;
    };

    void buildEdgeTree(EdgeTree* edgeTree) const;
    bool closestFacingPoint(const Vector3& position, const Vector3& normal, double maxDistance,
        Vector3* closest, Vector3* closestNormal) const;
    bool closestEdgePoint(const EdgeTree& edgeTree, const Vector3& position, double maxDistance,
        Vector3* closest) const;

    const std::vector<Vector3>* m_vertices = nullptr;
    const std::vector<std::vector<size_t>>* m_triangles = nullptr;
    std::vector<Vector3> m_triangleNormals;
    std::unique_ptr<std::vector<AxisAlignedBoudingBox>> m_triangleBoxes;
    std::unique_ptr<AxisAlignedBoudingBoxTree> m_triangleTree;
    EdgeTree m_boundary;
    EdgeTree m_creases;
};

}

#endif
