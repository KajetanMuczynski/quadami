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

// The per-face counts of the quality report on hand-made faces. A face is
// folded when a corner turns against the face's area vector (scaled Jacobian
// <= 0), whatever its neighbours and whichever way it faces. Then the
// boundary loops and non-manifold vertices, and the holes counted in the
// output of islands that were closed in the input (ENVELOPE §3.2): a closed
// head at 10k came out with 2 holes and the add-on showed nothing.

#include <AutoRemesher/MeasureMesh>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

AutoRemesher::QualityReport measure(const std::vector<double>& positions,
    const std::vector<std::vector<uint32_t>>& faces)
{
    AutoRemesher::RemeshMesh mesh;
    mesh.positions = positions;
    for (const auto& face : faces) {
        mesh.faceSizes.push_back((uint32_t)face.size());
        mesh.faceIndices.insert(mesh.faceIndices.end(), face.begin(), face.end());
    }
    AutoRemesher::QualityReport report;
    AutoRemesher::measureMesh(mesh, &report);
    return report;
}

// The unit cube's corners, vertex 4 z + 2 y + x at (x, y, z), shifted by dx.
std::vector<double> cubeCorners(double dx)
{
    std::vector<double> positions;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                positions.insert(positions.end(), { x + dx, (double)y, (double)z });
    return positions;
}

// Its six faces, outward, corner indices offset by `first`; bottom, top,
// then the four sides.
std::vector<std::vector<uint32_t>> cubeFaces(uint32_t first)
{
    std::vector<std::vector<uint32_t>> faces = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 },
        { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
    for (auto& face : faces)
        for (auto& corner : face)
            corner += first;
    return faces;
}

int topologyFailures()
{
    struct Case {
        std::string name;
        std::vector<double> positions;
        std::vector<std::vector<uint32_t>> faces;
        size_t boundaryEdges;
        size_t boundaryLoops;
        size_t nonManifoldVertices;
    };
    const std::vector<double> cube = cubeCorners(0.0);
    const auto faces = cubeFaces(0);
    const std::vector<std::vector<uint32_t>> open(faces.begin() + 1, faces.end());
    const std::vector<std::vector<uint32_t>> tube(faces.begin() + 2, faces.end());
    // Two cubes sharing corner 7 of the first as corner 0 of the second.
    std::vector<double> twoCubes = cube;
    const std::vector<double> shifted = cubeCorners(1.0);
    for (int i = 1; i < 8; ++i)
        twoCubes.insert(twoCubes.end(),
            { shifted[3 * i], shifted[3 * i + 1] + 1.0, shifted[3 * i + 2] + 1.0 });
    std::vector<std::vector<uint32_t>> twoCubeFaces = faces;
    for (const auto& face : cubeFaces(7))
        twoCubeFaces.push_back(face);
    // Two squares touching at vertex 2.
    const std::vector<double> touching = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 2, 1, 0, 2, 2, 0, 1, 2, 0 };
    // A repeated corner is one corner of the fan, not a pinch.
    const std::vector<double> square = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
    const std::vector<Case> cases = {
        { "closed cube", cube, faces, 0, 0, 0 },
        { "cube without its bottom", cube, open, 4, 1, 0 },
        { "open tube", cube, tube, 8, 2, 0 },
        { "two cubes at a corner", twoCubes, twoCubeFaces, 0, 0, 1 },
        { "two squares at a corner", touching, { { 0, 1, 2, 3 }, { 2, 4, 5, 6 } }, 8, 1, 1 },
        { "repeated corner", square, { { 0, 1, 1, 2, 3 } }, 4, 1, 0 },
    };
    int failures = 0;
    printf("%-24s %16s %13s %22s\n", "case", "boundary edges", "loops", "non-manifold vertices");
    for (const auto& c : cases) {
        const AutoRemesher::QualityReport report = measure(c.positions, c.faces);
        const bool ok = report.boundaryEdgeCount == c.boundaryEdges && report.boundaryLoopCount == c.boundaryLoops
            && report.nonManifoldVertexCount == c.nonManifoldVertices;
        failures += ok ? 0 : 1;
        printf("%-24s %6zu (want %zu) %4zu (want %zu) %10zu (want %zu)%s\n", c.name.c_str(),
            report.boundaryEdgeCount, c.boundaryEdges, report.boundaryLoopCount, c.boundaryLoops,
            report.nonManifoldVertexCount, c.nonManifoldVertices, ok ? "" : "  FAIL");
    }
    return failures;
}

// Three islands one after the other, as remesh() returns them: a closed
// input that came out closed, a closed input that came out without its
// bottom, and an open input (a grid) whose border is not a hole. A closed
// island without output is counted as closed, not as opened.
int closedIslandFailures()
{
    using Island = ::AutoRemesher::AutoRemesher::RemeshedIsland;
    AutoRemesher::RemeshMesh mesh;
    std::vector<Island> islands;
    const auto add = [&](const std::vector<double>& positions,
                         const std::vector<std::vector<uint32_t>>& faces, bool closedInput) {
        const uint32_t first = (uint32_t)(mesh.positions.size() / 3);
        mesh.positions.insert(mesh.positions.end(), positions.begin(), positions.end());
        for (const auto& face : faces) {
            mesh.faceSizes.push_back((uint32_t)face.size());
            for (const uint32_t corner : face)
                mesh.faceIndices.push_back(first + corner);
        }
        Island island;
        island.vertexCount = positions.size() / 3;
        island.faceCount = faces.size();
        island.closedInput = closedInput;
        islands.push_back(island);
    };
    const auto faces = cubeFaces(0);
    add(cubeCorners(0.0), faces, true);
    add({}, {}, true);
    add(cubeCorners(2.0), std::vector<std::vector<uint32_t>>(faces.begin() + 1, faces.end()), true);
    add({ 0, 0, 5, 1, 0, 5, 1, 1, 5, 0, 1, 5 }, { { 0, 1, 2, 3 } }, false);

    AutoRemesher::QualityReport report;
    AutoRemesher::measureMesh(mesh, &report);
    AutoRemesher::measureClosedIslands(mesh, islands, &report);
    const bool ok = 3 == report.closedInputIslandCount && 1 == report.openedClosedIslandCount
        && 1 == report.closedIslandHoleCount && 4 == report.closedIslandHoleEdgeCount
        && 2 == report.boundaryLoopCount && 8 == report.boundaryEdgeCount;
    printf("closed islands %zu (want 3), opened %zu (want 1), holes %zu (want 1), hole edges %zu (want 4), "
           "all loops %zu (want 2)%s\n\n",
        report.closedInputIslandCount, report.openedClosedIslandCount, report.closedIslandHoleCount,
        report.closedIslandHoleEdgeCount, report.boundaryLoopCount, ok ? "" : "  FAIL");
    return ok ? 0 : 1;
}

// Lost parts: the input is two unit cubes, at x = 0 and at x = 10, the output
// only the first.  Input area 12 at a target of 12 quads makes the quad width
// 1, so the second cube's corners are 9 to 10 quad widths from the output.
// Islands without output are not collapsed; one with 2 of 6 planned quads is.
int lostPartFailures()
{
    using Island = ::AutoRemesher::AutoRemesher::RemeshedIsland;
    std::vector<double> positions = cubeCorners(0.0);
    const std::vector<double> second = cubeCorners(10.0);
    positions.insert(positions.end(), second.begin(), second.end());
    std::vector<uint32_t> triangles;
    for (const uint32_t first : { 0u, 8u }) {
        for (const auto& face : cubeFaces(first))
            triangles.insert(triangles.end(), { face[0], face[1], face[2], face[0], face[2], face[3] });
    }
    AutoRemesher::RemeshMesh mesh;
    mesh.positions = cubeCorners(0.0);
    for (const auto& face : cubeFaces(0)) {
        mesh.faceSizes.push_back(4);
        mesh.faceIndices.insert(mesh.faceIndices.end(), face.begin(), face.end());
    }
    std::vector<Island> islands(3);
    islands[0].faceCount = 6;
    islands[0].targetQuads = 6.0;
    islands[1].targetQuads = 6.0;
    islands[2].faceCount = 2;
    islands[2].targetQuads = 6.0;

    AutoRemesher::QualityReport report;
    AutoRemesher::measureLostParts(positions, triangles, 12, mesh, islands, &report);
    const bool ok = 8 == report.farInputVertexCount && std::fabs(report.farthestInputQuadWidths - 10.0) < 1e-9
        && 1 == report.collapsedIslandCount && 2 == report.smallestCollapsedIslandFaces
        && 6 == report.smallestCollapsedIslandTargetQuads && 1 == report.lostPieceCount
        && 8 == report.pieceLostVertexCount && 0 == report.thinLostVertexCount && 0 == report.otherLostVertexCount;
    AutoRemesher::QualityReport whole;
    AutoRemesher::measureLostParts(positions, std::vector<uint32_t>(triangles.begin(), triangles.begin() + 36), 12,
        mesh, {}, &whole);
    const bool wholeOk = 0 == whole.farInputVertexCount && whole.farthestInputQuadWidths < 1e-9;
    printf("far vertices %zu (want 8), farthest %.3f quad widths (want 10), collapsed %zu (want 1), "
           "smallest %zu of %zu (want 2 of 6), lost pieces %zu with %zu vertices (want 1 with 8); "
           "kept cube: far %zu (want 0)%s\n\n",
        report.farInputVertexCount, report.farthestInputQuadWidths, report.collapsedIslandCount,
        report.smallestCollapsedIslandFaces, report.smallestCollapsedIslandTargetQuads,
        report.lostPieceCount, report.pieceLostVertexCount,
        whole.farInputVertexCount, ok && wholeOk ? "" : "  FAIL");
    return ok && wholeOk ? 0 : 1;
}

// Why far vertices are lost, on one piece whose output covers only its end at
// x = 0: a slab 10 long, 1 wide and 0.1 thick, whose far corners have the
// other side 0.1 away (thin), and a cube of side 10, whose far corners have
// no other side within two quad widths (other).  Quad width about 1 in both.
// Then a single-sided strip, lost along its open edges.
int lostPartCauseFailures()
{
    const auto box = [](double x, double y, double z) {
        std::vector<double> corners = cubeCorners(0.0);
        for (size_t i = 0; i < corners.size(); i += 3) {
            corners[i] *= x;
            corners[i + 1] *= y;
            corners[i + 2] *= z;
        }
        std::vector<uint32_t> triangles;
        for (const auto& face : cubeFaces(0))
            triangles.insert(triangles.end(), { face[0], face[1], face[2], face[0], face[2], face[3] });
        return std::make_pair(corners, triangles);
    };
    // One quad on the x = 0 face, within one quad width of its four corners.
    AutoRemesher::RemeshMesh end;
    end.positions = { 0, 0, 0, 0, 1, 0, 0, 1, 0.1, 0, 0, 0.1 };
    end.faceSizes = { 4 };
    end.faceIndices = { 0, 1, 2, 3 };
    const auto slab = box(10.0, 1.0, 0.1);
    AutoRemesher::QualityReport thin;
    AutoRemesher::measureLostParts(slab.first, slab.second, 22, end, {}, &thin);
    // The thickness in quad widths, recorded as a diagnostic.
    const double expectedThickness = 0.1 / std::sqrt(22.2 / 22.0);
    double thickness = 0.0;
    for (const auto& it : thin.diagnostics) {
        if (it.first == "lost_thin_thickness_p50")
            thickness = it.second;
    }
    const bool thinOk = 4 == thin.farInputVertexCount && 4 == thin.thinLostVertexCount && 0 == thin.otherLostVertexCount
        && 0 == thin.openEdgeLostVertexCount && 0 == thin.lostPieceCount && std::fabs(thickness / expectedThickness - 1.0) < 1e-9;

    AutoRemesher::RemeshMesh corner;
    corner.positions = { 0, 0, 0, 0.5, 0, 0, 0.5, 0.5, 0, 0, 0.5, 0 };
    corner.faceSizes = { 4 };
    corner.faceIndices = { 0, 1, 2, 3 };
    const auto cube = box(10.0, 10.0, 10.0);
    AutoRemesher::QualityReport other;
    AutoRemesher::measureLostParts(cube.first, cube.second, 600, corner, {}, &other);
    const bool otherOk = 7 == other.farInputVertexCount && 7 == other.otherLostVertexCount
        && 0 == other.openEdgeLostVertexCount
        && 0 == other.thinLostVertexCount && 0 == other.lostPieceCount && other.diagnostics.empty();

    // A single-sided strip 10 long and 1 wide: no other side, but its far
    // corners lie on its open edges.  Quad width 1.
    const std::vector<double> strip = { 0, 0, 0, 10, 0, 0, 10, 1, 0, 0, 1, 0 };
    AutoRemesher::RemeshMesh stripEnd;
    stripEnd.positions = { 0, 0, 0, 0.5, 0, 0, 0.5, 1, 0, 0, 1, 0 };
    stripEnd.faceSizes = { 4 };
    stripEnd.faceIndices = { 0, 1, 2, 3 };
    AutoRemesher::QualityReport open;
    AutoRemesher::measureLostParts(strip, { 0, 1, 2, 0, 2, 3 }, 10, stripEnd, {}, &open);
    const bool openOk = 2 == open.farInputVertexCount && 2 == open.openEdgeLostVertexCount
        && 0 == open.thinLostVertexCount && 0 == open.otherLostVertexCount;
    printf("slab: far %zu, thin %zu (want 4, 4), thickness %.4f quad widths (want %.4f); "
           "cube: far %zu, other %zu (want 7, 7); strip: far %zu, open edge %zu (want 2, 2)%s\n\n",
        thin.farInputVertexCount, thin.thinLostVertexCount, thickness, expectedThickness,
        other.farInputVertexCount, other.otherLostVertexCount, open.farInputVertexCount,
        open.openEdgeLostVertexCount, thinOk && otherOk && openOk ? "" : "  FAIL");
    return thinOk && otherOk && openOk ? 0 : 1;
}

// Handles per island from the islands' twice-handle counts: a torus that came
// out as a sphere, a sphere that gained two handles, an island with a
// non-manifold input (not measured) and an unchanged genus-2 island.
int handleFailures()
{
    using Island = ::AutoRemesher::AutoRemesher::RemeshedIsland;
    std::vector<Island> islands(4);
    const long long counts[4][2] = { { 2, 0 }, { 0, 4 }, { -1, -1 }, { 4, 4 } };
    for (size_t i = 0; i < islands.size(); ++i) {
        islands[i].inputTwiceHandles = counts[i][0];
        islands[i].outputTwiceHandles = counts[i][1];
    }
    AutoRemesher::RemeshMesh mesh;
    AutoRemesher::QualityReport report;
    AutoRemesher::measureClosedIslands(mesh, islands, &report);
    const bool ok = 1 == report.handlesLost && 2 == report.handlesAdded && 2 == report.handlesChangedIslandCount;
    printf("handles lost %zu (want 1), added %zu (want 2), islands changed %zu (want 2)%s\n\n",
        report.handlesLost, report.handlesAdded, report.handlesChangedIslandCount, ok ? "" : "  FAIL");
    return ok ? 0 : 1;
}

}

int main()
{
    struct Case {
        std::string name;
        std::vector<double> positions;
        std::vector<std::vector<uint32_t>> faces;
        size_t folded;
        size_t degenerate;
        size_t flips;
    };
    const std::vector<double> square = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
    // A 2 x 2 grid of unit quads, vertex (i, j) at index 3 * j + i.
    std::vector<double> grid;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            grid.insert(grid.end(), { (double)i, (double)j, 0.0 });
    const std::vector<Case> cases = {
        { "convex quad", square, { { 0, 1, 2, 3 } }, 0, 0, 0 },
        { "convex quad, reversed", square, { { 3, 2, 1, 0 } }, 0, 0, 0 },
        { "skew quad", { 0, 0, 0, 1, 0, 0.2, 1, 1, 0, 0, 1, 0.2 }, { { 0, 1, 2, 3 } }, 0, 0, 0 },
        { "dart", { 0, 0, 0, 2, 0, 0, 0.5, 0.5, 0, 0, 2, 0 }, { { 0, 1, 2, 3 } }, 1, 0, 0 },
        { "bow-tie", { 0, 0, 0, 3, 1, 0, 3, 0, 0, 0, 2, 0 }, { { 0, 1, 2, 3 } }, 1, 0, 0 },
        { "convex pentagon", { 0, 0, 0, 2, 0, 0, 3, 1, 0, 1, 2, 0, -1, 1, 0 }, { { 0, 1, 2, 3, 4 } }, 0, 0, 0 },
        { "arrow pentagon", { 0, 0, 0, 2, 0, 0, 2, 2, 0, 1, 0.5, 0, 0, 2, 0 }, { { 0, 1, 2, 3, 4 } }, 1, 0, 0 },
        { "repeated corner", square, { { 0, 1, 1, 2 } }, 0, 1, 0 },
        { "zero area", { 0, 0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0 }, { { 0, 1, 2, 3 } }, 0, 1, 0 },
        // One quad wound the other way: its two inner edges are flips, but
        // no corner of any face turns back, so nothing is folded.
        { "reversed quad in a grid", grid, { { 0, 1, 4, 3 }, { 1, 2, 5, 4 }, { 3, 4, 7, 6 }, { 4, 7, 8, 5 } }, 0, 0, 2 },
    };

    int failures = 0;
    failures += topologyFailures();
    failures += closedIslandFailures();
    failures += handleFailures();
    failures += lostPartFailures();
    failures += lostPartCauseFailures();
    printf("%-24s %13s %17s %11s\n", "case", "folded", "degenerate", "flips");
    for (const auto& c : cases) {
        const AutoRemesher::QualityReport report = measure(c.positions, c.faces);
        const bool ok = report.foldedFaceCount == c.folded && report.degenerateFaceCount == c.degenerate
            && report.orientationFlipCount == c.flips;
        failures += ok ? 0 : 1;
        printf("%-24s %6zu (want %zu) %10zu (want %zu) %4zu (want %zu)%s\n", c.name.c_str(),
            report.foldedFaceCount, c.folded, report.degenerateFaceCount, c.degenerate,
            report.orientationFlipCount, c.flips, ok ? "" : "  FAIL");
    }
    return failures ? 1 : 0;
}
