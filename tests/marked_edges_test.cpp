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

// Marked edges (RemeshRequest::featureEdges, TODO 5.4): Blender's Sharp
// marks reach the core as vertex pairs.  Malformed lists are rejected, pairs
// that are not an edge of the mesh are ignored and counted, a marked line on
// a smooth surface gets a row of output edges, and marks on nearly every edge
// are not used.

#include <AutoRemesher/Remesh>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

AutoRemesher::RemeshRequest cube()
{
    AutoRemesher::RemeshRequest request;
    request.positions = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1 };
    request.triangles = { 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
        1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7 };
    request.parameters.targetQuadCount = 300;
    return request;
}

// A UV sphere of radius 1, rings x segments, and its equator as marks: no
// dihedral test finds it, the angle there is 360 / rings degrees.
AutoRemesher::RemeshRequest sphere(int rings, int segments)
{
    AutoRemesher::RemeshRequest request;
    const double pi = std::acos(-1.0);
    for (int i = 1; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            const double theta = pi * i / rings, phi = 2.0 * pi * j / segments;
            request.positions.insert(request.positions.end(),
                { std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta) });
        }
    }
    const uint32_t north = (uint32_t)(request.positions.size() / 3), south = north + 1;
    request.positions.insert(request.positions.end(), { 0, 0, 1, 0, 0, -1 });
    const auto at = [segments](int ring, int j) { return (uint32_t)((ring - 1) * segments + (j % segments)); };
    for (int j = 0; j < segments; ++j) {
        request.triangles.insert(request.triangles.end(), { north, at(1, j), at(1, j + 1) });
        request.triangles.insert(request.triangles.end(), { south, at(rings - 1, j + 1), at(rings - 1, j) });
        for (int i = 1; i + 1 < rings; ++i) {
            request.triangles.insert(request.triangles.end(), { at(i, j), at(i + 1, j), at(i + 1, j + 1) });
            request.triangles.insert(request.triangles.end(), { at(i, j), at(i + 1, j + 1), at(i, j + 1) });
        }
    }
    for (int j = 0; j < segments; ++j)
        request.featureEdges.insert(request.featureEdges.end(), { at(rings / 2, j), at(rings / 2, j + 1) });
    return request;
}

// Share of points along the marked edges that an output edge follows: within
// a tenth of the quad edge and within 10 degrees of the marked edge's
// direction (TARGETS A1, crease_coverage() in tools/bench/metrics.py).
double followedShare(const AutoRemesher::RemeshRequest& request, const AutoRemesher::RemeshMesh& mesh)
{
    const auto point = [](const std::vector<double>& p, size_t v, double* out) {
        for (int k = 0; k < 3; ++k)
            out[k] = p[3 * v + k];
    };
    size_t samples = 0, followed = 0;
    for (size_t e = 0; e + 1 < request.featureEdges.size(); e += 2) {
        double a[3], b[3];
        point(request.positions, request.featureEdges[e], a);
        point(request.positions, request.featureEdges[e + 1], b);
        const double ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        const double abLength = std::sqrt(ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2]);
        for (int s = 0; s < 4; ++s) {
            const double t = (s + 0.5) / 4.0;
            const double q[3] = { a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2]) };
            double nearest = 1e30;
            size_t offset = 0;
            for (uint32_t size : mesh.faceSizes) {
                for (uint32_t c = 0; c < size; ++c) {
                    double u[3], v[3];
                    point(mesh.positions, mesh.faceIndices[offset + c], u);
                    point(mesh.positions, mesh.faceIndices[offset + (c + 1) % size], v);
                    double d[3] = { v[0] - u[0], v[1] - u[1], v[2] - u[2] }, w[3] = { q[0] - u[0], q[1] - u[1], q[2] - u[2] };
                    const double dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                    const double h = dd > 0.0 ? std::max(0.0, std::min(1.0, (w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / dd)) : 0.0;
                    const double x = w[0] - h * d[0], y = w[1] - h * d[1], z = w[2] - h * d[2];
                    const double along = dd > 0.0 && abLength > 0.0
                        ? std::fabs(d[0] * ab[0] + d[1] * ab[1] + d[2] * ab[2]) / (std::sqrt(dd) * abLength)
                        : 0.0;
                    if (along > std::cos(10.0 * std::acos(-1.0) / 180.0))
                        nearest = std::min(nearest, x * x + y * y + z * z);
                }
                offset += size;
            }
            ++samples;
            // The quad edge is about 0.09 here.
            followed += std::sqrt(nearest) < 0.009 ? 1 : 0;
        }
    }
    return samples > 0 ? (double)followed / (double)samples : 0.0;
}

bool hasWarning(const AutoRemesher::RemeshResult& result, const char* text)
{
    for (const std::string& warning : result.report.warnings) {
        if (warning.find(text) != std::string::npos)
            return true;
    }
    return false;
}

bool check(const char* name, bool ok, const AutoRemesher::RemeshResult& result)
{
    printf("%s: %s: %s%s\n", name, AutoRemesher::toString(result.status), result.message.c_str(), ok ? "" : "  FAIL");
    return ok;
}

}

int main()
{
    bool ok = true;

    AutoRemesher::RemeshRequest odd = cube();
    odd.featureEdges = { 0, 1, 2 };
    AutoRemesher::RemeshResult result = AutoRemesher::remesh(odd, {});
    ok &= check("odd length", AutoRemesher::RemeshStatus::InvalidInput == result.status, result);

    AutoRemesher::RemeshRequest outOfRange = cube();
    outOfRange.featureEdges = { 0, 8 };
    result = AutoRemesher::remesh(outOfRange, {});
    ok &= check("index past the end", AutoRemesher::RemeshStatus::InvalidInput == result.status, result);

    // The bottom square's four sides, one given twice and once reversed, and
    // two pairs that are no edge: opposite corners of the cube and a vertex
    // with itself.
    AutoRemesher::RemeshRequest marked = cube();
    marked.featureEdges = { 0, 1, 1, 2, 2, 3, 3, 0, 1, 0, 0, 6, 5, 5 };
    result = AutoRemesher::remesh(marked, {});
    const bool warned = hasWarning(result, "2 marked edges are not an edge");
    ok &= check("ignored pairs", AutoRemesher::RemeshStatus::Ok == result.status && 7 == result.report.inputFeatureEdgeCount && 2 == result.report.ignoredFeatureEdgeCount && warned,
        result);
    printf("  feature edges %zu, ignored %zu\n", result.report.inputFeatureEdgeCount, result.report.ignoredFeatureEdgeCount);

    // The equator gets a row of edges with the marks and not without them
    // (marked set of tools/bench: 86-100 % followed, 0-35 % without).
    AutoRemesher::RemeshRequest equator = sphere(16, 32);
    equator.parameters.targetQuadCount = 1500;
    result = AutoRemesher::remesh(equator, {});
    const double withMarks = AutoRemesher::RemeshStatus::Ok == result.status ? followedShare(equator, result.mesh) : 0.0;
    AutoRemesher::RemeshRequest plain = equator;
    plain.featureEdges.clear();
    result = AutoRemesher::remesh(plain, {});
    const double withoutMarks = AutoRemesher::RemeshStatus::Ok == result.status ? followedShare(equator, result.mesh) : 1.0;
    ok &= check("equator followed", withMarks >= 0.8 && withoutMarks < 0.5, result);
    printf("  followed with marks %.2f, without %.2f\n", withMarks, withoutMarks);

    // Every edge marked, as an FBX import marks them: not used, and said so.
    AutoRemesher::RemeshRequest flood = sphere(16, 32);
    flood.featureEdges.clear();
    for (size_t t = 0; t < flood.triangles.size(); t += 3) {
        for (int k = 0; k < 3; ++k)
            flood.featureEdges.insert(flood.featureEdges.end(), { flood.triangles[t + k], flood.triangles[t + (k + 1) % 3] });
    }
    flood.parameters.targetQuadCount = 1500;
    result = AutoRemesher::remesh(flood, {});
    ok &= check("every edge marked", AutoRemesher::RemeshStatus::Ok == result.status && hasWarning(result, "too dense"), result);

    return ok ? 0 : 1;
}
