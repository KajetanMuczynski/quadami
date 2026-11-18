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

// The input cache (RemeshRequest::inputCachePath, docs/PREVIEW.md §4.2):
// a run that writes the file, one that reads it with the arrays and one that
// reads it without them all give the mesh of a run without a cache, to the
// bit; so do plans, and a run from a file a plan wrote (which adds the fold
// survey).  Another mesh, other fold settings, another tag or a damaged
// file are never read: with the arrays the run prepares the input again and
// rewrites the file, without them it is InvalidInput.
//
// The input: a sphere with marked edges and a smaller one given as a
// triangle soup, so the seam weld, the marks and two islands all go through
// the file.

#include <AutoRemesher/Remesh>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace AutoRemesher;

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s: %s\n", condition ? "ok" : "FAILED", what);
    if (!condition)
        ++failures;
}

// An icosphere, appended to positions and triangles; `soup` gives every
// triangle its own three vertices.
void sphere(int subdivisions, double cx, double radius, bool soup, std::vector<double>* positions,
    std::vector<uint32_t>* triangles)
{
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<std::array<double, 3>> v = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 },
        { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
        { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
    const auto normalize = [](std::array<double, 3> p) {
        const double l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        return std::array<double, 3> { p[0] / l, p[1] / l, p[2] / l };
    };
    for (auto& p : v)
        p = normalize(p);
    std::vector<std::array<uint32_t, 3>> f = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
        { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
        { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
        { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    for (int s = 0; s < subdivisions; ++s) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoints;
        const auto midpoint = [&](uint32_t a, uint32_t b) {
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            const auto found = midpoints.find(key);
            if (found != midpoints.end())
                return found->second;
            v.push_back(normalize({ v[a][0] + v[b][0], v[a][1] + v[b][1], v[a][2] + v[b][2] }));
            return midpoints[key] = (uint32_t)(v.size() - 1);
        };
        std::vector<std::array<uint32_t, 3>> finer;
        for (const auto& t3 : f) {
            const uint32_t ab = midpoint(t3[0], t3[1]), bc = midpoint(t3[1], t3[2]), ca = midpoint(t3[2], t3[0]);
            finer.push_back({ t3[0], ab, ca });
            finer.push_back({ t3[1], bc, ab });
            finer.push_back({ t3[2], ca, bc });
            finer.push_back({ ab, bc, ca });
        }
        f.swap(finer);
    }
    const auto put = [&](const std::array<double, 3>& p) {
        positions->push_back(cx + radius * p[0]);
        positions->push_back(radius * p[1]);
        positions->push_back(radius * p[2]);
        return (uint32_t)(positions->size() / 3 - 1);
    };
    if (soup) {
        for (const auto& t3 : f)
            for (const uint32_t i : t3)
                triangles->push_back(put(v[i]));
        return;
    }
    const uint32_t offset = (uint32_t)(positions->size() / 3);
    for (const auto& p : v)
        put(p);
    for (const auto& t3 : f)
        for (const uint32_t i : t3)
            triangles->push_back(offset + i);
}

double diagnostic(const RemeshResult& result, const std::string& name)
{
    for (const auto& entry : result.report.diagnostics)
        if (entry.first == name)
            return entry.second;
    return -1.0;
}

bool sameMesh(const RemeshResult& a, const RemeshResult& b)
{
    return RemeshStatus::Ok == a.status && RemeshStatus::Ok == b.status && a.mesh.positions == b.mesh.positions
        && a.mesh.faceSizes == b.mesh.faceSizes && a.mesh.faceIndices == b.mesh.faceIndices
        && a.report.inputVertexCount == b.report.inputVertexCount
        && a.report.inputTriangleCount == b.report.inputTriangleCount
        && a.report.ignoredFeatureEdgeCount == b.report.ignoredFeatureEdgeCount;
}

bool samePlan(const RemeshResult& a, const RemeshResult& b)
{
    return RemeshStatus::Ok == a.status && RemeshStatus::Ok == b.status
        && a.plan.vertexEdgeLengths == b.plan.vertexEdgeLengths && a.plan.vertexIslands == b.plan.vertexIslands
        && a.plan.plannedQuads == b.plan.plannedQuads;
}

RemeshRequest withoutArrays(RemeshRequest request)
{
    request.positions.clear();
    request.triangles.clear();
    request.featureEdges.clear();
    return request;
}

bool exists(const std::string& path)
{
    return std::ifstream(path).good();
}

}

int main()
{
    RemeshRequest request;
    sphere(4, 0.0, 1.0, false, &request.positions, &request.triangles);
    sphere(2, 3.0, 0.4, true, &request.positions, &request.triangles);
    // A few marked edges along the first triangles of the large sphere.
    for (size_t t = 0; t < 6; ++t)
        request.featureEdges.insert(request.featureEdges.end(), { request.triangles[3 * t], request.triangles[3 * t + 1] });
    request.parameters.targetQuadCount = 400;
    request.inputCacheTag = "test build";

    const std::string path = "input_cache_test.qic";
    std::remove(path.c_str());
    const RemeshResult reference = remesh(request);
    check(RemeshStatus::Ok == reference.status, "reference run without a cache");
    check(-1.0 == diagnostic(reference, "input_cache_read"), "no cache diagnostics without a cache");

    RemeshRequest cached = request;
    cached.inputCachePath = path;
    const RemeshResult written = remesh(cached);
    check(sameMesh(reference, written), "a run that writes the file gives the same mesh");
    check(exists(path) && 0.0 == diagnostic(written, "input_cache_read"), "the file is written, not read");
    const RemeshResult read = remesh(cached);
    check(sameMesh(reference, read) && 1.0 == diagnostic(read, "input_cache_read"),
        "a run that reads the file with the arrays gives the same mesh");
    const RemeshResult readAlone = remesh(withoutArrays(cached));
    check(sameMesh(reference, readAlone) && 1.0 == diagnostic(readAlone, "input_cache_read"),
        "a run that reads the file without the arrays gives the same mesh");

    // Plans, and a run from a file a plan wrote.
    const RemeshResult planReference = planRemesh(request);
    const RemeshResult planRead = planRemesh(withoutArrays(cached));
    check(samePlan(planReference, planRead) && 1.0 == diagnostic(planRead, "input_cache_read"),
        "a plan from the file is the same plan");
    std::remove(path.c_str());
    const RemeshResult planWritten = planRemesh(cached);
    check(samePlan(planReference, planWritten) && exists(path), "a plan writes the file");
    const RemeshResult afterPlan = remesh(withoutArrays(cached));
    check(sameMesh(reference, afterPlan) && 1.0 == diagnostic(afterPlan, "input_cache_read")
            && diagnostic(afterPlan, "input_cache_write_ms") > 0.0,
        "a run from a plan's file gives the same mesh and writes the fold survey back");
    check(diagnostic(reference, "input_folded_triangles") == diagnostic(afterPlan, "input_folded_triangles"),
        "the run from a plan's file reports the fold survey");
    const RemeshResult afterSurvey = remesh(withoutArrays(cached));
    check(sameMesh(reference, afterSurvey) && 0.0 == diagnostic(afterSurvey, "input_cache_write_ms"),
        "the next run reads the survey and writes nothing");

    // Other fold settings: prepared again from the file's arrays.
    RemeshRequest repaired = request;
    repaired.parameters.repairFolds = true;
    const RemeshResult repairedReference = remesh(repaired);
    RemeshRequest repairedCached = withoutArrays(cached);
    repairedCached.parameters.repairFolds = true;
    const RemeshResult repairedRead = remesh(repairedCached);
    check(sameMesh(repairedReference, repairedRead) && 0.0 == diagnostic(repairedRead, "input_cache_read"),
        "other fold settings prepare the input again");
    check(1.0 == diagnostic(remesh(repairedCached), "input_cache_read"), "and the rewritten file is read next time");

    // Another mesh with the same file: prepared again, never read.
    RemeshRequest other;
    sphere(3, 0.0, 2.0, false, &other.positions, &other.triangles);
    other.parameters = request.parameters;
    other.inputCacheTag = request.inputCacheTag;
    const RemeshResult otherReference = remesh(other);
    other.inputCachePath = path;
    const RemeshResult otherCached = remesh(other);
    check(sameMesh(otherReference, otherCached) && 0.0 == diagnostic(otherCached, "input_cache_read"),
        "another mesh is prepared again");

    // Another tag, a damaged file, no file.
    RemeshRequest otherTag = withoutArrays(other);
    otherTag.inputCacheTag = "another build";
    const RemeshResult refused = remesh(otherTag);
    const std::string prefix = InputCacheUnusableMessage;
    check(RemeshStatus::InvalidInput == refused.status && 0 == refused.message.compare(0, prefix.size(), prefix),
        "a file from another build is unusable");
    {
        std::ifstream in(path, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), (std::streamsize)(bytes.size() / 2));
    }
    const RemeshResult damaged = remesh(withoutArrays(other));
    check(RemeshStatus::InvalidInput == damaged.status && 0 == damaged.message.compare(0, prefix.size(), prefix),
        "a damaged file is unusable");
    const RemeshResult rewritten = remesh(other);
    check(sameMesh(otherReference, rewritten) && 0.0 == diagnostic(rewritten, "input_cache_read"),
        "with the arrays a damaged file is written again");
    std::remove(path.c_str());
    const RemeshResult missing = remesh(withoutArrays(other));
    check(RemeshStatus::InvalidInput == missing.status, "no file and no arrays is InvalidInput");

    std::remove(path.c_str());
    std::printf("%s\n", 0 == failures ? "PASS" : "FAIL");
    return 0 == failures ? 0 : 1;
}
