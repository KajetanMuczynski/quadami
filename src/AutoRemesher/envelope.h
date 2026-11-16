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
#ifndef AUTO_REMESHER_ENVELOPE_H
#define AUTO_REMESHER_ENVELOPE_H

#include <AutoRemesher/Vector3>
#include <cstddef>
#include <vector>

namespace AutoRemesher {

// A closed surface around a part whose holes and handles are finer than the
// quads asked of it: chain mail, strung beads, feathers of crossing strands.
// The quads cannot hold that topology (a closed quad mesh of Euler
// characteristic chi < 0 with vertices of valence 6 or less has at least
// 3|chi| faces), so the parametrization of such a part breaks and its
// extraction comes out as spikes or empty.  The envelope closes every gap narrower than about twice
// `closingRadius` and keeps the rest of the shape, like a voxel remesh.
//
// The input is grown by `closingRadius` (the points within that distance of
// a triangle) and shrunk again by `closingRadius - offset`, on a grid of
// `cellSize`; the result lies `offset` outside the input where nothing was
// closed, so a single-sided sheet becomes a slab 2 * offset thick.  Cavities
// sealed inside are filled, so only the outer surface is returned: its
// largest connected piece, closed and two-manifold (marching tetrahedra on
// the Freudenthal split of each cell), oriented outwards.  If the grid would
// have more than `maximumCells` nodes the cells are made larger.
struct EnvelopeResult {
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;
    double cellSize = 0.0;
    size_t cells = 0;
    // Triangles of the smaller pieces left out.
    size_t droppedTriangles = 0;
};

bool buildEnvelope(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double closingRadius, double offset, double cellSize, size_t maximumCells,
    EnvelopeResult* result);

}

#endif
