/*
 *  Copyright (c) 2026 Jeremy HU <jeremy-at-dust3d dot org>. All rights reserved.
 *  Modifications Copyright (C) 2026 Quadami contributors, GPL-3.0-or-later (see LICENSE).
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */
#include "qexextraction.h"
#include <AutoRemesher/AutoRemesher>
#include <AutoRemesher/FeatureEdges>
#include <AutoRemesher/FoldRepair>
#include <AutoRemesher/IsotropicRemesher>
#include <AutoRemesher/MeshSeparator>
#include <AutoRemesher/Parameterizer>
#include <AutoRemesher/QuadExtractor>
#include <AutoRemesher/SurfaceProjector>
#include <AutoRemesher/ThinParts>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
// Qt defines `emit` as a macro, which collides with TBB profiling.h's `void emit()`.
// macOS `<mach/mach.h>` also defines `emit`. Undefine before including TBB headers.
#if defined(__APPLE__) || defined(emit)
#undef emit
#endif

// oneAPI TBB (2021+) moved headers under <oneapi/tbb/>. Use __has_include where
// available (clang + GCC) to pick the right path, falling back to the legacy path.
#if defined(__has_include)
#if __has_include(<oneapi/tbb/blocked_range.h>)
#include <oneapi/tbb/blocked_range.h>
#else
#include <tbb/blocked_range.h>
#endif
#if __has_include(<oneapi/tbb/mutex.h>)
#include <oneapi/tbb/mutex.h>
#else
#include <tbb/mutex.h>
#endif
#if __has_include(<oneapi/tbb/parallel_for.h>)
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/parallel_sort.h>
#else
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>
#endif
#else
#include <tbb/blocked_range.h>
#include <tbb/mutex.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>
#endif
#include <cfloat>
#include <meshoptimizer.h>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace AutoRemesher {

namespace {
    const float parallelPhaseBegin = 0.03f;
    const float parallelPhaseEnd = 0.95f;

    // How an island's own 0..1 progress splits across its three stages, from the
    // measured cost of each on a typical model.  The phase report prints the
    // real accumulated times, so these can be re-checked against a run.
    const float islandResampleEnd = 0.17f;
    const float islandParameterizeEnd = 0.50f;
    // Quad extraction runs from islandParameterizeEnd to 1.0.

    const double decimateTriggerRatio = 8.0;
    const double decimateTargetRatio = 4.0;

    const double triangleAreaPerSquaredEdge = 0.86602540378 * 0.5;

    // Rounds of tangential smoothing after the surface projection (TODO 2.8),
    // the extractor's own count.
    const size_t surfaceRelaxIterations = 5;

    // The area share of a small island (an eye, a button) can be a handful of
    // triangles. The isotropic remesher then shrinks it to a few triangles and
    // the parameterizer finds no quads on it, so the island vanished from the
    // output. Such an island is remeshed as if it had asked for this many
    // triangles instead, but never more than its input has, so that specks of
    // scan noise do not each grow to this size.
    const double minimumIslandTriangleCount = 64.0;

    // An island that still fails is run again with its triangle and quad
    // targets raised by this factor, up to this many times, if it asked for at
    // most this many triangles. The factor is small because a repaired island
    // keeps its raised count, which the total then overshoots by. Larger
    // islands are not retried, since a run costs as much as the island is
    // large, and neither are islands with fewer input triangles than the
    // minimum above, so that a triangle soup is not retried triangle by
    // triangle.
    const double islandRetryGrowth = 1.25;
    const size_t maximumIslandRetries = 4;
    const double maximumRetriedIslandTriangleCount = 1024.0;

    using SurfaceTopology = AutoRemesher::SurfaceTopology;

    // Faces is indexable, and so is each face; their corners index vertices
    // 0..vertexCount-1.  Vertices no face uses are not counted.
    template <typename Faces>
    SurfaceTopology surfaceTopology(const Faces& faces, size_t vertexCount)
    {
        SurfaceTopology topology;
        const size_t unused = std::numeric_limits<size_t>::max();
        std::vector<size_t> parent(vertexCount, unused);
        const auto root = [&](size_t v) {
            while (parent[v] != v)
                v = parent[v] = parent[parent[v]];
            return v;
        };
        size_t usedVertices = 0;
        std::vector<std::pair<size_t, size_t>> edges;
        for (size_t f = 0; f < faces.size(); ++f) {
            const auto& face = faces[f];
            for (size_t i = 0; i < face.size(); ++i) {
                if (unused == parent[face[i]]) {
                    parent[face[i]] = face[i];
                    ++usedVertices;
                }
            }
            const size_t first = root(face[0]);
            for (size_t i = 0; i < face.size(); ++i) {
                parent[root(face[i])] = first;
                const size_t a = face[i], b = face[(i + 1) % face.size()];
                edges.push_back({ std::min(a, b), std::max(a, b) });
            }
        }
        for (size_t v = 0; v < vertexCount; ++v) {
            if (parent[v] == v)
                ++topology.components;
        }
        tbb::parallel_sort(edges.begin(), edges.end());
        size_t edgeCount = 0;
        for (size_t i = 0; i < edges.size();) {
            size_t j = i;
            while (j < edges.size() && edges[j] == edges[i])
                ++j;
            ++edgeCount;
            if (1 == j - i)
                ++topology.boundaryEdges;
            else if (j - i > 2)
                ++topology.nonManifoldEdges;
            i = j;
        }
        topology.eulerCharacteristic = (long long)usedVertices - (long long)edgeCount + (long long)faces.size();
        return topology;
    }

    // A flat triangle index buffer as surfaceTopology() reads faces.
    struct FlatTriangles {
        struct Triangle {
            const unsigned int* corners;
            size_t size() const { return 3; }
            size_t operator[](size_t i) const { return corners[i]; }
        };
        const std::vector<unsigned int>& indices;
        size_t size() const { return indices.size() / 3; }
        Triangle operator[](size_t f) const { return { indices.data() + 3 * f }; }
    };

    // Whether decimating a surface of topology `input` to `output` kept it:
    // one piece, no new non-manifold edge, and a closed surface stays closed.
    // On a manifold input the Euler characteristic, so the genus, is kept too
    // (a valid edge collapse keeps it, also on a border).
    bool keepsTopology(const SurfaceTopology& input, const SurfaceTopology& output)
    {
        return output.components == input.components
            && output.nonManifoldEdges <= input.nonManifoldEdges
            && (0 != input.boundaryEdges || 0 == output.boundaryEdges)
            && (0 != input.nonManifoldEdges || output.eulerCharacteristic == input.eulerCharacteristic);
    }

    // Whether a run gave the island a usable surface: one piece with some
    // faces, every edge on at most two of them, and, if the input island is
    // closed, a closed result of the same Euler characteristic (so the same
    // genus). Open islands are not held to their boundary: extraction still
    // caps short boundary loops (REFERENCE M15).
    bool isHealthyIslandResult(const SurfaceTopology& input, const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& quads)
    {
        if (quads.empty())
            return false;
        const SurfaceTopology output = surfaceTopology(quads, vertices.size());
        if (1 != output.components || 0 != output.nonManifoldEdges)
            return false;
        if (0 == input.boundaryEdges && 0 == input.nonManifoldEdges) {
            return 0 == output.boundaryEdges
                && output.eulerCharacteristic == input.eulerCharacteristic;
        }
        return true;
    }

    // Thrown from a cancellation point and caught in remesh().  Deliberately not
    // a std::exception, so the per-island error handling doesn't take it for a
    // failed island.
    struct RemeshCancelled {
    };

    // Crease length of one stage's triangle mesh around the sharp-edge angle, for the
    // `features` record (diagnostics only; research/crease-preservation-2026-10-26.md).
    // Measured on each stage's geometry, apart from the island's FeatureEdgeSet:
    // nearTheta counts the edges within 1 deg of theta (where a test without
    // tolerance is a coin flip), geThetaMinus1 the length the set's test keeps.
    struct CreaseLengths {
        double geTheta = 0.0;
        double gtTheta = 0.0;
        double geThetaMinus1 = 0.0;
        size_t nearTheta = 0;
    };

    CreaseLengths creaseLengths(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles, double thetaDegrees)
    {
        CreaseLengths lengths;
        if (vertices.size() > (size_t)std::numeric_limits<unsigned int>::max())
            return lengths;
        // (edge key, face) pairs, sorted: never more memory than the
        // decimation that follows.
        std::vector<std::pair<uint64_t, unsigned int>> edges;
        edges.reserve(triangles.size() * 3);
        for (size_t i = 0; i < triangles.size(); ++i) {
            const auto& t = triangles[i];
            if (3 != t.size())
                continue;
            for (size_t j = 0; j < 3; ++j) {
                const auto ends = std::minmax(t[j], t[(j + 1) % 3]);
                edges.push_back({ ((uint64_t)ends.first << 32) | ends.second, (unsigned int)i });
            }
        }
        tbb::parallel_sort(edges.begin(), edges.end());
        const auto normal = [&](unsigned int f) {
            const auto& t = triangles[f];
            return Vector3::normal(vertices[t[0]], vertices[t[1]], vertices[t[2]]);
        };
        for (size_t i = 0; i + 1 < edges.size(); ++i) {
            // Only manifold edges: two faces, no third.
            if (edges[i].first != edges[i + 1].first
                || (i > 0 && edges[i - 1].first == edges[i].first)
                || (i + 2 < edges.size() && edges[i + 2].first == edges[i].first))
                continue;
            const Vector3 first = normal(edges[i].second);
            const Vector3 second = normal(edges[i + 1].second);
            if (first.isZero() || second.isZero()) // a zero-area triangle has no dihedral
                continue;
            const double degrees = Vector3::angle(first, second) * (180.0 / M_PI);
            const double length = (vertices[(size_t)(edges[i].first >> 32)]
                - vertices[(size_t)(edges[i].first & 0xffffffffu)])
                                      .length();
            if (degrees >= thetaDegrees)
                lengths.geTheta += length;
            if (degrees > thetaDegrees)
                lengths.gtTheta += length;
            if (degrees >= thetaDegrees - 1.0)
                lengths.geThetaMinus1 += length;
            if (std::fabs(degrees - thetaDegrees) < 1.0)
                ++lengths.nearTheta;
        }
        return lengths;
    }
}

const double AutoRemesher::m_defaultSharpEdgeDegrees = 90;

double AutoRemesher::calculateAverageEdgeLength(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& faces)
{
    double sumOfLength = 0.0;
    size_t edgeCount = 0;
    for (const auto& face : faces) {
        for (size_t i = 0; i < face.size(); ++i) {
            size_t j = (i + 1) % face.size();
            sumOfLength += (vertices[face[i]] - vertices[face[j]]).length();
            ++edgeCount;
        }
    }
    if (0 == edgeCount)
        return 0.0;
    return sumOfLength / edgeCount;
}

void AutoRemesher::initializeVoxelSize()
{
    double area = calculateMeshArea(m_vertices, m_triangles);
    // m_targetTriangleCount is two per quad; T gets m_trianglesPerQuad.
    double triangleArea = area / (0.5 * m_targetTriangleCount * m_trianglesPerQuad);
    m_voxelSize = std::sqrt(triangleArea / triangleAreaPerSquaredEdge);
#if AUTO_REMESHER_DEBUG
    std::cerr << "Area: " << area << " voxelSize: " << m_voxelSize << std::endl;
#endif
}

double AutoRemesher::calculateMeshArea(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles)
{
    double area = 0.0;
    for (const auto& it : triangles) {
        area += Vector3::area(vertices[it[0]], vertices[it[1]], vertices[it[2]]);
    }
    return area;
}

double AutoRemesher::inputCleanupTolerance(const std::vector<Vector3>& vertices)
{
    if (vertices.empty())
        return 0.0;
    Vector3 lowerBound = vertices.front();
    Vector3 upperBound = vertices.front();
    for (const auto& position : vertices) {
        for (size_t i = 0; i < 3; ++i) {
            lowerBound[i] = std::min(lowerBound[i], position[i]);
            upperBound[i] = std::max(upperBound[i], position[i]);
        }
    }
    return 4.0 * FLT_EPSILON * (upperBound - lowerBound).length();
}

// Marching cubes on a binary volume puts vertices exactly on grid corners, so
// its output has zero-length edges between coincident vertices: 21 % of the
// triangles of sculpt_degenerate, 134 on blob_sym.  The cross field and the
// parametrization take such a triangle for a flip (sculpt_degenerate: 500-777
// non-quads, wrong genus), and meshopt refuses every collapse next to one.
// Only edges are collapsed: welding every coincident pair also merged the
// cat's 50 pairs where the surface touches itself, into 100 non-manifold
// edges.  A part whose topology the merge would change (an edge whose ends
// are joined some other way too) is left as it is, whole: per-edge link
// checks are not worth their cost until an input needs them.
AutoRemesher::InputCleanupStats AutoRemesher::cleanInput(const std::vector<Vector3>& vertices,
    std::vector<std::vector<size_t>>& triangles,
    double tolerance)
{
    InputCleanupStats stats;
    const size_t vertexCount = vertices.size();
    std::vector<size_t> parent(vertexCount);
    std::iota(parent.begin(), parent.end(), (size_t)0);
    const auto root = [&](size_t v) {
        while (parent[v] != v)
            v = parent[v] = parent[parent[v]];
        return v;
    };
    const double toleranceSquared = tolerance * tolerance;
    for (const auto& triangle : triangles) {
        for (size_t i = 0; i < 3; ++i) {
            const size_t a = triangle[i], b = triangle[(i + 1) % 3];
            if (a == b || (vertices[a] - vertices[b]).lengthSquared() > toleranceSquared)
                continue;
            const size_t ra = root(a), rb = root(b);
            if (ra != rb) {
                parent[std::max(ra, rb)] = std::min(ra, rb);
                ++stats.mergedVertices;
            }
        }
    }

    const auto countZeroArea = [&]() {
        for (const auto& triangle : triangles) {
            if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[2] == triangle[0])
                continue;
            const Vector3 normal = Vector3::crossProduct(vertices[triangle[1]] - vertices[triangle[0]],
                vertices[triangle[2]] - vertices[triangle[0]]);
            if (0.0 == normal.x() && 0.0 == normal.y() && 0.0 == normal.z())
                ++stats.zeroAreaTriangles;
        }
    };
    if (0 == stats.mergedVertices) {
        countZeroArea();
        return stats;
    }

    // Connected parts of the input, by shared corners.
    std::vector<size_t> part(vertexCount);
    std::iota(part.begin(), part.end(), (size_t)0);
    const auto partOf = [&](size_t v) {
        while (part[v] != v)
            v = part[v] = part[part[v]];
        return v;
    };
    for (const auto& triangle : triangles) {
        for (size_t i = 1; i < 3; ++i) {
            const size_t a = partOf(triangle[0]), b = partOf(triangle[i]);
            if (a != b)
                part[std::max(a, b)] = std::min(a, b);
        }
    }

    // Each part that has a collapsed edge, before and after.  A merge stays
    // inside one part, since the merged ends share a triangle.
    std::unordered_map<size_t, std::pair<std::vector<std::vector<size_t>>, std::vector<std::vector<size_t>>>> changedParts;
    for (size_t v = 0; v < vertexCount; ++v) {
        if (root(v) != v)
            changedParts[partOf(v)];
    }
    for (const auto& triangle : triangles) {
        auto found = changedParts.find(partOf(triangle[0]));
        if (found == changedParts.end())
            continue;
        found->second.first.push_back(triangle);
        const size_t a = root(triangle[0]), b = root(triangle[1]), c = root(triangle[2]);
        if (a != b && b != c && c != a)
            found->second.second.push_back({ a, b, c });
    }
    std::unordered_map<size_t, bool> partKept;
    for (const auto& it : changedParts) {
        const bool keeps = keepsTopology(surfaceTopology(it.second.first, vertexCount),
            surfaceTopology(it.second.second, vertexCount));
        partKept[it.first] = !keeps;
        if (!keeps)
            ++stats.keptParts;
    }

    stats.mergedVertices = 0;
    for (size_t v = 0; v < vertexCount; ++v) {
        if (root(v) != v && !partKept[partOf(v)])
            ++stats.mergedVertices;
    }
    size_t kept = 0;
    for (size_t t = 0; t < triangles.size(); ++t) {
        std::vector<size_t> triangle = std::move(triangles[t]);
        const auto foundPart = partKept.find(partOf(triangle[0]));
        if (foundPart != partKept.end() && !foundPart->second) {
            for (auto& corner : triangle)
                corner = root(corner);
            if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[2] == triangle[0]) {
                ++stats.droppedTriangles;
                continue;
            }
        }
        triangles[kept++] = std::move(triangle);
    }
    triangles.resize(kept);
    countZeroArea();
    return stats;
}

void AutoRemesher::orientInput(const std::vector<Vector3>& vertices,
    std::vector<std::vector<size_t>>& triangles,
    InputCleanupStats* stats)
{
    // Each triangle side as (low, high, triangle, forward), sorted, so the
    // two sides of a manifold edge sit next to each other.
    struct Side {
        size_t low;
        size_t high;
        size_t triangle;
        bool forward;
        bool operator<(const Side& other) const
        {
            return low != other.low ? low < other.low : (high != other.high ? high < other.high : triangle < other.triangle);
        }
    };
    std::vector<Side> sides;
    sides.reserve(triangles.size() * 3);
    for (size_t t = 0; t < triangles.size(); ++t) {
        for (size_t i = 0; i < 3; ++i) {
            const size_t a = triangles[t][i], b = triangles[t][(i + 1) % 3];
            if (a != b)
                sides.push_back({ std::min(a, b), std::max(a, b), t, a < b });
        }
    }
    tbb::parallel_sort(sides.begin(), sides.end());

    // Neighbours across manifold edges, and whether the two walk the edge
    // the same way (then one of them is turned against the other).
    std::vector<std::vector<std::pair<size_t, bool>>> neighbours(triangles.size());
    bool anyMismatch = false;
    for (size_t i = 0; i < sides.size();) {
        size_t j = i + 1;
        while (j < sides.size() && sides[j].low == sides[i].low && sides[j].high == sides[i].high)
            ++j;
        if (2 == j - i && sides[i].triangle != sides[i + 1].triangle) {
            const bool mismatch = sides[i].forward == sides[i + 1].forward;
            anyMismatch = anyMismatch || mismatch;
            neighbours[sides[i].triangle].push_back({ sides[i + 1].triangle, mismatch });
            neighbours[sides[i + 1].triangle].push_back({ sides[i].triangle, mismatch });
        }
        i = j;
    }
    if (!anyMismatch)
        return;

    // Breadth first over each piece: flip[t] says whether t disagrees with
    // the piece's first triangle.
    std::vector<int8_t> flip(triangles.size(), -1);
    std::vector<size_t> piece;
    for (size_t seed = 0; seed < triangles.size(); ++seed) {
        if (-1 != flip[seed])
            continue;
        piece.clear();
        piece.push_back(seed);
        flip[seed] = 0;
        bool orientable = true;
        double area[2] = { 0.0, 0.0 };
        for (size_t k = 0; k < piece.size(); ++k) {
            const size_t t = piece[k];
            area[flip[t]] += Vector3::area(vertices[triangles[t][0]], vertices[triangles[t][1]], vertices[triangles[t][2]]);
            for (const auto& neighbour : neighbours[t]) {
                const int8_t wanted = (int8_t)(flip[t] ^ (neighbour.second ? 1 : 0));
                if (-1 == flip[neighbour.first]) {
                    flip[neighbour.first] = wanted;
                    piece.push_back(neighbour.first);
                } else if (flip[neighbour.first] != wanted) {
                    orientable = false;
                }
            }
        }
        if (!orientable) {
            ++stats->nonOrientablePieces;
            continue;
        }
        // Ties keep the first triangle's side, so the result does not hang
        // on rounding of equal areas.
        const int8_t turned = area[1] > area[0] ? 0 : 1;
        for (const size_t t : piece) {
            if (turned == flip[t]) {
                std::swap(triangles[t][1], triangles[t][2]);
                ++stats->flippedTriangles;
            }
        }
    }
}

bool AutoRemesher::decimateIfTooDense(std::vector<Vector3>& vertices,
    std::vector<std::vector<size_t>>& triangles,
    const SurfaceTopology& topology,
    double voxelSize,
    double sharpEdgeDegrees,
    FeatureEdgeSet& featureEdges,
    size_t* droppedFeatureEdges,
    size_t islandIndex,
    DecimationStats* stats)
{
    if (nullptr != stats)
        ++stats->islandsConsidered;

    if (vertices.empty() || triangles.empty() || voxelSize <= 0.0)
        return false;

    if (vertices.size() > (size_t)std::numeric_limits<unsigned int>::max())
        return false;

    const double targetTriangleArea = voxelSize * voxelSize * triangleAreaPerSquaredEdge;
    if (targetTriangleArea <= 0.0)
        return false;
    const double islandTargetTriangleCount = calculateMeshArea(vertices, triangles) / targetTriangleArea;
    if (islandTargetTriangleCount < 1.0)
        return false;

    if ((double)triangles.size() < islandTargetTriangleCount * decimateTriggerRatio)
        return false;

    const size_t decimateTriangleCount = (size_t)(islandTargetTriangleCount * decimateTargetRatio);

    std::vector<unsigned int> indices;
    indices.reserve(triangles.size() * 3);
    for (const auto& triangle : triangles) {
        if (3 != triangle.size())
            return false;
        for (size_t i = 0; i < 3; ++i)
            indices.push_back((unsigned int)triangle[i]);
    }

    Vector3 lowerBound = vertices.front();
    Vector3 upperBound = vertices.front();
    for (const auto& position : vertices) {
        for (size_t i = 0; i < 3; ++i) {
            lowerBound[i] = std::min(lowerBound[i], position[i]);
            upperBound[i] = std::max(upperBound[i], position[i]);
        }
    }
    const Vector3 center = (lowerBound + upperBound) * 0.5;

    std::vector<float> positions;
    positions.reserve(vertices.size() * 3);
    for (const auto& position : vertices) {
        positions.push_back((float)(position.x() - center.x()));
        positions.push_back((float)(position.y() - center.y()));
        positions.push_back((float)(position.z() - center.z()));
    }

    std::vector<unsigned char> vertexLock;
    if (!featureEdges.empty())
        vertexLock = featureVertexLock(featureEdges, vertices.size());

    // T must keep the input's topology: a non-manifold edge in it becomes a
    // hole or a wrong genus in the output.  Plain meshopt merges vertices by
    // position (on the cat 50 coincident pairs made 100 non-manifold edges)
    // and checks collapses for flipped triangles but not for the link
    // condition, so on a thin shell or a noisy scan a collapse could join two
    // sheets into a non-manifold edge, which the isotropic remesher then
    // opened into a hole.  meshopt_SimplifyKeepTopology, our addition to the
    // vendored copy (thirdparty/meshoptimizer/README.md), does neither.
    std::vector<unsigned int> decimated(indices.size());
    float resultError = 0.0f;
    decimated.resize(meshopt_simplifyWithAttributes(decimated.data(),
        indices.data(), indices.size(),
        positions.data(), vertices.size(), sizeof(float) * 3,
        nullptr, 0, nullptr, 0,
        vertexLock.empty() ? nullptr : vertexLock.data(),
        decimateTriangleCount * 3, FLT_MAX,
        meshopt_SimplifyRegularize | meshopt_SimplifyKeepTopology, &resultError));

    if (decimated.size() < 3 || decimated.size() >= indices.size())
        return false;

    // The link condition keeps the topology of a manifold input.  If it
    // changed all the same (a non-manifold input), the island goes to the
    // isotropic remesher as it is: slower, but T keeps the input's topology.
    if (!keepsTopology(topology, surfaceTopology(FlatTriangles { decimated }, vertices.size()))) {
        if (nullptr != stats)
            ++stats->islandsKeptUndecimated;
        return false;
    }

    std::vector<size_t> outputIndexOfInput(vertices.size(), std::numeric_limits<size_t>::max());
    std::vector<Vector3> decimatedVertices;
    std::vector<std::vector<size_t>> decimatedTriangles;
    decimatedTriangles.reserve(decimated.size() / 3);
    for (size_t i = 0; i + 2 < decimated.size(); i += 3) {
        std::vector<size_t> triangle(3);
        for (size_t j = 0; j < 3; ++j) {
            const unsigned int inputIndex = decimated[i + j];
            if (std::numeric_limits<size_t>::max() == outputIndexOfInput[inputIndex]) {
                outputIndexOfInput[inputIndex] = decimatedVertices.size();
                decimatedVertices.push_back(vertices[inputIndex]);
            }
            triangle[j] = outputIndexOfInput[inputIndex];
        }
        decimatedTriangles.push_back(triangle);
    }

    if (nullptr != stats) {
        ++stats->islandsDecimated;
        stats->trianglesBefore += triangles.size();
        stats->trianglesAfter += decimatedTriangles.size();
    }

#if AUTO_REMESHER_DEBUG
    std::cerr << "Island[" << islandIndex << "]: Decimated " << triangles.size()
              << " triangles to " << decimatedTriangles.size()
              << " (target " << decimateTriangleCount
              << ", island target " << (size_t)islandTargetTriangleCount
              << "), normalized error: " << resultError << std::endl;
#else
    (void)islandIndex;
    (void)resultError;
#endif

    featureEdges = remapFeatureEdges(featureEdges, outputIndexOfInput,
        decimatedVertices, decimatedTriangles, sharpEdgeDegrees, droppedFeatureEdges);
    vertices = std::move(decimatedVertices);
    triangles = std::move(decimatedTriangles);
    return true;
}

void AutoRemesher::resample(std::vector<Vector3>& vertices,
    std::vector<std::vector<size_t>>& triangles,
    const SurfaceTopology& topology,
    double voxelSize,
    double adaptivity,
    double sharpEdgeDegrees,
    FeatureEdgeSet& featureEdges,
    SharpEdgeStats* sharpEdgeStats,
    double smoothNormalDegrees,
    size_t islandIndex,
    DecimationStats* decimationStats,
    std::atomic<long long>* adaptiveFieldTimeUs,
    const ProgressHandler* progressHandler,
    std::vector<Vector3>* decimatedVerticesOut,
    std::vector<std::vector<size_t>>* decimatedTrianglesOut)
{
    auto t_decimateStart = std::chrono::high_resolution_clock::now();
    decimateIfTooDense(vertices, triangles, topology, voxelSize, sharpEdgeDegrees, featureEdges,
        nullptr != sharpEdgeStats ? &sharpEdgeStats->decimationDropped : nullptr, islandIndex, decimationStats);
    if (nullptr != sharpEdgeStats)
        sharpEdgeStats->decimatedEdges = featureEdges.size();
    if (nullptr != decimationStats) {
        decimationStats->timeUs += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - t_decimateStart)
                                       .count();
    }

    if (nullptr != decimatedVerticesOut)
        *decimatedVerticesOut = vertices;
    if (nullptr != decimatedTrianglesOut)
        *decimatedTrianglesOut = triangles;

    auto t_fieldStart = std::chrono::high_resolution_clock::now();
    std::vector<double> vertexTargetLengths;
    if (adaptivity > 0.0 && !vertices.empty()) {
        // A target-length field redistributes the uniform triangle budget.  The
        // field is deliberately computed on the input mesh: IsotropicRemesher
        // propagates it to vertices created by edge splits.
        const double minRatio = 0.35;
        const double maxRatio = 3.0;
        const double epsilon = 1e-12;

        // Do not add face normals directly from parallel workers: adjacent
        // faces write to the same vertex.  Compute faces in parallel, then do
        // the small accumulation pass serially.
        std::vector<Vector3> faceNormals(triangles.size());
        std::vector<double> faceAreas(triangles.size(), 0.0);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, triangles.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i != range.end(); ++i) {
                    const auto& tri = triangles[i];
                    faceAreas[i] = Vector3::area(
                        vertices[tri[0]], vertices[tri[1]], vertices[tri[2]]);
                    if (faceAreas[i] > epsilon)
                        faceNormals[i] = Vector3::normal(
                            vertices[tri[0]], vertices[tri[1]], vertices[tri[2]]);
                }
            });

        std::vector<Vector3> normals(vertices.size());
        std::vector<std::vector<size_t>> neighbors(vertices.size());
        for (size_t i = 0; i < triangles.size(); ++i) {
            const auto& tri = triangles[i];
            if (faceAreas[i] <= epsilon)
                continue;
            const Vector3 weightedNormal = faceNormals[i] * faceAreas[i];
            for (size_t j = 0; j < 3; ++j) {
                normals[tri[j]] += weightedNormal;
                neighbors[tri[j]].push_back(tri[(j + 1) % 3]);
                neighbors[tri[j]].push_back(tri[(j + 2) % 3]);
            }
        }
        tbb::parallel_for(tbb::blocked_range<size_t>(0, normals.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i != range.end(); ++i) {
                    normals[i].normalize();
                    auto& ring = neighbors[i];
                    std::sort(ring.begin(), ring.end());
                    ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
                }
            });

        // Mean normal variation per unit length is less sensitive to a single
        // bad triangle than the previous maximum-one-ring estimate.
        std::vector<double> vertexCurvature(vertices.size(), 0.0);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, vertices.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t v = range.begin(); v != range.end(); ++v) {
                    const auto& ring = neighbors[v];
                    if (ring.empty() || normals[v].lengthSquared() <= epsilon)
                        continue;
                    double weightedCurvature = 0.0;
                    double totalWeight = 0.0;
                    for (const auto& u : ring) {
                        const double length = (vertices[u] - vertices[v]).length();
                        if (length <= epsilon || normals[u].lengthSquared() <= epsilon)
                            continue;
                        double cosine = Vector3::dotProduct(normals[v], normals[u]);
                        cosine = std::max(-1.0, std::min(1.0, cosine));
                        weightedCurvature += std::acos(cosine);
                        totalWeight += length;
                    }
                    if (totalWeight > epsilon)
                        vertexCurvature[v] = weightedCurvature / totalWeight;
                }
            });

        // A percentile reference prevents a few very sharp/noisy vertices from
        // making the rest of the surface appear flat.
        std::vector<double> nonZeroCurvatures;
        nonZeroCurvatures.reserve(vertexCurvature.size());
        for (double curvature : vertexCurvature) {
            if (curvature > epsilon)
                nonZeroCurvatures.push_back(curvature);
        }
        if (!nonZeroCurvatures.empty()) {
            const size_t referenceIndex = (nonZeroCurvatures.size() - 1) * 3 / 4;
            std::nth_element(nonZeroCurvatures.begin(),
                nonZeroCurvatures.begin() + referenceIndex, nonZeroCurvatures.end());
            const double curvatureReference = nonZeroCurvatures[referenceIndex];
            vertexTargetLengths.resize(vertices.size());
            std::vector<double> importance(vertices.size(), 1.0);
            const double strength = std::min(adaptivity, 2.0) * 7.0;
            tbb::parallel_for(tbb::blocked_range<size_t>(0, vertices.size()),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t v = range.begin(); v != range.end(); ++v) {
                        const double normalized = std::min(4.0,
                            vertexCurvature[v] / std::max(curvatureReference, epsilon));
                        importance[v] += strength * normalized * normalized;
                    }
                });

            // Keep integral(area / h^2) equal to the uniform field, which
            // preserves the budget implied by voxelSize while moving triangles
            // from flat regions to detailed ones.
            double totalArea = 0.0;
            double weightedImportance = 0.0;
            for (size_t i = 0; i < triangles.size(); ++i) {
                const auto& tri = triangles[i];
                totalArea += faceAreas[i];
                weightedImportance += faceAreas[i] * (importance[tri[0]] + importance[tri[1]] + importance[tri[2]]) / 3.0;
            }
            if (totalArea > epsilon) {
                const double averageImportance = weightedImportance / totalArea;
                for (size_t v = 0; v < vertices.size(); ++v) {
                    double multiplier = std::sqrt(averageImportance / importance[v]);
                    multiplier = std::max(minRatio, std::min(maxRatio, multiplier));
                    vertexTargetLengths[v] = voxelSize * multiplier;
                }
            } else {
                vertexTargetLengths.clear();
            }
        }
    }
    if (nullptr != adaptiveFieldTimeUs) {
        *adaptiveFieldTimeUs += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - t_fieldStart)
                                    .count();
    }

#if AUTO_REMESHER_DEBUG
    std::cerr << "Island[" << islandIndex << "]: Uniformly remeshing on target edge length: " << voxelSize << std::endl;
#endif
    IsotropicRemesher isotropicRemesher(vertices, triangles);
    if (nullptr != progressHandler && *progressHandler)
        isotropicRemesher.setProgressHandler(*progressHandler);
    isotropicRemesher.setTargetEdgeLength(voxelSize);
    if (!vertexTargetLengths.empty())
        isotropicRemesher.setVertexTargetEdgeLengths(&vertexTargetLengths);
    isotropicRemesher.setFeatureEdges(featureEdges);
    isotropicRemesher.setSmoothNormalDegrees(smoothNormalDegrees);
    isotropicRemesher.remesh();
    vertices = isotropicRemesher.remeshedVertices();
    triangles = isotropicRemesher.remeshedTriangles();
    featureEdges = reclassifyFeatureEdges(isotropicRemesher.featureEdges(), vertices, triangles,
        sharpEdgeDegrees, nullptr != sharpEdgeStats ? &sharpEdgeStats->classified : nullptr);
    if (nullptr != sharpEdgeStats) {
        sharpEdgeStats->carried = isotropicRemesher.featureEdges().size();
        sharpEdgeStats->flipped = isotropicRemesher.clearedFeatureEdges();
    }
#if AUTO_REMESHER_DEBUG
    std::cerr << "Island[" << islandIndex << "]: Uniformly remesh done, vertex count: " << vertices.size() << " triangle count: " << triangles.size() << std::endl;
#endif
}

void AutoRemesher::updateProgress(size_t threadIndex, float progress, const char* status)
{
    if (nullptr == m_progressHandler)
        return;

    std::lock_guard<std::mutex> lock(m_progressMutex);
    if (threadIndex >= m_threadProgress.size())
        return;
    if (nullptr != status && '\0' != status[0])
        m_threadStatus[threadIndex] = status;
    if (progress > m_threadProgress[threadIndex]) {
        m_progressSum += (double)(progress - m_threadProgress[threadIndex])
            * m_threadProgressWeights[threadIndex];
        m_threadProgress[threadIndex] = progress;
    }

    const double overall = parallelPhaseBegin
        + (parallelPhaseEnd - parallelPhaseBegin) * std::min(1.0, std::max(0.0, m_progressSum));

    // Steps now report many times per island, so only wake the UI when the bar
    // would actually move or the status line would change.
    const int permille = (int)(overall * 1000.0);
    const char* islandStatus = m_threadStatus[threadIndex];
    if (permille == m_reportedPermille && islandStatus == m_reportedStatus)
        return;
    m_reportedPermille = permille;
    m_reportedStatus = islandStatus;

    // With several islands in flight, the run as a whole is only as far along as
    // its slowest island, so that is the step worth naming.
    size_t slowest = threadIndex;
    for (size_t i = 0; i < m_threadProgress.size(); ++i) {
        if (m_threadProgress[i] < m_threadProgress[slowest])
            slowest = i;
    }
    const char* name = m_threadStatus[slowest];
    m_progressHandler(m_tag, (float)overall, nullptr != name ? name : "");
}

void AutoRemesher::throwIfCancelled() const
{
    if (m_cancelCheck && m_cancelCheck())
        throw RemeshCancelled();
}

ProgressHandler AutoRemesher::makeStageProgress(size_t islandIndex, float begin, float end, float stageOrder)
{
    return [this, islandIndex, begin, end, stageOrder,
               lastTime = std::chrono::high_resolution_clock::now(),
               lastName = (const char*)nullptr,
               lastOrder = 0.0f](float fraction, const char* name) mutable {
        // Every stage reports between its steps, so this is where a running
        // island notices a cancellation.  The stages only hold memory through
        // RAII here, which the unwinding frees.
        throwIfCancelled();
        const auto now = std::chrono::high_resolution_clock::now();
        if (nullptr != lastName) {
            accumulateStageTime(lastName, lastOrder,
                std::chrono::duration_cast<std::chrono::microseconds>(now - lastTime).count());
        }
        lastTime = now;
        lastName = name;
        lastOrder = stageOrder + fraction;
        updateProgress(islandIndex, begin + (end - begin) * fraction, name);
    };
}

void AutoRemesher::accumulateStageTime(const char* name, float order, long long microseconds)
{
    if (nullptr == name || '\0' == name[0])
        return;
    std::lock_guard<std::mutex> lock(m_stageTimingMutex);
    for (auto& it : m_stageTimes) {
        if (it.name == name) {
            it.microseconds += microseconds;
            return;
        }
    }
    m_stageTimes.push_back({ name, order, microseconds });
}

bool AutoRemesher::remesh()
{
    m_cancelled = false;
    m_diagnostics.clear();
    try {
        return remeshIslands();
    } catch (const RemeshCancelled&) {
        m_cancelled = true;
    } catch (...) {
        // TBB without exact exception propagation rethrows a cancellation from
        // a worker thread as a different type, so go by the check instead.
        if (!(m_cancelCheck && m_cancelCheck()))
            throw;
        m_cancelled = true;
    }
    std::cerr << "Remesh cancelled" << std::endl;
    return false;
}

bool AutoRemesher::remeshIslands()
{
    // Validate inputs before any sizing math. In particular a zero target
    // triangle count would divide by zero in initializeVoxelSize().
    const char* invalidInputReason = nullptr;
    if (m_vertices.empty())
        invalidInputReason = "input mesh has no vertices";
    else if (m_triangles.empty())
        invalidInputReason = "input mesh has no triangles";
    else if (0 == m_targetTriangleCount)
        invalidInputReason = "target triangle count must be greater than zero";
    else if (!(m_trianglesPerQuad > 0.0) || !std::isfinite(m_trianglesPerQuad))
        invalidInputReason = "triangles per quad must be a positive number";
    if (nullptr != invalidInputReason) {
        std::cerr << "Invalid remesh input: " << invalidInputReason << std::endl;
        if (nullptr != m_progressHandler)
            m_progressHandler(m_tag, 1.0, invalidInputReason);
        return false;
    }
    throwIfCancelled();
    auto t_start = std::chrono::high_resolution_clock::now();
    const double cpuStart = processCpuSeconds();

    InputCleanupStats cleanup = cleanInput(m_vertices, m_triangles, inputCleanupTolerance(m_vertices));
    orientInput(m_vertices, m_triangles, &cleanup);
    if (m_triangles.empty()) {
        std::cerr << "Input mesh has only degenerate triangles" << std::endl;
        if (nullptr != m_progressHandler)
            m_progressHandler(m_tag, 1.0, "Input mesh has only degenerate triangles");
        return false;
    }

    // After orientation: the fold test is a sign test.  Moves vertices only.
    // Always detected, so the report can say where the input folds; only
    // repaired when asked for.
    if (nullptr != m_progressHandler)
        m_progressHandler(m_tag, 0.0f, m_repairInputFolds ? "Repairing folded input" : "Checking for folded input");
    FoldRepair::Options foldOptions;
    foldOptions.flattenSpikes = m_flattenSpikes;
    const FoldRepair::Stats folds = m_repairInputFolds
        ? FoldRepair::repair(&m_vertices, m_triangles, foldOptions)
        : FoldRepair::survey(m_vertices, m_triangles, foldOptions);
    // The spikes' largest drop in quad edges: sqrt(area per target quad).
    double spikeDropQuads = 0.0;
    if (folds.flattenedSpikes > 0 && m_targetTriangleCount > 0) {
        double area = 0.0;
        for (const std::vector<size_t>& t : m_triangles)
            area += 0.5 * Vector3::crossProduct(m_vertices[t[1]] - m_vertices[t[0]], m_vertices[t[2]] - m_vertices[t[0]]).length();
        const double quadEdge = std::sqrt(area / (0.5 * m_targetTriangleCount));
        if (quadEdge > 0.0)
            spikeDropQuads = folds.maximumSpikeDrop / quadEdge;
    }
    throwIfCancelled();

    // Each label names the step that is about to run, not the one that just
    // finished, so the status line matches what the process is actually doing.
    if (nullptr != m_progressHandler)
        m_progressHandler(m_tag, 0.0f, "Computing voxel size");
    auto t_voxelStart = std::chrono::high_resolution_clock::now();
    initializeVoxelSize();
    auto t_voxelEnd = std::chrono::high_resolution_clock::now();

    if (nullptr != m_progressHandler)
        m_progressHandler(m_tag, 0.01f, "Splitting mesh into islands");
    std::vector<std::vector<std::vector<size_t>>> trianglesIslands;
    auto t_splitStart = std::chrono::high_resolution_clock::now();
    MeshSeparator::splitToIslands(m_triangles, trianglesIslands);
    auto t_afterSplit = std::chrono::high_resolution_clock::now();

    if (trianglesIslands.empty()) {
        std::cerr << "Input mesh is empty" << std::endl;
        if (nullptr != m_progressHandler)
            m_progressHandler(m_tag, 1.0, "Input mesh is empty");
        return false;
    }

#if AUTO_REMESHER_DEBUG
    std::cerr << "Split to islands: " << trianglesIslands.size() << std::endl;
#endif

    struct IslandContext {
        std::vector<Vector3> vertices;
        std::vector<std::vector<size_t>> triangles;
        // Taken before resampling replaces vertices and triangles.
        QuadExtractor::Boundary inputBoundary;
        SurfaceTopology inputTopology;
        long long inputTwiceHandles = -1;
        double voxelSize;
        double area;
        double targetQuadCount;
        double scaling;
        double adaptivity;
        double anisotropy;
        double sharpEdgeDegrees;
        double smoothNormalDegrees;
        // The island's sharp edges, in its own vertex numbering: classified
        // once here, on the input, and carried to T by resampleIsland.
        FeatureEdgeSet featureEdges;
        // Input vertex of each island vertex.
        std::vector<size_t> inputVertices;
    };

    // Copies one island out of the input, with its own vertex numbering.
    const auto buildIslandContext = [&](size_t islandIndex, IslandContext& context) {
        const auto& island = trianglesIslands[islandIndex];
        context.vertices.clear();
        context.inputVertices.clear();
        context.triangles.clear();
        context.triangles.reserve(island.size());
        std::unordered_map<size_t, size_t> oldToNewVertexMap;
        oldToNewVertexMap.reserve(island.size() * 2);
        for (const auto& face : island) {
            std::vector<size_t> triangle;
            triangle.reserve(3);
            for (size_t i = 0; i < 3; ++i) {
                auto insertResult = oldToNewVertexMap.insert({ face[i], context.vertices.size() });
                if (insertResult.second) {
                    context.vertices.push_back(m_vertices[face[i]]);
                    context.inputVertices.push_back(face[i]);
                }
                triangle.push_back(insertResult.first->second);
            }
            context.triangles.push_back(std::move(triangle));
        }
        context.area = calculateMeshArea(context.vertices, context.triangles);
        context.inputTopology = surfaceTopology(context.triangles, context.vertices.size());
        context.inputTwiceHandles = 0 == context.inputTopology.nonManifoldEdges
            ? twiceHandleCount(context.vertices.size(), context.triangles)
            : -1;
        context.inputBoundary = QuadExtractor::findBoundary(context.vertices, context.triangles);

        context.scaling = m_scaling;
        context.voxelSize = m_voxelSize;
        context.adaptivity = m_adaptivity;
        context.anisotropy = m_anisotropy;
        context.sharpEdgeDegrees = m_sharpEdgeDegrees;
        context.smoothNormalDegrees = m_smoothNormalDegrees;
        context.featureEdges = classifySharpEdges(context.vertices, context.triangles, context.sharpEdgeDegrees);
    };

    if (nullptr != m_progressHandler)
        m_progressHandler(m_tag, 0.02f, "Building island contexts");
    // Islands are compacted independently of each other, and writing into a
    // pre-sized vector by index keeps them in the original order.
    std::vector<IslandContext> islandContexes(trianglesIslands.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, trianglesIslands.size()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t islandIndex = range.begin(); islandIndex != range.end(); ++islandIndex)
                buildIslandContext(islandIndex, islandContexes[islandIndex]);
        });
    // Callers ask for two triangles per quad. Each island is asked for its
    // input area's share of the quads, rather than for a density over its
    // remeshed area, so the counts add up to the request even where
    // remeshing smooths noise away or decimation loses area. A small island
    // is raised to minimumIslandTriangleCount, which also shortens its edges.
    double totalIslandArea = 0.0;
    for (const auto& context : islandContexes)
        totalIslandArea += context.area;
    for (auto& context : islandContexes) {
        const double shareTriangleCount = totalIslandArea > 0.0
            ? m_targetTriangleCount * context.area / totalIslandArea
            : 0.0;
        const double floorTriangleCount = std::min(minimumIslandTriangleCount, (double)context.triangles.size());
        if (shareTriangleCount >= floorTriangleCount || context.area <= 0.0) {
            context.targetQuadCount = 0.5 * shareTriangleCount;
            continue;
        }
        // T keeps the resolution's triangles per quad, as on every other island.
        context.targetQuadCount = 0.5 * floorTriangleCount;
        context.voxelSize = std::sqrt(context.area
            / (context.targetQuadCount * m_trianglesPerQuad * triangleAreaPerSquaredEdge));
    }
    auto t_buildEnd = std::chrono::high_resolution_clock::now();
    throwIfCancelled();
    if (nullptr != m_progressHandler)
        m_progressHandler(m_tag, parallelPhaseBegin, "Remeshing uniformly");

    std::atomic<long long> resampleTime(0);
    std::atomic<long long> adaptiveFieldTime(0);
    std::atomic<long long> parameterizeTimeAccumulated(0);
    std::atomic<long long> extractTimeAccumulated(0);
    DecimationStats decimationStats;
    DecimationStats retryDecimationStats;

    m_threadProgressWeights.assign(islandContexes.size(), 1.0f);
    for (size_t i = 0; i < islandContexes.size(); ++i) {
        if (!m_triangles.empty())
            m_threadProgressWeights[i] = (float)(((double)islandContexes[i].triangles.size() / m_triangles.size()));
    }
    m_threadProgress.assign(islandContexes.size(), 0.0f);
    m_threadStatus.assign(islandContexes.size(), nullptr);
    m_progressSum = 0.0;

    // Everything one island's run produces: its quads, and the intermediate
    // meshes and maps the Qt app previews.
    struct IslandRun {
        std::vector<Vector3> decimatedVertices;
        std::vector<std::vector<size_t>> decimatedTriangles;
        std::vector<Vector3> isotropicVertices;
        std::vector<std::vector<size_t>> isotropicTriangles;
        std::vector<std::vector<Vector2>> capturedUvs;
        std::vector<std::vector<Vector2>> capturedOriginalUvs;
        std::vector<uint8_t> capturedExtractedConnectionMoved;
        std::vector<Vector3> capturedSingularVertices;
        std::vector<size_t> capturedSingularVertexIndices;
        std::vector<std::pair<Vector3, Vector3>> capturedExtractedConnections;
        std::vector<Vector3> remeshedVertices;
        std::vector<std::vector<size_t>> remeshedQuads;
        std::vector<DiagnosticRecord> diagnostics;
        DiagnosticRecord features; // crease length per stage, set by resampleIsland
        // Of the island after decimation and after isotropic remeshing.
        SurfaceTopology decimatedTopology;
        SurfaceTopology isotropicTopology;
        // T's sharp edges (FeatureEdgeSet).
        FeatureEdgeSet isotropicFeatureEdges;
        bool parameterized = false;
        bool extracted = false;
        long long resampleUs = 0;
        long long parameterizeUs = 0;
        long long extractUs = 0;
    };
    std::vector<IslandRun> islandRuns(islandContexes.size());

    // Decimates and isotropically remeshes the island in place.
    const auto resampleIsland = [&](size_t islandIndex, IslandContext& ctx, IslandRun& run,
                                    DecimationStats* stats) {
        throwIfCancelled();
        updateProgress(islandIndex, 0.0f, "Remeshing uniformly");
        const ProgressHandler isotropicProgress = makeStageProgress(islandIndex,
            0.0f, islandResampleEnd, -1.0f);

        const CreaseLengths inputCreases = creaseLengths(ctx.vertices, ctx.triangles, ctx.sharpEdgeDegrees);
        auto t0 = std::chrono::high_resolution_clock::now();
        run.isotropicFeatureEdges = ctx.featureEdges;
        SharpEdgeStats sharpEdgeStats;
        resample(ctx.vertices, ctx.triangles, ctx.inputTopology, ctx.voxelSize, ctx.adaptivity,
            ctx.sharpEdgeDegrees, run.isotropicFeatureEdges, &sharpEdgeStats, ctx.smoothNormalDegrees, islandIndex, stats,
            &adaptiveFieldTime, &isotropicProgress,
            &run.decimatedVertices, &run.decimatedTriangles);
        auto t1 = std::chrono::high_resolution_clock::now();
        run.resampleUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        resampleTime += run.resampleUs;

        run.isotropicVertices = ctx.vertices;
        run.isotropicTriangles = ctx.triangles;
        run.decimatedTopology = surfaceTopology(run.decimatedTriangles, run.decimatedVertices.size());
        run.isotropicTopology = surfaceTopology(run.isotropicTriangles, run.isotropicVertices.size());

        // Crease length (relative to the target edge) that each stage still has.
        const double h = ctx.voxelSize > 0.0 ? ctx.voxelSize : 1.0;
        const CreaseLengths decimatedCreases = creaseLengths(run.decimatedVertices, run.decimatedTriangles, ctx.sharpEdgeDegrees);
        const CreaseLengths isotropicCreases = creaseLengths(run.isotropicVertices, run.isotropicTriangles, ctx.sharpEdgeDegrees);
        run.features = DiagnosticRecord("features");
        run.features.set("sharp_edge_degrees", ctx.sharpEdgeDegrees);
        run.features.set("input_ge_theta_rel_h", inputCreases.geTheta / h);
        run.features.set("input_gt_theta_rel_h", inputCreases.gtTheta / h);
        run.features.set("input_ge_theta_minus1_rel_h", inputCreases.geThetaMinus1 / h);
        run.features.set("input_near_theta_edges", (double)inputCreases.nearTheta);
        run.features.set("decimated_ge_theta_rel_h", decimatedCreases.geTheta / h);
        run.features.set("isotropic_ge_theta_rel_h", isotropicCreases.geTheta / h);
        run.features.set("isotropic_gt_theta_rel_h", isotropicCreases.gtTheta / h);
        run.features.set("isotropic_ge_theta_minus1_rel_h", isotropicCreases.geThetaMinus1 / h);
        run.features.set("isotropic_near_theta_edges", (double)isotropicCreases.nearTheta);
        // The island's FeatureEdgeSet (TODO 2.6): on the input, after the
        // decimation, what the remesh carried, and T's own set, which the
        // field, the singularity freeze and the constraints use.
        run.features.set("set_input_edges", (double)ctx.featureEdges.size());
        run.features.set("set_decimated_edges", (double)sharpEdgeStats.decimatedEdges);
        run.features.set("set_decimation_dropped", (double)sharpEdgeStats.decimationDropped);
        run.features.set("set_flipped", (double)sharpEdgeStats.flipped);
        run.features.set("set_t_carried", (double)sharpEdgeStats.carried);
        run.features.set("set_t_classified", (double)sharpEdgeStats.classified);
        run.features.set("set_t_edges", (double)run.isotropicFeatureEdges.size());
        run.features.set("set_t_length_rel_h", featureEdgeLength(run.isotropicFeatureEdges, ctx.vertices) / h);

        updateProgress(islandIndex, islandResampleEnd);
    };

    // Parameterizes the remeshed island and extracts its quads.
    const auto parameterizeIsland = [&](size_t islandIndex, const IslandContext& ctx, IslandRun& run) {
        throwIfCancelled();
        auto t0 = std::chrono::high_resolution_clock::now();

        const auto& vertices = ctx.vertices;
        const auto& triangles = ctx.triangles;

        if (vertices.empty() || triangles.empty()) {
            // Still retire the island, otherwise its share of the bar
            // is never filled in and the total stalls short of the end.
            updateProgress(islandIndex, 1.0f);
            return;
        }

        updateProgress(islandIndex, islandResampleEnd);
        Parameterizer parameterizer(&vertices, &triangles, nullptr);
        parameterizer.setProgressHandler(
            makeStageProgress(islandIndex, islandResampleEnd, islandParameterizeEnd, 0.0f));
        if (ctx.scaling > 0.0)
            parameterizer.setScaling(ctx.scaling);
        parameterizer.setTargetQuadCount(ctx.targetQuadCount);
        parameterizer.setGradientAdaptivity(ctx.adaptivity);
        parameterizer.setAnisotropy(ctx.anisotropy);
        parameterizer.setSharpEdgeDegrees(ctx.sharpEdgeDegrees);
        parameterizer.setFeatureEdges(&run.isotropicFeatureEdges);
        bool parameterizeSucceeded = true;
        try {
            parameterizeSucceeded = parameterizer.parameterize();
        } catch (const RemeshCancelled&) {
            throw;
        } catch (const std::exception& e) {
            // A pathological island must not abort the whole remesh,
            // so log the parameterizer failure and skip its quads.
            parameterizeSucceeded = false;
            std::cerr << "Island " << (islandIndex + 1)
                      << ": parameterization failed (" << e.what()
                      << "), skipping this island." << std::endl;
        } catch (...) {
            parameterizeSucceeded = false;
            std::cerr << "Island " << (islandIndex + 1)
                      << ": parameterization failed (unknown error), skipping this island." << std::endl;
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        run.parameterizeUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        parameterizeTimeAccumulated += run.parameterizeUs;
        run.parameterized = parameterizeSucceeded;
        run.diagnostics = parameterizer.takeDiagnostics();

        if (parameterizeSucceeded) {
            throwIfCancelled();
            updateProgress(islandIndex, islandParameterizeEnd);
            std::unique_ptr<std::vector<std::vector<Vector2>>> uvs = parameterizer.takeTriangleUvs();
            if (uvs) {
                // Save a copy of UVs for the [param] preview overlay
                run.capturedUvs = *uvs;
                run.capturedOriginalUvs = parameterizer.originalTriangleUvs();
            }
            // Capture singular vertex positions for the [param] preview
            run.capturedSingularVertices = parameterizer.singularVertexPositions();
            run.capturedSingularVertexIndices = parameterizer.singularVertexIndices();
            QuadExtractor extractor(&vertices, &triangles, uvs.get());
            extractor.setOriginalTriangleUvs(&run.capturedOriginalUvs);
            extractor.setSingularVertices(&run.capturedSingularVertexIndices);
            extractor.setInputBoundary(&ctx.inputBoundary);
            extractor.setUseQex(m_useQex);
            extractor.setTangentialSmoothing(m_tangentialQuadSmoothing);
            extractor.setTargetQuadCount((size_t)std::max(0.0, ctx.targetQuadCount));
            extractor.setProgressHandler(
                makeStageProgress(islandIndex, islandParameterizeEnd, 1.0f, 1.0f));
            const bool extracted = extractor.extract();
            const QexOutcome& qex = extractor.qexOutcome();
            if (qex.attempted) {
                DiagnosticRecord record("extractor");
                record.label("used", qex.used ? "qex" : "trace");
                record.label("fallback_reason", qex.reason);
                record.set("qex_steps", (double)qex.steps);
                record.set("qex_steps_per_triangle", triangles.empty() ? 0.0 : (double)qex.steps / (double)triangles.size());
                record.set("qex_faces", (double)qex.faces);
                record.set("qex_non_quads", (double)qex.nonQuads);
                record.set("qex_dropped_faces", (double)qex.droppedFaces);
                record.set("qex_cut_vertices", (double)qex.cutVertices);
                run.diagnostics.push_back(std::move(record));
            }
            if (extracted) {
                run.extracted = true;
                run.capturedExtractedConnections = extractor.extractedConnections();
                run.capturedExtractedConnectionMoved = extractor.extractedConnectionMoved();
                run.remeshedVertices = extractor.remeshedVertices();
                run.remeshedQuads = extractor.remeshedQuads();
                // The extractor leaves the output on T; this puts it on the
                // island's own input faces (TODO 2.8).
                const auto projectStart = std::chrono::high_resolution_clock::now();
                FeatureEdgeSet creases;
                for (const auto& edge : ctx.featureEdges.edges)
                    creases.edges.emplace_back(ctx.inputVertices[edge.first], ctx.inputVertices[edge.second]);
                creases.flags = ctx.featureEdges.flags;
                creases.normalize();
                SurfaceProjector projector(&m_vertices, &trianglesIslands[islandIndex], &creases);
                const auto projection = projector.project(&run.remeshedVertices, run.remeshedQuads);
                DiagnosticRecord record("surface_projection");
                record.set("vertices", (double)projection.vertices);
                record.set("projected", (double)projection.projected);
                record.set("kept_unmatched", (double)projection.keptUnmatched);
                record.set("kept_far", (double)projection.keptFar);
                record.set("kept_folding", (double)projection.keptFolding);
                record.set("mean_move_edges", projection.meanMove);
                record.set("max_move_edges", projection.maxMove);
                record.set("seconds", std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - projectStart).count());
                run.diagnostics.push_back(std::move(record));
                // Projection pulls vertices apart where T's chords were
                // uneven; even the quads out again along M, with the
                // vertices next to creases put onto them.
                const auto relaxStart = std::chrono::high_resolution_clock::now();
                const auto relaxation = projector.relax(&run.remeshedVertices, run.remeshedQuads, surfaceRelaxIterations);
                DiagnosticRecord relaxRecord("surface_relax");
                relaxRecord.set("snapped", (double)relaxation.snapped);
                relaxRecord.set("snap_folding", (double)relaxation.snapFolding);
                relaxRecord.set("free", (double)relaxation.free);
                relaxRecord.set("moves", (double)relaxation.moves);
                relaxRecord.set("kept_folding", (double)relaxation.keptFolding);
                relaxRecord.set("seconds", std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - relaxStart).count());
                run.diagnostics.push_back(std::move(relaxRecord));
            }
        }
        updateProgress(islandIndex, 1.0f);
        auto t2 = std::chrono::high_resolution_clock::now();
        run.extractUs = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
        extractTimeAccumulated += run.extractUs;
    };

    tbb::parallel_for(tbb::blocked_range<size_t>(0, islandContexes.size()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i)
                resampleIsland(i, islandContexes[i], islandRuns[i], &decimationStats);
        });
    auto t_isotropicEnd = std::chrono::high_resolution_clock::now();
    throwIfCancelled();
    const size_t isotropicPeakRss = peakResidentSetBytes();

    // Parts thinner than one quad at the requested count, on each island's
    // first T (a retried island's T is finer, the request is not).  Report only.
    std::vector<ThinParts> thinParts(islandContexes.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, islandContexes.size()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i) {
                const auto& vertices = islandRuns[i].isotropicVertices;
                const auto& triangles = islandRuns[i].isotropicTriangles;
                double area = 0.0;
                for (const auto& triangle : triangles) {
                    if (3 == triangle.size())
                        area += 0.5 * Vector3::crossProduct(vertices[triangle[1]] - vertices[triangle[0]], vertices[triangle[2]] - vertices[triangle[0]]).length();
                }
                if (islandContexes[i].targetQuadCount > 0.0 && area > 0.0)
                    thinParts[i] = measureThinParts(vertices, triangles, std::sqrt(area / islandContexes[i].targetQuadCount));
            }
        });
    auto t_thinPartsEnd = std::chrono::high_resolution_clock::now();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, islandContexes.size()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i)
                parameterizeIsland(i, islandContexes[i], islandRuns[i]);
        });

    // On a coarse triangle mesh the rounded parametrization can fold over
    // (REFERENCE M1, M2), and a small island then comes out empty or torn
    // even at the minimum size above. Such an island is run again at a
    // slightly higher count, a few times; it is small, so this costs little.
    auto t_retryStart = std::chrono::high_resolution_clock::now();
    std::vector<size_t> islandsToRetry;
    for (size_t i = 0; i < islandContexes.size(); ++i) {
        const auto& ctx = islandContexes[i];
        if ((double)trianglesIslands[i].size() < minimumIslandTriangleCount
            || 2.0 * ctx.targetQuadCount > maximumRetriedIslandTriangleCount)
            continue;
        if (!isHealthyIslandResult(ctx.inputTopology, islandRuns[i].remeshedVertices, islandRuns[i].remeshedQuads))
            islandsToRetry.push_back(i);
    }
    std::vector<size_t> islandRetryAttemptCounts(islandContexes.size(), 0);
    std::atomic<size_t> islandRetryAttempts(0);
    std::atomic<size_t> islandsRepaired(0);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, islandsToRetry.size()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t k = range.begin(); k != range.end(); ++k) {
                const size_t i = islandsToRetry[k];
                double voxelSize = islandContexes[i].voxelSize;
                double targetQuadCount = islandContexes[i].targetQuadCount;
                for (size_t attempt = 0; attempt < maximumIslandRetries; ++attempt) {
                    ++islandRetryAttempts;
                    ++islandRetryAttemptCounts[i];
                    voxelSize /= std::sqrt(islandRetryGrowth);
                    targetQuadCount *= islandRetryGrowth;
                    IslandContext ctx;
                    buildIslandContext(i, ctx);
                    ctx.voxelSize = voxelSize;
                    ctx.targetQuadCount = targetQuadCount;
                    IslandRun run;
                    resampleIsland(i, ctx, run, &retryDecimationStats);
                    parameterizeIsland(i, ctx, run);
                    const bool healthy = isHealthyIslandResult(ctx.inputTopology, run.remeshedVertices, run.remeshedQuads);
                    // Prefer a torn island to a missing one.
                    const bool replace = healthy || (islandRuns[i].remeshedQuads.empty() && !run.remeshedQuads.empty());
                    // The extractor records of the attempt that is dropped stay, marked
                    // superseded, so libQEx fallbacks are counted per attempt (TODO 6.1b).
                    IslandRun& dropped = replace ? islandRuns[i] : run;
                    IslandRun& kept = replace ? run : islandRuns[i];
                    for (auto& record : dropped.diagnostics) {
                        if (record.stage == "extractor")
                            kept.diagnostics.push_back(std::move(record.label("superseded", "yes")));
                    }
                    if (replace) {
                        islandContexes[i] = std::move(ctx);
                        islandRuns[i] = std::move(run);
                    }
                    if (healthy) {
                        ++islandsRepaired;
                        break;
                    }
                }
            }
        });
    auto t_parallelEnd = std::chrono::high_resolution_clock::now();

    m_islandCount = islandRuns.size();
    m_islandsWithoutOutput = 0;
    for (const auto& run : islandRuns) {
        if (run.remeshedQuads.empty())
            ++m_islandsWithoutOutput;
    }

    if (nullptr != m_progressHandler)
        m_progressHandler(m_tag, parallelPhaseEnd, "Merging mesh islands");

    {
        // Merges either the decimated or the isotropic mesh of every island.
        auto mergeIslands = [&](bool decimated,
                                std::vector<Vector3>& mergedVertices,
                                std::vector<std::vector<size_t>>& mergedTriangles) {
            for (const auto& run : islandRuns) {
                const auto& islandVertices = decimated ? run.decimatedVertices : run.isotropicVertices;
                const auto& islandTriangles = decimated ? run.decimatedTriangles : run.isotropicTriangles;
                const size_t vertexOffset = mergedVertices.size();
                mergedVertices.insert(mergedVertices.end(),
                    islandVertices.begin(), islandVertices.end());
                for (const auto& triangle : islandTriangles) {
                    std::vector<size_t> offsetTriangle;
                    offsetTriangle.reserve(triangle.size());
                    for (const size_t index : triangle)
                        offsetTriangle.push_back(index + vertexOffset);
                    mergedTriangles.push_back(std::move(offsetTriangle));
                }
            }
        };

        m_isotropicVertices.clear();
        m_isotropicTriangles.clear();
        m_decimatedVertices.clear();
        m_decimatedTriangles.clear();
        mergeIslands(false, m_isotropicVertices, m_isotropicTriangles);
        m_decimated = decimationStats.islandsDecimated.load() + retryDecimationStats.islandsDecimated.load() > 0;
        if (m_decimated) {
            mergeIslands(true, m_decimatedVertices, m_decimatedTriangles);
        }
    }

    // Merge isotropic UVs from all islands (for [param] preview)
    m_isotropicTriangleUvs.clear();
    m_isotropicOriginalTriangleUvs.clear();
    for (const auto& run : islandRuns) {
        if (run.capturedUvs.empty())
            continue;
        m_isotropicTriangleUvs.insert(m_isotropicTriangleUvs.end(),
            run.capturedUvs.begin(), run.capturedUvs.end());
        m_isotropicOriginalTriangleUvs.insert(m_isotropicOriginalTriangleUvs.end(),
            run.capturedOriginalUvs.begin(), run.capturedOriginalUvs.end());
    }

    // Merge singular vertex positions from all islands (for [param] preview)
    m_isotropicSingularVertices.clear();
    for (const auto& run : islandRuns) {
        m_isotropicSingularVertices.insert(m_isotropicSingularVertices.end(),
            run.capturedSingularVertices.begin(), run.capturedSingularVertices.end());
    }

    // Merge the raw quad-extraction connections for the [param] preview.
    m_isotropicExtractedConnections.clear();
    m_isotropicExtractedConnectionMoved.clear();
    for (const auto& run : islandRuns) {
        m_isotropicExtractedConnections.insert(m_isotropicExtractedConnections.end(),
            run.capturedExtractedConnections.begin(), run.capturedExtractedConnections.end());
        m_isotropicExtractedConnectionMoved.resize(m_isotropicExtractedConnections.size()
                - run.capturedExtractedConnections.size(),
            0);
        m_isotropicExtractedConnectionMoved.insert(m_isotropicExtractedConnectionMoved.end(),
            run.capturedExtractedConnectionMoved.begin(),
            run.capturedExtractedConnectionMoved.end());
        m_isotropicExtractedConnectionMoved.resize(m_isotropicExtractedConnections.size(), 0);
    }
    m_remeshedIslands.clear();
    m_remeshedIslands.reserve(islandRuns.size());
    for (size_t i = 0; i < islandRuns.size(); ++i) {
        const auto& run = islandRuns[i];
        const auto& input = islandContexes[i].inputTopology;
        RemeshedIsland island;
        island.closedInput = 0 == input.boundaryEdges && 0 == input.nonManifoldEdges;
        island.targetQuads = islandContexes[i].targetQuadCount;
        if (!run.remeshedQuads.empty()) {
            island.vertexCount = run.remeshedVertices.size();
            island.faceCount = run.remeshedQuads.size();
            island.inputTwiceHandles = islandContexes[i].inputTwiceHandles;
            if (island.inputTwiceHandles >= 0)
                island.outputTwiceHandles = twiceHandleCount(run.remeshedVertices.size(), run.remeshedQuads);
        }
        m_remeshedIslands.push_back(island);
    }
    for (const auto& run : islandRuns) {
        if (run.remeshedQuads.empty())
            continue;
        size_t vertexStartIndex = m_remeshedVertices.size();
        m_remeshedVertices.insert(m_remeshedVertices.end(),
            run.remeshedVertices.begin(), run.remeshedVertices.end());
        for (const auto& it : run.remeshedQuads) {
            std::vector<size_t> quad;
            quad.reserve(it.size());
            for (const auto& v : it)
                quad.push_back(vertexStartIndex + v);
            m_remeshedQuads.push_back(quad);
        }
    }

    auto t_mergeEnd = std::chrono::high_resolution_clock::now();

    const auto elapsedUs = [](const std::chrono::high_resolution_clock::time_point& from,
                               const std::chrono::high_resolution_clock::time_point& to) {
        return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
    };
    const long long t_voxelUs = elapsedUs(t_voxelStart, t_voxelEnd);
    const long long t_splitUs = elapsedUs(t_splitStart, t_afterSplit);
    const long long t_buildUs = elapsedUs(t_afterSplit, t_buildEnd);
    const long long t_isotropicWallUs = elapsedUs(t_buildEnd, t_isotropicEnd);
    const long long t_parameterizeWallUs = elapsedUs(t_isotropicEnd, t_retryStart);
    const long long t_retryWallUs = elapsedUs(t_retryStart, t_parallelEnd);
    const long long t_parallelWallUs = elapsedUs(t_buildEnd, t_parallelEnd);
    const long long t_mergeUs = elapsedUs(t_parallelEnd, t_mergeEnd);
    const long long t_totalUs = elapsedUs(t_start, t_mergeEnd);

    // Includes the retried islands, so the isotropic remesh time below is net
    // of all decimation.
    const long long t_decimateUs = decimationStats.timeUs.load() + retryDecimationStats.timeUs.load();
    const long long t_adaptiveFieldUs = adaptiveFieldTime.load();
    const size_t decimatedIslands = decimationStats.islandsDecimated.load();

    m_phaseReport.clear();
    {
        std::ostringstream line;
        // Whole milliseconds hide the per-island steps on a mesh split into many
        // small islands, so keep one decimal place.
        const auto milliseconds = [](long long microseconds) {
            std::ostringstream value;
            value.setf(std::ios::fixed);
            value.precision(1);
            value << (double)microseconds / 1000.0 << " ms";
            return value.str();
        };
        auto phase = [&](const char* name, long long microseconds) {
            line.str(std::string());
            line << name << ": " << milliseconds(microseconds);
            m_phaseReport.push_back(line.str());
        };

        line.str(std::string());
        line << "Islands: " << islandContexes.size()
             << ", input triangles: " << m_triangles.size();
        m_phaseReport.push_back(line.str());

        phase("Compute voxel size", t_voxelUs);
        phase("Split into islands", t_splitUs);
        phase("Build island contexts", t_buildUs);

        line.str(std::string());
        const size_t undecimatedIslands = decimationStats.islandsKeptUndecimated.load();
        if (decimatedIslands > 0 || undecimatedIslands > 0) {
            line << "Mesh simplifier: RAN on " << decimatedIslands << " of "
                 << decimationStats.islandsConsidered.load() << " islands, "
                 << decimationStats.trianglesBefore.load() << " -> "
                 << decimationStats.trianglesAfter.load() << " triangles, ";
            if (undecimatedIslands > 0)
                line << undecimatedIslands << " kept undecimated (decimating changed their topology), ";
            line << milliseconds(t_decimateUs);
        } else {
            line << "Mesh simplifier: SKIPPED (no island above "
                 << (long long)decimateTriggerRatio << "x target triangle count), "
                 << milliseconds(t_decimateUs);
        }
        m_phaseReport.push_back(line.str());

        // The accumulated figures sum the islands, so on a multi-island mesh they
        // add up to more than the wall clock next to them.  That gap is the point:
        // accumulated / wall is how many cores the phase actually kept busy.
        phase("Adaptive target length field (accumulated)", t_adaptiveFieldUs);
        phase("Isotropic remesh (accumulated)",
            resampleTime.load() - t_decimateUs - t_adaptiveFieldUs);
        phase("Parameterize (accumulated)", parameterizeTimeAccumulated.load());
        phase("Quad extract (accumulated)", extractTimeAccumulated.load());

        {
            std::lock_guard<std::mutex> lock(m_stageTimingMutex);
            std::sort(m_stageTimes.begin(), m_stageTimes.end(),
                [](const StageTime& first, const StageTime& second) {
                    return first.order < second.order;
                });
            for (const auto& it : m_stageTimes) {
                line.str(std::string());
                line << "    " << it.name << ": " << milliseconds(it.microseconds);
                m_phaseReport.push_back(line.str());
            }
        }

        phase("Isotropic phase wall clock", t_isotropicWallUs);
        phase("Parameterize phase wall clock", t_parameterizeWallUs);

        line.str(std::string());
        line << "Small islands retried: " << islandsToRetry.size()
             << ", attempts: " << islandRetryAttempts.load()
             << ", repaired: " << islandsRepaired.load()
             << ", " << milliseconds(t_retryWallUs);
        m_phaseReport.push_back(line.str());
        phase("Parallel phase wall clock", t_parallelWallUs);

        {
            const long long accumulated = resampleTime.load()
                + parameterizeTimeAccumulated.load() + extractTimeAccumulated.load();
            line.str(std::string());
            line.setf(std::ios::fixed);
            line.precision(2);
            line << "Cores kept busy across the parallel phase: "
                 << (t_parallelWallUs > 0 ? (double)accumulated / t_parallelWallUs : 0.0)
                 << " (islands are the unit of parallelism)";
            m_phaseReport.push_back(line.str());
        }

        phase("Merge islands", t_mergeUs);
        phase("Total", t_totalUs);
    }

    for (const auto& line : m_phaseReport)
        std::cerr << line << std::endl;

    {
        const auto milliseconds = [](long long microseconds) {
            return (double)microseconds / 1000.0;
        };
        size_t nonQuadFaces = 0;
        for (const auto& face : m_remeshedQuads)
            if (4 != face.size())
                ++nonQuadFaces;
        DiagnosticRecord run("run");
        run.set("input_vertices", (double)m_vertices.size());
        run.set("input_triangles", (double)m_triangles.size());
        run.set("target_triangles", (double)m_targetTriangleCount);
        run.set("islands", (double)islandContexes.size());
        run.set("adaptivity", m_adaptivity);
        run.set("anisotropy", m_anisotropy);
        run.label("extractor", m_useQex ? "qex" : "trace");
        run.set("output_vertices", (double)m_remeshedVertices.size());
        run.set("output_faces", (double)m_remeshedQuads.size());
        run.set("output_non_quads", (double)nonQuadFaces);
        run.set("voxel_ms", milliseconds(t_voxelUs));
        run.set("split_ms", milliseconds(t_splitUs));
        run.set("build_ms", milliseconds(t_buildUs));
        run.set("isotropic_wall_ms", milliseconds(t_isotropicWallUs));
        run.set("parameterize_wall_ms", milliseconds(t_parameterizeWallUs));
        run.set("retry_wall_ms", milliseconds(t_retryWallUs));
        run.set("merge_ms", milliseconds(t_mergeUs));
        run.set("total_ms", milliseconds(t_totalUs));
        run.set("cpu_seconds", processCpuSeconds() - cpuStart);
        run.set("isotropic_peak_rss_bytes", (double)isotropicPeakRss);
        run.set("peak_rss_bytes", (double)peakResidentSetBytes());
        run.set("decimated_islands", (double)decimatedIslands);
        run.set("decimated_triangles_before", (double)decimationStats.trianglesBefore.load());
        run.set("decimated_triangles_after", (double)decimationStats.trianglesAfter.load());
        run.set("undecimated_islands", (double)decimationStats.islandsKeptUndecimated.load());
        run.set("input_merged_vertices", (double)cleanup.mergedVertices);
        run.set("input_dropped_triangles", (double)cleanup.droppedTriangles);
        run.set("input_cleanup_kept_parts", (double)cleanup.keptParts);
        run.set("input_zero_area_triangles", (double)cleanup.zeroAreaTriangles);
        run.set("input_flipped_triangles", (double)cleanup.flippedTriangles);
        run.set("input_non_orientable_pieces", (double)cleanup.nonOrientablePieces);
        {
            run.set("input_fold_repair", m_repairInputFolds ? 1.0 : 0.0);
            run.set("input_folded_triangles", (double)folds.foldedTriangles);
            run.set("input_fold_clusters", (double)folds.clusters);
            run.set("input_fold_patches_repaired", (double)folds.repairedPatches);
            run.set("input_folded_triangles_after", (double)folds.foldedTrianglesAfter);
            run.set("input_fold_clusters_after", (double)folds.clustersAfter);
            run.set("input_fold_moved_vertices", (double)folds.movedVertices);
            run.set("input_fold_max_offset", folds.maximumOffset);
            run.set("input_fold_max_move", folds.maximumMove);
            run.set("input_fold_spikes", (double)folds.flattenedSpikes);
            run.set("input_fold_spike_drop_quads", spikeDropQuads);
            run.set("fold_repair_ms", folds.seconds * 1000.0);
        }
        {
            // T's defects beyond the input's: a non-manifold edge or a hole in
            // T becomes a hole or a wrong genus in the output.
            size_t decimatedNonManifoldEdges = 0;
            size_t isotropicNonManifoldEdges = 0;
            size_t openedClosedIslands = 0;
            for (size_t i = 0; i < islandRuns.size(); ++i) {
                decimatedNonManifoldEdges += islandRuns[i].decimatedTopology.nonManifoldEdges;
                isotropicNonManifoldEdges += islandRuns[i].isotropicTopology.nonManifoldEdges;
                const auto& input = islandContexes[i].inputTopology;
                const auto& isotropic = islandRuns[i].isotropicTopology;
                if (0 == input.boundaryEdges && 0 == input.nonManifoldEdges
                    && (0 != isotropic.boundaryEdges || 0 != isotropic.nonManifoldEdges))
                    ++openedClosedIslands;
            }
            run.set("decimated_non_manifold_edges", (double)decimatedNonManifoldEdges);
            run.set("isotropic_non_manifold_edges", (double)isotropicNonManifoldEdges);
            run.set("isotropic_opened_closed_islands", (double)openedClosedIslands);
        }
        {
            double area = 0.0, thinArea = 0.0;
            size_t spots = 0;
            std::vector<std::pair<double, double>> growth;
            for (const auto& thin : thinParts) {
                area += thin.area;
                thinArea += thin.thinArea;
                spots += thin.spots;
                growth.insert(growth.end(), thin.growth.begin(), thin.growth.end());
            }
            run.set("thin_area_share", area > 0.0 ? thinArea / area : 0.0);
            run.set("thin_spots", (double)spots);
            run.set("thin_growth_p50", medianThinGrowth(std::move(growth)));
            run.set("thin_parts_ms", milliseconds(std::chrono::duration_cast<std::chrono::microseconds>(t_thinPartsEnd - t_isotropicEnd).count()));
        }
        run.set("retried_islands", (double)islandsToRetry.size());
        run.set("repaired_islands", (double)islandsRepaired.load());
        m_diagnostics.push_back(std::move(run));

        {
            std::lock_guard<std::mutex> lock(m_stageTimingMutex);
            for (const auto& it : m_stageTimes) {
                {
                    DiagnosticRecord record("step");
                    record.label("name", it.name);
                    record.set("order", it.order);
                    record.set("accumulated_ms", milliseconds(it.microseconds));
                    m_diagnostics.push_back(std::move(record));
                }
            }
        }

        // A retried island reports the run it kept.
        for (size_t i = 0; i < islandRuns.size(); ++i) {
            const auto& islandRun = islandRuns[i];
            size_t quads = 0;
            for (const auto& face : islandRun.remeshedQuads)
                if (4 == face.size())
                    ++quads;
            DiagnosticRecord record("island", i);
            record.set("input_triangles", (double)trianglesIslands[i].size());
            record.set("target_quads", islandContexes[i].targetQuadCount);
            record.set("isotropic_triangles", (double)islandContexes[i].triangles.size());
            record.set("input_boundary_edges", (double)islandContexes[i].inputTopology.boundaryEdges);
            record.set("input_non_manifold_edges", (double)islandContexes[i].inputTopology.nonManifoldEdges);
            record.set("decimated_boundary_edges", (double)islandRun.decimatedTopology.boundaryEdges);
            record.set("decimated_non_manifold_edges", (double)islandRun.decimatedTopology.nonManifoldEdges);
            record.set("isotropic_boundary_edges", (double)islandRun.isotropicTopology.boundaryEdges);
            record.set("isotropic_non_manifold_edges", (double)islandRun.isotropicTopology.nonManifoldEdges);
            record.set("isotropic_euler_characteristic", (double)islandRun.isotropicTopology.eulerCharacteristic);
            record.set("resample_ms", milliseconds(islandRun.resampleUs));
            record.set("parameterize_ms", milliseconds(islandRun.parameterizeUs));
            record.set("extract_ms", milliseconds(islandRun.extractUs));
            record.set("parameterized", islandRun.parameterized ? 1.0 : 0.0);
            record.set("extracted", islandRun.extracted ? 1.0 : 0.0);
            record.set("retry_attempts", (double)islandRetryAttemptCounts[i]);
            record.set("output_faces", (double)islandRun.remeshedQuads.size());
            record.set("output_quads", (double)quads);
            record.set("thin_area_share", thinParts[i].area > 0.0 ? thinParts[i].thinArea / thinParts[i].area : 0.0);
            record.set("thin_spots", (double)thinParts[i].spots);
            m_diagnostics.push_back(std::move(record));
        }
        for (size_t i = 0; i < islandRuns.size(); ++i) {
            if (!islandRuns[i].features.values.empty()) {
                m_diagnostics.push_back(islandRuns[i].features);
                m_diagnostics.back().island = i;
            }
            for (const auto& record : islandRuns[i].diagnostics) {
                m_diagnostics.push_back(record);
                m_diagnostics.back().island = i;
            }
        }
    }

#if AUTO_REMESHER_DEBUG
    std::cerr << "Remesh done" << std::endl;
#endif

    if (nullptr != m_progressHandler)
        m_progressHandler(m_tag, 1.0, "Done");

    return true;
}

}
