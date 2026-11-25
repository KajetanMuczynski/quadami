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

// Mirror symmetry (symmetry.h, RemeshParameters::symmetryAxes): the result is
// closed where the input is, exactly symmetric about each plane, and its
// halves share their seam vertices; bad planes are rejected.

#include <AutoRemesher/Remesh>
#include <AutoRemesher/Symmetry>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// A UV sphere (around x rows) or torus, its columns turned by half a column so
// that the planes x = 0 and y = 0 cut through triangles, not along edges.
AutoRemesher::RemeshRequest surface(bool torus, double shift)
{
    AutoRemesher::RemeshRequest request;
    const uint32_t around = 48, rows = torus ? 24 : 24;
    std::vector<std::vector<uint32_t>> rings;
    for (uint32_t i = 0; i <= rows; ++i) {
        rings.emplace_back();
        const bool pole = !torus && (0 == i || rows == i);
        if (torus && rows == i) {
            rings.back() = rings.front();
            break;
        }
        for (uint32_t j = 0; j < (pole ? 1 : around); ++j) {
            const double phi = 2.0 * M_PI * (j + 0.5) / around;
            rings.back().push_back((uint32_t)(request.positions.size() / 3));
            if (torus) {
                const double theta = 2.0 * M_PI * i / rows;
                const double r = 1.0 + 0.35 * std::cos(theta);
                request.positions.insert(request.positions.end(),
                    { shift + r * std::cos(phi), r * std::sin(phi), 0.35 * std::sin(theta) });
            } else {
                const double theta = M_PI * i / rows;
                request.positions.insert(request.positions.end(),
                    { shift + std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta) });
            }
        }
    }
    const auto add = [&](uint32_t a, uint32_t b, uint32_t c) {
        request.triangles.insert(request.triangles.end(), { a, b, c });
    };
    for (uint32_t i = 0; i < rows; ++i) {
        const auto& upper = rings[i];
        const auto& lower = rings[i + 1];
        for (uint32_t j = 0; j < around; ++j) {
            const uint32_t k = (j + 1) % around;
            if (1 == upper.size()) {
                add(upper[0], lower[j], lower[k]);
            } else if (1 == lower.size()) {
                add(upper[j], lower[0], upper[k]);
            } else {
                add(upper[j], lower[j], lower[k]);
                add(upper[j], lower[k], upper[k]);
            }
        }
    }
    return request;
}

// Every vertex's mirror image about axis = center is a vertex too.
bool mirrored(const std::vector<double>& positions, int axis, double center)
{
    std::set<std::tuple<double, double, double>> points;
    for (size_t v = 0; v < positions.size(); v += 3)
        points.insert(std::make_tuple(positions[v], positions[v + 1], positions[v + 2]));
    for (size_t v = 0; v < positions.size(); v += 3) {
        double p[3] = { positions[v], positions[v + 1], positions[v + 2] };
        p[axis] = 2.0 * center - p[axis];
        if (!points.count(std::make_tuple(p[0], p[1], p[2])))
            return false;
    }
    return true;
}

void closedAndSymmetric(const std::string& name, AutoRemesher::RemeshRequest request, uint32_t axes,
    uint32_t negative, double center)
{
    request.parameters.targetQuadCount = 800;
    request.parameters.symmetryAxes = axes;
    request.parameters.symmetryKeepNegative = negative;
    request.parameters.symmetryCenter = { { center, 0.0, 0.0 } };
    const AutoRemesher::RemeshResult result = AutoRemesher::remesh(request);
    check(AutoRemesher::RemeshStatus::Ok == result.status, name + ": status " + result.message);
    const auto& report = result.report;
    check(0 == report.boundaryEdgeCount, name + ": open edges " + std::to_string(report.boundaryEdgeCount));
    check(0 == report.nonManifoldEdgeCount, name + ": non-manifold edges");
    check(0 == report.symmetryOpenSeamEdgeCount, name + ": open seam edges");
    check(report.symmetrySeamVertexCount > 0, name + ": no seam");
    check(report.quadCount > 400 && report.quadCount < 1200, name + ": quads " + std::to_string(report.quadCount));
    for (int axis = 0; axis < 3; ++axis) {
        if (axes & (1u << axis))
            check(mirrored(result.mesh.positions, axis, 0 == axis ? center : 0.0), name + ": not mirrored on " + "xyz"[axis]);
    }
    std::printf("%s: %zu quads, %zu non-quads, seam %zu, notches %zu, straightened %zu, folds %zu\n", name.c_str(),
        report.quadCount, report.nonQuadCount, report.symmetrySeamVertexCount, report.symmetryNotchCount,
        report.symmetryStraightenedVertexCount, report.foldedOrTwistedFaceCount);
}

}

int main()
{
    closedAndSymmetric("sphere x", surface(false, 0.0), 1, 0, 0.0);
    closedAndSymmetric("sphere -x", surface(false, 0.0), 1, 1, 0.0);
    closedAndSymmetric("sphere off centre", surface(false, 0.3), 1, 0, 0.3);
    closedAndSymmetric("torus x y", surface(true, 0.0), 3, 0, 0.0);

    // A plane the mesh does not reach would make a separate copy.
    {
        AutoRemesher::RemeshRequest request = surface(false, 5.0);
        request.parameters.targetQuadCount = 500;
        request.parameters.symmetryAxes = 1;
        const auto result = AutoRemesher::remesh(request);
        check(AutoRemesher::RemeshStatus::InvalidInput == result.status, "plane outside the mesh accepted");
    }
    // Nothing on the kept side.
    {
        AutoRemesher::RemeshRequest request = surface(false, 5.0);
        request.parameters.targetQuadCount = 500;
        request.parameters.symmetryAxes = 1;
        request.parameters.symmetryKeepNegative = 1;
        const auto result = AutoRemesher::remesh(request);
        check(AutoRemesher::RemeshStatus::InvalidInput == result.status, "empty kept side accepted");
    }
    // The cut: a marked edge across the plane keeps its kept piece, the
    // section lies in the plane, and one marked edge on the far side goes.
    {
        AutoRemesher::RemeshRequest request = surface(false, 0.0);
        const uint32_t a = 1 + 0, b = 1 + 47; // two vertices of the first ring, across x = 0 at phi ~ 0
        const uint32_t c = 1 + 24, d = 1 + 25; // a far-side edge (phi ~ pi)
        request.featureEdges = { a, b, c, d };
        std::vector<double> positions = request.positions;
        std::vector<uint32_t> triangles = request.triangles, features = request.featureEdges;
        const AutoRemesher::SymmetryPlane plane { 1, 0.0, 1 };
        const auto counts = AutoRemesher::cutAtPlane(&positions, &triangles, &features, plane);
        check(counts.splitTriangleCount > 0, "cut split no triangle");
        check(2 == features.size(), "marked edges after the cut: " + std::to_string(features.size() / 2));
        check(1 == counts.droppedFeatureEdgeCount, "dropped marked edges: " + std::to_string(counts.droppedFeatureEdgeCount));
        for (size_t v = 0; v < positions.size(); v += 3)
            check(positions[v + 1] >= 0.0, "vertex left on the cut side");
        const std::vector<double> section = AutoRemesher::planeSection(positions, triangles, plane);
        check(!section.empty(), "no cross-section");
        for (size_t i = 1; i < section.size(); i += 3)
            check(0.0 == section[i], "cross-section off the plane");
    }
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
