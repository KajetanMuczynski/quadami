/*
 *  Copyright (c) 2026 Jeremy HU <jeremy-at-dust3d dot org>. All rights reserved.
 *  Modifications Copyright (C) 2026 Quadami contributors, GPL-3.0-or-later (see LICENSE).
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */
#ifndef AUTO_REMESHER_FRAME_FIELD_H
#define AUTO_REMESHER_FRAME_FIELD_H

#include <AutoRemesher/Diagnostics>
#include <AutoRemesher/SurfaceMesh>
#include <AutoRemesher/Vector3>
#include <cmath>
#include <cstddef>
#include <vector>

namespace AutoRemesher {

class FrameField {
public:
    // When `diagnostics` is given, appends a "frame_field" record to it.
    // `markedCorners`, per corner, locks the field to that corner's edge
    // whatever its angle (marked edges).
    static bool create(const SurfaceMesh& mesh, double sharpEdgeDegrees,
        std::vector<Vector3>* field,
        std::vector<DiagnosticRecord>* diagnostics = nullptr,
        const std::vector<char>* markedCorners = nullptr);

    // The angle a cross field asks the map to have around an interior
    // vertex, in quarter turns: 4 at a regular vertex, 3 at a valence-3
    // cone, 5 at a valence-5 one, 0 or less where no map can follow the
    // field. The surface's own angle sum takes part, so this is exact where
    // quarter turns summed mod 4 are not: those read a needle tip asking for
    // 0 or pi / 2 as a regular vertex or a valence-5 cone.
    // `residual(c)` is what is left of the field's turn across the edge of
    // corner c (from c's face to the opposite one, against parallel
    // transport) after the quarter turns matched there; quarterResidual()
    // gives it for the nearest quarter turns. False for a boundary or
    // non-manifold vertex.
    template <typename Residual>
    static bool coneQuarters(const SurfaceMesh& mesh, size_t vertex, const Residual& residual, int* quarters)
    {
        const std::vector<size_t>& incident = mesh.cornersAroundVertex(vertex);
        if (incident.empty())
            return false;
        double angle = 0.0;
        const size_t start = incident.front();
        size_t c = start, steps = 0;
        do {
            const size_t opposite = mesh.oppositeCorner(c);
            if (opposite == SurfaceMesh::npos || ++steps > incident.size())
                return false;
            const Vector3 out = mesh.edgeVector(c), in = -mesh.edgeVector(mesh.previousCorner(c));
            angle += std::atan2(Vector3::crossProduct(out, in).length(), Vector3::dotProduct(out, in));
            angle += residual(c);
            c = mesh.nextCorner(opposite);
        } while (c != start);
        if (steps != incident.size())
            return false;
        *quarters = (int)std::lround(angle / (0.5 * M_PI));
        return true;
    }

    static double quarterResidual(double turn)
    {
        const double quarter = 0.5 * M_PI;
        return turn - quarter * std::round(turn / quarter);
    }
};
}
#endif
