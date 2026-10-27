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

// The sharp edges of an island (TODO 2.6), with one test instead of a test
// of each stage's own.  The input's set, classified once, drives the
// decimation lock, the remesher's feature vertices and the crease snap; it
// is carried through the decimation (remapFeatureEdges).  The set of the
// isotropic remesh T (reclassifyFeatureEdges), with the same test, drives
// the field lock, the singularity freeze and the isoline constraints.
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

// The set of the isotropic remesh T: the hard edges of `carried` (the
// remesher's carried set) that are still edges of T, plus every interior
// edge of T that passes the sharp test on T itself; `classified` counts the
// latter.  The input's other sharp edges are not carried: the remesher's
// smoothing moves their vertices off the crease and bends them, and the
// field and isolines constrained to a bent edge fold the parameterization
// (TODO 2.6 measurements: continuous UV flips x4.5 on the cad set), where
// the edges sharp on T are the creases T really has.
FeatureEdgeSet reclassifyFeatureEdges(const FeatureEdgeSet& carried,
    const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double sharpEdgeDegrees, size_t* classified = nullptr);

// Carries a set through a decimation that kept a subset of the vertices
// (`outputIndexOfInput`, SIZE_MAX for a removed vertex) and rebuilt the
// faces: the set's edges that are still edges, plus every edge between two
// of the set's surviving vertices that passes the sharp test on the
// decimated mesh, so a vertex the decimation removed from a chain leaves a
// chain edge rather than a gap.  `dropped` counts set edges whose two ends
// both survive but are no longer joined by an edge.
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
