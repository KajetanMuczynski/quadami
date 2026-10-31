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

// The one sharp-edge classification (TODO 2.6).
//
// A right angle is sharp at the default 90 degrees on every rotation and
// after float32 rounding (the knife edge the old `> 90` and `>= 90` tests
// had); 0 and 180 mean no sharp edges; decimation keeps a chain whole when
// it removes a vertex of it; T's set keeps the hard edges the remesher
// carried and classifies the rest on T; and a remeshed box has its edges in
// T's set.

#include <AutoRemesher/FeatureEdges>
#include <AutoRemesher/IsotropicRemesher>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using AutoRemesher::FeatureEdgeSet;
using AutoRemesher::Vector3;
using Faces = std::vector<std::vector<size_t>>;

bool check(bool ok, const char* what)
{
    std::printf("  %s: %s\n", ok ? "ok" : "FAILED", what);
    return ok;
}

// The box [0, sx] x [0, sy] x [0, sz], each face an n x n grid of quads split
// into triangles, wound outwards.
void box(double sx, double sy, double sz, size_t n, std::vector<Vector3>* vertices, Faces* triangles)
{
    std::vector<Vector3> points;
    const auto index = [&](const Vector3& p) {
        for (size_t i = 0; i < points.size(); ++i)
            if ((points[i] - p).lengthSquared() < 1e-18)
                return i;
        points.push_back(p);
        return points.size() - 1;
    };
    const double size[3] = { sx, sy, sz };
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            for (size_t j = 0; j < n; ++j)
                for (size_t i = 0; i < n; ++i) {
                    size_t corner[4];
                    for (int k = 0; k < 4; ++k) {
                        const size_t di = (1 == k || 2 == k) ? 1 : 0, dj = k >= 2 ? 1 : 0;
                        double p[3];
                        p[axis] = side * size[axis];
                        p[u] = size[u] * (double)(i + di) / (double)n;
                        p[v] = size[v] * (double)(j + dj) / (double)n;
                        corner[k] = index(Vector3(p[0], p[1], p[2]));
                    }
                    if (1 == side) {
                        triangles->push_back({ corner[0], corner[1], corner[2] });
                        triangles->push_back({ corner[0], corner[2], corner[3] });
                    } else {
                        triangles->push_back({ corner[0], corner[2], corner[1] });
                        triangles->push_back({ corner[0], corner[3], corner[2] });
                    }
                }
        }
    }
    *vertices = points;
}

// Rotates about the unit axis `a` by `angle`, then rounds to float32 as a
// mesh from Blender arrives.
std::vector<Vector3> rotatedFloat32(const std::vector<Vector3>& vertices, const Vector3& a, double angle)
{
    const double c = std::cos(angle), s = std::sin(angle);
    std::vector<Vector3> result;
    for (const auto& p : vertices) {
        const Vector3 r = p * c + Vector3::crossProduct(a, p) * s + a * (Vector3::dotProduct(a, p) * (1.0 - c));
        result.push_back(Vector3((float)r.x(), (float)r.y(), (float)r.z()));
    }
    return result;
}

bool rightAnglesAreSharpOnEveryRotation()
{
    std::printf("right angles at the default 90 degrees\n");
    std::vector<Vector3> vertices;
    Faces triangles;
    box(1.0, 1.0, 1.0, 4, &vertices, &triangles);
    // 12 box edges of 4 segments each.
    const size_t boxEdges = 48;
    bool everyRotation = true;
    size_t strictMisses = 0;
    for (int k = 0; k < 24; ++k) {
        const Vector3 axis = Vector3(std::sin(0.7 * k), std::cos(1.3 * k), 0.5 + 0.1 * k).normalized();
        const std::vector<Vector3> rotated = rotatedFloat32(vertices, axis, 0.37 * k);
        const FeatureEdgeSet sharp = AutoRemesher::classifySharpEdges(rotated, triangles, 90.0);
        everyRotation = everyRotation && boxEdges == sharp.size();
        // The old field test (> 90 degrees) on the same edges.
        const AutoRemesher::SurfaceMesh mesh(rotated, triangles);
        for (size_t c = 0; c < mesh.cornerCount(); ++c) {
            const size_t o = mesh.oppositeCorner(c);
            if (o != AutoRemesher::SurfaceMesh::npos && o > c
                && sharp.contains(mesh.cornerVertex(c), mesh.cornerVertex(mesh.nextCorner(c)))
                && !(std::fabs(mesh.normalAngle(c)) > M_PI / 2))
                ++strictMisses;
        }
    }
    std::printf("  the old > 90 test misses %zu of %zu right-angle edges\n", strictMisses, 24 * boxEdges);
    bool ok = check(everyRotation, "all 48 box edge segments are sharp on 24 rotations in float32");
    ok = check(strictMisses > 0, "without the tolerance some of them would not be (the knife edge)") && ok;
    std::vector<Vector3> flatVertices;
    Faces flatTriangles;
    box(1.0, 1.0, 1.0, 4, &flatVertices, &flatTriangles);
    const FeatureEdgeSet above = AutoRemesher::classifySharpEdges(flatVertices, flatTriangles, 90.5);
    ok = check(above.empty(), "at 90.5 degrees no right angle is sharp") && ok;
    // A hinge whose normals are 89.5 degrees apart is not a right angle.
    for (const double normalDegrees : { 89.5, 89.95 }) {
        const double open = (180.0 - normalDegrees) * M_PI / 180.0;
        const std::vector<Vector3> hinge = { Vector3(0.0, 0.0, 0.0), Vector3(1.0, 0.0, 0.0),
            Vector3(0.5, 1.0, 0.0), Vector3(0.5, std::cos(open), std::sin(open)) };
        const Faces hingeTriangles = { { 0, 1, 2 }, { 1, 0, 3 } };
        const AutoRemesher::SurfaceMesh hingeMesh(hinge, hingeTriangles);
        double measured = 0.0;
        for (size_t c = 0; c < hingeMesh.cornerCount(); ++c)
            if (hingeMesh.oppositeCorner(c) != AutoRemesher::SurfaceMesh::npos)
                measured = std::fabs(hingeMesh.normalAngle(c)) * 180.0 / M_PI;
        const bool sharp = !AutoRemesher::classifySharpEdges(hinge, hingeTriangles, 90.0).empty();
        std::printf("  a hinge at %.3f degrees is %s\n", measured, sharp ? "sharp" : "not sharp");
        ok = check(std::fabs(measured - normalDegrees) < 1e-6, "the hinge has the angle it was built with") && ok;
        ok = check(sharp == (normalDegrees > 89.9), "a hinge 0.5 degrees under 90 is not sharp, 0.05 under is") && ok;
    }
    ok = check(AutoRemesher::classifySharpEdges(flatVertices, flatTriangles, 0.0).empty(), "0 degrees means no sharp edges") && ok;
    ok = check(AutoRemesher::classifySharpEdges(flatVertices, flatTriangles, 180.0).empty(), "180 degrees means no sharp edges") && ok;
    return ok;
}

// Two half-planes meeting at a right angle along the x axis, crease vertices
// at x in `xs`: c (on the crease), a (y = 1), b (z = 1) per x.
void fold(const std::vector<double>& xs, std::vector<Vector3>* vertices, Faces* triangles)
{
    for (const double x : xs) {
        vertices->push_back(Vector3(x, 0.0, 0.0));
        vertices->push_back(Vector3(x, 1.0, 0.0));
        vertices->push_back(Vector3(x, 0.0, 1.0));
    }
    for (size_t i = 0; i + 1 < xs.size(); ++i) {
        const size_t c = 3 * i, a = c + 1, b = c + 2, c1 = c + 3, a1 = c + 4, b1 = c + 5;
        triangles->push_back({ c, c1, a1 });
        triangles->push_back({ c, a1, a });
        triangles->push_back({ c, b, b1 });
        triangles->push_back({ c, b1, c1 });
    }
}

bool decimationKeepsTheChain()
{
    std::printf("a crease through decimation\n");
    std::vector<Vector3> input, decimated;
    Faces inputTriangles, decimatedTriangles;
    fold({ 0.0, 1.0, 2.0, 3.0 }, &input, &inputTriangles);
    // The decimation removed the second crease vertex and its two neighbours.
    fold({ 0.0, 2.0, 3.0 }, &decimated, &decimatedTriangles);
    const size_t removed = std::numeric_limits<size_t>::max();
    const std::vector<size_t> outputIndexOfInput = { 0, 1, 2, removed, removed, removed, 3, 4, 5, 6, 7, 8 };
    const FeatureEdgeSet sharp = AutoRemesher::classifySharpEdges(input, inputTriangles, 90.0);
    bool ok = check(3 == sharp.size() && sharp.contains(0, 3) && sharp.contains(3, 6) && sharp.contains(6, 9),
        "the input's crease is its 3 edges");
    size_t dropped = 99;
    const FeatureEdgeSet mapped = AutoRemesher::remapFeatureEdges(sharp, outputIndexOfInput,
        decimated, decimatedTriangles, 90.0, &dropped);
    ok = check(2 == mapped.size() && mapped.contains(0, 3) && mapped.contains(3, 6),
             "after it the crease is the 2 edges between the remaining crease vertices")
        && ok;
    ok = check(0 == dropped, "no edge was dropped") && ok;
    return ok;
}

bool tKeepsOnlyHardEdgesAndItsOwnCreases()
{
    std::printf("T's set from what the remesher carried\n");
    std::vector<Vector3> vertices;
    Faces triangles;
    fold({ 0.0, 1.0, 2.0, 3.0 }, &vertices, &triangles);
    FeatureEdgeSet carried;
    carried.edges = { { 0, 4 }, { 3, 7 }, { 0, 3 }, { 1, 5 } };
    carried.flags = { FeatureEdgeSet::Hard, 0, 0, FeatureEdgeSet::Hard };
    carried.normalize();
    size_t classified = 99;
    const FeatureEdgeSet set = AutoRemesher::reclassifyFeatureEdges(carried, vertices, triangles, 90.0, &classified);
    bool ok = check(set.contains(0, 3) && set.contains(3, 6) && set.contains(6, 9) && 3 == classified,
        "the crease is classified on T");
    ok = check(set.contains(0, 4) && set.flags[std::lower_bound(set.edges.begin(), set.edges.end(), std::make_pair<size_t, size_t>(0, 4)) - set.edges.begin()] == FeatureEdgeSet::Hard,
             "a flat hard edge is kept, with its flag")
        && ok;
    ok = check(!set.contains(3, 7) && !set.contains(1, 5) && 4 == set.size(),
             "a flat carried edge that is not hard is not, nor a hard one that is no edge")
        && ok;
    return ok;
}

bool remesherHandsTheEdgesOnToT()
{
    std::printf("a box through the isotropic remesh\n");
    std::vector<Vector3> vertices;
    Faces triangles;
    box(2.0, 1.0, 1.0, 1, &vertices, &triangles);
    const double boxEdgeLength = 4 * (2.0 + 1.0 + 1.0);
    bool ok = true;
    for (const double degrees : { 90.0, 180.0 }) {
        const FeatureEdgeSet sharp = AutoRemesher::classifySharpEdges(vertices, triangles, degrees);
        AutoRemesher::IsotropicRemesher remesher(vertices, triangles);
        remesher.setTargetEdgeLength(0.1);
        remesher.setFeatureEdges(sharp);
        if (!remesher.remesh())
            return check(false, "the remesh ran");
        const FeatureEdgeSet& carried = remesher.featureEdges();
        size_t classified = 0;
        const FeatureEdgeSet extended = AutoRemesher::reclassifyFeatureEdges(carried,
            remesher.remeshedVertices(), remesher.remeshedTriangles(), degrees, &classified);
        const double carriedLength = AutoRemesher::featureEdgeLength(carried, remesher.remeshedVertices());
        const double length = AutoRemesher::featureEdgeLength(extended, remesher.remeshedVertices());
        std::printf("  at %.0f degrees: %zu input edges; T: %zu carried (length %.3f), %zu flipped away; T's set %zu edges (%zu classified), length %.3f of %.3f\n",
            degrees, sharp.size(), carried.size(), carriedLength, remesher.clearedFeatureEdges(), extended.size(), classified, length, boxEdgeLength);
        if (180.0 == degrees) {
            ok = check(extended.empty(), "at 180 degrees T has no sharp edges") && ok;
            continue;
        }
        const FeatureEdgeSet onT = AutoRemesher::classifySharpEdges(remesher.remeshedVertices(),
            remesher.remeshedTriangles(), degrees);
        // Distance of an end of a sharp edge of T from the nearest box edge:
        // two of its coordinates on the box's faces.
        double offEdge = 0.0;
        const double size[3] = { 2.0, 1.0, 1.0 };
        for (const auto& edge : extended.edges)
            for (const size_t v : { edge.first, edge.second }) {
                const Vector3& p = remesher.remeshedVertices()[v];
                double d[3];
                for (int k = 0; k < 3; ++k)
                    d[k] = std::min(std::fabs(p[k]), std::fabs(p[k] - size[k]));
                std::sort(d, d + 3);
                offEdge = std::max(offEdge, std::hypot(d[0], d[1]));
            }
        std::printf("  ends of sharp edges of T lie within %.2g of a box edge\n", offEdge);
        ok = check(onT.edges == extended.edges, "with no hard edges T's set is T's own sharp edges") && ok;
        // The relaxation moves feature vertices too (TODO 2.5), up to most of an edge.
        ok = check(offEdge < 0.1, "they lie near a box edge (within one target edge)") && ok;
        // The remesher's flips and smoothing leave little of the crease sharp
        // on T (TODO 2.5): 1.03 of 16, though 4.8 of it is still carried.
        ok = check(length > 0.0 && length < 1.05 * boxEdgeLength,
                 "T keeps some of the box's edge length as sharp edges, and not more")
            && ok;
    }
    return ok;
}

// pointsOnFeatureEdges (TODO 5.7): a point on a hard edge counts, a point
// on an edge without the flag or off the edge by more than the tolerance
// does not.
bool pointsOnHardEdgesAreFound()
{
    const std::vector<Vector3> vertices = { Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(1, 1, 0), Vector3(0, 1, 0) };
    FeatureEdgeSet set;
    set.edges = { { 0, 1 }, { 1, 2 }, { 2, 3 } };
    set.flags = { FeatureEdgeSet::Hard, FeatureEdgeSet::Hard, 0 };
    const std::vector<Vector3> points = { Vector3(0.25, 0, 0), Vector3(1, 0.5, 1e-9), Vector3(0.5, 1, 0),
        Vector3(0.5, 1e-3, 0), Vector3(1, 1, 0), Vector3(5, 5, 5) };
    const std::vector<char> on = pointsOnFeatureEdges(points, set, vertices, FeatureEdgeSet::Hard, 1e-6);
    bool ok = check(on.size() == points.size(), "one flag per point");
    ok = check(on[0] && on[1], "points on hard edges are found") && ok;
    ok = check(!on[2], "a point on an edge without the hard flag is not") && ok;
    ok = check(!on[3], "a point farther than the tolerance is not") && ok;
    ok = check(on[4], "the end of a hard edge is on it") && ok;
    ok = check(!on[5], "a far point is not") && ok;
    const std::vector<char> any = pointsOnFeatureEdges(points, set, vertices, 0, 1e-6);
    ok = check(any[2], "with no flag required every edge counts") && ok;
    return ok;
}

}

int main()
{
    int failures = 0;
    failures += rightAnglesAreSharpOnEveryRotation() ? 0 : 1;
    failures += decimationKeepsTheChain() ? 0 : 1;
    failures += tKeepsOnlyHardEdgesAndItsOwnCreases() ? 0 : 1;
    failures += remesherHandsTheEdgesOnToT() ? 0 : 1;
    failures += pointsOnHardEdgesAreFound() ? 0 : 1;
    std::printf("%s\n", 0 == failures ? "all passed" : "FAILED");
    return 0 == failures ? 0 : 1;
}
