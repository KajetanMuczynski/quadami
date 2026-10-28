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
#include <AutoRemesher/SurfaceProjector>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#if defined(__has_include)
#if __has_include(<oneapi/tbb/blocked_range.h>)
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#else
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#endif
#else
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#endif

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
        if (QuadSmoothing::Laplacian != parameters.quadSmoothing && QuadSmoothing::Tangential != parameters.quadSmoothing)
            return "quad smoothing must be laplacian or tangential";
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

void measureLostParts(const std::vector<double>& positions, const std::vector<uint32_t>& triangles,
    size_t targetQuads, const RemeshMesh& mesh,
    const std::vector<::AutoRemesher::AutoRemesher::RemeshedIsland>& islands,
    QualityReport* report, const std::vector<size_t>* leftOut)
{
    for (const auto& island : islands) {
        if (0 == island.faceCount || island.faceCount >= 0.5 * island.targetQuads)
            continue;
        if (0 == report->collapsedIslandCount
            || island.faceCount * report->smallestCollapsedIslandTargetQuads
                < report->smallestCollapsedIslandFaces * island.targetQuads) {
            report->smallestCollapsedIslandFaces = island.faceCount;
            report->smallestCollapsedIslandTargetQuads = (size_t)std::lround(island.targetQuads);
        }
        ++report->collapsedIslandCount;
    }

    if (0 == targetQuads || mesh.faceSizes.empty())
        return;
    const auto point = [&](size_t vertex) {
        return Vector3(positions[3 * vertex], positions[3 * vertex + 1], positions[3 * vertex + 2]);
    };
    double area = 0.0;
    std::vector<char> used(positions.size() / 3, 0);
    for (size_t i = 0; i < triangles.size(); i += 3) {
        area += 0.5 * Vector3::crossProduct(point(triangles[i + 1]) - point(triangles[i]), point(triangles[i + 2]) - point(triangles[i])).length();
        used[triangles[i]] = used[triangles[i + 1]] = used[triangles[i + 2]] = 1;
    }
    if (nullptr != leftOut) {
        for (const size_t vertex : *leftOut) {
            if (vertex < used.size())
                used[vertex] = 0;
        }
    }
    std::vector<uint32_t> inputVertices;
    for (size_t vertex = 0; vertex < used.size(); ++vertex) {
        if (used[vertex])
            inputVertices.push_back((uint32_t)vertex);
    }
    const double quadWidth = std::sqrt(area / (double)targetQuads);
    if (!(quadWidth > 0.0))
        return;

    // The output as triangles, each face a fan from its first corner.
    std::vector<Vector3> outputVertices;
    outputVertices.reserve(mesh.positions.size() / 3);
    for (size_t i = 0; i < mesh.positions.size(); i += 3)
        outputVertices.emplace_back(mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]);
    std::vector<std::vector<size_t>> outputTriangles;
    outputTriangles.reserve(2 * mesh.faceSizes.size());
    size_t corner = 0;
    for (const uint32_t size : mesh.faceSizes) {
        for (uint32_t k = 1; k + 1 < size; ++k)
            outputTriangles.push_back({ mesh.faceIndices[corner], mesh.faceIndices[corner + k], mesh.faceIndices[corner + k + 1] });
        corner += size;
    }
    const SurfaceProjector output(&outputVertices, &outputTriangles);

    // The search radius bounds the work per vertex: the distance to the
    // output moves by at most the step between two vertices, so the previous
    // vertex's distance plus the step bounds the next search (the input
    // numbers neighbours close together).  Only a vertex with nothing within
    // the radius is searched again without a bound.
    const double radius = LostPartQuadWidths * quadWidth;
    std::vector<double> distances(inputVertices.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, inputVertices.size()), [&](const tbb::blocked_range<size_t>& range) {
        Vector3 previous;
        double previousDistance = std::numeric_limits<double>::max();
        for (size_t i = range.begin(); i != range.end(); ++i) {
            const Vector3 position = point(inputVertices[i]);
            const double bound = previousDistance + (position - previous).length();
            double distance = output.distance(position, std::min(radius, bound * (1.0 + 1e-9) + 1e-12 * quadWidth));
            if (distance >= radius)
                distance = output.distance(position, std::numeric_limits<double>::max());
            distances[i] = distance;
            previous = position;
            previousDistance = distance;
        }
    });
    std::vector<size_t> far;
    for (size_t i = 0; i < distances.size(); ++i) {
        report->farthestInputQuadWidths = std::max(report->farthestInputQuadWidths, distances[i] / quadWidth);
        if (distances[i] > radius)
            far.push_back(i);
    }
    report->farInputVertexCount = far.size();
    if (far.empty())
        return;

    // Why each far vertex is lost.  A piece of the input with no output
    // within one quad width came out with nothing (a speck, a hair card, a
    // whole island); a higher target does not necessarily bring it back.
    // Otherwise the part was cut off or smoothed away: thin if its other
    // side is close, and then the target is what limits it.
    std::vector<uint32_t> parent(used.size());
    std::iota(parent.begin(), parent.end(), 0u);
    const auto root = [&](uint32_t vertex) {
        while (parent[vertex] != vertex)
            vertex = parent[vertex] = parent[parent[vertex]];
        return vertex;
    };
    for (size_t i = 0; i < triangles.size(); i += 3) {
        const uint32_t a = root(triangles[i]);
        parent[root(triangles[i + 1])] = a;
        parent[root(triangles[i + 2])] = a;
    }
    std::vector<char> pieceHasOutput(used.size(), 0);
    for (size_t i = 0; i < inputVertices.size(); ++i) {
        if (distances[i] <= quadWidth)
            pieceHasOutput[root(inputVertices[i])] = 1;
    }
    std::vector<size_t> attached;
    std::vector<uint32_t> lostPieces;
    for (const size_t i : far) {
        const uint32_t piece = root(inputVertices[i]);
        if (pieceHasOutput[piece]) {
            attached.push_back(i);
        } else {
            ++report->pieceLostVertexCount;
            lostPieces.push_back(piece);
        }
    }
    std::sort(lostPieces.begin(), lostPieces.end());
    report->lostPieceCount = (size_t)(std::unique(lostPieces.begin(), lostPieces.end()) - lostPieces.begin());
    if (attached.empty())
        return;

    // Thickness at a sample of the attached far vertices: the distance to the
    // nearest input vertex whose normal faces it (the part's other side),
    // searched in a grid of cells as wide as the thin limit.  A single-sided
    // surface (a leaf, a petal, cloth) has no other side; there the nearest
    // open edge, an edge of one input triangle, is what is close.
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    edges.reserve(triangles.size());
    for (size_t i = 0; i < triangles.size(); i += 3) {
        for (size_t k = 0; k < 3; ++k) {
            const uint32_t a = triangles[i + k], b = triangles[i + (k + 1) % 3];
            edges.emplace_back(std::min(a, b), std::max(a, b));
        }
    }
    std::sort(edges.begin(), edges.end());
    std::vector<char> onOpenEdge(used.size(), 0);
    for (size_t i = 0; i < edges.size();) {
        size_t j = i + 1;
        while (j < edges.size() && edges[j] == edges[i])
            ++j;
        if (j - i == 1)
            onOpenEdge[edges[i].first] = onOpenEdge[edges[i].second] = 1;
        i = j;
    }
    std::vector<Vector3> normals(used.size());
    for (size_t i = 0; i < triangles.size(); i += 3) {
        const Vector3 normal = Vector3::crossProduct(point(triangles[i + 1]) - point(triangles[i]),
            point(triangles[i + 2]) - point(triangles[i]));
        for (size_t k = 0; k < 3; ++k)
            normals[triangles[i + k]] += normal;
    }
    for (auto& normal : normals)
        normal.normalize();
    const double cellSize = ThinLostPartQuadWidths * quadWidth;
    Vector3 low = point(inputVertices[0]);
    for (const uint32_t vertex : inputVertices) {
        for (int k = 0; k < 3; ++k)
            low[k] = std::min(low[k], point(vertex)[k]);
    }
    using Cell = std::array<long long, 3>;
    const auto cellOf = [&](const Vector3& position) {
        return Cell { (long long)std::floor((position[0] - low[0]) / cellSize),
            (long long)std::floor((position[1] - low[1]) / cellSize),
            (long long)std::floor((position[2] - low[2]) / cellSize) };
    };
    std::vector<std::pair<Cell, uint32_t>> grid;
    grid.reserve(inputVertices.size());
    for (const uint32_t vertex : inputVertices)
        grid.emplace_back(cellOf(point(vertex)), vertex);
    std::sort(grid.begin(), grid.end());
    const size_t sampleCount = std::min<size_t>(attached.size(), 1024);
    std::vector<double> thickness(sampleCount, std::numeric_limits<double>::max());
    std::vector<char> nearOpenEdge(sampleCount, 0);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, sampleCount), [&](const tbb::blocked_range<size_t>& range) {
        for (size_t s = range.begin(); s != range.end(); ++s) {
            const uint32_t vertex = inputVertices[attached[s * attached.size() / sampleCount]];
            const Vector3 position = point(vertex);
            const Cell cell = cellOf(position);
            for (long long dx = -1; dx <= 1; ++dx) {
                for (long long dy = -1; dy <= 1; ++dy) {
                    for (long long dz = -1; dz <= 1; ++dz) {
                        const Cell near = { cell[0] + dx, cell[1] + dy, cell[2] + dz };
                        auto it = std::lower_bound(grid.begin(), grid.end(), std::make_pair(near, (uint32_t)0));
                        for (; it != grid.end() && it->first == near; ++it) {
                            const double distance = (point(it->second) - position).length();
                            if (Vector3::dotProduct(normals[it->second], normals[vertex]) < -0.5)
                                thickness[s] = std::min(thickness[s], distance);
                            if (onOpenEdge[it->second] && distance <= cellSize)
                                nearOpenEdge[s] = 1;
                        }
                    }
                }
            }
        }
    });
    std::vector<double> thin;
    size_t openEdgeSamples = 0;
    for (size_t s = 0; s < sampleCount; ++s) {
        if (thickness[s] <= cellSize)
            thin.push_back(thickness[s] / quadWidth);
        else if (nearOpenEdge[s])
            ++openEdgeSamples;
    }
    const auto scaled = [&](size_t samples) {
        return (size_t)std::llround((double)attached.size() * samples / sampleCount);
    };
    report->thinLostVertexCount = scaled(thin.size());
    report->openEdgeLostVertexCount = std::min(scaled(openEdgeSamples), attached.size() - report->thinLostVertexCount);
    report->otherLostVertexCount = attached.size() - report->thinLostVertexCount - report->openEdgeLostVertexCount;
    if (!thin.empty()) {
        std::sort(thin.begin(), thin.end());
        // In quad widths; recorded only.  Neither predicts the target that
        // brings the parts back (off by up to 30 times on the ladder).
        report->diagnostics.emplace_back("lost_thin_thickness_p10", thin[thin.size() / 10]);
        report->diagnostics.emplace_back("lost_thin_thickness_p50", thin[thin.size() / 2]);
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

const char* toString(QuadSmoothing smoothing)
{
    return QuadSmoothing::Tangential == smoothing ? "tangential" : "laplacian";
}

bool parseQuadSmoothing(const std::string& text, QuadSmoothing* smoothing)
{
    if ("laplacian" == text)
        *smoothing = QuadSmoothing::Laplacian;
    else if ("tangential" == text)
        *smoothing = QuadSmoothing::Tangential;
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
    engine.setTangentialQuadSmoothing(QuadSmoothing::Tangential == parameters.quadSmoothing);
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
    {
        const auto lostPartsStart = std::chrono::steady_clock::now();
        measureLostParts(request.positions, request.triangles, parameters.targetQuadCount, mesh,
            engine.remeshedIslands(), &report, &engine.leftOutVertices());
        report.diagnostics.emplace_back("lost_parts_ms",
            1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - lostPartsStart).count());
    }

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
    if (report.farInputVertexCount > 0) {
        // One sentence per cause that holds at least a quarter of the far
        // vertices.  What each says a higher target does was measured in
        // research/thin-warning-2026-10-27.md (10 models, targets x1 to x8, 6
        // replicates, at a fixed distance of 3 h0 of the first target): the
        // thin parts lost first came back on 7 models, mostly on 2 more, and
        // on 2 of 6 replicates of the last; 25 of 50 and 9 of 9 lost pieces
        // came back; 9 in 10 open-edge vertices came back.  Each raise also
        // finds finer parts that are now thinner than a quad, so the warning
        // itself can stay.
        const std::string away = " (up to " + std::to_string((long long)std::floor(report.farthestInputQuadWidths))
            + " quad widths away)";
        const auto counts = [&](size_t vertices) { return 4 * vertices >= report.farInputVertexCount; };
        std::string text;
        if (counts(report.thinLostVertexCount)) {
            text = "Parts thinner than one quad are missing" + away + ". At "
                + std::to_string(parameters.targetQuadCount)
                + " target quads there is no room for them: a limit of Target Quads, not a remeshing error. "
                  "Raise Target Quads (1.5 to 8 times as many brought such parts back on most test models), "
                  "though each raise can show finer parts still too thin; or thicken them.";
        }
        if (counts(report.pieceLostVertexCount)) {
            text += std::string(text.empty() ? "" : " ") + std::to_string(report.lostPieceCount)
                + (1 == report.lostPieceCount ? " separate piece" : " separate pieces")
                + " came out with no quads at all" + (text.empty() ? away : std::string())
                + ". Raising Target Quads brought some or all such pieces back on test models (at up to 8 "
                  "times as many); delete specks you do not need.";
        }
        if (counts(report.openEdgeLostVertexCount)) {
            text += std::string(text.empty() ? "" : " ") + "Parts along open edges of single-sided surfaces "
                                                           "(leaves, petals, cloth) are missing"
                + (text.empty() ? away : std::string())
                + ". Raising Target Quads brought most of them back on a test model, not all; giving the "
                  "surface thickness (Solidify) removes the open edges.";
        }
        if (counts(report.otherLostVertexCount) || text.empty()) {
            text += std::string(text.empty() ? "" : " ") + "Parts that are not thinner than a quad are missing"
                + away + ", so Target Quads may not be the cause. If raising it does not bring them back, "
                         "please report it.";
        }
        report.warnings.push_back(text);
    }
    if (report.collapsedIslandCount > 0) {
        report.warnings.push_back(std::to_string(report.collapsedIslandCount)
            + (1 == report.collapsedIslandCount ? " island" : " islands")
            + " came out with far fewer quads than planned (smallest: "
            + std::to_string(report.smallestCollapsedIslandFaces) + " of "
            + std::to_string(report.smallestCollapsedIslandTargetQuads) + ")");
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
                + ") is thinner than one quad at " + std::to_string(parameters.targetQuadCount)
                + " target quads, a limit of Target Quads rather than a remeshing error: expect folds or "
                  "lost parts there. ";
            const double needed = growth * (double)parameters.targetQuadCount;
            if (needed > (double)MaximumTargetQuadCount) {
                text += "No target quad count fits a quad across half of it: thicken those parts";
            } else {
                // Two significant digits, rounded up. The count fits a quad across
                // the area-weighted median of the parts thin at this count, and a
                // finer T shows thinner parts, so it is a first step, not the count
                // that clears the warning: on a vine-covered sword it came out at
                // 1.6-2.1 times the count asked for at every count from 3,000 to 75,000.
                const double step = std::pow(10.0, std::floor(std::log10(std::max(needed, 1.0))) - 1.0);
                text += "At least "
                    + std::to_string((long long)(std::ceil(needed / step) * step))
                    + " target quads (an estimate) would fit a quad across about half of it, but each raise "
                      "also reveals thinner parts, so it can take several raises; thickening those parts "
                      "also works";
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
        << ", \"quad_smoothing\": " << jsonString(toString(parameters.quadSmoothing))
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
        << ",\n              \"collapsed\": " << jsonCount(report.collapsedIslandCount)
        << ", \"smallest_collapsed_faces\": " << jsonCount(report.smallestCollapsedIslandFaces)
        << ", \"smallest_collapsed_target_quads\": " << jsonCount(report.smallestCollapsedIslandTargetQuads)
        << "},\n";
    out << "  \"lost_parts\": {"
        << "\"farthest_input_quad_widths\": " << jsonNumber(report.farthestInputQuadWidths)
        << ", \"far_input_vertices\": " << jsonCount(report.farInputVertexCount)
        << ", \"threshold_quad_widths\": " << jsonNumber(LostPartQuadWidths)
        << ", \"piece_vertices\": " << jsonCount(report.pieceLostVertexCount)
        << ", \"pieces\": " << jsonCount(report.lostPieceCount)
        << ", \"thin_vertices\": " << jsonCount(report.thinLostVertexCount)
        << ", \"open_edge_vertices\": " << jsonCount(report.openEdgeLostVertexCount)
        << ", \"other_vertices\": " << jsonCount(report.otherLostVertexCount)
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
