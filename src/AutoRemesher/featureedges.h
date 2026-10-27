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
#ifndef AUTO_REMESHER_FEATURE_EDGES_H
#define AUTO_REMESHER_FEATURE_EDGES_H

#include <AutoRemesher/SurfaceMesh>
#include <AutoRemesher/Vector3>
#include <cstddef>
#include <utility>
#include <vector>

namespace AutoRemesher {

// The sharp edges of an island (TODO 2.6), classified once on the input and
// carried through decimation and the isotropic remesh to T, so every stage
// (decimation lock, remesher feature vertices, field lock, singularity
// freeze, isoline constraints, crease snap) works on the same edges instead
// of testing dihedrals of its own.
//
// An interior edge is sharp when the angle between its two face normals is
// at least the Sharp Edge angle minus kSharpEdgeToleranceDegrees.  Without
// the tolerance a right angle at the default 90 degrees is a coin flip:
// axis-aligned and Blender float32 right angles compute a hair above or
// below 90 degrees edge by edge.  Boundary edges are not in the set; every
// stage handles them as boundary.
constexpr double kSharpEdgeToleranceDegrees = 1.0;

struct FeatureEdgeSet {
    // Bit of `flags` reserved for hard (user-marked) edges, TODO 5.4.
    static constexpr unsigned char Hard = 0x1;

    // Undirected edges (first < second) in the vertex numbering of the mesh
    // the set belongs to, sorted; `flags` is parallel to it.
    std::vector<std::pair<size_t, size_t>> edges;
    std::vector<unsigned char> flags;

    bool empty() const { return edges.empty(); }
    size_t size() const { return edges.size(); }
    bool contains(size_t a, size_t b) const;
    // Sorts and merges duplicates (their flags are or-ed).
    void normalize();
};

// False for 0 and for 180 and more: then no edge is sharp anywhere.
bool sharpEdgesEnabled(double sharpEdgeDegrees);

// The one sharp test.  `normalAngle` is the unsigned angle between the two
// face normals, in radians.
bool isSharpNormalAngle(double normalAngle, double sharpEdgeDegrees);

// The sharp interior edges of a triangle mesh.  Edges next to a degenerate
// face (no normal) are never sharp.
FeatureEdgeSet classifySharpEdges(const SurfaceMesh& mesh, double sharpEdgeDegrees);
FeatureEdgeSet classifySharpEdges(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles, double sharpEdgeDegrees);

// The set on a mesh made from the classified one (decimated, remeshed):
// the edges carried over (`carried`, those that are still edges of
// `triangles`), plus every interior edge whose two ends are both on a
// feature (`onFeature`, per vertex) and that passes the sharp test on this
// mesh.  So a vertex the decimation removed from a chain, or a flip in the
// remesh, leaves a chain edge rather than a gap, and nothing away from the
// input's features is ever classified again.  `added` counts the latter.
FeatureEdgeSet extendFeatureEdges(const FeatureEdgeSet& carried,
    const std::vector<char>& onFeature,
    const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double sharpEdgeDegrees, size_t* added = nullptr);

// Carries a set through a decimation that kept a subset of the vertices
// (`outputIndexOfInput`, SIZE_MAX for a removed vertex) and rebuilt the
// faces, with extendFeatureEdges().  `dropped` counts set edges whose two
// ends both survive but are no longer joined by an edge.
FeatureEdgeSet remapFeatureEdges(const FeatureEdgeSet& set,
    const std::vector<size_t>& outputIndexOfInput,
    const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double sharpEdgeDegrees, size_t* dropped = nullptr);

// Per corner of `mesh` (the edge from the corner's vertex to the next one):
// 1 when that edge is in `set`.  Both corners of an interior edge get it.
std::vector<char> featureCornerFlags(const SurfaceMesh& mesh, const FeatureEdgeSet& set);

// meshopt_simplify vertex lock bits for the ends of the set's edges.
std::vector<unsigned char> featureVertexLock(const FeatureEdgeSet& set, size_t vertexCount);

// Total length of the set's edges.
double featureEdgeLength(const FeatureEdgeSet& set, const std::vector<Vector3>& vertices);

}

#endif
