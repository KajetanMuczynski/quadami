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

// Sharp edges lock the cross field on the remeshed island.  On a coarse
// island the remesh bevels a crease into a zigzag, the locked directions
// disagree, and the map can lose a handle: the rotated CSG bracket of the
// cad set (tools/bench/make_synthetic.py) at 1000 quads lost handles in 12 of
// 12 runs (fast and quality, 6 replicates each).  Such an island is run
// again without its sharp edges.  This test checks that the bracket keeps
// its genus and stays closed at both resolutions, and that a box, whose run
// with sharp edges is faithful, is not run again (no warning).

#include <AutoRemesher/Remesh>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool readObj(const char* path, AutoRemesher::RemeshRequest& request)
{
    FILE* file = std::fopen(path, "r");
    if (nullptr == file)
        return false;
    char line[512];
    while (std::fgets(line, sizeof(line), file)) {
        double x, y, z;
        unsigned a, b, c;
        if (0 == std::strncmp(line, "v ", 2) && 3 == std::sscanf(line + 2, "%lf %lf %lf", &x, &y, &z))
            request.positions.insert(request.positions.end(), { x, y, z });
        else if (0 == std::strncmp(line, "f ", 2) && 3 == std::sscanf(line + 2, "%u %u %u", &a, &b, &c))
            request.triangles.insert(request.triangles.end(), { a - 1, b - 1, c - 1 });
    }
    std::fclose(file);
    return !request.triangles.empty();
}

// A box of 2 x 2 x 2 with every face split into n x n squares.
AutoRemesher::RemeshRequest box(unsigned n)
{
    AutoRemesher::RemeshRequest request;
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = -1; side <= 1; side += 2) {
            const uint32_t base = (uint32_t)(request.positions.size() / 3);
            for (unsigned i = 0; i <= n; ++i) {
                for (unsigned j = 0; j <= n; ++j) {
                    double p[3];
                    p[axis] = side;
                    p[(axis + 1) % 3] = -1.0 + 2.0 * i / n;
                    p[(axis + 2) % 3] = -1.0 + 2.0 * j / n;
                    request.positions.insert(request.positions.end(), { p[0], p[1], p[2] });
                }
            }
            for (unsigned i = 0; i < n; ++i) {
                for (unsigned j = 0; j < n; ++j) {
                    const uint32_t v00 = base + i * (n + 1) + j, v10 = v00 + n + 1, v01 = v00 + 1, v11 = v10 + 1;
                    if (side > 0)
                        request.triangles.insert(request.triangles.end(), { v00, v10, v11, v00, v11, v01 });
                    else
                        request.triangles.insert(request.triangles.end(), { v00, v11, v10, v00, v01, v11 });
                }
            }
        }
    }
    return request;
}

bool sharpEdgesDropped(const AutoRemesher::RemeshResult& result)
{
    for (const auto& warning : result.report.warnings) {
        if (0 == warning.rfind("Sharp edges were not kept", 0))
            return true;
    }
    return false;
}

}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: sharp_edge_fallback_test <cad-csg-bracket-rot.obj>\n");
        return 2;
    }
    int failures = 0;
    for (const auto resolution : { AutoRemesher::MeshResolution::Fast, AutoRemesher::MeshResolution::Quality }) {
        AutoRemesher::RemeshRequest request;
        if (!readObj(argv[1], request)) {
            std::fprintf(stderr, "cannot read %s\n", argv[1]);
            return 2;
        }
        request.parameters.targetQuadCount = 1000;
        request.parameters.resolution = resolution;
        const auto result = AutoRemesher::remesh(request);
        const bool ok = AutoRemesher::RemeshStatus::Ok == result.status && 0 == result.report.handlesLost
            && 0 == result.report.handlesAdded && 0 == result.report.closedIslandHoleCount;
        std::printf("bracket %s: handles lost %zu added %zu, holes %zu, sharp edges dropped %s: %s\n",
            AutoRemesher::MeshResolution::Fast == resolution ? "fast" : "quality", result.report.handlesLost,
            result.report.handlesAdded, result.report.closedIslandHoleCount,
            sharpEdgesDropped(result) ? "yes" : "no", ok ? "ok" : "FAIL");
        failures += ok ? 0 : 1;
    }
    {
        AutoRemesher::RemeshRequest request = box(16);
        request.parameters.targetQuadCount = 1000;
        const auto result = AutoRemesher::remesh(request);
        const bool ok = AutoRemesher::RemeshStatus::Ok == result.status && !sharpEdgesDropped(result)
            && 0 == result.report.closedIslandHoleCount;
        std::printf("box: sharp edges dropped %s: %s\n", sharpEdgesDropped(result) ? "yes" : "no", ok ? "ok" : "FAIL");
        failures += ok ? 0 : 1;
    }
    return 0 == failures ? 0 : 1;
}
