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

// Tangential smoothing of the extracted quads (RemeshParameters::quadSmoothing,
// TODO 2.8).  The input is a unit sphere with a ridge round its equator, about
// a quarter of a quad wide and half a quad high, like a sculpted vine.
// Laplacian smoothing pulls the vertices on the ridge down onto its flanks;
// tangential smoothing keeps the output nearer to the crest, and both outputs
// stay closed and all quads.  At three targets, so that one lucky layout
// cannot decide it (the crest distance falls by 9-40 % at 900-1100 quads).

#include <AutoRemesher/Remesh>
#include <AutoRemesher/SurfaceProjector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
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

const double pi = 3.14159265358979323846;
const double ridgeHeight = 0.05, ridgeWidth = 0.03;

AutoRemesher::RemeshRequest ridgedSphere(uint32_t targetQuads, AutoRemesher::QuadSmoothing smoothing)
{
    const size_t rings = 200, segments = 400;
    AutoRemesher::RemeshRequest request;
    auto add = [&](double x, double y, double z) {
        request.positions.insert(request.positions.end(), { x, y, z });
    };
    add(0, 0, 1);
    for (size_t r = 1; r < rings; ++r) {
        const double theta = pi * r / rings, z = std::cos(theta);
        const double radius = 1.0 + ridgeHeight * std::exp(-(z / ridgeWidth) * (z / ridgeWidth));
        for (size_t s = 0; s < segments; ++s) {
            const double phi = 2.0 * pi * s / segments;
            add(radius * std::sin(theta) * std::cos(phi), radius * std::sin(theta) * std::sin(phi), radius * z);
        }
    }
    add(0, 0, -1);
    const uint32_t bottom = (uint32_t)(request.positions.size() / 3 - 1);
    const auto at = [&](size_t r, size_t s) { return (uint32_t)(1 + (r - 1) * segments + s % segments); };
    auto triangle = [&](uint32_t a, uint32_t b, uint32_t c) {
        request.triangles.insert(request.triangles.end(), { a, b, c });
    };
    for (size_t s = 0; s < segments; ++s) {
        triangle(0, at(1, s), at(1, s + 1));
        triangle(at(rings - 1, s), bottom, at(rings - 1, s + 1));
    }
    for (size_t r = 1; r + 1 < rings; ++r) {
        for (size_t s = 0; s < segments; ++s) {
            triangle(at(r, s), at(r + 1, s), at(r + 1, s + 1));
            triangle(at(r, s), at(r + 1, s + 1), at(r, s + 1));
        }
    }
    request.parameters.targetQuadCount = targetQuads;
    request.parameters.quadSmoothing = smoothing;
    return request;
}

void checkValid(const AutoRemesher::RemeshResult& result)
{
    check(0 == result.report.nonQuadCount, "all quads");
    check(0 == result.report.boundaryEdgeCount && 0 == result.report.nonManifoldEdgeCount, "closed and manifold");
}

// Mean distance from points on the ridge's crest to the output faces (split
// into fans of triangles).
double crestDistance(const AutoRemesher::RemeshMesh& mesh)
{
    const auto vertex = [&](uint32_t i) {
        return Vector3(mesh.positions[3 * i], mesh.positions[3 * i + 1], mesh.positions[3 * i + 2]);
    };
    const size_t samples = 360;
    double sum = 0.0;
    for (size_t k = 0; k < samples; ++k) {
        const double phi = 2.0 * pi * (k + 0.5) / samples;
        const Vector3 p((1.0 + ridgeHeight) * std::cos(phi), (1.0 + ridgeHeight) * std::sin(phi), 0.0);
        double best = std::numeric_limits<double>::max();
        size_t first = 0;
        for (const uint32_t size : mesh.faceSizes) {
            for (uint32_t i = 1; i + 1 < size; ++i) {
                const Vector3 q = AutoRemesher::closestPointOnTriangle(p, vertex(mesh.faceIndices[first]),
                    vertex(mesh.faceIndices[first + i]), vertex(mesh.faceIndices[first + i + 1]));
                best = std::min(best, (q - p).length());
            }
            first += size;
        }
        sum += best;
    }
    return sum / samples;
}

}

int main()
{
    double laplacianSum = 0.0, tangentialSum = 0.0;
    AutoRemesher::RemeshResult tangential;
    for (const uint32_t target : { 900u, 1000u, 1100u }) {
        const AutoRemesher::RemeshResult laplacian
            = AutoRemesher::remesh(ridgedSphere(target, AutoRemesher::QuadSmoothing::Laplacian));
        tangential = AutoRemesher::remesh(ridgedSphere(target, AutoRemesher::QuadSmoothing::Tangential));
        check(AutoRemesher::RemeshStatus::Ok == laplacian.status, "laplacian remeshes");
        check(AutoRemesher::RemeshStatus::Ok == tangential.status, "tangential remeshes");
        if (failures)
            return 1;
        checkValid(laplacian);
        checkValid(tangential);
        const double before = crestDistance(laplacian.mesh), after = crestDistance(tangential.mesh);
        std::printf("target %u: mean crest distance laplacian %.5f, tangential %.5f (x%.2f)\n", target, before,
            after, after / before);
        laplacianSum += before;
        tangentialSum += after;
    }
    check(tangentialSum < 0.95 * laplacianSum, "tangential smoothing keeps the output nearer to the crest");
    check(std::string::npos != AutoRemesher::toJson(tangential).find("\"quad_smoothing\": \"tangential\""),
        "the report names the smoothing");
    AutoRemesher::QuadSmoothing parsed = AutoRemesher::QuadSmoothing::Laplacian;
    check(AutoRemesher::parseQuadSmoothing("tangential", &parsed) && AutoRemesher::QuadSmoothing::Tangential == parsed,
        "parses tangential");
    check(!AutoRemesher::parseQuadSmoothing("tangent", &parsed), "rejects other words");
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("quad smoothing: ok\n");
    return 0;
}
