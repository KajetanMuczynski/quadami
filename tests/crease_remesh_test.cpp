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

// The isotropic remesh keeps the input's creases as edge chains (TODO 2.5).
//
// A cylinder's curved rims stay on the input's rim polygon and keep its
// length, no face of T lies on two crease edges, and a prism whose caps have
// a 30 degree and a 45 degree corner gets a map without flipped triangles:
// before 2.5 the remesh rounded these creases away; keeping them folded the
// caps until faces on two crease edges were split and the edges at the
// 30 degree corner lost their isoline constraints.

#include <AutoRemesher/FeatureEdges>
#include <AutoRemesher/IsotropicRemesher>
#include <AutoRemesher/Remesh>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
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

// A closed cylinder of radius 1 and height 2 around z, `segments` around,
// `rows` quads high, the caps fans around a centre vertex, wound outwards.
void cylinder(size_t segments, size_t rows, std::vector<Vector3>* vertices, Faces* triangles)
{
    for (size_t r = 0; r <= rows; ++r)
        for (size_t s = 0; s < segments; ++s) {
            const double angle = 2.0 * M_PI * (double)s / (double)segments;
            vertices->push_back(Vector3(std::cos(angle), std::sin(angle), 2.0 * (double)r / (double)rows));
        }
    const auto ring = [&](size_t r, size_t s) { return r * segments + s % segments; };
    for (size_t r = 0; r < rows; ++r)
        for (size_t s = 0; s < segments; ++s) {
            triangles->push_back({ ring(r, s), ring(r, s + 1), ring(r + 1, s + 1) });
            triangles->push_back({ ring(r, s), ring(r + 1, s + 1), ring(r + 1, s) });
        }
    const size_t bottom = vertices->size();
    vertices->push_back(Vector3(0, 0, 0));
    const size_t top = vertices->size();
    vertices->push_back(Vector3(0, 0, 2));
    for (size_t s = 0; s < segments; ++s) {
        triangles->push_back({ bottom, ring(0, s + 1), ring(0, s) });
        triangles->push_back({ top, ring(rows, s), ring(rows, s + 1) });
    }
}

// A prism over the triangle `a, b, c` (counter-clockwise in the xy plane),
// height 1.5, two triangles per side, wound outwards.
void prism(const Vector3& a, const Vector3& b, const Vector3& c, std::vector<Vector3>* vertices, Faces* triangles)
{
    const Vector3 up(0, 0, 1.5);
    *vertices = { a, b, c, a + up, b + up, c + up };
    *triangles = { { 0, 2, 1 }, { 3, 4, 5 } };
    for (size_t i = 0; i < 3; ++i) {
        const size_t j = (i + 1) % 3;
        triangles->push_back({ i, j, j + 3 });
        triangles->push_back({ i, j + 3, i + 3 });
    }
}

double distanceToSegment(const Vector3& p, const Vector3& a, const Vector3& b)
{
    const Vector3 ab = b - a;
    const double t = std::max(0.0, std::min(1.0, Vector3::dotProduct(p - a, ab) / ab.lengthSquared()));
    return (p - (a + ab * t)).length();
}

bool cylinderRimsStayOnTheirPolygon()
{
    std::printf("a cylinder's rims through the isotropic remesh\n");
    const size_t segments = 48;
    std::vector<Vector3> vertices;
    Faces triangles;
    cylinder(segments, 8, &vertices, &triangles);
    const FeatureEdgeSet sharp = AutoRemesher::classifySharpEdges(vertices, triangles, 90.0);
    const double rimLength = AutoRemesher::featureEdgeLength(sharp, vertices);
    AutoRemesher::IsotropicRemesher remesher(vertices, triangles);
    remesher.setTargetEdgeLength(0.05);
    remesher.setFeatureEdges(sharp);
    if (!remesher.remesh())
        return check(false, "the remesh ran");
    const std::vector<Vector3>& tVertices = remesher.remeshedVertices();
    const Faces& tTriangles = remesher.remeshedTriangles();
    const FeatureEdgeSet& chains = remesher.featureEdges();

    double offRim = 0.0;
    for (const auto& edge : chains.edges)
        for (const size_t v : { edge.first, edge.second }) {
            double nearest = 1e300;
            for (const auto& rimEdge : sharp.edges)
                nearest = std::min(nearest, distanceToSegment(tVertices[v], vertices[rimEdge.first], vertices[rimEdge.second]));
            offRim = std::max(offRim, nearest);
        }
    std::vector<size_t> chainDegree(tVertices.size(), 0);
    for (const auto& edge : chains.edges) {
        ++chainDegree[edge.first];
        ++chainDegree[edge.second];
    }
    const bool closedLoops = std::all_of(chainDegree.begin(), chainDegree.end(),
        [](size_t degree) { return 0 == degree || 2 == degree; });
    size_t facesOnTwo = 0;
    for (const auto& f : tTriangles) {
        size_t onChain = 0;
        for (size_t k = 0; k < 3; ++k)
            onChain += chains.contains(std::min(f[k], f[(k + 1) % 3]), std::max(f[k], f[(k + 1) % 3])) ? 1 : 0;
        facesOnTwo += onChain >= 2 ? 1 : 0;
    }
    const double chainLength = AutoRemesher::featureEdgeLength(chains, tVertices);
    std::printf("  %zu input rim edges (length %.4f), %zu chain edges in T (length %.4f), farthest chain vertex %.2g off the rim, %zu faces on two chain edges\n",
        sharp.size(), rimLength, chains.size(), chainLength, offRim, facesOnTwo);
    bool ok = check(2 * segments == sharp.size(), "both rims are sharp");
    // Chain vertices slide along the rim and are projected back onto it.
    ok = check(offRim < 1e-9, "every chain vertex lies on the input's rim polygon") && ok;
    ok = check(closedLoops, "the chains are closed loops") && ok;
    // Chords across the polygon's corners make it a little shorter, by
    // 1 - cos(3.75 degrees) at most.
    ok = check(chainLength > 0.997 * rimLength && chainLength <= rimLength * (1.0 + 1e-12),
             "and keep its length")
        && ok;
    ok = check(0 == facesOnTwo, "no face of T lies on two chain edges") && ok;
    return ok;
}

double diagnostic(const AutoRemesher::RemeshResult& result, const std::string& stage, const std::string& key)
{
    double total = 0.0;
    for (const auto& record : result.stageDiagnostics)
        if (record.stage == stage)
            for (const auto& value : record.values)
                if (value.first == key)
                    total += value.second;
    return total;
}

bool prismCapsDoNotFold()
{
    std::printf("prisms whose caps have acute corners\n");
    struct Case {
        const char* name;
        Vector3 c;
    };
    // Cap corners 30/60/90 (the cad set's wedge) and 45/45/90.
    const Case cases[] = { { "30-60-90", Vector3(0, 1.15470052, 0) }, { "45-45-90", Vector3(0, 2, 0) } };
    bool ok = true;
    for (const Case& c : cases) {
        std::vector<Vector3> vertices;
        Faces triangles;
        prism(Vector3(0, 0, 0), Vector3(2, 0, 0), c.c, &vertices, &triangles);
        AutoRemesher::RemeshRequest request;
        for (const Vector3& v : vertices)
            for (int k = 0; k < 3; ++k)
                request.positions.push_back(v[k]);
        for (const auto& f : triangles)
            for (const size_t i : f)
                request.triangles.push_back((uint32_t)i);
        request.parameters.targetQuadCount = 1000;
        const AutoRemesher::RemeshResult result = AutoRemesher::remesh(request);
        const double flipped = diagnostic(result, "map_check", "map_flipped");
        const double acute = diagnostic(result, "crease_constraints", "acute_rel_edge");
        const double kept = diagnostic(result, "features", "set_t_length_rel_h");
        std::printf("  %s: status %s, %zu quads, %zu non-quads, %zu folded; T's set %.1f h, %.1f T edges without isolines at acute corners, %.0f map triangles flipped\n",
            c.name, AutoRemesher::toString(result.status), result.report.quadCount, result.report.nonQuadCount,
            result.report.foldedFaceCount, kept, acute, flipped);
        ok = check(AutoRemesher::RemeshStatus::Ok == result.status, "the remesh succeeds") && ok;
        // All nine edges are creases (90, 120 and 150 or 135 degrees); the
        // prism's edge length is 21.8 or 23.3 quad widths at 1k quads.
        ok = check(kept > 15.0, "T keeps the creases") && ok;
        // 412 (30-60-90) and 276 (45-45-90) flipped before the two fixes;
        // a few flips next to a cone are left to the extraction.
        ok = check(flipped <= 4.0, "the map has (almost) no flipped triangles") && ok;
        ok = check(0 == result.report.foldedFaceCount, "no output face is folded") && ok;
    }
    return ok;
}

}

int main()
{
    int failures = 0;
    failures += cylinderRimsStayOnTheirPolygon() ? 0 : 1;
    failures += prismCapsDoNotFold() ? 0 : 1;
    std::printf("%s\n", 0 == failures ? "all passed" : "FAILED");
    return 0 == failures ? 0 : 1;
}
