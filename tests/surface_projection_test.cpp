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

// Projection of the output onto the input (SurfaceProjector, TODO 2.8).
//
// Vertices off the input land on it; boundary vertices land on the input's
// boundary; a vertex on a thin part goes to the sheet facing its way, not to
// the nearer one facing away; a move that would fold a face is not made.
// The relaxation after it evens out quads along the input, keeps the
// boundary, and puts vertices next to an input crease onto the crease.

#include <AutoRemesher/SurfaceProjector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using AutoRemesher::SurfaceProjector;
using AutoRemesher::Vector3;
using Faces = std::vector<std::vector<size_t>>;

bool check(bool ok, const char* what)
{
    std::printf("  %s: %s\n", ok ? "ok" : "FAILED", what);
    return ok;
}

// An n x n grid of unit quads at height z over [0, n]^2, counter-clockwise
// seen from +z.
void grid(size_t n, double z, std::vector<Vector3>* vertices, Faces* quads)
{
    for (size_t j = 0; j <= n; ++j)
        for (size_t i = 0; i <= n; ++i)
            vertices->push_back(Vector3((double)i, (double)j, z));
    for (size_t j = 0; j < n; ++j)
        for (size_t i = 0; i < n; ++i) {
            const size_t a = j * (n + 1) + i;
            quads->push_back({ a, a + 1, a + n + 2, a + n + 1 });
        }
}

// Triangles of a square [lo, hi]^2 at height z; `up` picks the winding.
void square(double lo, double hi, double z, bool up, std::vector<Vector3>* vertices, Faces* triangles)
{
    const size_t a = vertices->size();
    vertices->push_back(Vector3(lo, lo, z));
    vertices->push_back(Vector3(hi, lo, z));
    vertices->push_back(Vector3(hi, hi, z));
    vertices->push_back(Vector3(lo, hi, z));
    if (up) {
        triangles->push_back({ a, a + 1, a + 2 });
        triangles->push_back({ a, a + 2, a + 3 });
    } else {
        triangles->push_back({ a, a + 2, a + 1 });
        triangles->push_back({ a, a + 3, a + 2 });
    }
}

bool landsOnPlaneAndBoundary()
{
    std::printf("open plane\n");
    std::vector<Vector3> input;
    Faces triangles;
    square(0.0, 4.0, 0.0, true, &input, &triangles);
    std::vector<Vector3> output;
    Faces quads;
    grid(4, 0.1, &output, &quads);
    // Pull the boundary vertices a little inwards, as a chord of T would.
    for (auto& v : output) {
        if (0.0 == v.x())
            v.setX(0.05);
        if (4.0 == v.y())
            v.setY(3.95);
    }
    const auto result = SurfaceProjector(&input, &triangles).project(&output, quads);
    bool onPlane = true, onBoundary = true;
    for (const auto& v : output) {
        onPlane = onPlane && std::fabs(v.z()) < 1e-12;
        const bool boundary = v.x() < 0.5 || v.y() > 3.5 || v.x() > 3.5 || v.y() < 0.5;
        if (boundary)
            onBoundary = onBoundary
                && (v.x() < 1e-12 || v.y() > 4.0 - 1e-12 || v.x() > 4.0 - 1e-12 || v.y() < 1e-12);
    }
    bool ok = check(25 == result.vertices && 25 == result.projected, "every vertex projected");
    ok = check(onPlane, "every vertex on the plane") && ok;
    ok = check(onBoundary, "boundary vertices on the input boundary") && ok;
    return ok;
}

bool picksTheSheetFacingTheSameWay()
{
    std::printf("thin slab\n");
    // Bottom sheet faces up, the top sheet 0.05 above faces down: the inside
    // of a slab seen from a vertex that sits between them, nearer the top.
    std::vector<Vector3> input;
    Faces triangles;
    square(-1.0, 3.0, 0.0, true, &input, &triangles);
    square(-1.0, 3.0, 0.05, false, &input, &triangles);
    std::vector<Vector3> output;
    Faces quads;
    grid(2, 0.04, &output, &quads);
    SurfaceProjector(&input, &triangles).project(&output, quads);
    // Vertex 4 is the only interior one; the others go to the input's
    // boundary, which is out of reach, so they stay.
    bool ok = check(std::fabs(output[4].z()) < 1e-12, "the interior vertex lands on the sheet facing up");
    bool kept = true;
    for (size_t v = 0; v < output.size(); ++v)
        kept = kept && (4 == v || std::fabs(output[v].z() - 0.04) < 1e-12);
    ok = check(kept, "no vertex lands on the sheet facing down") && ok;
    return ok;
}

bool keepsMovesThatFold()
{
    std::printf("overhang\n");
    // The input is the plane z = 0 and the output a 3 x 3 grid on it, except
    // that interior vertex 6 is lifted 0.5 and leans back over vertex 5: in
    // 3D its faces are fine (scaled Jacobian 0.16), but dropped straight
    // onto the plane two of them would fold.
    std::vector<Vector3> input;
    Faces triangles;
    square(-1.0, 4.0, 0.0, true, &input, &triangles);
    std::vector<Vector3> output;
    Faces quads;
    grid(3, 0.0, &output, &quads);
    output[6] = Vector3(0.8, 1.0, 0.5);
    const auto result = SurfaceProjector(&input, &triangles).project(&output, quads);
    bool ok = check(std::fabs(output[6].z() - 0.5) < 1e-12, "the leaning vertex stays where it is");
    ok = check(1 == result.keptFolding, "it is reported as kept") && ok;
    return ok;
}

bool relaxEvensOutAlongThePlane()
{
    std::printf("relax on a plane\n");
    std::vector<Vector3> input;
    Faces triangles;
    square(-1.0, 5.0, 0.0, true, &input, &triangles);
    std::vector<Vector3> output;
    Faces quads;
    grid(4, 0.0, &output, &quads);
    const std::vector<Vector3> before = output;
    // Vertex 12 is the centre (2, 2); pull it off towards a corner.
    output[12] = Vector3(2.4, 2.3, 0.0);
    const auto creases = AutoRemesher::classifySharpEdges(input, triangles, 90.0);
    const auto result = SurfaceProjector(&input, &triangles, &creases).relax(&output, quads, 5);
    bool ok = check(9 == result.free && 0 == result.snapped, "the 9 interior vertices are free, no crease");
    const double offBefore = std::hypot(0.4, 0.3);
    const double offAfter = std::hypot(output[12].x() - 2.0, output[12].y() - 2.0);
    ok = check(offAfter < 0.5 * offBefore, "the pulled vertex moves back towards the centre") && ok;
    bool onPlane = true, boundaryKept = true;
    for (size_t v = 0; v < output.size(); ++v) {
        onPlane = onPlane && std::fabs(output[v].z()) < 1e-12;
        const bool boundary = 0 == v % 5 || 4 == v % 5 || v < 5 || v >= 20;
        if (boundary)
            boundaryKept = boundaryKept && (output[v] - before[v]).length() < 1e-12;
    }
    ok = check(onPlane, "every vertex stays on the plane") && ok;
    ok = check(boundaryKept, "boundary vertices stay") && ok;
    return ok;
}

// A sheet folded along the x axis: flat for y <= 0, rising at 45 degrees for
// y >= 0, so its crease (dihedral 45 degrees) is the line y = z = 0.
void foldedSheet(std::vector<Vector3>* vertices, Faces* triangles)
{
    const double ys[] = { -3.0, 0.0, 3.0 };
    for (const double y : ys) {
        vertices->push_back(Vector3(-1.0, y, std::max(0.0, y)));
        vertices->push_back(Vector3(5.0, y, std::max(0.0, y)));
    }
    for (size_t row = 0; row < 2; ++row) {
        const size_t a = 2 * row;
        triangles->push_back({ a, a + 1, a + 3 });
        triangles->push_back({ a, a + 3, a + 2 });
    }
}

// A 4 x 4 quad grid on the folded sheet over x in [0, 4], y in [-2, 2], with
// the middle vertex of the crease row (index 12) moved 0.1 up the slope.
void gridOnFoldedSheet(std::vector<Vector3>* vertices, Faces* quads)
{
    grid(4, 0.0, vertices, quads);
    for (auto& v : *vertices) {
        v.setY(v.y() - 2.0);
        v.setZ(std::max(0.0, v.y()));
    }
    (*vertices)[12] = Vector3(2.0, 0.1, 0.1);
}

bool relaxPutsVerticesOnCreases()
{
    std::printf("relax next to a crease\n");
    std::vector<Vector3> input;
    Faces triangles;
    foldedSheet(&input, &triangles);
    std::vector<Vector3> output;
    Faces quads;
    gridOnFoldedSheet(&output, &quads);
    const auto creases = AutoRemesher::classifySharpEdges(input, triangles, 30.0);
    const auto result = SurfaceProjector(&input, &triangles, &creases).relax(&output, quads, 5);
    bool ok = check(3 == result.snapped && 6 == result.free, "the crease row is snapped, the other 6 are free");
    ok = check(std::fabs(output[12].y()) < 1e-12 && std::fabs(output[12].z()) < 1e-12,
             "the vertex next to the crease lands on it")
        && ok;
    bool onSheet = true;
    for (const auto& v : output)
        onSheet = onSheet && std::fabs(v.z() - std::max(0.0, v.y())) < 1e-9;
    ok = check(onSheet, "every vertex stays on the sheet") && ok;

    std::printf("relax with creases off\n");
    std::vector<Vector3> unsnapped;
    Faces sameQuads;
    gridOnFoldedSheet(&unsnapped, &sameQuads);
    const auto noCreases = AutoRemesher::classifySharpEdges(input, triangles, 0.0);
    const auto off = SurfaceProjector(&input, &triangles, &noCreases).relax(&unsnapped, sameQuads, 0);
    ok = check(0 == off.snapped && std::fabs(unsnapped[12].y() - 0.1) < 1e-12, "sharp edge 0 snaps nothing") && ok;
    return ok;
}
}

int main()
{
    std::printf("\n");
    int failures = 0;
    failures += landsOnPlaneAndBoundary() ? 0 : 1;
    failures += picksTheSheetFacingTheSameWay() ? 0 : 1;
    failures += keepsMovesThatFold() ? 0 : 1;
    failures += relaxEvensOutAlongThePlane() ? 0 : 1;
    failures += relaxPutsVerticesOnCreases() ? 0 : 1;
    return failures ? 1 : 0;
}
