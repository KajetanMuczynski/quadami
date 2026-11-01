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
#ifndef AUTO_REMESHER_QEX_EXTRACTION_H
#define AUTO_REMESHER_QEX_EXTRACTION_H
#include <AutoRemesher/Vector2>
#include <AutoRemesher/Vector3>
#include <cstddef>
#include <string>
#include <vector>

namespace AutoRemesher {

// libQEx's deterministic limits (thirdparty/libQEx/PATCHES.md). Steps, not
// seconds, so that a run is the same on every machine and under any load.
// On the corpus, closed and real-sculpt runs that libQEx finished, the most a
// run used was 1,361 steps per triangle (a thin torus; median 1.5, a real
// sculpt head up to 470); 4,000 leaves room above that and still ends a
// runaway trace in seconds.
constexpr size_t QexStepsPerTriangle = 4000;
// Grid vertices are about one per quad; 8 per target quad bounds libQEx's
// memory by the target rather than by how far the map folds.
constexpr size_t QexGridVerticesPerQuad = 8;

struct QexLimits {
    size_t maxSteps = 0; // 0: none
    size_t maxGridVertices = 0; // 0: none
};

struct QexOutcome {
    bool attempted = false;
    bool used = false;
    // Why the island fell back to our own tracing, empty if it didn't.
    std::string reason;
    size_t steps = 0;
    size_t faces = 0;
    size_t nonQuads = 0;
    // Faces dropped by dropUnrepairableFaces() (an edge used by more than two).
    size_t droppedFaces = 0;
    // Vertex copies made to cut the island at non-manifold edges and vertices.
    size_t cutVertices = 0;
    // Our own tracing: the map's UV area, grid line crossings, faces traced,
    // and whether the area or the faces were over the cap
    // (QuadExtractor::traceMesh).
    double traceUvArea = 0.0;
    size_t traceCrossPoints = 0;
    size_t traceFaces = 0;
    bool traceCapped = false;
};

// The polygons libQEx (Ebke et al. 2013) extracts from the integer-grid map,
// given per triangle corner as QuadExtractor takes it.  Faces QEx leaves as
// n-gons are kept.  Returns false, with the reason in outcome->reason, when
// OpenMesh rejects the triangle mesh (a non-manifold edge or vertex), libQEx
// hits a limit or one of its connectivity guards, or returns no face; the
// caller then falls back to its own extraction.
bool extractWithQex(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    const std::vector<std::vector<Vector2>>& triangleUvs,
    const QexLimits& limits,
    std::vector<Vector3>* outVertices,
    std::vector<std::vector<size_t>>* outPolygons,
    QexOutcome* outcome);

// Twice the number of handles of a polygon surface, 2C - chi - B over its C
// components with B boundary loops; cutting or filling a hole leaves it as it
// is. Each fan of faces around a vertex counts as its own vertex, and each
// sheet of an edge more than two faces use as its own boundary edge, so a
// pinch or a fold touching along an edge splits the surface rather than adding
// a handle.
long long twiceHandleCount(size_t vertexCount, const std::vector<std::vector<size_t>>& faces);

}

#endif
