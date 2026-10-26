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

// Thin-island floor (TODO 4.1): islandThinWidth finds a tube's diameter and
// calls a sphere and a wide plate not thin; remesh() gives a thin tube
// island next to a ball more quads than its area share, takes them from
// the ball, keeps the total at the target, and with the floor off plans by
// area share alone.

#include <AutoRemesher/Remesh>
#include <AutoRemesher/ThinParts>
#include <cmath>
#include <cstdio>
#include <limits>
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

// A UV sphere at `center` with radii (r, r, r * flatten).
void sphere(const Vector3& center, double r, double flatten, std::vector<Vector3>* vertices, std::vector<std::vector<size_t>>* triangles)
{
    const size_t rings = 60, segments = 120;
    const size_t first = vertices->size();
    vertices->push_back(center + Vector3(0, 0, r * flatten));
    for (size_t i = 1; i < rings; ++i) {
        const double theta = M_PI * i / rings;
        for (size_t j = 0; j < segments; ++j) {
            const double phi = 2.0 * M_PI * j / segments;
            vertices->push_back(center + Vector3(r * std::sin(theta) * std::cos(phi), r * std::sin(theta) * std::sin(phi), r * flatten * std::cos(theta)));
        }
    }
    vertices->push_back(center + Vector3(0, 0, -r * flatten));
    const size_t south = vertices->size() - 1;
    const auto at = [&](size_t i, size_t j) { return first + 1 + (i - 1) * segments + j % segments; };
    for (size_t j = 0; j < segments; ++j) {
        triangles->push_back({ first, at(1, j), at(1, j + 1) });
        for (size_t i = 1; i + 1 < rings; ++i) {
            triangles->push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1) });
            triangles->push_back({ at(i, j), at(i + 1, j + 1), at(i, j + 1) });
        }
        triangles->push_back({ at(rings - 1, j), south, at(rings - 1, j + 1) });
    }
}

// A closed tube along x from `start`, `length` long and `diameter` across,
// with fan caps.
void tube(const Vector3& start, double length, double diameter, std::vector<Vector3>* vertices, std::vector<std::vector<size_t>>* triangles)
{
    const size_t rings = 400, segments = 16;
    const size_t first = vertices->size();
    for (size_t i = 0; i <= rings; ++i) {
        for (size_t j = 0; j < segments; ++j) {
            const double phi = 2.0 * M_PI * j / segments;
            vertices->push_back(start + Vector3(length * i / rings, 0.5 * diameter * std::cos(phi), 0.5 * diameter * std::sin(phi)));
        }
    }
    const size_t left = vertices->size();
    vertices->push_back(start);
    const size_t right = vertices->size();
    vertices->push_back(start + Vector3(length, 0, 0));
    const auto at = [&](size_t i, size_t j) { return first + i * segments + j % segments; };
    for (size_t j = 0; j < segments; ++j) {
        for (size_t i = 0; i < rings; ++i) {
            triangles->push_back({ at(i, j), at(i, j + 1), at(i + 1, j + 1) });
            triangles->push_back({ at(i, j), at(i + 1, j + 1), at(i + 1, j) });
        }
        triangles->push_back({ left, at(0, j + 1), at(0, j) });
        triangles->push_back({ right, at(rings, j), at(rings, j + 1) });
    }
}

AutoRemesher::RemeshResult remeshBallAndTube(double quadsAcross, uint32_t targetQuads)
{
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;
    sphere(Vector3(0, 0, 0), 1.0, 1.0, &vertices, &triangles);
    tube(Vector3(1.5, 0, 0), 3.0, 0.1, &vertices, &triangles);
    AutoRemesher::RemeshRequest request;
    for (const Vector3& v : vertices)
        request.positions.insert(request.positions.end(), { v.x(), v.y(), v.z() });
    for (const auto& t : triangles)
        request.triangles.insert(request.triangles.end(), { (uint32_t)t[0], (uint32_t)t[1], (uint32_t)t[2] });
    request.parameters.targetQuadCount = targetQuads;
    request.parameters.thinIslandQuadsAcross = quadsAcross;
    return AutoRemesher::remesh(request);
}

// A value of the run's own stage record.
double diagnostic(const AutoRemesher::RemeshResult& result, const char* key)
{
    for (const auto& record : result.stageDiagnostics) {
        if (record.stage != "run")
            continue;
        for (const auto& it : record.values)
            if (it.first == key)
                return it.second;
    }
    return -1.0;
}

}

int main()
{
    const double infinity = std::numeric_limits<double>::infinity();
    {
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        sphere(Vector3(0, 0, 0), 1.0, 1.0, &vertices, &triangles);
        check(infinity == AutoRemesher::islandThinWidth(vertices, triangles, 0.5, 4096), "a sphere is not thin");
    }
    {
        // A disc of radius 1, 0.04 thick: its median distance to the rim is
        // 1 - 1 / sqrt(2), about 0.29.
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        sphere(Vector3(0, 0, 0), 1.0, 0.02, &vertices, &triangles);
        const double width = AutoRemesher::islandThinWidth(vertices, triangles, 0.5, 4096);
        check(width > 0.45 && width < 0.7, "a plate is as wide as twice its median distance to the rim");
        check(infinity == AutoRemesher::islandThinWidth(vertices, triangles, 0.2, 4096), "a plate wider than the radius is not thin");
    }
    {
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        tube(Vector3(0, 0, 0), 3.0, 0.1, &vertices, &triangles);
        const double width = AutoRemesher::islandThinWidth(vertices, triangles, 0.5, 4096);
        // 16 sides: the facing and the side-turned neighbours are a few
        // segments around, so a little off the diameter either way.
        check(width > 0.08 && width < 0.13, "a tube is about as wide as its diameter");
        check(width == AutoRemesher::islandThinWidth(vertices, triangles, 0.5, 100000), "sampling every vertex of a small island");
    }
    {
        // Area share: the tube has 0.94 of 13.5 area units, about 70 of 1000
        // quads, so quad edges of 0.12 against 0.1 across.
        const AutoRemesher::RemeshResult off = remeshBallAndTube(0.0, 1000);
        const AutoRemesher::RemeshResult on = remeshBallAndTube(2.0, 1000);
        check(AutoRemesher::RemeshStatus::Ok == off.status && AutoRemesher::RemeshStatus::Ok == on.status, "ball and tube remesh");
        check(0.0 == diagnostic(off, "thin_floor_islands") && 0.0 == diagnostic(off, "thin_floor_given_quads"), "off: no floor");
        check(1.0 == diagnostic(on, "thin_floor_islands"), "on: the tube is thin");
        const double given = diagnostic(on, "thin_floor_given_quads");
        const double wanted = diagnostic(on, "thin_floor_wanted_quads");
        check(given > 0.0 && given <= wanted, "on: the tube gets quads");
        check(given <= 0.25 * 1000 + 1e-9, "on: the ball gives up at most a quarter");
        check(on.report.quadCount > 800 && on.report.quadCount < 1200, "on: the total stays near the target");
    }
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("thin island floor: ok\n");
    return 0;
}
