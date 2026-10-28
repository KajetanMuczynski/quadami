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
#include <AutoRemesher/FeatureEdges>
#include <algorithm>
#include <cmath>
#include <limits>
#include <meshoptimizer.h>

namespace AutoRemesher {

constexpr unsigned char FeatureEdgeSet::Hard;

namespace {

    std::pair<size_t, size_t> undirected(size_t a, size_t b)
    {
        return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
    }

    // The unsigned angle between the normals of the two faces at `corner`'s
    // edge; negative when the edge is a boundary or a face has no normal.
    double cornerNormalAngle(const SurfaceMesh& mesh, size_t corner)
    {
        const size_t neighbour = mesh.adjacentFace(corner);
        if (neighbour == SurfaceMesh::npos)
            return -1.0;
        const Vector3 first = mesh.faceNormal(mesh.cornerFace(corner));
        const Vector3 second = mesh.faceNormal(neighbour);
        if (first.isZero() || second.isZero())
            return -1.0;
        return std::fabs(mesh.normalAngle(corner));
    }

    // `carried` edges that are still edges of the mesh, plus every other
    // interior edge that passes the sharp test on it; `added` counts these.
    FeatureEdgeSet carryAndClassify(const FeatureEdgeSet& carried,
        const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles,
        double sharpEdgeDegrees, size_t* added)
    {
        const SurfaceMesh mesh(vertices, triangles);
        FeatureEdgeSet result;
        size_t addedEdges = 0;
        for (size_t c = 0; c < mesh.cornerCount(); ++c) {
            const size_t opposite = mesh.oppositeCorner(c);
            if (opposite == SurfaceMesh::npos || opposite < c)
                continue;
            const size_t a = mesh.cornerVertex(c), b = mesh.cornerVertex(mesh.nextCorner(c));
            const auto edge = undirected(a, b);
            const auto found = std::lower_bound(carried.edges.begin(), carried.edges.end(), edge);
            if (found != carried.edges.end() && *found == edge) {
                result.edges.push_back(edge);
                result.flags.push_back(carried.flags[found - carried.edges.begin()]);
            } else if (isSharpNormalAngle(cornerNormalAngle(mesh, c), sharpEdgeDegrees)) {
                result.edges.push_back(edge);
                result.flags.push_back(0);
                ++addedEdges;
            }
        }
        result.normalize();
        if (nullptr != added)
            *added = addedEdges;
        return result;
    }

}

bool FeatureEdgeSet::contains(size_t a, size_t b) const
{
    return std::binary_search(edges.begin(), edges.end(), undirected(a, b));
}

void FeatureEdgeSet::normalize()
{
    flags.resize(edges.size(), 0);
    std::vector<size_t> order(edges.size());
    for (size_t i = 0; i < order.size(); ++i) {
        edges[i] = undirected(edges[i].first, edges[i].second);
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return edges[a] < edges[b]; });
    std::vector<std::pair<size_t, size_t>> sortedEdges;
    std::vector<unsigned char> sortedFlags;
    sortedEdges.reserve(edges.size());
    sortedFlags.reserve(edges.size());
    for (const size_t i : order) {
        if (!sortedEdges.empty() && sortedEdges.back() == edges[i]) {
            sortedFlags.back() |= flags[i];
            continue;
        }
        sortedEdges.push_back(edges[i]);
        sortedFlags.push_back(flags[i]);
    }
    edges = std::move(sortedEdges);
    flags = std::move(sortedFlags);
}

bool sharpEdgesEnabled(double sharpEdgeDegrees)
{
    return sharpEdgeDegrees > 0.0 && sharpEdgeDegrees < 180.0;
}

bool isSharpNormalAngle(double normalAngle, double sharpEdgeDegrees)
{
    if (!sharpEdgesEnabled(sharpEdgeDegrees))
        return false;
    return normalAngle >= (sharpEdgeDegrees - kSharpEdgeToleranceDegrees) * (M_PI / 180.0);
}

FeatureEdgeSet classifySharpEdges(const SurfaceMesh& mesh, double sharpEdgeDegrees)
{
    FeatureEdgeSet set;
    if (!sharpEdgesEnabled(sharpEdgeDegrees))
        return set;
    for (size_t c = 0; c < mesh.cornerCount(); ++c) {
        const size_t opposite = mesh.oppositeCorner(c);
        if (opposite == SurfaceMesh::npos || opposite < c)
            continue;
        if (!isSharpNormalAngle(cornerNormalAngle(mesh, c), sharpEdgeDegrees))
            continue;
        set.edges.push_back(undirected(mesh.cornerVertex(c), mesh.cornerVertex(mesh.nextCorner(c))));
    }
    set.normalize();
    return set;
}

FeatureEdgeSet classifySharpEdges(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles, double sharpEdgeDegrees)
{
    if (!sharpEdgesEnabled(sharpEdgeDegrees))
        return FeatureEdgeSet();
    return classifySharpEdges(SurfaceMesh(vertices, triangles), sharpEdgeDegrees);
}

FeatureEdgeSet reclassifyFeatureEdges(const FeatureEdgeSet& carried,
    const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double sharpEdgeDegrees, size_t* classified)
{
    FeatureEdgeSet hard;
    for (size_t i = 0; i < carried.edges.size(); ++i) {
        if (0 != (carried.flags[i] & FeatureEdgeSet::Hard)) {
            hard.edges.push_back(carried.edges[i]);
            hard.flags.push_back(carried.flags[i]);
        }
    }
    return carryAndClassify(hard, vertices, triangles, sharpEdgeDegrees, classified);
}

FeatureEdgeSet remapFeatureEdges(const FeatureEdgeSet& set,
    const std::vector<size_t>& outputIndexOfInput,
    const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double sharpEdgeDegrees, size_t* dropped)
{
    constexpr size_t removed = std::numeric_limits<size_t>::max();
    FeatureEdgeSet mapped;
    for (size_t i = 0; i < set.edges.size(); ++i) {
        const size_t a = outputIndexOfInput[set.edges[i].first];
        const size_t b = outputIndexOfInput[set.edges[i].second];
        if (a != removed && b != removed) {
            mapped.edges.push_back(undirected(a, b));
            mapped.flags.push_back(set.flags[i]);
        }
    }
    mapped.normalize();

    const FeatureEdgeSet result = carryAndClassify(mapped, vertices, triangles, sharpEdgeDegrees, nullptr);
    if (nullptr != dropped) {
        size_t kept = 0;
        for (const auto& edge : mapped.edges)
            kept += result.contains(edge.first, edge.second) ? 1 : 0;
        *dropped = mapped.edges.size() - kept;
    }
    return result;
}

std::vector<char> featureCornerFlags(const SurfaceMesh& mesh, const FeatureEdgeSet& set,
    unsigned char requiredFlags)
{
    std::vector<char> flags(mesh.cornerCount(), 0);
    if (set.empty())
        return flags;
    for (size_t c = 0; c < mesh.cornerCount(); ++c) {
        if (mesh.oppositeCorner(c) == SurfaceMesh::npos)
            continue;
        const auto edge = undirected(mesh.cornerVertex(c), mesh.cornerVertex(mesh.nextCorner(c)));
        const auto found = std::lower_bound(set.edges.begin(), set.edges.end(), edge);
        if (found != set.edges.end() && *found == edge
            && requiredFlags == (set.flags[found - set.edges.begin()] & requiredFlags))
            flags[c] = 1;
    }
    return flags;
}

std::vector<unsigned char> featureVertexLock(const FeatureEdgeSet& set, size_t vertexCount)
{
    std::vector<unsigned char> lock(vertexCount, 0);
    for (size_t i = 0; i < set.edges.size(); ++i) {
        // A hard edge stays as it is: no collapse moves its ends, so it
        // survives the decimation.
        const unsigned char bit = 0 != (set.flags[i] & FeatureEdgeSet::Hard)
            ? meshopt_SimplifyVertex_Lock
            : meshopt_SimplifyVertex_Priority;
        lock[set.edges[i].first] |= bit;
        lock[set.edges[i].second] |= bit;
    }
    return lock;
}

double featureEdgeLength(const FeatureEdgeSet& set, const std::vector<Vector3>& vertices)
{
    double length = 0.0;
    for (const auto& edge : set.edges)
        length += (vertices[edge.second] - vertices[edge.first]).length();
    return length;
}

}
