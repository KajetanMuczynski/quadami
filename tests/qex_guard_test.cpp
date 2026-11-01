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

// libQEx with the fork's guards (TODO 6.1b, thirdparty/libQEx/PATCHES.md).
//
// A flat square mapped onto the grid comes out as its quads. The same map
// under a grid-vertex cap below its size ends with a reason, and so do three
// recorded folded maps (data/README.md) on which unguarded libQEx looped
// forever, overflowed the heap, or grew until memory ran out, so the caller
// can fall back. ctest's timeout and sanitizer builds catch a guard that stops
// working. The handle count that decides whether a libQEx island is traced
// again is checked on small surfaces. Our own tracing, the fallback, stops
// under the same bound when the map's area is far over the target.

#include <AutoRemesher/QexExtraction>
#include <AutoRemesher/QuadExtractor>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using AutoRemesher::QexLimits;
using AutoRemesher::QexOutcome;
using AutoRemesher::Vector2;
using AutoRemesher::Vector3;

int failures = 0;

void expect(bool condition, const char* what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Map {
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;
    std::vector<std::vector<Vector2>> uvs;
};

// n x n cells, two triangles each, `uvPerCell` grid units per cell edge.
Map makeSquare(size_t n, double uvPerCell)
{
    Map map;
    for (size_t j = 0; j <= n; ++j) {
        for (size_t i = 0; i <= n; ++i)
            map.vertices.emplace_back((double)i, (double)j, 0.0);
    }
    const auto at = [n](size_t i, size_t j) { return j * (n + 1) + i; };
    const auto uv = [uvPerCell](size_t i, size_t j) { return Vector2(i * uvPerCell, j * uvPerCell); };
    for (size_t j = 0; j < n; ++j) {
        for (size_t i = 0; i < n; ++i) {
            map.triangles.push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1) });
            map.uvs.push_back({ uv(i, j), uv(i + 1, j), uv(i + 1, j + 1) });
            map.triangles.push_back({ at(i, j), at(i + 1, j + 1), at(i, j + 1) });
            map.uvs.push_back({ uv(i, j), uv(i + 1, j + 1), uv(i, j + 1) });
        }
    }
    return map;
}

// The OBJ the fixture is stored as: `v` per vertex, `vt` per triangle corner,
// `f a/ta b/tb c/tc`, all doubles in round-trip precision.
bool readMap(const char* path, Map* map)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::vector<Vector2> cornerUvs;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string tag;
        fields >> tag;
        if ("v" == tag) {
            double x, y, z;
            fields >> x >> y >> z;
            map->vertices.emplace_back(x, y, z);
        } else if ("vt" == tag) {
            double u, v;
            fields >> u >> v;
            cornerUvs.emplace_back(u, v);
        } else if ("f" == tag) {
            std::vector<size_t> triangle;
            std::vector<Vector2> uvs;
            std::string corner;
            while (fields >> corner) {
                const size_t slash = corner.find('/');
                triangle.push_back(std::stoul(corner.substr(0, slash)) - 1);
                uvs.push_back(cornerUvs.at(std::stoul(corner.substr(slash + 1)) - 1));
            }
            map->triangles.push_back(triangle);
            map->uvs.push_back(uvs);
        }
    }
    return !map->triangles.empty();
}

// As QuadExtractor sets them.
QexLimits limitsFor(const Map& map, size_t targetQuads)
{
    QexLimits limits;
    limits.maxSteps = AutoRemesher::QexStepsPerTriangle * (map.triangles.size() + 100);
    limits.maxGridVertices = AutoRemesher::QexGridVerticesPerQuad * targetQuads + 1000;
    return limits;
}

}

int main(int argc, char* argv[])
{
    if (argc < 4) {
        std::printf("usage: qex_guard_test <data/qex-budget-ellipsoid.obj> <data/qex-oneway-sculpt-parts.obj> "
                    "<data/qex-faceloop-sculpt-parts.obj>\n");
        return 1;
    }
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> polygons;
    QexOutcome outcome;

    {
        // 8 x 8 cells, half a grid unit each: 4 x 4 quads.
        const Map square = makeSquare(8, 0.5);
        const bool used = AutoRemesher::extractWithQex(square.vertices, square.triangles, square.uvs,
            limitsFor(square, 16), &vertices, &polygons, &outcome);
        expect(used, "a flat square is extracted");
        expect(16 == polygons.size(), "a flat square gives its 16 quads");
        expect(0 == outcome.nonQuads, "a flat square gives quads only");
        expect(outcome.reason.empty(), "no fallback reason when extracted");

        QexLimits tight;
        tight.maxGridVertices = 4;
        const bool capped = AutoRemesher::extractWithQex(square.vertices, square.triangles, square.uvs,
            tight, &vertices, &polygons, &outcome);
        expect(!capped, "the grid-vertex cap stops the extraction");
        expect(std::string::npos != outcome.reason.find("grid vertex cap"), "the cap is named as the reason");
    }

    {
        // Three 4 x 4 cell sheets on one edge (vine ribbons glued along an
        // edge): OpenMesh takes no edge with three triangles, so the island
        // is cut there and each sheet gives its own 2 x 2 quads.
        const size_t n = 4;
        Map fin;
        const Vector3 away[] = { Vector3(1.0, 0.0, 0.0), Vector3(-1.0, 0.0, 0.0), Vector3(0.0, 0.0, 1.0) };
        std::vector<size_t> spine;
        for (size_t j = 0; j <= n; ++j) {
            spine.push_back(fin.vertices.size());
            fin.vertices.emplace_back(0.0, (double)j, 0.0);
        }
        for (const Vector3& direction : away) {
            std::vector<size_t> index(spine);
            index.resize((n + 1) * (n + 1));
            for (size_t i = 1; i <= n; ++i) {
                for (size_t j = 0; j <= n; ++j) {
                    index[i * (n + 1) + j] = fin.vertices.size();
                    fin.vertices.push_back(direction * (double)i + Vector3(0.0, (double)j, 0.0));
                }
            }
            const auto at = [&](size_t i, size_t j) { return index[i * (n + 1) + j]; };
            const auto uv = [](size_t i, size_t j) { return Vector2(i * 0.5, j * 0.5); };
            for (size_t i = 0; i < n; ++i) {
                for (size_t j = 0; j < n; ++j) {
                    fin.triangles.push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1) });
                    fin.uvs.push_back({ uv(i, j), uv(i + 1, j), uv(i + 1, j + 1) });
                    fin.triangles.push_back({ at(i, j), at(i + 1, j + 1), at(i, j + 1) });
                    fin.uvs.push_back({ uv(i, j), uv(i + 1, j + 1), uv(i, j + 1) });
                }
            }
        }
        const bool used = AutoRemesher::extractWithQex(fin.vertices, fin.triangles, fin.uvs,
            limitsFor(fin, 12), &vertices, &polygons, &outcome);
        expect(used, "three sheets on one edge are extracted");
        expect(12 == polygons.size(), "three sheets on one edge give their 12 quads");
        expect(2 * (n + 1) == outcome.cutVertices, "the shared edge is cut into three");

        const Map square = makeSquare(8, 0.5);
        AutoRemesher::extractWithQex(square.vertices, square.triangles, square.uvs,
            limitsFor(square, 16), &vertices, &polygons, &outcome);
        expect(0 == outcome.cutVertices, "a manifold square is not cut");
    }

    {
        // The fallback tracer: a square with 16 grid units of area traces at a
        // target of 1 (bound 8 + 1,000); the same square at 10 grid units per
        // cell, 6,400 in all, is left empty before any tracing.
        for (const double uvPerCell : { 0.5, 10.0 }) {
            const Map square = makeSquare(8, uvPerCell);
            AutoRemesher::QuadExtractor extractor(&square.vertices, &square.triangles, &square.uvs);
            extractor.setUseQex(false);
            extractor.setTargetQuadCount(1);
            const bool extracted = extractor.extract();
            const bool capped = extractor.qexOutcome().traceCapped;
            if (uvPerCell < 1.0) {
                expect(extracted && !capped, "a square within the bound is traced");
                expect(!extractor.remeshedQuads().empty(), "a traced square has faces");
            } else {
                expect(!extracted && capped, "a square far over the bound is not traced");
                expect(extractor.remeshedQuads().empty(), "an untraced square has no faces");
                expect(0 == extractor.qexOutcome().traceCrossPoints, "the cap comes before the tracing");
            }
        }
    }

    {
        // Handle count: a quad torus has one handle, a disk none, and two disks
        // pinched at a vertex or touching along an edge used three times none.
        const size_t n = 6;
        std::vector<std::vector<size_t>> torus;
        for (size_t j = 0; j < n; ++j) {
            for (size_t i = 0; i < n; ++i)
                torus.push_back({ j * n + i, j * n + (i + 1) % n, ((j + 1) % n) * n + (i + 1) % n, ((j + 1) % n) * n + i });
        }
        expect(2 == AutoRemesher::twiceHandleCount(n * n, torus), "a torus has one handle");
        const std::vector<std::vector<size_t>> disk = { { 0, 1, 2, 3 } };
        expect(0 == AutoRemesher::twiceHandleCount(4, disk), "a disk has no handle");
        const std::vector<std::vector<size_t>> pinched = { { 0, 1, 2, 3 }, { 0, 4, 5, 6 } };
        expect(0 == AutoRemesher::twiceHandleCount(7, pinched), "a pinch is no handle");
        const std::vector<std::vector<size_t>> fin = { { 0, 1, 2, 3 }, { 1, 0, 4, 5 }, { 0, 1, 6, 7 } };
        expect(0 == AutoRemesher::twiceHandleCount(8, fin), "an edge with three faces is no handle");
    }

    // Each fixture with the target of the run it came from and the guard that must stop it.
    const struct {
        const char* path;
        size_t targetQuads;
        const char* reason;
    } folded[] = {
        { argv[1], 500, "step budget" },
        { argv[2], 5000, "connected one way only" },
        { argv[3], 15000, "face loop does not close" },
    };
    for (const auto& fixture : folded) {
        Map map;
        if (!readMap(fixture.path, &map)) {
            std::printf("FAIL: %s does not read\n", fixture.path);
            ++failures;
            continue;
        }
        const bool used = AutoRemesher::extractWithQex(map.vertices, map.triangles, map.uvs,
            limitsFor(map, fixture.targetQuads), &vertices, &polygons, &outcome);
        if (used || std::string::npos == outcome.reason.find(fixture.reason)) {
            std::printf("FAIL: %s: expected a fallback for \"%s\", got %s \"%s\"\n", fixture.path,
                fixture.reason, used ? "an extraction" : "a fallback", outcome.reason.c_str());
            ++failures;
        }
    }

    if (failures)
        return 1;
    std::printf("qex guards: all passed\n");
    return 0;
}
