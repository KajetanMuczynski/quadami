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

// Keep parts (RemeshParameters::keepParts, TODO G18).  A map can lose a part
// a few quads across: here, pits pressed into a sphere, each about a quad
// wide and three deep at 800 quads.  An island whose result lost a quad or
// more of its input is parameterized again with the cones near the lost
// input protected, and the second result is kept only if it loses less.
//
// Which pits a run loses is chance and differs between platforms, so the
// test checks what must hold either way: the pitted sphere is run again; a
// kept second result covers more of the input, by the report's own measure
// and by the pass's; a second result that is not kept leaves the output as
// it is without the pass; a sphere without pits is not run again and comes
// out the same with the pass on or off; and the result does not depend on
// the run.

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

int failures = 0;

void check(bool condition, const char* what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// An icosphere of radius 1 with `pits` Gaussian pits of the given depth and
// angular width, spread over it by the golden angle.
AutoRemesher::RemeshRequest pittedSphere(int subdivisions, size_t pits, double depth, double width)
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
    const double pi = std::acos(-1.0);
    std::vector<std::array<double, 3>> centres;
    for (size_t k = 0; k < pits; ++k) {
        const double polar = std::acos(1.0 - 2.0 * ((double)k + 0.5) / (double)pits);
        const double turn = pi * (1.0 + std::sqrt(5.0)) * ((double)k + 0.5);
        centres.push_back({ std::cos(turn) * std::sin(polar), std::sin(turn) * std::sin(polar), std::cos(polar) });
    }
    AutoRemesher::RemeshRequest request;
    for (const auto& p : v) {
        double radius = 1.0;
        for (const auto& c : centres) {
            const double angle = std::acos(std::max(-1.0, std::min(1.0, p[0] * c[0] + p[1] * c[1] + p[2] * c[2])));
            radius -= depth * std::exp(-0.5 * (angle / width) * (angle / width));
        }
        request.positions.insert(request.positions.end(), { radius * p[0], radius * p[1], radius * p[2] });
    }
    for (const auto& t3 : f)
        request.triangles.insert(request.triangles.end(), t3.begin(), t3.end());
    return request;
}

// The value of `key` in the island's record, or `missing`.
double islandValue(const AutoRemesher::RemeshResult& result, const std::string& key, double missing)
{
    for (const auto& record : result.stageDiagnostics)
        if (record.stage == "island")
            for (const auto& value : record.values)
                if (value.first == key)
                    return value.second;
    return missing;
}

bool sameMesh(const AutoRemesher::RemeshResult& a, const AutoRemesher::RemeshResult& b)
{
    return a.mesh.positions == b.mesh.positions && a.mesh.faceSizes == b.mesh.faceSizes
        && a.mesh.faceIndices == b.mesh.faceIndices;
}

}

int main()
{
    {
        AutoRemesher::RemeshRequest request = pittedSphere(5, 20, 0.4, 0.04);
        request.parameters.targetQuadCount = 800;
        const auto on = AutoRemesher::remesh(request);
        const auto again = AutoRemesher::remesh(request);
        request.parameters.keepParts = false;
        const auto off = AutoRemesher::remesh(request);
        const bool kept = 1 == on.report.keepPartsKeptIslandCount;
        const double before = islandValue(on, "keep_parts_lost_quads", -1.0);
        const double after = islandValue(on, "keep_parts_lost_quads_after", -1.0);
        std::printf("pitted sphere: retried %zu, kept %zu, lost quads %.2f -> %.2f, uncovered %.4f (off) -> %.4f (on)\n",
            on.report.keepPartsRetriedIslandCount, on.report.keepPartsKeptIslandCount, before, after,
            off.report.uncoveredAreaShare, on.report.uncoveredAreaShare);
        check(AutoRemesher::RemeshStatus::Ok == on.status && AutoRemesher::RemeshStatus::Ok == off.status, "pitted sphere remeshes");
        check(0 == off.report.keepPartsRetriedIslandCount && 0 == off.report.keepPartsKeptIslandCount, "off: no island is run again");
        check(0.0 == islandValue(off, "keep_parts", -1.0), "off: the island's record says so");
        check(1 == on.report.keepPartsRetriedIslandCount, "on: the pitted sphere is run again");
        check(before >= 1.0, "on: it lost a quad or more first");
        check(islandValue(on, "keep_parts", -1.0) == (kept ? 2.0 : 1.0), "on: the island's record names the result kept");
        if (kept) {
            check(after <= 0.75 * before, "kept: the second result loses at most 3/4 of the first");
            check(on.report.uncoveredAreaShare < off.report.uncoveredAreaShare, "kept: the report's uncovered share is lower");
        } else {
            check(after == before, "not kept: the lost quads stay");
            check(sameMesh(on, off), "not kept: the output is the one without the pass");
        }
        check(sameMesh(on, again), "on: two runs give the same output");
    }
    {
        AutoRemesher::RemeshRequest request = pittedSphere(5, 0, 0.0, 1.0);
        request.parameters.targetQuadCount = 800;
        const auto on = AutoRemesher::remesh(request);
        request.parameters.keepParts = false;
        const auto off = AutoRemesher::remesh(request);
        std::printf("sphere: retried %zu, lost quads %.2f\n", on.report.keepPartsRetriedIslandCount,
            islandValue(on, "keep_parts_lost_quads", -1.0));
        check(AutoRemesher::RemeshStatus::Ok == on.status, "sphere remeshes");
        check(0 == on.report.keepPartsRetriedIslandCount, "sphere: not run again");
        check(sameMesh(on, off), "sphere: same output with the pass on and off");
    }
    return 0 == failures ? 0 : 1;
}
