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
// of each stage's own.  The input's set drives the decimation lock and the
// crease snap; the decimated mesh's (remapFeatureEdges) the remesher's
// feature vertices; T's (reclassifyFeatureEdges) the field lock, the
// singularity freeze and the isoline constraints.  Each is the same test on
// that mesh plus the edges carried from the one before: all of them through
// the decimation, the hard ones through the remesh.
//
// An interior edge is sharp when the angle between its two face normals is
// at least the Sharp Edge angle minus kSharpEdgeToleranceDegrees.  Without
// the tolerance a right angle at the default 90 degrees is a coin flip:
// axis-aligned and Blender float32 right angles compute a hair above or
// below 90 degrees edge by edge, by up to 0.009 degrees on the cad and
// primitives sets (thin cap triangles).  The tolerance stays well below
// the edges that are genuinely a little under the angle: on organic meshes
// 80-90 % of the edges within 1 degree of 90 are 0.1 degrees or more off,
// and a 1 degree tolerance marked them too (ext_corset: holes 1.3 -> 3.3).
// Boundary edges are not in the set; every stage handles them as boundary.
constexpr double kSharpEdgeToleranceDegrees = 0.1;

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
// latter.  The input's other sharp edges are not carried.  Before TODO 2.5
// the remesher bent them, and constraints on a bent edge folded the
// parameterization (cad set: continuous UV flips x2.2 summed, SJ p1 0.62 ->
// 0.47).  Since 2.5 the remesher keeps them on the crease, and carrying all
// of them was still worse than this set on the cad set (6 replicates:
// non-quads 1.7 -> 14, libQEx fallbacks 1 -> 6 of 144 runs, UV flips x1.09).
FeatureEdgeSet reclassifyFeatureEdges(const FeatureEdgeSet& carried,
    const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double sharpEdgeDegrees, size_t* classified = nullptr);

// Carries a set through a decimation that kept a subset of the vertices
// (`outputIndexOfInput`, SIZE_MAX for a removed vertex) and rebuilt the
// faces: the set's edges that are still edges (the decimation moves no
// vertex, so they are still the input's creases), plus every other edge
// sharp on the decimated mesh.  The latter close a chain where the
// decimation removed a vertex of it, and keep the creases the decimation
// makes on thin parts (a thin ellipsoid's rim), which the remesher must
// keep: without them the closed set lost fidelity (TODO 2.6: uncovered area
// x1.5).  `dropped` counts set edges whose two ends both survive but are no
// longer joined by an edge.
FeatureEdgeSet remapFeatureEdges(const FeatureEdgeSet& set,
    const std::vector<size_t>& outputIndexOfInput,
    const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double sharpEdgeDegrees, size_t* dropped = nullptr);

// Normal angle above which a detected chain is a fold of the surface (a
// pleat a sculpting brush left), not a crease.
constexpr double kFoldNormalAngleDegrees = 170.0;

// The set the isotropic remesh keeps as chains (TODO 2.5): `set` without its
// detected noise.  Chains are maximal paths of set edges through vertices
// on two of them that turn by 45 degrees or less (the remesher's corners
// end them).  A chain without a hard edge is dropped when its
// length-weighted normal angle is above kFoldNormalAngleDegrees, or when its
// component (chains joined at corners) has five chains or more that are
// shorter than `minLength` on average.  The remesher fixes a chain's
// corners, so short chains freeze vertices closer than a T edge; zigzag
// noise (a crumpled or pleated region) is a large
// component of mostly short chains, since every turn above 45 degrees ends
// one; a fold kept through the remesh folds the map.  Before 2.5 the
// remesher's flips and relaxation erased them.  `dropped` counts the
// dropped edges.
FeatureEdgeSet filterDetectedChains(const FeatureEdgeSet& set,
    const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double minLength, size_t* dropped = nullptr);

// `set` without the components (edges joined at shared vertices) shorter
// than `minLength` that have no hard edge: the quad mesh cannot follow a
// crease shorter than one of its edges, and the map folds around one it is
// made to (on the sculpt set, a 0.2 h piece at a horn's tip lost the
// horn).  Applied to T's set only: in the remesh such a piece keeps a tip
// that the remesh would cut otherwise.  `dropped` counts the dropped edges.
FeatureEdgeSet dropShortFeatureComponents(const FeatureEdgeSet& set, const std::vector<Vector3>& vertices,
    double minLength, size_t* dropped = nullptr);

// Per corner of `mesh` (the edge from the corner's vertex to the next one):
// 1 when that edge is in `set` with every bit of `requiredFlags`.  Both
// corners of an interior edge get it.
std::vector<char> featureCornerFlags(const SurfaceMesh& mesh, const FeatureEdgeSet& set,
    unsigned char requiredFlags = 0);

// meshopt_simplify vertex lock bits for the ends of the set's edges: hard
// edges locked, the others kept by priority.
std::vector<unsigned char> featureVertexLock(const FeatureEdgeSet& set, size_t vertexCount);

// Total length of the set's edges.
double featureEdgeLength(const FeatureEdgeSet& set, const std::vector<Vector3>& vertices);

}

#endif
