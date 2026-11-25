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
#ifndef AUTO_REMESHER_SYMMETRY_H
#define AUTO_REMESHER_SYMMETRY_H
#include <AutoRemesher/Remesh>
#include <array>
#include <vector>

namespace AutoRemesher {

// Mirror symmetry by cut, remesh and mirror (TODO G12, the interim of 5.5;
// research/symmetry-2026-11-25.md).  RemeshParameters::symmetryAxes names the
// planes axis = symmetryCenter[axis]; the input is cut at each, the kept part
// (positive side, or negative where symmetryKeepNegative has the axis's bit)
// is remeshed at the target divided by two per plane, and the result is
// mirrored back plane by plane.  At each plane the remeshed part's border
// is put onto the plane (border arcs that cut a corner of the cross-section
// are pulled onto it, and the corners get a seam vertex), the seam vertices slide along the input's
// cross-section so that edge paths cross the plane straight, and the mirror
// copy shares them, so the halves are welded exactly.
RemeshResult remeshSymmetric(const RemeshRequest& request, const RemeshCallbacks& callbacks);

// The steps, for tests.  Planes are given by axis (0, 1, 2), center and
// side: +1 keeps axis >= center, -1 keeps axis <= center.
struct SymmetryPlane {
    int axis = 0;
    double center = 0.0;
    int side = 1;
};

struct CutCounts {
    size_t snappedVertexCount = 0;
    size_t splitTriangleCount = 0;
    size_t inPlaneTriangleCount = 0;
    size_t droppedTriangleCount = 0;
    size_t keptTriangleCount = 0;
    size_t droppedFeatureEdgeCount = 0;
};

// Cuts the triangle mesh (positions xyz, triangles, featureEdges as vertex
// pairs) at the plane and keeps the plane's side, open along the plane:
// vertices within 1e-3 of the median edge snap onto it, crossing triangles
// split at the crossing (one new vertex per crossed edge), faces in the
// plane and on the other side are dropped, a marked edge that crosses keeps
// its mark on the kept piece.  Unused vertices are removed.
CutCounts cutAtPlane(std::vector<double>* positions, std::vector<uint32_t>* triangles,
    std::vector<uint32_t>* featureEdges, const SymmetryPlane& plane);

// The border edges of the triangle mesh lying in the plane (both ends on
// it exactly), as segment end points: x0 y0 z0 x1 y1 z1 per segment.
std::vector<double> planeSection(const std::vector<double>& positions, const std::vector<uint32_t>& triangles,
    const SymmetryPlane& plane);

struct SeamCounts {
    size_t seamVertexCount = 0;
    size_t notchCount = 0;
    double notchDepthMax = 0.0; // in mean edge lengths
    size_t crossedVertexCount = 0;
    size_t flatFaceCount = 0;
    size_t pinnedCornerCount = 0;
    size_t straightenedVertexCount = 0;
    size_t openSeamEdgeCount = 0;
};

// Puts mesh's border near the plane onto it, straightens the seam along
// `section` (segments as planeSection() gives them, the input's cross-section)
// and appends the mirror copy welded along the seam.
SeamCounts mirrorAcross(RemeshMesh* mesh, const SymmetryPlane& plane, const std::vector<double>& section);

}

#endif
