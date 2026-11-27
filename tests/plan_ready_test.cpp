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

// RemeshCallbacks::planReady (docs/PREVIEW.md §4.8): a run that plans
// itself on the way gives the same mesh as a plain run, to the bit, and the
// plan it hands over is the one planRemesh() makes; the progress reaches
// PlanReadyProgress with the plan and never goes back; a handler that says
// no ends the run as Cancelled.  Two spheres, so two islands are planned
// before either is parameterized.

#include <AutoRemesher/Remesh>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace AutoRemesher;

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s: %s\n", condition ? "ok" : "FAILED", what);
    if (!condition)
        ++failures;
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

bool samePlan(const RemeshPlan& a, const RemeshPlan& b)
{
    if (a.vertexEdgeLengths != b.vertexEdgeLengths || a.vertexIslands != b.vertexIslands
        || a.islands.size() != b.islands.size() || a.plannedQuads != b.plannedQuads)
        return false;
    for (size_t i = 0; i < a.islands.size(); ++i) {
        if (a.islands[i].areaShare != b.islands[i].areaShare || a.islands[i].plannedQuads != b.islands[i].plannedQuads
            || a.islands[i].floorQuads != b.islands[i].floorQuads || a.islands[i].leftOut != b.islands[i].leftOut)
            return false;
    }
    const PlanCover &ca = a.cover, &cb = b.cover;
    return ca.positions == cb.positions && ca.triangles == cb.triangles && ca.cornerUvs == cb.cornerUvs
        && ca.triangleIslands == cb.triangleIslands;
}

// The cover's uv area per island, in quads.
std::vector<double> coverQuads(const RemeshPlan& plan)
{
    std::vector<double> quads(plan.islands.size(), 0.0);
    const PlanCover& cover = plan.cover;
    for (size_t f = 0; f < cover.triangleIslands.size(); ++f) {
        const float* uv = &cover.cornerUvs[6 * f];
        quads[cover.triangleIslands[f]] += 0.5 * std::fabs((uv[2] - uv[0]) * (uv[5] - uv[1]) - (uv[4] - uv[0]) * (uv[3] - uv[1]));
    }
    return quads;
}

}

int main()
{
    RemeshRequest request;
    sphere(4, 0.0, 1.0, &request.positions, &request.triangles);
    sphere(3, 3.0, 0.5, &request.positions, &request.triangles);
    request.parameters.targetQuadCount = 1200;

    const RemeshResult plain = remesh(request);
    check(RemeshStatus::Ok == plain.status, "plain run succeeds");
    const RemeshResult planned = planRemesh(request);
    check(RemeshStatus::Ok == planned.status, "planRemesh succeeds");

    std::printf("run that plans itself and goes on\n");
    RemeshResult handed;
    int calls = 0;
    std::vector<float> fractions;
    float atPlan = -1.0f;
    RemeshCallbacks callbacks;
    callbacks.progress = [&](float fraction, const char*) { fractions.push_back(fraction); };
    callbacks.planReady = [&](const RemeshResult& plan) {
        ++calls;
        handed = plan;
        atPlan = fractions.empty() ? 0.0f : fractions.back();
        return true;
    };
    const RemeshResult continued = remesh(request, callbacks);
    check(RemeshStatus::Ok == continued.status, "the run succeeds");
    check(1 == calls, "the handler is called once");
    check(continued.mesh.positions == plain.mesh.positions && continued.mesh.faceSizes == plain.mesh.faceSizes
            && continued.mesh.faceIndices == plain.mesh.faceIndices,
        "same mesh as a plain run, to the bit");
    check(RemeshStatus::Ok == handed.status && samePlan(handed.plan, planned.plan), "the handed plan is planRemesh()'s");
    check(2 == handed.plan.islands.size() && 2 == handed.report.islandCount, "both islands planned");
    {
        const PlanCover& cover = planned.plan.cover;
        const size_t faces = cover.triangleIslands.size();
        bool indices = faces > 0 && cover.triangles.size() == 3 * faces && cover.cornerUvs.size() == 6 * faces;
        for (const uint32_t v : cover.triangles)
            indices = indices && 3 * (size_t)v < cover.positions.size();
        check(indices, "the cover has a map per corner and its corners name its vertices");
        // One uv unit is one quad: the map covers about the planned quads.
        const std::vector<double> quads = coverQuads(planned.plan);
        bool sized = true;
        for (size_t i = 0; i < quads.size(); ++i) {
            const double ratio = quads[i] / planned.plan.islands[i].plannedQuads;
            std::printf("  island %zu: cover %.0f quads, planned %.0f\n", i, quads[i], planned.plan.islands[i].plannedQuads);
            sized = sized && ratio > 0.8 && ratio < 1.25;
        }
        check(sized, "each island's map covers its planned quads within 25 %");
    }
    check(handed.report.inputTriangleCount == request.triangles.size() / 3, "the plan's report has the input counts");
    check(atPlan <= PlanReadyProgress + 1e-6f && atPlan > 0.1f, "progress has got near PlanReadyProgress at the plan");
    bool rising = true;
    for (size_t i = 1; i < fractions.size(); ++i)
        rising = rising && fractions[i] + 1e-6f >= fractions[i - 1];
    check(rising, "progress never goes back");
    bool after = false;
    for (const float f : fractions)
        after = after || f > PlanReadyProgress;
    check(after && fractions.back() >= 0.99f, "progress goes on past the plan to the end");

    std::printf("handler that stops the run\n");
    RemeshCallbacks stopping;
    stopping.planReady = [](const RemeshResult&) { return false; };
    const RemeshResult stopped = remesh(request, stopping);
    check(RemeshStatus::Cancelled == stopped.status, "the run ends as Cancelled");
    check(stopped.mesh.faceSizes.empty(), "and has no mesh");

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
