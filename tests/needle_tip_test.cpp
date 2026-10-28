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

// A thin blade whose sharp edges meet at a needle tip. The sharp-edge locks
// of the frame field made the field ask for no angle at the tip (a valence-0
// cone), and the singularity simplifier, which read charges mod 4, could
// also "cancel" a valence-1 tip with a valence-3 cone into one. No map can
// follow such a field: the continuous cover folded over the whole island and
// a few quads came out instead of the target (Kajetan's sword, 2026-10-21).
// Checked here: cone angles are exact (they add up to Gauss-Bonnet), no
// vertex is left without an angle, and the blade remeshes to its target.

#include <AutoRemesher/AutoRemesher>
#include <AutoRemesher/FrameField>
#include <AutoRemesher/QuadParameterizer>
#include <AutoRemesher/SingularitySimplifier>
#include <AutoRemesher/SurfaceMesh>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

using AutoRemesher::SurfaceMesh;
using AutoRemesher::Vector3;

namespace {

struct Mesh {
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;
};

// A blade along x: a rhombus cross-section of half width `width` and half
// thickness `thickness` tapering linearly from x = 0 to a point at x =
// `length`, closed at the back by a shallow pyramid. Its two cutting edges
// and two spine ridges all meet at the tip.
Mesh blade(double width, double thickness, double length, size_t rings)
{
    Mesh mesh;
    for (size_t i = 0; i < rings; ++i) {
        const double s = 1.0 - (double)i / rings, x = length * i / rings;
        mesh.vertices.push_back(Vector3(x, width * s, 0));
        mesh.vertices.push_back(Vector3(x, 0, thickness * s));
        mesh.vertices.push_back(Vector3(x, -width * s, 0));
        mesh.vertices.push_back(Vector3(x, 0, -thickness * s));
    }
    const size_t tip = mesh.vertices.size();
    mesh.vertices.push_back(Vector3(length, 0, 0));
    const size_t back = mesh.vertices.size();
    mesh.vertices.push_back(Vector3(-thickness, 0, 0));
    for (size_t i = 0; i + 1 < rings; ++i)
        for (size_t k = 0; k < 4; ++k) {
            const size_t a = 4 * i + k, b = 4 * i + (k + 1) % 4;
            const size_t c = 4 * (i + 1) + (k + 1) % 4, d = 4 * (i + 1) + k;
            mesh.triangles.push_back({ a, b, c });
            mesh.triangles.push_back({ a, c, d });
        }
    for (size_t k = 0; k < 4; ++k) {
        mesh.triangles.push_back({ 4 * (rings - 1) + k, 4 * (rings - 1) + (k + 1) % 4, tip });
        mesh.triangles.push_back({ back, (k + 1) % 4, k });
    }
    return mesh;
}

Mesh octahedronSphere(int subdivisions)
{
    Mesh mesh;
    mesh.vertices = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    mesh.triangles = { { 0, 2, 4 }, { 2, 1, 4 }, { 1, 3, 4 }, { 3, 0, 4 },
        { 2, 0, 5 }, { 1, 2, 5 }, { 3, 1, 5 }, { 0, 3, 5 } };
    for (int s = 0; s < subdivisions; ++s) {
        std::map<std::pair<size_t, size_t>, size_t> midpoints;
        const auto midpoint = [&](size_t a, size_t b) {
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            const auto found = midpoints.find(key);
            if (found != midpoints.end())
                return found->second;
            mesh.vertices.push_back(((mesh.vertices[a] + mesh.vertices[b]) * 0.5).normalized());
            midpoints[key] = mesh.vertices.size() - 1;
            return mesh.vertices.size() - 1;
        };
        std::vector<std::vector<size_t>> finer;
        for (const auto& f : mesh.triangles) {
            const size_t ab = midpoint(f[0], f[1]), bc = midpoint(f[1], f[2]), ca = midpoint(f[2], f[0]);
            finer.push_back({ f[0], ab, ca });
            finer.push_back({ f[1], bc, ab });
            finer.push_back({ f[2], ca, bc });
            finer.push_back({ ab, bc, ca });
        }
        mesh.triangles.swap(finer);
    }
    return mesh;
}

// Charges 4 - quarters of the cone angles of `field` on a closed mesh, with
// the edges matched by `rotation` (quarter turns from a corner's face to the
// opposite one) or, when it is empty, by the nearest quarter turns.
struct Cones {
    int chargeSum = 0;
    int smallestAngle = 4;
    size_t notInterior = 0;
};

Cones cones(const SurfaceMesh& mesh, const std::vector<Vector3>& field, const std::vector<int>& rotation)
{
    const auto angleIn = [&](size_t face, const Vector3& edge) {
        const Vector3 u = field[face].normalized();
        const Vector3 v = Vector3::crossProduct(mesh.faceNormal(face).normalized(), u);
        return std::atan2(Vector3::dotProduct(edge, v), Vector3::dotProduct(edge, u));
    };
    const auto residual = [&](size_t c) {
        const Vector3 edge = mesh.edgeVector(c);
        const double turn = angleIn(mesh.cornerFace(c), edge) - angleIn(mesh.cornerFace(mesh.oppositeCorner(c)), edge);
        if (rotation.empty())
            return AutoRemesher::FrameField::quarterResidual(turn);
        return std::remainder(turn + 0.5 * M_PI * rotation[c], 2.0 * M_PI);
    };
    Cones result;
    for (size_t v = 0; v < mesh.vertexCount(); ++v) {
        int quarters = 4;
        if (!AutoRemesher::FrameField::coneQuarters(mesh, v, residual, &quarters)) {
            ++result.notInterior;
            continue;
        }
        result.chargeSum += 4 - quarters;
        result.smallestAngle = std::min(result.smallestAngle, quarters);
    }
    return result;
}

size_t countAtLeast(const std::vector<int>& charges, int limit)
{
    return (size_t)std::count_if(charges.begin(), charges.end(), [&](int charge) { return charge >= limit; });
}

// On a closed genus-0 mesh: the field's charges add up to 4 * Euler
// characteristic = 8 (cone angles are exact), the simplifier's charges
// (4 - quarters) agree and it never leaves a vertex without an angle, and
// the cover's rotations give every vertex an angle and an unfolded map.
int check(const char* name, const Mesh& input, double quadEdge)
{
    int failures = 0;
    SurfaceMesh mesh(input.vertices, input.triangles);
    std::vector<Vector3> field;
    const AutoRemesher::FeatureEdgeSet sharpEdges = AutoRemesher::classifySharpEdges(mesh, 90.0);
    if (!AutoRemesher::FrameField::create(mesh, sharpEdges, &field)) {
        std::printf("%s: frame field failed\n", name);
        return 1;
    }
    const Cones fieldCones = cones(mesh, field, {});
    std::printf("%s: field charges add up to %d, smallest cone angle %d quarter turns\n",
        name, fieldCones.chargeSum, fieldCones.smallestAngle);
    if (8 != fieldCones.chargeSum || 0 != fieldCones.notInterior)
        ++failures;

    AutoRemesher::SingularitySimplifier simplifier(mesh, &field);
    simplifier.setFeatureEdges(&sharpEdges);
    const std::vector<int> before = simplifier.vertexCharges();
    simplifier.simplify();
    const std::vector<int> after = simplifier.vertexCharges();
    int sumBefore = 0, sumAfter = 0;
    for (int charge : before)
        sumBefore += charge;
    for (int charge : after)
        sumAfter += charge;
    std::printf("%s: simplifier charges add up to %d before and %d after, %zu and %zu without an angle\n",
        name, sumBefore, sumAfter, countAtLeast(before, 4), countAtLeast(after, 4));
    if (8 != sumBefore || 8 != sumAfter || countAtLeast(after, 4) > countAtLeast(before, 4))
        ++failures;

    AutoRemesher::QuadParameterizer::Result cover;
    if (!AutoRemesher::QuadParameterizer::parameterize(input.vertices, input.triangles, &field, quadEdge, sharpEdges, &cover)) {
        std::printf("%s: cover failed\n", name);
        return failures + 1;
    }
    const Cones coverCones = cones(mesh, cover.field, cover.cornerRotations);
    double flipped = -1.0;
    for (const auto& record : cover.diagnostics)
        if (record.stage == "cover_result")
            for (const auto& item : record.values)
                if (item.first == "continuous_flipped_area_fraction")
                    flipped = item.second;
    std::printf("%s: cover charges add up to %d, smallest cone angle %d quarter turns, %.4f of the area flipped\n",
        name, coverCones.chargeSum, coverCones.smallestAngle, flipped);
    // Valence 2 is the smallest cone the cover is given.
    if (8 != coverCones.chargeSum || coverCones.smallestAngle < 2 || !(flipped >= 0.0 && flipped < 0.05))
        ++failures;
    return failures;
}

} // namespace

int main()
{
    int failures = 0;
    const Mesh needle = blade(10.0, 1.0, 100.0, 20);
    failures += check("sphere", octahedronSphere(3), 0.2);
    failures += check("blade", needle, 2.0);

    // Targets where master before the fix folded the blade's map (18, 150
    // and 239 quads for 200, 700 and 800). The continuous map must keep its
    // size: a folded one has net area near 0 and is rescaled by orders of
    // magnitude. At 200 the extractor still loses quads on this coarse a
    // blade (colliding cones), which is not checked here.
    for (const size_t targetQuads : { 200, 700, 800 }) {
        AutoRemesher::AutoRemesher remesher(needle.vertices, needle.triangles);
        remesher.setTargetTriangleCount(2 * targetQuads);
        if (!remesher.remesh()) {
            std::printf("target %zu: remesh failed\n", targetQuads);
            ++failures;
            continue;
        }
        size_t quads = 0;
        for (const auto& face : remesher.remeshedQuads())
            if (4 == face.size())
                ++quads;
        double rescale = 0.0;
        for (const auto& record : remesher.diagnostics())
            if (record.stage == "cover_result")
                for (const auto& item : record.values)
                    if (item.first == "continuous_rescale")
                        rescale = item.second;
        std::printf("target %zu: %zu quads, continuous map rescaled by %g\n", targetQuads, quads, rescale);
        if (!(rescale > 0.5 && rescale < 2.0) || (targetQuads > 200 && (double)quads < 0.6 * targetQuads))
            ++failures;
    }
    if (0 != failures) {
        std::printf("FAILED: %d checks\n", failures);
        return 1;
    }
    std::printf("PASSED\n");
    return 0;
}
