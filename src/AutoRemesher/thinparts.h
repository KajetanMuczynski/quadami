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
#ifndef AUTO_REMESHER_THIN_PARTS_H
#define AUTO_REMESHER_THIN_PARTS_H

#include <AutoRemesher/Vector3>
#include <cstddef>
#include <utility>
#include <vector>

namespace AutoRemesher {

// Parts of a surface thinner than one quad: ribbons, plates, horns and
// tips.  A third of the remaining defect score of the corpus sits on them
// (research/defect-ledger-2026-10-22.md): the map cannot fit a quad across
// the part, and the output folds there or loses the part.  Measured only,
// for the report; nothing here changes the mesh.
//
// A vertex is thin if another vertex within `quadEdge` has a normal facing
// it (dot < -0.5): the other side of the part.  Its thickness is the
// distance to the nearest such vertex.  Thin vertices joined by edges form
// spots; spots smaller than one quad (area < quadEdge^2) are not counted,
// since a sharp crease reads as a thin line.  Run on the isotropic remesh
// T, whose edges are well shaped and a fraction of `quadEdge` long.
struct ThinParts {
    double area = 0.0;
    double thinArea = 0.0;
    size_t spots = 0;
    // Per thin vertex of a counted spot: (quadEdge / thickness)^2, the
    // factor by which the quad count would have to grow for one quad to fit
    // across the part there, and the vertex's area as its weight.
    std::vector<std::pair<double, double>> growth;
};

ThinParts measureThinParts(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles, double quadEdge);

// The factor by which the quad count would have to grow for half of the
// thin area (by area) to get one quad across: the area-weighted median of
// `growth`.  0 when there is none.
double medianThinGrowth(std::vector<std::pair<double, double>> growth);

}

#endif
