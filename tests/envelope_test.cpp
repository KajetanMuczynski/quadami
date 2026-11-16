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

// Envelope of parts with holes and handles finer than the quads
// (buildEnvelope): a torus whose hole is narrower than the closing is closed
// over, one with a wide hole keeps it, an open sheet becomes a closed slab;
// each envelope is closed, two-manifold, oriented outwards and lies about
// `offset` from the input.  remesh() envelopes a plate with 64 small holes
// asked for fewer quads than the holes need, keeps a full plate just above
// that, and keeps the holes when they are several quads wide.

#include <AutoRemesher/Envelope>
#include <AutoRemesher/Remesh>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using AutoRemesher::Vector3;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void torus(double major, double minor, std::vector<Vector3>* vertices, std::vector<std::vector<size_t>>* triangles)
{
    const size_t rings = 96, segments = 32;
    for (size_t i = 0; i < rings; ++i) {
        const double u = 2.0 * M_PI * i / rings;
        for (size_t j = 0; j < segments; ++j) {
            const double v = 2.0 * M_PI * j / segments;
            const double r = major + minor * std::cos(v);
            vertices->push_back(Vector3(r * std::cos(u), r * std::sin(u), minor * std::sin(v)));
        }
    }
    const auto at = [&](size_t i, size_t j) { return (i % rings) * segments + j % segments; };
    for (size_t i = 0; i < rings; ++i) {
        for (size_t j = 0; j < segments; ++j) {
            triangles->push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1) });
            triangles->push_back({ at(i, j), at(i + 1, j + 1), at(i, j + 1) });
        }
    }
}

// A unit square of n x n cells in the plane z = 0, without the cells in
// odd rows and odd columns: ((n - 1) / 2)^2 square holes one cell wide.
void perforatedPlate(size_t n, std::vector<Vector3>* vertices, std::vector<std::vector<size_t>>* triangles)
{
    for (size_t y = 0; y <= n; ++y)
        for (size_t x = 0; x <= n; ++x)
            vertices->push_back(Vector3((double)x / n, (double)y / n, 0.0));
    const auto at = [&](size_t x, size_t y) { return y * (n + 1) + x; };
    for (size_t y = 0; y < n; ++y) {
        for (size_t x = 0; x < n; ++x) {
            if (1 == x % 2 && 1 == y % 2)
                continue;
            triangles->push_back({ at(x, y), at(x + 1, y), at(x + 1, y + 1) });
            triangles->push_back({ at(x, y), at(x + 1, y + 1), at(x, y + 1) });
        }
    }
}

struct Topology {
    long long euler = 0;
    bool closedManifold = true;
    bool oriented = true;
    double volume = 0.0;
};

Topology topology(const AutoRemesher::EnvelopeResult& envelope)
{
    Topology result;
    std::map<std::pair<size_t, size_t>, int> directed;
    std::map<std::pair<size_t, size_t>, int> undirected;
    for (const auto& t : envelope.triangles) {
        for (size_t k = 0; k < 3; ++k) {
            const size_t a = t[k], b = t[(k + 1) % 3];
            ++directed[{ a, b }];
            ++undirected[{ std::min(a, b), std::max(a, b) }];
        }
        result.volume += Vector3::dotProduct(envelope.vertices[t[0]],
                             Vector3::crossProduct(envelope.vertices[t[1]], envelope.vertices[t[2]]))
            / 6.0;
    }
    for (const auto& it : undirected)
        result.closedManifold = result.closedManifold && 2 == it.second;
    for (const auto& it : directed)
        result.oriented = result.oriented && 1 == it.second;
    result.euler = (long long)envelope.vertices.size() - (long long)undirected.size() + (long long)envelope.triangles.size();
    return result;
}

// Largest distance from an input vertex to the nearest envelope vertex.
double farthestInputVertex(const std::vector<Vector3>& input, const AutoRemesher::EnvelopeResult& envelope)
{
    double farthest = 0.0;
    for (const auto& p : input) {
        double nearest = 1e300;
        for (const auto& q : envelope.vertices)
            nearest = std::min(nearest, (p - q).lengthSquared());
        farthest = std::max(farthest, std::sqrt(nearest));
    }
    return farthest;
}

AutoRemesher::RemeshResult remeshPlate(uint32_t targetQuads)
{
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;
    perforatedPlate(17, &vertices, &triangles);
    AutoRemesher::RemeshRequest request;
    for (const Vector3& v : vertices)
        request.positions.insert(request.positions.end(), { v.x(), v.y(), v.z() });
    for (const auto& t : triangles)
        request.triangles.insert(request.triangles.end(), { (uint32_t)t[0], (uint32_t)t[1], (uint32_t)t[2] });
    request.parameters.targetQuadCount = targetQuads;
    return AutoRemesher::remesh(request);
}

}

int main()
{
    const size_t maximumNodes = (size_t)1 << 22;
    {
        // Hole radius 0.1 against a closing radius of 0.15.
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        torus(0.3, 0.2, &vertices, &triangles);
        AutoRemesher::EnvelopeResult envelope;
        check(AutoRemesher::buildEnvelope(vertices, triangles, 0.15, 0.05, 0.02, maximumNodes, &envelope), "narrow torus envelope");
        const Topology t = topology(envelope);
        check(t.closedManifold, "narrow torus: closed two-manifold");
        check(t.oriented, "narrow torus: consistently oriented");
        check(2 == t.euler, "narrow torus: hole closed over (sphere)");
        check(t.volume > 0.0, "narrow torus: faces point outwards");
        check(0 == envelope.droppedTriangles, "narrow torus: one piece");
        // The outer half of the tube, away from the closed hole.
        std::vector<Vector3> outer;
        for (const auto& v : vertices)
            if (std::hypot(v.x(), v.y()) >= 0.3)
                outer.push_back(v);
        check(farthestInputVertex(outer, envelope) < 0.05 + 2.0 * envelope.cellSize, "narrow torus: envelope hugs the outside");
    }
    {
        // Hole radius 0.8.
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        torus(1.0, 0.2, &vertices, &triangles);
        AutoRemesher::EnvelopeResult envelope;
        check(AutoRemesher::buildEnvelope(vertices, triangles, 0.15, 0.05, 0.02, maximumNodes, &envelope), "wide torus envelope");
        const Topology t = topology(envelope);
        check(t.closedManifold && t.oriented, "wide torus: closed, two-manifold, oriented");
        check(0 == t.euler, "wide torus: keeps its hole");
        check(t.volume > 0.0, "wide torus: faces point outwards");
    }
    {
        // A closed sphere: the grown shell has a cavity inside, which is
        // filled, so only the outer surface comes out, enclosing the whole
        // ball of radius 1.05 (4.85; 4.85 - 4.19 with the inner surface).
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        const size_t rings = 40, segments = 80;
        vertices.push_back(Vector3(0, 0, 1));
        for (size_t i = 1; i < rings; ++i)
            for (size_t j = 0; j < segments; ++j) {
                const double theta = M_PI * i / rings, phi = 2.0 * M_PI * j / segments;
                vertices.push_back(Vector3(std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)));
            }
        vertices.push_back(Vector3(0, 0, -1));
        const auto at = [&](size_t i, size_t j) { return 1 + (i - 1) * segments + j % segments; };
        for (size_t j = 0; j < segments; ++j) {
            triangles.push_back({ 0, at(1, j), at(1, j + 1) });
            triangles.push_back({ vertices.size() - 1, at(rings - 1, j + 1), at(rings - 1, j) });
        }
        for (size_t i = 1; i + 1 < rings; ++i)
            for (size_t j = 0; j < segments; ++j) {
                triangles.push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1) });
                triangles.push_back({ at(i, j), at(i + 1, j + 1), at(i, j + 1) });
            }
        AutoRemesher::EnvelopeResult envelope;
        check(AutoRemesher::buildEnvelope(vertices, triangles, 0.15, 0.05, 0.03, maximumNodes, &envelope), "sphere envelope");
        const Topology t = topology(envelope);
        check(t.closedManifold && t.oriented && 2 == t.euler, "sphere: one closed sphere");
        check(0 == envelope.droppedTriangles, "sphere: no inner surface");
        check(t.volume > 4.6 && t.volume < 5.1, "sphere: encloses the ball");
    }
    {
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        perforatedPlate(17, &vertices, &triangles);
        AutoRemesher::EnvelopeResult envelope;
        check(AutoRemesher::buildEnvelope(vertices, triangles, 0.12, 0.04, 0.02, maximumNodes, &envelope), "plate envelope");
        const Topology t = topology(envelope);
        check(t.closedManifold && t.oriented, "plate: closed, two-manifold, oriented");
        check(2 == t.euler, "plate: holes one cell (0.059) wide closed, a slab");
        check(t.volume > 0.0, "plate: faces point outwards");
    }
    {
        // The grid is coarsened to stay within the node budget.
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        torus(0.3, 0.2, &vertices, &triangles);
        AutoRemesher::EnvelopeResult envelope;
        check(AutoRemesher::buildEnvelope(vertices, triangles, 0.15, 0.05, 0.002, 100000, &envelope), "coarsened envelope");
        check(envelope.cells <= 100000 && envelope.cellSize > 0.002, "grid within the node budget");
    }
    {
        // chi = 1 - 64 = -63: the holes need at least 189 quads.
        const AutoRemesher::RemeshResult few = remeshPlate(60);
        check(AutoRemesher::RemeshStatus::Ok == few.status, "plate at 60 quads remeshes");
        check(1 == few.report.envelopedIslandCount, "plate at 60 quads is enveloped");
        check(few.report.faceCount > 0, "enveloped plate has faces");
        bool warned = false;
        for (const std::string& warning : few.report.warnings)
            warned = warned || std::string::npos != warning.find("closed shell");
        check(warned, "enveloped plate is warned about");
        // Above the bound the map can still break (holes 1.3 quads wide at
        // 400): the result is checked, and the envelope's kept if it covers
        // the input better.  It may or may not be enveloped, but it is never
        // a handful of quads.
        const AutoRemesher::RemeshResult above = remeshPlate(400);
        check(AutoRemesher::RemeshStatus::Ok == above.status, "plate at 400 quads remeshes");
        check(above.report.quadCount > 200, "plate at 400 quads is not a handful of quads");
        // Holes 4 quads wide: the plate keeps them.
        const AutoRemesher::RemeshResult enough = remeshPlate(4000);
        check(AutoRemesher::RemeshStatus::Ok == enough.status, "plate at 4000 quads remeshes");
        check(0 == enough.report.envelopedIslandCount, "plate at 4000 quads is not enveloped");
    }
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("envelope: ok\n");
    return 0;
}
