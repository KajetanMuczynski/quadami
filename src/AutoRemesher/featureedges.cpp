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
#include <cstdint>
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

FeatureEdgeSet filterDetectedChains(const FeatureEdgeSet& set,
    const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double minLength, size_t* dropped)
{
    if (nullptr != dropped)
        *dropped = 0;
    if (set.empty())
        return set;
    const SurfaceMesh mesh(vertices, triangles);
    std::vector<double> normalAngle(set.size(), 0.0);
    for (size_t c = 0; c < mesh.cornerCount(); ++c) {
        const size_t opposite = mesh.oppositeCorner(c);
        if (opposite == SurfaceMesh::npos || opposite < c)
            continue;
        const auto edge = undirected(mesh.cornerVertex(c), mesh.cornerVertex(mesh.nextCorner(c)));
        const auto found = std::lower_bound(set.edges.begin(), set.edges.end(), edge);
        if (found != set.edges.end() && *found == edge)
            normalAngle[found - set.edges.begin()] = std::max(0.0, cornerNormalAngle(mesh, c));
    }

    std::vector<std::vector<size_t>> edgesAtVertex(vertices.size());
    for (size_t e = 0; e < set.size(); ++e) {
        edgesAtVertex[set.edges[e].first].push_back(e);
        edgesAtVertex[set.edges[e].second].push_back(e);
    }
    const auto otherEnd = [&](size_t e, size_t v) {
        return set.edges[e].first == v ? set.edges[e].second : set.edges[e].first;
    };
    // The remesher's corner test (IsotropicHalfedgeMesh::featureCorners).
    const double cosCornerTurn = std::cos(45.0 * M_PI / 180.0);
    std::vector<char> corner(vertices.size(), 0);
    for (size_t v = 0; v < vertices.size(); ++v) {
        const auto& at = edgesAtVertex[v];
        if (at.empty())
            continue;
        if (2 != at.size()) {
            corner[v] = 1;
            continue;
        }
        const Vector3 in = vertices[v] - vertices[otherEnd(at[0], v)];
        const Vector3 out = vertices[otherEnd(at[1], v)] - vertices[v];
        const double inLength = in.length(), outLength = out.length();
        corner[v] = (inLength <= 0.0 || outLength <= 0.0
                        || Vector3::dotProduct(in, out) < cosCornerTurn * inLength * outLength)
            ? 1
            : 0;
    }

    constexpr size_t kZigzagChains = 5;
    // Components: vertices joined by set edges.
    std::vector<size_t> parent(vertices.size());
    for (size_t v = 0; v < parent.size(); ++v)
        parent[v] = v;
    const auto find = [&](size_t v) {
        while (parent[v] != v)
            v = parent[v] = parent[parent[v]];
        return v;
    };
    for (const auto& edge : set.edges)
        parent[find(edge.first)] = find(edge.second);

    struct Chain {
        size_t component = 0;
        double length = 0.0;
        bool hard = false;
        bool dropped = false;
    };
    std::vector<Chain> chains;
    std::vector<size_t> chainOfEdge(set.size(), SIZE_MAX);
    const auto walk = [&](size_t start, size_t firstEdge) {
        Chain chain;
        chain.component = find(start);
        double weightedAngle = 0.0;
        size_t v = start, e = firstEdge;
        while (true) {
            chainOfEdge[e] = chains.size();
            const double edgeLength = (vertices[set.edges[e].second] - vertices[set.edges[e].first]).length();
            chain.length += edgeLength;
            weightedAngle += normalAngle[e] * edgeLength;
            chain.hard = chain.hard || 0 != (set.flags[e] & FeatureEdgeSet::Hard);
            v = otherEnd(e, v);
            if (corner[v] || v == start)
                break;
            const auto& at = edgesAtVertex[v];
            e = SIZE_MAX != chainOfEdge[at[0]] ? at[1] : at[0];
            if (SIZE_MAX != chainOfEdge[e])
                break;
        }
        const bool fold = chain.length > 0.0
            && weightedAngle / chain.length > kFoldNormalAngleDegrees * (M_PI / 180.0);
        chain.dropped = !chain.hard && fold;
        chains.push_back(chain);
    };
    for (size_t v = 0; v < vertices.size(); ++v) {
        if (!corner[v])
            continue;
        for (const size_t e : edgesAtVertex[v]) {
            if (SIZE_MAX == chainOfEdge[e])
                walk(v, e);
        }
    }
    // Closed loops without a corner.
    for (size_t e = 0; e < set.size(); ++e) {
        if (SIZE_MAX == chainOfEdge[e])
            walk(set.edges[e].first, e);
    }

    // Zigzag noise: a component of kZigzagChains or more chains, shorter than
    // `minLength` on average.  Its longer chains are pieces of the same
    // crumpled region.  A short chain joined to long ones (between two
    // corners of a box) or to a few short ones (the ridges at a spike's tip)
    // is a real feature's.
    std::vector<double> componentLength(vertices.size(), 0.0);
    std::vector<size_t> componentChains(vertices.size(), 0);
    std::vector<char> componentHard(vertices.size(), 0);
    for (const Chain& chain : chains) {
        componentLength[chain.component] += chain.length;
        ++componentChains[chain.component];
        componentHard[chain.component] |= chain.hard ? 1 : 0;
    }
    for (Chain& chain : chains) {
        if (!componentHard[chain.component] && componentChains[chain.component] >= kZigzagChains
            && componentLength[chain.component] < minLength * (double)componentChains[chain.component])
            chain.dropped = true;
    }
    std::vector<char> keep(set.size(), 1);
    for (size_t e = 0; e < set.size(); ++e)
        keep[e] = chains[chainOfEdge[e]].dropped ? 0 : 1;

    FeatureEdgeSet result;
    for (size_t e = 0; e < set.size(); ++e) {
        if (keep[e]) {
            result.edges.push_back(set.edges[e]);
            result.flags.push_back(set.flags[e]);
        }
    }
    if (nullptr != dropped)
        *dropped = set.size() - result.size();
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

FeatureEdgeSet dropShortFeatureComponents(const FeatureEdgeSet& set, const std::vector<Vector3>& vertices,
    double minLength, size_t* dropped)
{
    if (nullptr != dropped)
        *dropped = 0;
    if (set.empty())
        return set;
    std::vector<size_t> parent(vertices.size());
    for (size_t v = 0; v < parent.size(); ++v)
        parent[v] = v;
    const auto find = [&](size_t v) {
        while (parent[v] != v)
            v = parent[v] = parent[parent[v]];
        return v;
    };
    for (const auto& edge : set.edges)
        parent[find(edge.first)] = find(edge.second);
    std::vector<double> componentLength(vertices.size(), 0.0);
    std::vector<char> componentHard(vertices.size(), 0);
    for (size_t e = 0; e < set.size(); ++e) {
        const size_t c = find(set.edges[e].first);
        componentLength[c] += (vertices[set.edges[e].second] - vertices[set.edges[e].first]).length();
        componentHard[c] |= 0 != (set.flags[e] & FeatureEdgeSet::Hard) ? 1 : 0;
    }
    FeatureEdgeSet result;
    for (size_t e = 0; e < set.size(); ++e) {
        const size_t c = find(set.edges[e].first);
        if (!componentHard[c] && componentLength[c] < minLength) {
            if (nullptr != dropped)
                ++*dropped;
            continue;
        }
        result.edges.push_back(set.edges[e]);
        result.flags.push_back(set.flags[e]);
    }
    return result;
}

}
