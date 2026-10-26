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

// Parts thinner than one quad (ThinParts): a sphere has none, a sphere
// squashed into a plate is thin all over, and remesh()
// warns about the plate with a target that fits a quad across it, and not
// about the sphere.

#include <AutoRemesher/Remesh>
#include <AutoRemesher/ThinParts>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using AutoRemesher::Vector3;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// A UV sphere of radius 1 with z scaled by `flatten`.
void sphere(double flatten, std::vector<Vector3>* vertices, std::vector<std::vector<size_t>>* triangles)
{
    const size_t rings = 60, segments = 120;
    vertices->push_back(Vector3(0, 0, flatten));
    for (size_t i = 1; i < rings; ++i) {
        const double theta = M_PI * i / rings;
        for (size_t j = 0; j < segments; ++j) {
            const double phi = 2.0 * M_PI * j / segments;
            vertices->push_back(Vector3(std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), flatten * std::cos(theta)));
        }
    }
    vertices->push_back(Vector3(0, 0, -flatten));
    const size_t south = vertices->size() - 1;
    const auto at = [&](size_t i, size_t j) { return 1 + (i - 1) * segments + j % segments; };
    for (size_t j = 0; j < segments; ++j) {
        triangles->push_back({ 0, at(1, j), at(1, j + 1) });
        for (size_t i = 1; i + 1 < rings; ++i) {
            triangles->push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1) });
            triangles->push_back({ at(i, j), at(i + 1, j + 1), at(i, j + 1) });
        }
        triangles->push_back({ at(rings - 1, j), south, at(rings - 1, j + 1) });
    }
}

AutoRemesher::RemeshResult remeshSphere(double flatten, uint32_t targetQuads)
{
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;
    sphere(flatten, &vertices, &triangles);
    AutoRemesher::RemeshRequest request;
    for (const Vector3& v : vertices)
        request.positions.insert(request.positions.end(), { v.x(), v.y(), v.z() });
    for (const auto& t : triangles)
        request.triangles.insert(request.triangles.end(), { (uint32_t)t[0], (uint32_t)t[1], (uint32_t)t[2] });
    request.parameters.targetQuadCount = targetQuads;
    return AutoRemesher::remesh(request);
}

double diagnostic(const AutoRemesher::RemeshResult& result, const char* key)
{
    for (const auto& it : result.report.diagnostics)
        if (it.first == key)
            return it.second;
    return -1.0;
}

}

int main()
{
    {
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        sphere(1.0, &vertices, &triangles);
        const AutoRemesher::ThinParts thin = AutoRemesher::measureThinParts(vertices, triangles, 0.2);
        check(thin.area > 12.0 && thin.area < 12.6, "sphere area");
        check(0.0 == thin.thinArea && 0 == thin.spots, "a sphere has no thin part");
        check(0.0 == AutoRemesher::medianThinGrowth(thin.growth), "no growth without thin parts");
    }
    {
        // Half thickness 0.02, so 0.04 across at the middle against quad edges of 0.2.
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        sphere(0.02, &vertices, &triangles);
        const AutoRemesher::ThinParts thin = AutoRemesher::measureThinParts(vertices, triangles, 0.2);
        check(thin.thinArea > 0.95 * thin.area, "a plate is thin all over, rim included");
        // Its two faces, or one if the rim joins them: a rim vertex's normal
        // faces sideways, so nothing near it faces against it.
        check(1 <= thin.spots && thin.spots <= 2, "a plate is one or two spots");
        const double growth = AutoRemesher::medianThinGrowth(thin.growth);
        check(growth > 20.0 && growth < 60.0, "a quad fits across the plate at about (0.2 / 0.04)^2 times the count");
    }
    {
        const AutoRemesher::RemeshResult plate = remeshSphere(0.02, 300);
        check(AutoRemesher::RemeshStatus::Ok == plate.status, "plate remeshes");
        check(1.0 == diagnostic(plate, "thin_parts_warned"), "plate is warned about");
        bool found = false;
        for (const std::string& warning : plate.report.warnings)
            found = found || std::string::npos != warning.find("thinner than one quad");
        check(found, "plate warning text");
        const AutoRemesher::RemeshResult round = remeshSphere(1.0, 300);
        check(0.0 == diagnostic(round, "thin_parts_warned"), "sphere is not warned about");
        check(0.0 == diagnostic(round, "thin_area_share"), "sphere has no thin area");
    }
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("thin parts: ok\n");
    return 0;
}
