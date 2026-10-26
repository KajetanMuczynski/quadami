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
#include <AutoRemesher/AutoRemesher>
#include <AutoRemesher/MeasureMesh>
#include <AutoRemesher/Remesh>
#include <AutoRemesher/RemeshPreview>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>

namespace AutoRemesher {

namespace {

    const char* validate(const RemeshRequest& request)
    {
        if (request.positions.empty())
            return "input mesh has no vertices";
        if (0 != request.positions.size() % 3)
            return "positions must hold three coordinates per vertex";
        if (request.triangles.empty())
            return "input mesh has no triangles";
        if (0 != request.triangles.size() % 3)
            return "triangles must hold three vertex indices each";
        for (const double coordinate : request.positions) {
            if (!std::isfinite(coordinate))
                return "input mesh has a vertex coordinate that is not a finite number";
        }
        const size_t vertexCount = request.positions.size() / 3;
        for (const uint32_t index : request.triangles) {
            if (index >= vertexCount)
                return "a triangle refers to a vertex index past the end of positions";
        }
        const RemeshParameters& parameters = request.parameters;
        if (0 == parameters.targetQuadCount)
            return "target quad count must be greater than zero";
        static_assert(2000000 == MaximumTargetQuadCount && 22e3 == ApproximatePeakBytesPerQuad,
            "the message below states both");
        if (parameters.targetQuadCount > MaximumTargetQuadCount)
            return "target quad count is above the maximum of 2000000 (it would need about 44 GB of memory)";
        if (!std::isfinite(parameters.adaptivity) || !std::isfinite(parameters.anisotropy)
            || !std::isfinite(parameters.edgeScaling) || !std::isfinite(parameters.sharpEdgeDegrees)
            || !std::isfinite(parameters.smoothNormalDegrees))
            return "parameters must be finite numbers";
        if (MeshResolution::Fast != parameters.resolution && MeshResolution::Quality != parameters.resolution)
            return "resolution must be fast or quality";
        if (Extractor::Qex != parameters.extractor && Extractor::Trace != parameters.extractor)
            return "extractor must be qex or trace";
        return nullptr;
    }

    void forwardProgress(void* tag, float progress, const char* status)
    {
        const auto* handler = static_cast<const RemeshProgressHandler*>(tag);
        (*handler)(progress, nullptr != status ? status : "");
    }

    size_t findRoot(std::vector<size_t>& parents, size_t vertex)
    {
        while (parents[vertex] != vertex) {
            parents[vertex] = parents[parents[vertex]];
            vertex = parents[vertex];
        }
        return vertex;
    }
}

void measureMesh(const RemeshMesh& mesh, QualityReport* report)
{
    const size_t vertexCount = mesh.positions.size() / 3;
    const size_t faceCount = mesh.faceSizes.size();
    const auto position = [&](uint32_t vertex) {
        return Vector3(mesh.positions[3 * (size_t)vertex],
            mesh.positions[3 * (size_t)vertex + 1],
            mesh.positions[3 * (size_t)vertex + 2]);
    };

    report->vertexCount = vertexCount;
    report->faceCount = faceCount;

    // One entry per face side: the undirected edge as a sorted vertex pair,
    // which face it belongs to, whether the face walks it low to high, and
    // the face's corners at the low and the high vertex.
    struct EdgeUse {
        uint64_t key;
        uint32_t face;
        bool forward;
        size_t lowCorner;
        size_t highCorner;
    };
    std::vector<EdgeUse> uses;
    uses.reserve(mesh.faceIndices.size());
    std::vector<size_t> parents(vertexCount);
    std::iota(parents.begin(), parents.end(), (size_t)0);
    std::vector<uint8_t> used(vertexCount, 0);
    // Consecutive corners of a face on the same vertex: one corner for the
    // fans, as the edge between them has no length.
    std::vector<std::pair<size_t, size_t>> repeatedCorners;

    size_t offset = 0;
    for (size_t face = 0; face < faceCount; ++face) {
        const size_t size = mesh.faceSizes[face];
        const uint32_t* corners = mesh.faceIndices.data() + offset;
        offset += size;
        if (4 == size)
            ++report->quadCount;
        else
            ++report->nonQuadCount;
        if (0 == size) {
            ++report->degenerateFaceCount;
            continue;
        }

        bool repeated = false;
        for (size_t i = 0; i < size; ++i) {
            for (size_t j = i + 1; j < size; ++j) {
                if (corners[i] == corners[j])
                    repeated = true;
            }
        }

        // Twice the area vector, by a fan from the first corner.
        Vector3 normal;
        double longestEdgeSquared = 0.0;
        const Vector3 origin = position(corners[0]);
        for (size_t i = 0; i < size; ++i) {
            const Vector3 from = position(corners[i]);
            const Vector3 to = position(corners[(i + 1) % size]);
            longestEdgeSquared = std::max(longestEdgeSquared, (to - from).lengthSquared());
            if (i > 0 && i + 1 < size)
                normal += Vector3::crossProduct(from - origin, to - origin);
        }
        if (repeated || 0.5 * normal.length() <= 1e-12 * longestEdgeSquared) {
            ++report->degenerateFaceCount;
        } else {
            // Folded: some corner turns against the area vector, i.e. its
            // scaled Jacobian is <= 0, so the face is non-convex or twisted.
            for (size_t i = 0; i < size; ++i) {
                const Vector3 corner = position(corners[i]);
                const Vector3 next = position(corners[(i + 1) % size]);
                const Vector3 previous = position(corners[(i + size - 1) % size]);
                if (Vector3::dotProduct(Vector3::crossProduct(next - corner, previous - corner), normal) <= 0.0) {
                    ++report->foldedFaceCount;
                    break;
                }
            }
        }

        for (size_t i = 0; i < size; ++i) {
            const uint32_t first = corners[i];
            const uint32_t second = corners[(i + 1) % size];
            used[first] = 1;
            const size_t firstCorner = offset - size + i;
            const size_t secondCorner = offset - size + (i + 1) % size;
            if (first == second) {
                repeatedCorners.emplace_back(firstCorner, secondCorner);
                continue;
            }
            const uint32_t low = std::min(first, second);
            const uint32_t high = std::max(first, second);
            uses.push_back({ ((uint64_t)low << 32) | high, (uint32_t)face, first == low,
                first == low ? firstCorner : secondCorner, first == low ? secondCorner : firstCorner });
            const size_t firstRoot = findRoot(parents, first);
            const size_t secondRoot = findRoot(parents, second);
            if (firstRoot != secondRoot)
                parents[std::max(firstRoot, secondRoot)] = std::min(firstRoot, secondRoot);
        }
    }

    std::sort(uses.begin(), uses.end(), [](const EdgeUse& a, const EdgeUse& b) {
        return a.key != b.key ? a.key < b.key : a.face < b.face;
    });
    // Corners joined when their faces share an edge at the corner's vertex:
    // the fans around each vertex.  Vertices joined by boundary edges: the
    // boundary loops.
    std::vector<size_t> cornerParents(mesh.faceIndices.size());
    std::iota(cornerParents.begin(), cornerParents.end(), (size_t)0);
    std::vector<size_t> boundaryParents(vertexCount);
    std::iota(boundaryParents.begin(), boundaryParents.end(), (size_t)0);
    std::vector<uint8_t> onBoundary(vertexCount, 0);
    const auto join = [](std::vector<size_t>& parents, size_t a, size_t b) {
        a = findRoot(parents, a);
        b = findRoot(parents, b);
        if (a != b)
            parents[std::max(a, b)] = std::min(a, b);
    };
    for (const auto& pair : repeatedCorners)
        join(cornerParents, pair.first, pair.second);
    for (size_t begin = 0; begin < uses.size();) {
        size_t end = begin + 1;
        while (end < uses.size() && uses[end].key == uses[begin].key)
            ++end;
        const size_t count = end - begin;
        for (size_t k = begin + 1; k < end; ++k) {
            join(cornerParents, uses[begin].lowCorner, uses[k].lowCorner);
            join(cornerParents, uses[begin].highCorner, uses[k].highCorner);
        }
        if (1 == count) {
            ++report->boundaryEdgeCount;
            const size_t low = (size_t)(uses[begin].key >> 32);
            const size_t high = (size_t)(uses[begin].key & 0xffffffffu);
            onBoundary[low] = onBoundary[high] = 1;
            join(boundaryParents, low, high);
        } else if (2 == count) {
            const EdgeUse& a = uses[begin];
            const EdgeUse& b = uses[begin + 1];
            if (a.forward == b.forward)
                ++report->orientationFlipCount;
        } else {
            ++report->nonManifoldEdgeCount;
        }
        begin = end;
    }

    for (size_t vertex = 0; vertex < vertexCount; ++vertex) {
        if (used[vertex] && findRoot(parents, vertex) == vertex)
            ++report->componentCount;
        if (onBoundary[vertex] && findRoot(boundaryParents, vertex) == vertex)
            ++report->boundaryLoopCount;
    }

    // A vertex is non-manifold when its corners fall into more than one fan.
    const size_t noFan = std::numeric_limits<size_t>::max();
    std::vector<size_t> firstFan(vertexCount, noFan);
    std::vector<uint8_t> nonManifold(vertexCount, 0);
    for (size_t corner = 0; corner < mesh.faceIndices.size(); ++corner) {
        const uint32_t vertex = mesh.faceIndices[corner];
        const size_t fan = findRoot(cornerParents, corner);
        if (noFan == firstFan[vertex]) {
            firstFan[vertex] = fan;
        } else if (fan != firstFan[vertex] && !nonManifold[vertex]) {
            nonManifold[vertex] = 1;
            ++report->nonManifoldVertexCount;
        }
    }
}

void measureClosedIslands(const RemeshMesh& mesh,
    const std::vector<::AutoRemesher::AutoRemesher::RemeshedIsland>& islands,
    QualityReport* report)
{
    size_t vertexBegin = 0;
    size_t faceBegin = 0;
    size_t cornerBegin = 0;
    for (const auto& island : islands) {
        size_t cornerCount = 0;
        for (size_t face = faceBegin; face < faceBegin + island.faceCount; ++face)
            cornerCount += mesh.faceSizes[face];
        if (island.inputTwiceHandles >= 0 && island.outputTwiceHandles >= 0
            && island.inputTwiceHandles != island.outputTwiceHandles) {
            // Rounded up: an odd difference (a one-sided piece) still changed.
            const long long change = island.outputTwiceHandles - island.inputTwiceHandles;
            if (change > 0)
                report->handlesAdded += (size_t)((change + 1) / 2);
            else
                report->handlesLost += (size_t)((1 - change) / 2);
            ++report->handlesChangedIslandCount;
        }
        if (island.closedInput) {
            ++report->closedInputIslandCount;
            if (island.faceCount > 0) {
                RemeshMesh part;
                part.positions.assign(mesh.positions.begin() + 3 * vertexBegin,
                    mesh.positions.begin() + 3 * (vertexBegin + island.vertexCount));
                part.faceSizes.assign(mesh.faceSizes.begin() + faceBegin,
                    mesh.faceSizes.begin() + faceBegin + island.faceCount);
                part.faceIndices.reserve(cornerCount);
                for (size_t corner = cornerBegin; corner < cornerBegin + cornerCount; ++corner)
                    part.faceIndices.push_back(mesh.faceIndices[corner] - (uint32_t)vertexBegin);
                QualityReport measured;
                measureMesh(part, &measured);
                if (measured.boundaryEdgeCount > 0 || measured.nonManifoldEdgeCount > 0
                    || measured.nonManifoldVertexCount > 0)
                    ++report->openedClosedIslandCount;
                report->closedIslandHoleCount += measured.boundaryLoopCount;
                report->closedIslandHoleEdgeCount += measured.boundaryEdgeCount;
            }
        }
        vertexBegin += island.vertexCount;
        faceBegin += island.faceCount;
        cornerBegin += cornerCount;
    }
}

namespace {

    // Always the classic locale, so a host that installs the user's locale
    // globally still gets a decimal point and no digit grouping.
    std::string jsonNumber(double value)
    {
        if (!std::isfinite(value))
            return "null";
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(9) << value;
        return stream.str();
    }

    std::string jsonString(const std::string& text)
    {
        std::string out = "\"";
        for (const char c : text) {
            if ('"' == c || '\\' == c) {
                out += '\\';
                out += c;
            } else if ('\n' == c) {
                out += "\\n";
            } else if ((unsigned char)c < 0x20) {
                const char* hex = "0123456789abcdef";
                out += "\\u00";
                out += hex[((unsigned char)c >> 4) & 0xf];
                out += hex[(unsigned char)c & 0xf];
            } else {
                out += c;
            }
        }
        out += '"';
        return out;
    }

    std::string jsonCount(size_t value)
    {
        return std::to_string(value);
    }

}

const char* toString(RemeshStatus status)
{
    switch (status) {
    case RemeshStatus::Ok:
        return "ok";
    case RemeshStatus::InvalidInput:
        return "invalid_input";
    case RemeshStatus::Cancelled:
        return "cancelled";
    case RemeshStatus::Failed:
        return "failed";
    }
    return "failed";
}

const char* toString(MeshResolution resolution)
{
    return MeshResolution::Fast == resolution ? "fast" : "quality";
}

bool parseMeshResolution(const std::string& text, MeshResolution* resolution)
{
    if ("fast" == text)
        *resolution = MeshResolution::Fast;
    else if ("quality" == text)
        *resolution = MeshResolution::Quality;
    else
        return false;
    return true;
}

const char* toString(Extractor extractor)
{
    return Extractor::Trace == extractor ? "trace" : "qex";
}

bool parseExtractor(const std::string& text, Extractor* extractor)
{
    if ("qex" == text)
        *extractor = Extractor::Qex;
    else if ("trace" == text)
        *extractor = Extractor::Trace;
    else
        return false;
    return true;
}

RemeshResult remeshWithPreview(const RemeshRequest& request,
    const RemeshCallbacks& callbacks,
    RemeshPreview* preview)
{
    const auto start = std::chrono::steady_clock::now();
    RemeshResult result;
    result.parameters = request.parameters;
    QualityReport& report = result.report;
    report.inputVertexCount = request.positions.size() / 3;
    report.inputTriangleCount = request.triangles.size() / 3;
    const auto finish = [&](RemeshStatus status, const std::string& message) {
        result.status = status;
        result.message = message;
        report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return result;
    };

    if (const char* reason = validate(request))
        return finish(RemeshStatus::InvalidInput, reason);
    if (nullptr != callbacks.cancel && callbacks.cancel->cancelRequested())
        return finish(RemeshStatus::Cancelled, "cancelled before the run started");

    std::vector<Vector3> vertices;
    vertices.reserve(request.positions.size() / 3);
    for (size_t i = 0; i < request.positions.size(); i += 3)
        vertices.emplace_back(request.positions[i], request.positions[i + 1], request.positions[i + 2]);
    std::vector<std::vector<size_t>> triangles;
    triangles.reserve(request.triangles.size() / 3);
    for (size_t i = 0; i < request.triangles.size(); i += 3)
        triangles.push_back({ request.triangles[i], request.triangles[i + 1], request.triangles[i + 2] });

    const RemeshParameters& parameters = request.parameters;
    ::AutoRemesher::AutoRemesher engine(std::move(vertices), std::move(triangles));
    // The engine counts triangles, two per quad, as the app always passed it.
    engine.setTargetTriangleCount((size_t)parameters.targetQuadCount * 2);
    if (parameters.edgeScaling > 0.0)
        engine.setScaling(parameters.edgeScaling);
    engine.setGradientAdaptivity(parameters.adaptivity);
    engine.setAnisotropy(parameters.anisotropy);
    engine.setSharpEdgeDegrees(parameters.sharpEdgeDegrees);
    engine.setSmoothNormalDegrees(parameters.smoothNormalDegrees);
    engine.setUseQex(Extractor::Qex == parameters.extractor);
    engine.setRepairInputFolds(parameters.repairFolds);
    engine.setFlattenSpikes(parameters.flattenSpikes);
    engine.setTrianglesPerQuad(MeshResolution::Fast == parameters.resolution
            ? ::AutoRemesher::FastTrianglesPerQuad
            : ::AutoRemesher::QualityTrianglesPerQuad);
    if (callbacks.progress) {
        engine.setTag(const_cast<RemeshProgressHandler*>(&callbacks.progress));
        engine.setProgressHandler(forwardProgress);
    }
    if (nullptr != callbacks.cancel) {
        const CancelToken* token = callbacks.cancel;
        engine.setCancelCheck([token]() { return token->cancelRequested(); });
    }

    // Hosts get every outcome as a status: a worker process should report an
    // error, and an exception escaping into a Qt thread would end the app.
    bool completed = false;
    try {
        completed = engine.remesh();
    } catch (const std::exception& e) {
        return finish(RemeshStatus::Failed, std::string("remeshing failed: ") + e.what());
    }

    if (nullptr != preview)
        preview->phaseReport = engine.phaseReport();
    result.stageDiagnostics = engine.diagnostics();
    if (!result.stageDiagnostics.empty())
        report.diagnostics = summarizeDiagnostics(result.stageDiagnostics);
    if (engine.cancelled())
        return finish(RemeshStatus::Cancelled, "cancelled");
    if (!completed)
        return finish(RemeshStatus::Failed, "the core stopped before remeshing any island");

    for (const auto& step : engine.stepTimes())
        report.stepSeconds.emplace_back(step.name, step.microseconds / 1e6);
    report.islandCount = engine.islandCount();
    report.islandsWithoutOutput = engine.islandsWithoutOutput();

    const auto& remeshedVertices = engine.remeshedVertices();
    const auto& remeshedFaces = engine.remeshedQuads();
    RemeshMesh& mesh = result.mesh;
    mesh.positions.reserve(remeshedVertices.size() * 3);
    for (const auto& vertex : remeshedVertices) {
        mesh.positions.push_back(vertex.x());
        mesh.positions.push_back(vertex.y());
        mesh.positions.push_back(vertex.z());
    }
    mesh.faceSizes.reserve(remeshedFaces.size());
    mesh.faceIndices.reserve(remeshedFaces.size() * 4);
    for (const auto& face : remeshedFaces) {
        mesh.faceSizes.push_back((uint32_t)face.size());
        for (const size_t vertex : face)
            mesh.faceIndices.push_back((uint32_t)vertex);
    }
    measureMesh(mesh, &report);
    measureClosedIslands(mesh, engine.remeshedIslands(), &report);

    if (nullptr != preview) {
        preview->decimated = engine.decimated();
        preview->decimatedVertices = engine.decimatedVertices();
        preview->decimatedTriangles = engine.decimatedTriangles();
        preview->isotropicVertices = engine.isotropicVertices();
        preview->isotropicTriangles = engine.isotropicTriangles();
        preview->isotropicTriangleUvs = engine.isotropicTriangleUvs();
        preview->isotropicOriginalTriangleUvs = engine.isotropicOriginalTriangleUvs();
        preview->isotropicExtractedConnectionMoved = engine.isotropicExtractedConnectionMoved();
        preview->isotropicSingularVertices = engine.isotropicSingularVertices();
        preview->isotropicExtractedConnections = engine.isotropicExtractedConnections();
    }

    if (0 == report.faceCount)
        return finish(RemeshStatus::Failed, "remeshing produced no faces");
    if (report.islandsWithoutOutput > 0) {
        report.warnings.push_back(std::to_string(report.islandsWithoutOutput) + " of "
            + std::to_string(report.islandCount) + " islands produced no faces");
    }
    if (report.closedIslandHoleCount > 0) {
        report.warnings.push_back("Output has " + std::to_string(report.closedIslandHoleCount)
            + (1 == report.closedIslandHoleCount ? " hole" : " holes") + " where the input was closed ("
            + std::to_string(report.closedIslandHoleEdgeCount) + " open edges)");
    }
    if (report.handlesLost > 0) {
        report.warnings.push_back("Output has " + std::to_string(report.handlesLost)
            + (1 == report.handlesLost ? " handle" : " handles")
            + " fewer than the input (tunnels or loops closed up, on "
            + std::to_string(report.handlesChangedIslandCount)
            + (1 == report.handlesChangedIslandCount ? " island" : " islands")
            + "): handles only a few quads around close up. A higher Target Quads keeps more of them");
    }
    if (report.handlesAdded > 0) {
        report.warnings.push_back("Output has " + std::to_string(report.handlesAdded)
            + (1 == report.handlesAdded ? " handle" : " handles")
            + " more than the input (surfaces joined across a thin gap)");
    }
    {
        // Parts thinner than one quad (ThinParts).  On the corpus, 3 % of the
        // surface, or 10 spots on 1 %, catches 84 % of the runs whose thin
        // parts fold or get lost; a third of the runs it fires on come out
        // clean (closed thin shells, sharp creases), so it states a risk.
        double share = 0.0, spots = 0.0, growth = 0.0;
        for (const auto& it : report.diagnostics) {
            if (it.first == "thin_area_share")
                share = it.second;
            else if (it.first == "thin_spots")
                spots = it.second;
            else if (it.first == "thin_growth_p50")
                growth = it.second;
        }
        const bool warned = share >= 0.03 || (spots >= 10 && share >= 0.01);
        if (warned) {
            std::string text = std::to_string((int)std::lround(100.0 * share)) + " % of the surface ("
                + std::to_string((long long)spots) + (1 == spots ? " spot" : " spots")
                + ") is thinner than one quad; expect folds or lost parts there. ";
            const double needed = growth * (double)parameters.targetQuadCount;
            if (needed > (double)MaximumTargetQuadCount) {
                text += "No target quad count fits a quad across most of it: thicken those parts";
            } else {
                // Two significant digits; a lower bound, since a finer T shows thinner parts.
                const double step = std::pow(10.0, std::floor(std::log10(std::max(needed, 1.0))) - 1.0);
                text += "Try at least "
                    + std::to_string((long long)(std::ceil(needed / step) * step)) + " target quads, or thicken those parts";
            }
            report.warnings.push_back(text);
        }
        // For front ends that add their own hint under the warning.
        report.diagnostics.push_back({ "thin_parts_warned", warned ? 1.0 : 0.0 });
    }
    return finish(RemeshStatus::Ok, std::string());
}

RemeshResult remesh(const RemeshRequest& request, const RemeshCallbacks& callbacks)
{
    return remeshWithPreview(request, callbacks, nullptr);
}

std::string toJson(const RemeshResult& result)
{
    const RemeshParameters& parameters = result.parameters;
    const QualityReport& report = result.report;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\n";
    out << "  \"schema\": \"autoremesher.remesh-result\",\n";
    out << "  \"interface_version\": \"" << InterfaceVersionMajor << "." << InterfaceVersionMinor << "\",\n";
    out << "  \"status\": " << jsonString(toString(result.status)) << ",\n";
    out << "  \"message\": " << jsonString(result.message) << ",\n";
    out << "  \"parameters\": {"
        << "\"target_quads\": " << parameters.targetQuadCount
        << ", \"adaptivity\": " << jsonNumber(parameters.adaptivity)
        << ", \"anisotropy\": " << jsonNumber(parameters.anisotropy)
        << ", \"edge_scaling\": " << jsonNumber(parameters.edgeScaling)
        << ", \"sharp_edge_degrees\": " << jsonNumber(parameters.sharpEdgeDegrees)
        << ", \"smooth_normal_degrees\": " << jsonNumber(parameters.smoothNormalDegrees)
        << ", \"resolution\": " << jsonString(toString(parameters.resolution))
        << ", \"repair_folds\": " << (parameters.repairFolds ? "true" : "false")
        << ", \"flatten_spikes\": " << (parameters.flattenSpikes ? "true" : "false")
        << ", \"extractor\": " << jsonString(toString(parameters.extractor))
        << "},\n";
    out << "  \"input\": {"
        << "\"vertices\": " << jsonCount(report.inputVertexCount)
        << ", \"triangles\": " << jsonCount(report.inputTriangleCount)
        << "},\n";
    out << "  \"output\": {"
        << "\"vertices\": " << jsonCount(report.vertexCount)
        << ", \"faces\": " << jsonCount(report.faceCount)
        << ", \"quads\": " << jsonCount(report.quadCount)
        << ", \"non_quads\": " << jsonCount(report.nonQuadCount)
        << ", \"quad_ratio\": " << jsonNumber(parameters.targetQuadCount > 0 ? (double)report.quadCount / parameters.targetQuadCount : 0.0)
        << ",\n             \"non_manifold_edges\": " << jsonCount(report.nonManifoldEdgeCount)
        << ", \"boundary_edges\": " << jsonCount(report.boundaryEdgeCount)
        << ", \"orientation_flips\": " << jsonCount(report.orientationFlipCount)
        << ", \"folded_faces\": " << jsonCount(report.foldedFaceCount)
        << ",\n             \"degenerate_faces\": " << jsonCount(report.degenerateFaceCount)
        << ", \"components\": " << jsonCount(report.componentCount)
        << ", \"boundary_loops\": " << jsonCount(report.boundaryLoopCount)
        << ", \"non_manifold_vertices\": " << jsonCount(report.nonManifoldVertexCount)
        << "},\n";
    out << "  \"islands\": {"
        << "\"input\": " << jsonCount(report.islandCount)
        << ", \"without_output\": " << jsonCount(report.islandsWithoutOutput)
        << ", \"closed_in_input\": " << jsonCount(report.closedInputIslandCount)
        << ", \"opened\": " << jsonCount(report.openedClosedIslandCount)
        << ",\n              \"holes\": " << jsonCount(report.closedIslandHoleCount)
        << ", \"hole_edges\": " << jsonCount(report.closedIslandHoleEdgeCount)
        << ",\n              \"handles_lost\": " << jsonCount(report.handlesLost)
        << ", \"handles_added\": " << jsonCount(report.handlesAdded)
        << ", \"handles_changed\": " << jsonCount(report.handlesChangedIslandCount)
        << "},\n";
    out << "  \"time\": {\"total_seconds\": " << jsonNumber(report.seconds) << ", \"steps\": [";
    for (size_t i = 0; i < report.stepSeconds.size(); ++i) {
        out << (0 == i ? "\n" : ",\n")
            << "    {\"name\": " << jsonString(report.stepSeconds[i].first)
            << ", \"seconds\": " << jsonNumber(report.stepSeconds[i].second) << "}";
    }
    out << (report.stepSeconds.empty() ? "]},\n" : "\n  ]},\n");
    out << "  \"warnings\": [";
    for (size_t i = 0; i < report.warnings.size(); ++i)
        out << (0 == i ? "" : ", ") << jsonString(report.warnings[i]);
    out << "],\n";
    out << "  \"diagnostics\": {";
    for (size_t i = 0; i < report.diagnostics.size(); ++i) {
        out << (0 == i ? "" : ", ") << jsonString(report.diagnostics[i].first)
            << ": " << jsonNumber(report.diagnostics[i].second);
    }
    out << "}\n";
    out << "}\n";
    return out.str();
}

}
