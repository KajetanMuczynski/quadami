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
#ifndef AUTO_REMESHER_INPUT_SANITIZER_H
#define AUTO_REMESHER_INPUT_SANITIZER_H

#include <AutoRemesher/Vector3>
#include <cstddef>
#include <vector>

namespace AutoRemesher {

// Input repairs that run before the zero-length edge cleanup (TODO 2.7,
// research/input-sanitizer-2026-10-26.md).  Each leaves a mesh without the
// defect it repairs exactly as it was, so clean input gives the same output.

// The bounding box diagonal of the vertices some triangle uses: a loose
// vertex far away must not scale the tolerances.
double usedBoundingDiagonal(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles);

struct SeamWeldStats {
    // Pairs of boundary edges joined into one manifold edge.
    size_t weldedEdges = 0;
    // Vertices that now use another vertex's index.
    size_t weldedVertices = 0;
    // Connected parts whose welds were undone because they would have made
    // an edge non-manifold.
    size_t keptParts = 0;
};

// The relative tolerance of weldSeams(): ends of two boundary edges this
// close, as a share of the diagonal, are one point.  It covers float32
// round trips and exports with six decimals; Blender's own Merge by Distance
// default (0.1 mm on a 2 m model) is 5e-5.
extern const double seamWeldRelativeTolerance;

// Joins open seams: an unwelded export, Edge Split, a mesh split for UVs.
// Two boundary edges (edges of one triangle) are welded when their ends
// pairwise lie within min(tolerance, a tenth of the shorter edge) of each
// other and neither has another such partner, so three sheets meeting along
// a line and a slit wider than the tolerance stay as they are.  Only the
// triangles' corners change: a welded vertex takes the lowest index of its
// group.  A connected part where the welds would make an edge
// non-manifold is left as it was, whole.
SeamWeldStats weldSeams(const std::vector<Vector3>& vertices,
    std::vector<std::vector<size_t>>& triangles,
    double tolerance);

// Removes every triangle whose three corners an earlier triangle already
// has, in either winding: duplicated faces, and the second side of a
// double-sided (zero-thickness) shell.  The first one is kept, in place.
// Returns how many were removed.
size_t removeDuplicateTriangles(std::vector<std::vector<size_t>>& triangles);

}

#endif
