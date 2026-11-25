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
#include <AutoRemesher/MeasureMesh>
#include <AutoRemesher/RemeshPreview>
#include <AutoRemesher/Symmetry>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <unordered_map>

namespace AutoRemesher {

namespace {

    uint64_t edgeKey(uint32_t a, uint32_t b)
    {
        return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
    }

    struct Point {
        double x, y, z;
    };

    Point at(const std::vector<double>& positions, size_t v)
    {
        return { positions[3 * v], positions[3 * v + 1], positions[3 * v + 2] };
    }

    Point sub(const Point& a, const Point& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    Point add(const Point& a, const Point& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    Point scale(const Point& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
    double dot(const Point& a, const Point& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    Point cross(const Point& a, const Point& b)
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }
    double length(const Point& a) { return std::sqrt(dot(a, a)); }

    // Signed distance to the plane, positive on the kept side.
    double sideOf(const std::vector<double>& positions, size_t v, const SymmetryPlane& plane)
    {
        return plane.side * (positions[3 * v + plane.axis] - plane.center);
    }

    // Removes the vertices no face uses; `faces` holds vertex indices.
    std::vector<uint32_t> compact(std::vector<double>* positions, std::vector<uint32_t>* faces)
    {
        const size_t count = positions->size() / 3;
        std::vector<uint32_t> map(count, std::numeric_limits<uint32_t>::max());
        for (uint32_t v : *faces)
            map[v] = 0;
        uint32_t next = 0;
        std::vector<double> kept;
        kept.reserve(positions->size());
        for (size_t v = 0; v < count; ++v) {
            if (map[v] == std::numeric_limits<uint32_t>::max())
                continue;
            map[v] = next++;
            kept.insert(kept.end(), positions->begin() + 3 * v, positions->begin() + 3 * v + 3);
        }
        for (uint32_t& v : *faces)
            v = map[v];
        positions->swap(kept);
        return map;
    }

    // Is a face folded: a corner turns against the face's area vector, or a
    // quad is twisted (as measureMesh counts them).
    bool folded(const std::vector<double>& positions, const uint32_t* corners, size_t size)
    {
        Point normal { 0.0, 0.0, 0.0 };
        for (size_t i = 0; i < size; ++i)
            normal = add(normal, cross(at(positions, corners[i]), at(positions, corners[(i + 1) % size])));
        if (length(normal) <= 0.0)
            return true;
        for (size_t i = 0; i < size; ++i) {
            const Point corner = at(positions, corners[i]);
            const Point next = sub(at(positions, corners[(i + 1) % size]), corner);
            const Point previous = sub(at(positions, corners[(i + size - 1) % size]), corner);
            if (dot(cross(next, previous), normal) <= 0.0)
                return true;
        }
        // A quad folded under both diagonals (twisted), which a saddle can be
        // with every corner turning the right way.
        if (4 == size) {
            const Point a = at(positions, corners[0]), b = at(positions, corners[1]);
            const Point c = at(positions, corners[2]), d = at(positions, corners[3]);
            if (dot(cross(sub(b, a), sub(c, a)), cross(sub(c, a), sub(d, a))) < 0.0
                && dot(cross(sub(c, b), sub(d, b)), cross(sub(d, b), sub(a, b))) < 0.0)
                return true;
        }
        return false;
    }

    Point closestOnSection(const Point& p, const std::vector<double>& section)
    {
        Point best = p;
        double bestDistance = std::numeric_limits<double>::max();
        for (size_t i = 0; i + 6 <= section.size(); i += 6) {
            const Point a { section[i], section[i + 1], section[i + 2] };
            const Point d = sub(Point { section[i + 3], section[i + 4], section[i + 5] }, a);
            const double denominator = dot(d, d);
            double t = denominator > 0.0 ? dot(sub(p, a), d) / denominator : 0.0;
            t = std::min(1.0, std::max(0.0, t));
            const Point q = add(a, scale(d, t));
            const double distance = dot(sub(q, p), sub(q, p));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = q;
            }
        }
        return best;
    }

    const char* axisName(int axis)
    {
        return 0 == axis ? "X" : (1 == axis ? "Y" : "Z");
    }

}

CutCounts cutAtPlane(std::vector<double>* positions, std::vector<uint32_t>* triangles,
    std::vector<uint32_t>* featureEdges, const SymmetryPlane& plane)
{
    CutCounts counts;
    std::vector<double>& p = *positions;
    const size_t vertexCount = p.size() / 3;
    const size_t triangleCount = triangles->size() / 3;

    std::vector<double> lengths;
    lengths.reserve(triangles->size());
    for (size_t t = 0; t < triangleCount; ++t) {
        for (int i = 0; i < 3; ++i) {
            const uint32_t a = (*triangles)[3 * t + i], b = (*triangles)[3 * t + (i + 1) % 3];
            lengths.push_back(length(sub(at(p, a), at(p, b))));
        }
    }
    double snap = 0.0;
    if (!lengths.empty()) {
        std::nth_element(lengths.begin(), lengths.begin() + lengths.size() / 2, lengths.end());
        snap = 1e-3 * lengths[lengths.size() / 2];
    }

    std::vector<double> distance(vertexCount);
    std::vector<int> sign(vertexCount);
    for (size_t v = 0; v < vertexCount; ++v) {
        double s = sideOf(p, v, plane);
        if (std::fabs(s) <= snap) {
            if (0.0 != s)
                ++counts.snappedVertexCount;
            p[3 * v + plane.axis] = plane.center;
            s = 0.0;
        }
        distance[v] = s;
        sign[v] = s > 0.0 ? 1 : (s < 0.0 ? -1 : 0);
    }

    std::unordered_map<uint64_t, uint32_t> crossing;
    const auto crossingPoint = [&](uint32_t a, uint32_t b) {
        const uint64_t key = edgeKey(a, b);
        auto found = crossing.find(key);
        if (found != crossing.end())
            return found->second;
        const uint32_t low = std::min(a, b), high = std::max(a, b);
        const double t = distance[low] / (distance[low] - distance[high]);
        const uint32_t index = (uint32_t)(p.size() / 3);
        for (int k = 0; k < 3; ++k)
            p.push_back(p[3 * low + k] + t * (p[3 * high + k] - p[3 * low + k]));
        p[3 * index + plane.axis] = plane.center;
        crossing.emplace(key, index);
        return index;
    };

    std::vector<uint32_t> kept;
    kept.reserve(triangles->size());
    for (size_t t = 0; t < triangleCount; ++t) {
        const uint32_t* corner = &(*triangles)[3 * t];
        int positive = 0, negative = 0;
        for (int i = 0; i < 3; ++i) {
            positive += sign[corner[i]] > 0;
            negative += sign[corner[i]] < 0;
        }
        if (0 == negative) {
            if (0 == positive)
                ++counts.inPlaneTriangleCount;
            else
                kept.insert(kept.end(), corner, corner + 3);
            continue;
        }
        if (0 == positive) {
            ++counts.droppedTriangleCount;
            continue;
        }
        ++counts.splitTriangleCount;
        uint32_t polygon[4];
        size_t size = 0;
        for (int i = 0; i < 3; ++i) {
            const uint32_t a = corner[i], b = corner[(i + 1) % 3];
            if (sign[a] >= 0)
                polygon[size++] = a;
            if (sign[a] * sign[b] < 0)
                polygon[size++] = crossingPoint(a, b);
        }
        for (size_t i = 1; i + 1 < size; ++i) {
            const uint32_t triangle[3] = { polygon[0], polygon[i], polygon[i + 1] };
            if (triangle[0] != triangle[1] && triangle[1] != triangle[2] && triangle[0] != triangle[2])
                kept.insert(kept.end(), triangle, triangle + 3);
        }
    }

    std::vector<uint32_t> features;
    for (size_t i = 0; i + 1 < featureEdges->size(); i += 2) {
        uint32_t a = (*featureEdges)[i], b = (*featureEdges)[i + 1];
        if (a >= vertexCount || b >= vertexCount) {
            ++counts.droppedFeatureEdgeCount;
            continue;
        }
        if (sign[a] < 0)
            std::swap(a, b);
        if (sign[a] < 0 || (sign[a] == 0 && sign[b] < 0)) {
            ++counts.droppedFeatureEdgeCount;
            continue;
        }
        if (sign[b] < 0) {
            auto found = crossing.find(edgeKey(a, b));
            if (found == crossing.end()) {
                ++counts.droppedFeatureEdgeCount;
                continue;
            }
            b = found->second;
        }
        features.push_back(a);
        features.push_back(b);
    }

    triangles->swap(kept);
    counts.keptTriangleCount = triangles->size() / 3;
    const std::vector<uint32_t> map = compact(positions, triangles);
    featureEdges->clear();
    for (size_t i = 0; i + 1 < features.size(); i += 2) {
        const uint32_t a = map[features[i]], b = map[features[i + 1]];
        if (a == std::numeric_limits<uint32_t>::max() || b == std::numeric_limits<uint32_t>::max()) {
            ++counts.droppedFeatureEdgeCount;
            continue;
        }
        featureEdges->push_back(a);
        featureEdges->push_back(b);
    }
    return counts;
}

std::vector<double> planeSection(const std::vector<double>& positions, const std::vector<uint32_t>& triangles,
    const SymmetryPlane& plane)
{
    std::unordered_map<uint64_t, int> uses;
    for (size_t t = 0; t + 2 < triangles.size(); t += 3)
        for (int i = 0; i < 3; ++i)
            ++uses[edgeKey(triangles[t + i], triangles[t + (i + 1) % 3])];
    std::vector<double> section;
    for (const auto& it : uses) {
        if (1 != it.second)
            continue;
        const uint32_t a = (uint32_t)(it.first >> 32), b = (uint32_t)(it.first & 0xffffffffu);
        if (positions[3 * a + plane.axis] != plane.center || positions[3 * b + plane.axis] != plane.center)
            continue;
        section.insert(section.end(), positions.begin() + 3 * a, positions.begin() + 3 * a + 3);
        section.insert(section.end(), positions.begin() + 3 * b, positions.begin() + 3 * b + 3);
    }
    return section;
}

SeamCounts mirrorAcross(RemeshMesh* mesh, const SymmetryPlane& plane, const std::vector<double>& section)
{
    SeamCounts counts;
    std::vector<double>& p = mesh->positions;
    const size_t vertexCount = p.size() / 3;
    std::vector<size_t> faceStart(mesh->faceSizes.size() + 1, 0);
    for (size_t f = 0; f < mesh->faceSizes.size(); ++f)
        faceStart[f + 1] = faceStart[f] + mesh->faceSizes[f];

    // Edges, border edges and the mean edge length.
    std::unordered_map<uint64_t, int> uses;
    double edgeSum = 0.0;
    for (size_t f = 0; f < mesh->faceSizes.size(); ++f) {
        const size_t size = mesh->faceSizes[f];
        for (size_t i = 0; i < size; ++i) {
            const uint32_t a = mesh->faceIndices[faceStart[f] + i], b = mesh->faceIndices[faceStart[f] + (i + 1) % size];
            if (1 == ++uses[edgeKey(a, b)])
                edgeSum += length(sub(at(p, a), at(p, b)));
        }
    }
    const double h = uses.empty() ? 1.0 : edgeSum / uses.size();
    std::vector<std::vector<uint32_t>> borderNeighbours(vertexCount);
    for (const auto& it : uses) {
        if (1 != it.second)
            continue;
        const uint32_t a = (uint32_t)(it.first >> 32), b = (uint32_t)(it.first & 0xffffffffu);
        borderNeighbours[a].push_back(b);
        borderNeighbours[b].push_back(a);
    }

    // Seam: border vertices on the plane, plus border arcs that leave it
    // briefly (cut a corner of the cross-section), pulled onto it.
    const double onTolerance = 0.05 * h;
    const size_t notchVertices = 8;
    const double notchDepth = 6.0 * h;
    const double cornerDepthMin = 0.25;
    const double cornerDepthMax = 3.0;
    std::vector<char> seam(vertexCount, 0);
    for (size_t v = 0; v < vertexCount; ++v)
        if (!borderNeighbours[v].empty() && std::fabs(sideOf(p, v, plane)) <= onTolerance)
            seam[v] = 1;
    std::vector<char> visited(vertexCount, 0);
    for (size_t start = 0; start < vertexCount; ++start) {
        if (seam[start] || visited[start] || borderNeighbours[start].empty())
            continue;
        std::vector<uint32_t> arc { (uint32_t)start };
        visited[start] = 1;
        bool touches = false;
        for (size_t i = 0; i < arc.size(); ++i) {
            for (uint32_t w : borderNeighbours[arc[i]]) {
                if (seam[w]) {
                    touches = true;
                } else if (!visited[w]) {
                    visited[w] = 1;
                    arc.push_back(w);
                }
            }
        }
        if (!touches || arc.size() > notchVertices)
            continue;
        double depth = 0.0;
        for (uint32_t v : arc)
            depth = std::max(depth, std::fabs(sideOf(p, v, plane)));
        if (depth > notchDepth)
            continue;
        ++counts.notchCount;
        counts.notchDepthMax = std::max(counts.notchDepthMax, depth / h);
        for (uint32_t v : arc)
            seam[v] = 2;
    }
    // Pulling a notch onto the plane drags its border vertices up to a few
    // edges along the axis; the inner vertices within two rings of them follow
    // part of the way (2/3, 1/3 of the nearest notch vertex's pull), as
    // proportional editing does, so the quads between do not fold over.
    {
        std::vector<std::vector<uint32_t>> ring(vertexCount);
        for (size_t f = 0; f < mesh->faceSizes.size(); ++f) {
            const size_t size = mesh->faceSizes[f];
            for (size_t i = 0; i < size; ++i) {
                const uint32_t a = mesh->faceIndices[faceStart[f] + i], b = mesh->faceIndices[faceStart[f] + (i + 1) % size];
                ring[a].push_back(b);
                ring[b].push_back(a);
            }
        }
        std::vector<double> pull(vertexCount, 0.0);
        std::vector<int> level(vertexCount, -1);
        std::vector<uint32_t> front;
        for (size_t v = 0; v < vertexCount; ++v) {
            if (2 == seam[v]) {
                pull[v] = -sideOf(p, v, plane);
                level[v] = 0;
                front.push_back((uint32_t)v);
            }
        }
        for (int step = 1; step <= 2; ++step) {
            std::vector<uint32_t> next;
            for (uint32_t v : front) {
                for (uint32_t w : ring[v]) {
                    if (seam[w] || (level[w] >= 0 && level[w] < step))
                        continue;
                    const double share = pull[v] * (3 - step) / (4.0 - step);
                    if (level[w] < 0) {
                        level[w] = step;
                        pull[w] = share;
                        next.push_back(w);
                    } else if (std::fabs(share) > std::fabs(pull[w])) {
                        pull[w] = share;
                    }
                }
            }
            front.swap(next);
        }
        for (size_t v = 0; v < vertexCount; ++v)
            if (level[v] > 0)
                p[3 * v + plane.axis] += plane.side * pull[v];
    }
    for (size_t v = 0; v < vertexCount; ++v) {
        if (seam[v]) {
            p[3 * v + plane.axis] = plane.center;
            ++counts.seamVertexCount;
        } else if (sideOf(p, v, plane) < 0.0) {
            // An inner vertex across the plane would overlap its mirror image.
            p[3 * v + plane.axis] = plane.center;
            ++counts.crossedVertexCount;
        }
    }

    // Faces with every corner on the seam would lie in the plane twice.
    std::vector<uint32_t> sizes, indices;
    for (size_t f = 0; f < mesh->faceSizes.size(); ++f) {
        bool flat = true;
        for (size_t i = faceStart[f]; i < faceStart[f + 1]; ++i)
            flat = flat && seam[mesh->faceIndices[i]];
        if (flat) {
            ++counts.flatFaceCount;
            continue;
        }
        sizes.push_back(mesh->faceSizes[f]);
        indices.insert(indices.end(), mesh->faceIndices.begin() + faceStart[f], mesh->faceIndices.begin() + faceStart[f + 1]);
    }
    mesh->faceSizes.swap(sizes);
    mesh->faceIndices.swap(indices);
    faceStart.assign(mesh->faceSizes.size() + 1, 0);
    for (size_t f = 0; f < mesh->faceSizes.size(); ++f)
        faceStart[f + 1] = faceStart[f] + mesh->faceSizes[f];

    std::vector<std::vector<uint32_t>> neighbours(vertexCount), facesOf(vertexCount);
    for (size_t f = 0; f < mesh->faceSizes.size(); ++f) {
        const size_t size = mesh->faceSizes[f];
        for (size_t i = 0; i < size; ++i) {
            const uint32_t a = mesh->faceIndices[faceStart[f] + i], b = mesh->faceIndices[faceStart[f] + (i + 1) % size];
            neighbours[a].push_back(b);
            neighbours[b].push_back(a);
            facesOf[a].push_back((uint32_t)f);
        }
    }
    for (auto& n : neighbours) {
        std::sort(n.begin(), n.end());
        n.erase(std::unique(n.begin(), n.end()), n.end());
    }
    const auto foldedAround = [&](uint32_t v) {
        for (uint32_t f : facesOf[v])
            if (folded(p, &mesh->faceIndices[faceStart[f]], mesh->faceSizes[f]))
                return true;
        return false;
    };

    // Corners: a seam edge that cuts a corner of the cross-section (a tip
    // such as a nose, or a notch pulled onto the plane straight along the
    // axis) leaves the corner off the result.  The nearer end of that edge
    // moves onto the corner, unless that folds a face; it then stays there.
    std::vector<char> pinned(vertexCount, 0);
    if (!section.empty()) {
        std::vector<std::pair<uint32_t, uint32_t>> seamEdges;
        for (size_t v = 0; v < vertexCount; ++v)
            for (uint32_t w : neighbours[v])
                if (seam[v] && seam[w] && v < w)
                    seamEdges.push_back({ (uint32_t)v, w });
        std::vector<Point> corners;
        for (size_t i = 0; i < section.size(); i += 3)
            corners.push_back({ section[i], section[i + 1], section[i + 2] });
        std::sort(corners.begin(), corners.end(), [](const Point& a, const Point& b) {
            return a.x != b.x ? a.x < b.x : (a.y != b.y ? a.y < b.y : a.z < b.z);
        });
        corners.erase(std::unique(corners.begin(), corners.end(), [](const Point& a, const Point& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }),
            corners.end());
        struct Cut {
            double depth;
            size_t corner, edge;
        };
        std::vector<Cut> cuts;
        for (size_t c = 0; c < corners.size(); ++c) {
            double best = std::numeric_limits<double>::max();
            size_t bestEdge = 0;
            for (size_t e = 0; e < seamEdges.size(); ++e) {
                const Point a = at(p, seamEdges[e].first);
                const Point d = sub(at(p, seamEdges[e].second), a);
                const double denominator = dot(d, d);
                double t = denominator > 0.0 ? dot(sub(corners[c], a), d) / denominator : 0.0;
                t = std::min(1.0, std::max(0.0, t));
                const double distance = length(sub(add(a, scale(d, t)), corners[c]));
                if (distance < best) {
                    best = distance;
                    bestEdge = e;
                }
            }
            if (best > cornerDepthMin * h && best < cornerDepthMax * h)
                cuts.push_back({ best, c, bestEdge });
        }
        std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.depth > b.depth; });
        for (const Cut& cut : cuts) {
            uint32_t ends[2] = { seamEdges[cut.edge].first, seamEdges[cut.edge].second };
            if (length(sub(at(p, ends[1]), corners[cut.corner])) < length(sub(at(p, ends[0]), corners[cut.corner])))
                std::swap(ends[0], ends[1]);
            for (uint32_t v : ends) {
                if (pinned[v])
                    continue;
                const Point before = at(p, v);
                const bool wasFoldedHere = foldedAround(v);
                p[3 * v] = corners[cut.corner].x;
                p[3 * v + 1] = corners[cut.corner].y;
                p[3 * v + 2] = corners[cut.corner].z;
                if (!wasFoldedHere && foldedAround(v)) {
                    p[3 * v] = before.x;
                    p[3 * v + 1] = before.y;
                    p[3 * v + 2] = before.z;
                    continue;
                }
                pinned[v] = 1;
                ++counts.pinnedCornerCount;
                break;
            }
        }
    }

    // Straighten: a seam vertex with two seam neighbours and one across
    // slides along the cross-section towards the foot of the one across,
    // blended with the middle of its seam neighbours; a move that folds a
    // face is taken back.
    struct Mover {
        uint32_t vertex, across, along[2];
    };
    std::vector<Mover> movers;
    for (size_t v = 0; v < vertexCount; ++v) {
        if (!seam[v] || pinned[v])
            continue;
        std::vector<uint32_t> along, across;
        for (uint32_t w : neighbours[v])
            (seam[w] ? along : across).push_back(w);
        if (2 == along.size() && 1 == across.size())
            movers.push_back({ (uint32_t)v, across[0], { along[0], along[1] } });
    }
    std::vector<char> wasFolded(movers.size());
    for (size_t i = 0; i < movers.size(); ++i)
        wasFolded[i] = foldedAround(movers[i].vertex);
    const int iterations = 20;
    const double weight = 0.8;
    std::vector<char> moved(movers.size(), 0);
    if (!section.empty()) {
        for (int iteration = 0; iteration < iterations; ++iteration) {
            std::vector<Point> before(movers.size());
            for (size_t i = 0; i < movers.size(); ++i) {
                const Mover& m = movers[i];
                before[i] = at(p, m.vertex);
                Point foot = at(p, m.across);
                (&foot.x)[plane.axis] = plane.center;
                const Point middle = scale(add(at(p, m.along[0]), at(p, m.along[1])), 0.5);
                Point target = closestOnSection(add(scale(foot, weight), scale(middle, 1.0 - weight)), section);
                (&target.x)[plane.axis] = plane.center;
                p[3 * m.vertex] = target.x;
                p[3 * m.vertex + 1] = target.y;
                p[3 * m.vertex + 2] = target.z;
            }
            for (int guard = 0; guard < 5; ++guard) {
                bool reverted = false;
                for (size_t i = 0; i < movers.size(); ++i) {
                    const Mover& m = movers[i];
                    const Point now = at(p, m.vertex);
                    if (now.x == before[i].x && now.y == before[i].y && now.z == before[i].z)
                        continue;
                    if (!wasFolded[i] && foldedAround(m.vertex)) {
                        p[3 * m.vertex] = before[i].x;
                        p[3 * m.vertex + 1] = before[i].y;
                        p[3 * m.vertex + 2] = before[i].z;
                        reverted = true;
                    }
                }
                if (!reverted)
                    break;
            }
            for (size_t i = 0; i < movers.size(); ++i) {
                const Point now = at(p, movers[i].vertex);
                if (now.x != before[i].x || now.y != before[i].y || now.z != before[i].z)
                    moved[i] = 1;
            }
        }
    }
    counts.straightenedVertexCount = (size_t)std::count(moved.begin(), moved.end(), 1);

    // The mirror copy, sharing the seam vertices.
    std::vector<uint32_t> image(vertexCount);
    for (size_t v = 0; v < vertexCount; ++v) {
        if (seam[v]) {
            image[v] = (uint32_t)v;
            continue;
        }
        image[v] = (uint32_t)(p.size() / 3);
        const double x = p[3 * v + plane.axis];
        p.insert(p.end(), p.begin() + 3 * v, p.begin() + 3 * v + 3);
        p[p.size() - 3 + plane.axis] = 2.0 * plane.center - x;
    }
    const size_t faceCount = mesh->faceSizes.size();
    for (size_t f = 0; f < faceCount; ++f) {
        const size_t size = mesh->faceSizes[f];
        mesh->faceSizes.push_back((uint32_t)size);
        for (size_t i = 0; i < size; ++i)
            mesh->faceIndices.push_back(image[mesh->faceIndices[faceStart[f] + size - 1 - i]]);
    }
    compact(&p, &mesh->faceIndices);

    // Holes left on the plane: border edges with both ends on it.
    uses.clear();
    size_t corner = 0;
    for (size_t f = 0; f < mesh->faceSizes.size(); ++f) {
        const size_t size = mesh->faceSizes[f];
        for (size_t i = 0; i < size; ++i)
            ++uses[edgeKey(mesh->faceIndices[corner + i], mesh->faceIndices[corner + (i + 1) % size])];
        corner += size;
    }
    for (const auto& it : uses) {
        const uint32_t a = (uint32_t)(it.first >> 32), b = (uint32_t)(it.first & 0xffffffffu);
        if (1 == it.second && p[3 * a + plane.axis] == plane.center && p[3 * b + plane.axis] == plane.center)
            ++counts.openSeamEdgeCount;
    }
    return counts;
}

RemeshResult remeshSymmetric(const RemeshRequest& request, const RemeshCallbacks& callbacks)
{
    const auto start = std::chrono::steady_clock::now();
    const RemeshParameters& parameters = request.parameters;
    const auto fail = [&](RemeshStatus status, const std::string& message) {
        RemeshResult result;
        result.parameters = parameters;
        result.status = status;
        result.message = message;
        result.report.inputVertexCount = request.positions.size() / 3;
        result.report.inputTriangleCount = request.triangles.size() / 3;
        result.report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return result;
    };
    if (parameters.symmetryAxes > 7)
        return fail(RemeshStatus::InvalidInput, "symmetry axes must be a combination of X, Y and Z");
    if (request.positions.empty() || request.triangles.empty())
        return fail(RemeshStatus::InvalidInput, "symmetry needs the input mesh, not only an input cache");
    for (uint32_t v : request.triangles)
        if (v >= request.positions.size() / 3)
            return fail(RemeshStatus::InvalidInput, "triangle refers to a vertex that does not exist");

    std::vector<SymmetryPlane> planes;
    for (int axis = 0; axis < 3; ++axis) {
        if (parameters.symmetryAxes & (1u << axis))
            planes.push_back({ axis, parameters.symmetryCenter[axis], (parameters.symmetryKeepNegative & (1u << axis)) ? -1 : 1 });
    }

    RemeshRequest half = request;
    half.parameters.symmetryAxes = 0;
    half.inputCachePath.clear();
    half.inputCacheTag.clear();
    std::vector<CutCounts> cuts;
    for (const SymmetryPlane& plane : planes) {
        cuts.push_back(cutAtPlane(&half.positions, &half.triangles, &half.featureEdges, plane));
        const CutCounts& cut = cuts.back();
        const std::string side = std::string(plane.side > 0 ? "+" : "-") + axisName(plane.axis);
        if (0 == cut.keptTriangleCount)
            return fail(RemeshStatus::InvalidInput, "nothing of the mesh is on the " + side + " side of the " + axisName(plane.axis) + " mirror plane");
        if (0 == cut.splitTriangleCount && 0 == cut.droppedTriangleCount && 0 == cut.inPlaneTriangleCount) {
            bool touches = false;
            for (size_t v = 0; v < half.positions.size() / 3 && !touches; ++v)
                touches = half.positions[3 * v + plane.axis] == plane.center;
            if (!touches)
                return fail(RemeshStatus::InvalidInput, std::string("the mesh does not reach the ") + axisName(plane.axis) + " mirror plane, so mirroring would make a separate copy; check the object's origin");
        }
    }
    const uint32_t divisor = 1u << planes.size();
    half.parameters.targetQuadCount = std::max<uint32_t>(1, (parameters.targetQuadCount + divisor / 2) / divisor);
    half.parameters.lostPartsReferenceTarget = parameters.lostPartsReferenceTarget
        ? std::max<uint32_t>(1, (parameters.lostPartsReferenceTarget + divisor / 2) / divisor)
        : 0;
    std::vector<std::vector<double>> sections;
    for (const SymmetryPlane& plane : planes)
        sections.push_back(planeSection(half.positions, half.triangles, plane));

    RemeshResult result = remeshWithPreview(half, callbacks, nullptr);
    result.parameters = parameters;
    QualityReport& report = result.report;
    report.inputVertexCount = request.positions.size() / 3;
    report.inputTriangleCount = request.triangles.size() / 3;
    report.inputFeatureEdgeCount = request.featureEdges.size() / 2;
    report.symmetryAxes = parameters.symmetryAxes;
    for (const CutCounts& cut : cuts) {
        report.symmetryInPlaneTriangleCount += cut.inPlaneTriangleCount;
        report.symmetryDroppedFeatureEdgeCount += cut.droppedFeatureEdgeCount;
    }
    if (RemeshStatus::Ok != result.status) {
        report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return result;
    }

    // Mirror back, last plane first; the cross-sections of the planes not
    // yet mirrored get their mirror images too.
    for (size_t k = planes.size(); k-- > 0;) {
        const SeamCounts seam = mirrorAcross(&result.mesh, planes[k], sections[k]);
        for (size_t j = 0; j < k; ++j) {
            std::vector<double>& other = sections[j];
            const size_t count = other.size();
            for (size_t i = 0; i < count; i += 3) {
                other.push_back(other[i]);
                other.push_back(other[i + 1]);
                other.push_back(other[i + 2]);
                double& x = other[other.size() - 3 + planes[k].axis];
                x = 2.0 * planes[k].center - x;
            }
        }
        report.symmetrySeamVertexCount += seam.seamVertexCount;
        report.symmetryNotchCount += seam.notchCount;
        report.symmetryNotchDepthMax = std::max(report.symmetryNotchDepthMax, seam.notchDepthMax);
        report.symmetryCrossedVertexCount += seam.crossedVertexCount;
        report.symmetryFlatFaceCount += seam.flatFaceCount;
        report.symmetryPinnedCornerCount += seam.pinnedCornerCount;
        report.symmetryStraightenedVertexCount += seam.straightenedVertexCount;
        report.symmetryOpenSeamEdgeCount += seam.openSeamEdgeCount;
    }

    // The mesh counts of the whole result.
    report.vertexCount = report.faceCount = report.quadCount = report.nonQuadCount = 0;
    report.nonManifoldEdgeCount = report.boundaryEdgeCount = report.orientationFlipCount = 0;
    report.foldedFaceCount = report.degenerateFaceCount = report.twistedQuadCount = 0;
    report.foldedOrTwistedFaceCount = report.creaseDiagonalQuadCount = 0;
    report.componentCount = report.boundaryLoopCount = report.nonManifoldVertexCount = 0;
    measureMesh(result.mesh, &report);
    discountCreaseDiagonalQuads(request.positions, request.triangles, result.mesh, &report);
    if (report.symmetryOpenSeamEdgeCount > 0) {
        report.warnings.push_back("The mirrored halves did not join everywhere: "
            + std::to_string(report.symmetryOpenSeamEdgeCount)
            + (1 == report.symmetryOpenSeamEdgeCount ? " open edge" : " open edges")
            + " on the mirror plane. A different Target Quads usually closes it");
    }
    report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}

}
