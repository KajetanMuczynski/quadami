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

// planRemesh() (docs/PREVIEW.md): a large sphere, a small one that gets the
// small-island floor, and a vertex no face uses.  Every vertex gets a planned
// edge and an island, the islands' quads add up, the small one sits at its
// floor, the large one's edge at adaptivity 0 is sqrt(area / quads), and a
// run at the same target lands near the planned count.

#include <AutoRemesher/Remesh>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// An icosphere, appended to positions and triangles.
void sphere(int subdivisions, double cx, double radius, std::vector<double>* positions, std::vector<uint32_t>* triangles)
{
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<std::array<double, 3>> v = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 },
        { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
        { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
    const auto normalize = [](std::array<double, 3> p) {
        const double l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        return std::array<double, 3> { p[0] / l, p[1] / l, p[2] / l };
    };
    for (auto& p : v)
        p = normalize(p);
    std::vector<std::array<uint32_t, 3>> f = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
        { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
        { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
        { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    for (int s = 0; s < subdivisions; ++s) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoints;
        const auto midpoint = [&](uint32_t a, uint32_t b) {
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            const auto found = midpoints.find(key);
            if (found != midpoints.end())
                return found->second;
            v.push_back(normalize({ v[a][0] + v[b][0], v[a][1] + v[b][1], v[a][2] + v[b][2] }));
            return midpoints[key] = (uint32_t)(v.size() - 1);
        };
        std::vector<std::array<uint32_t, 3>> finer;
        for (const auto& t3 : f) {
            const uint32_t ab = midpoint(t3[0], t3[1]), bc = midpoint(t3[1], t3[2]), ca = midpoint(t3[2], t3[0]);
            finer.push_back({ t3[0], ab, ca });
            finer.push_back({ t3[1], bc, ab });
            finer.push_back({ t3[2], ca, bc });
            finer.push_back({ ab, bc, ca });
        }
        f.swap(finer);
    }
    const uint32_t offset = (uint32_t)(positions->size() / 3);
    for (const auto& p : v) {
        positions->push_back(cx + radius * p[0]);
        positions->push_back(radius * p[1]);
        positions->push_back(radius * p[2]);
    }
    for (const auto& t3 : f) {
        for (const uint32_t i : t3)
            triangles->push_back(offset + i);
    }
}

}

int main()
{
    using namespace AutoRemesher;
    RemeshRequest request;
    sphere(5, 0.0, 1.0, &request.positions, &request.triangles);
    const size_t largeVertices = request.positions.size() / 3;
    sphere(2, 3.0, 0.05, &request.positions, &request.triangles);
    // A loose vertex at the large sphere's north pole.
    request.positions.insert(request.positions.end(), { 0.0, 1.0, 0.0 });
    const size_t vertexCount = request.positions.size() / 3;
    request.parameters.targetQuadCount = 2000;
    request.parameters.adaptivity = 0.0;

    const RemeshResult planned = planRemesh(request);
    check(RemeshStatus::Ok == planned.status, "plan status ok");
    const RemeshPlan& plan = planned.plan;
    check(plan.vertexEdgeLengths.size() == vertexCount, "an edge per input vertex");
    check(plan.vertexIslands.size() == vertexCount, "an island per input vertex");
    check(2 == plan.islands.size(), "two islands");
    if (0 != failures || 2 != plan.islands.size())
        return 1;
    double sum = 0.0;
    for (const auto& island : plan.islands)
        sum += island.plannedQuads;
    check(std::fabs(sum - plan.plannedQuads) < 1e-9, "islands add up to the planned quads");
    for (size_t v = 0; v < vertexCount; ++v) {
        if (plan.vertexIslands[v] >= plan.islands.size() || !(plan.vertexEdgeLengths[v] > 0.0f)) {
            check(false, "every vertex has an island and an edge");
            break;
        }
    }
    const uint32_t large = plan.vertexIslands[0];
    const uint32_t small = plan.vertexIslands[largeVertices];
    check(large != small, "the spheres are separate islands");
    check(plan.vertexIslands[vertexCount - 1] == large, "the loose vertex takes the nearest island");
    // The small sphere's share is about 5 quads, below the floor of 32.
    check(plan.islands[small].plannedQuads == plan.islands[small].floorQuads
            && plan.islands[small].floorQuads == 32.0,
        "the small sphere is planned at its floor");
    std::vector<float> edges(plan.vertexEdgeLengths.begin(), plan.vertexEdgeLengths.begin() + largeVertices);
    std::nth_element(edges.begin(), edges.begin() + edges.size() / 2, edges.end());
    const double median = edges[edges.size() / 2];
    const double expected = std::sqrt(4.0 * M_PI / plan.islands[large].plannedQuads);
    std::printf("planned %.1f quads, large sphere median edge %.5f, sqrt(area / quads) %.5f\n",
        plan.plannedQuads, median, expected);
    check(std::fabs(median / expected - 1.0) < 0.03, "uniform edge is sqrt(area / quads)");

    const RemeshResult run = remesh(request);
    check(RemeshStatus::Ok == run.status, "run status ok");
    const double ratio = (double)run.report.quadCount / plan.plannedQuads;
    std::printf("run %zu quads, %.3f of the plan\n", run.report.quadCount, ratio);
    check(ratio > 0.85 && ratio < 1.15, "the run lands within 15 % of the plan");

    if (0 == failures)
        std::printf("plan_test: ok\n");
    return 0 == failures ? 0 : 1;
}
