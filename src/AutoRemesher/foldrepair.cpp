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
#include <AutoRemesher/FoldRepair>
#include <AutoRemesher/Untangler>
#include <AutoRemesher/Vector2>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>
#include <tbb/task_arena.h>
#include <unordered_map>
#include <utility>

namespace AutoRemesher {
namespace {
    typedef std::array<size_t, 3> Face;

    // What stays fixed while only positions change: corners, edges and the
    // adjacency between them.
    struct Topology {
        struct Edge {
            size_t a, b;
            size_t count;
            // The first two triangles on the edge.
            size_t faces[2];
        };

        size_t vertexCount = 0;
        std::vector<Face> faces;
        std::vector<Edge> edges;
        // Edge of each triangle side, side i running from corner i to i + 1.
        std::vector<size_t> faceEdges;
        // Compressed rows: triangles of a vertex, neighbours of a vertex,
        // triangles across the edges of a triangle.
        std::vector<size_t> vertexFaceStart, vertexFaces;
        std::vector<size_t> vertexVertexStart, vertexVertices, vertexEdges;
        std::vector<size_t> faceFaceStart, faceFaces;

        Topology(size_t vertices, std::vector<Face> triangles)
            : vertexCount(vertices)
            , faces(std::move(triangles))
        {
            struct Side {
                size_t low, high, face, corner;
                bool operator<(const Side& other) const
                {
                    return low != other.low ? low < other.low : (high != other.high ? high < other.high : face < other.face);
                }
            };
            // Sorted by bucketing on the low end, then each short bucket.
            std::vector<size_t> bucketStart(vertexCount + 1, 0);
            for (const Face& face : faces)
                for (size_t i = 0; i < 3; ++i)
                    ++bucketStart[std::min(face[i], face[(i + 1) % 3]) + 1];
            std::partial_sum(bucketStart.begin(), bucketStart.end(), bucketStart.begin());
            std::vector<Side> sides(faces.size() * 3);
            {
                std::vector<size_t> next(bucketStart.begin(), bucketStart.end() - 1);
                for (size_t f = 0; f < faces.size(); ++f)
                    for (size_t i = 0; i < 3; ++i) {
                        const size_t a = faces[f][i], b = faces[f][(i + 1) % 3];
                        sides[next[std::min(a, b)]++] = { std::min(a, b), std::max(a, b), f, i };
                    }
            }
            tbb::parallel_for(tbb::blocked_range<size_t>(0, vertexCount), [&](const tbb::blocked_range<size_t>& range) {
                for (size_t v = range.begin(); v != range.end(); ++v)
                    std::sort(sides.begin() + bucketStart[v], sides.begin() + bucketStart[v + 1]);
            });
            faceEdges.assign(faces.size() * 3, 0);
            std::vector<std::pair<size_t, size_t>> faceLinks;
            faceLinks.reserve(sides.size());
            edges.reserve(sides.size() / 2 + 1);
            for (size_t i = 0; i < sides.size();) {
                size_t j = i + 1;
                while (j < sides.size() && sides[j].low == sides[i].low && sides[j].high == sides[i].high)
                    ++j;
                Edge edge { sides[i].low, sides[i].high, j - i, { sides[i].face, j - i > 1 ? sides[i + 1].face : sides[i].face } };
                for (size_t k = i; k < j; ++k) {
                    faceEdges[3 * sides[k].face + sides[k].corner] = edges.size();
                    for (size_t l = i; l < j; ++l)
                        if (l != k && sides[l].face != sides[k].face)
                            faceLinks.push_back({ sides[k].face, sides[l].face });
                }
                edges.push_back(edge);
                i = j;
            }
            // Bucketed by the first triangle, then each short row sorted.
            faceFaceStart.assign(faces.size() + 1, 0);
            for (const auto& link : faceLinks)
                ++faceFaceStart[link.first + 1];
            std::partial_sum(faceFaceStart.begin(), faceFaceStart.end(), faceFaceStart.begin());
            std::vector<size_t> rows(faceLinks.size());
            {
                std::vector<size_t> next(faceFaceStart.begin(), faceFaceStart.end() - 1);
                for (const auto& link : faceLinks)
                    rows[next[link.first]++] = link.second;
            }
            std::vector<size_t> rowLength(faces.size());
            tbb::parallel_for(tbb::blocked_range<size_t>(0, faces.size()), [&](const tbb::blocked_range<size_t>& range) {
                for (size_t f = range.begin(); f != range.end(); ++f) {
                    auto begin = rows.begin() + faceFaceStart[f], end = rows.begin() + faceFaceStart[f + 1];
                    std::sort(begin, end);
                    rowLength[f] = std::unique(begin, end) - begin;
                }
            });
            std::vector<size_t> compact(faces.size() + 1, 0);
            for (size_t f = 0; f < faces.size(); ++f)
                compact[f + 1] = compact[f] + rowLength[f];
            faceFaces.resize(compact.back());
            for (size_t f = 0; f < faces.size(); ++f)
                std::copy(rows.begin() + faceFaceStart[f], rows.begin() + faceFaceStart[f] + rowLength[f], faceFaces.begin() + compact[f]);
            faceFaceStart.swap(compact);

            vertexFaceStart.assign(vertexCount + 1, 0);
            for (const Face& face : faces)
                for (size_t v : face)
                    ++vertexFaceStart[v + 1];
            std::partial_sum(vertexFaceStart.begin(), vertexFaceStart.end(), vertexFaceStart.begin());
            vertexFaces.resize(faces.size() * 3);
            std::vector<size_t> fill(vertexFaceStart.begin(), vertexFaceStart.end() - 1);
            for (size_t f = 0; f < faces.size(); ++f)
                for (size_t v : faces[f])
                    vertexFaces[fill[v]++] = f;

            vertexVertexStart.assign(vertexCount + 1, 0);
            for (const Edge& edge : edges) {
                ++vertexVertexStart[edge.a + 1];
                ++vertexVertexStart[edge.b + 1];
            }
            std::partial_sum(vertexVertexStart.begin(), vertexVertexStart.end(), vertexVertexStart.begin());
            vertexVertices.resize(edges.size() * 2);
            vertexEdges.resize(edges.size() * 2);
            fill.assign(vertexVertexStart.begin(), vertexVertexStart.end() - 1);
            for (size_t e = 0; e < edges.size(); ++e) {
                const Edge& edge = edges[e];
                vertexEdges[fill[edge.a]] = e;
                vertexVertices[fill[edge.a]++] = edge.b;
                vertexEdges[fill[edge.b]] = e;
                vertexVertices[fill[edge.b]++] = edge.a;
            }
        }
    };

    // Per triangle: area vector (half the cross product), unit normal (zero
    // for a degenerate one), centroid, and shape: 4 sqrt(3) area over the
    // sum of the squared sides, 1 for an equilateral triangle and 0 for a
    // degenerate one.
    struct Geometry {
        std::vector<Vector3> areas, normals, centroids;
        std::vector<double> shapes;

        Geometry(const std::vector<Vector3>& vertices, const std::vector<Face>& faces)
            : areas(faces.size())
            , normals(faces.size())
            , centroids(faces.size())
            , shapes(faces.size())
        {
            tbb::parallel_for(tbb::blocked_range<size_t>(0, faces.size()), [&](const tbb::blocked_range<size_t>& range) {
                for (size_t f = range.begin(); f != range.end(); ++f) {
                    const Vector3& a = vertices[faces[f][0]];
                    const Vector3& b = vertices[faces[f][1]];
                    const Vector3& c = vertices[faces[f][2]];
                    areas[f] = 0.5 * Vector3::crossProduct(b - a, c - a);
                    const double length = areas[f].length();
                    normals[f] = length > 0.0 ? areas[f] / length : Vector3();
                    centroids[f] = (a + b + c) / 3.0;
                    const double sides = (b - a).lengthSquared() + (c - b).lengthSquared() + (a - c).lengthSquared();
                    shapes[f] = sides > 0.0 ? 4.0 * std::sqrt(3.0) * length / sides : 0.0;
                }
            });
        }
    };

    // A set of indices that is emptied in O(1).
    struct Marks {
        std::vector<uint32_t> stamps;
        uint32_t current = 0;

        void reset(size_t size)
        {
            if (stamps.size() != size) {
                stamps.assign(size, 0);
                current = 0;
            }
            if (++current == 0) {
                std::fill(stamps.begin(), stamps.end(), 0);
                current = 1;
            }
        }
        bool has(size_t i) const { return stamps[i] == current; }
        void set(size_t i) { stamps[i] = current; }
    };

    // Degenerate triangles get a zero normal.
    bool hasNormal(const Vector3& normal)
    {
        return 0.0 != normal.x() || 0.0 != normal.y() || 0.0 != normal.z();
    }

    const double M_PI_VALUE = 3.14159265358979323846;

    struct DetectScratch {
        std::vector<double> distance;
        std::vector<size_t> touched;
    };

    // The sign test's flags of all triangles; or, with `focus`, only of the
    // triangles that have a centroid in `focus` within the radius or share
    // an edge with one there, the others keeping their flag from `previous`
    // (after a repair moved those triangles; `focus` holds their old and
    // new centroids).
    std::vector<char> signTest(const Topology& topology, const Geometry& geometry, double radius,
        const std::vector<Vector3>* focus, const std::vector<char>* previous)
    {
        const size_t faceCount = topology.faces.size();
        std::vector<char> folded = previous ? *previous : std::vector<char>(faceCount, 0);
        if (0 == faceCount || !(radius > 0.0))
            return folded;

        // Candidates first, from the Euclidean disc on a grid of cells half
        // a radius wide; then the geodesic test on those only.
        Vector3 low = geometry.centroids[0];
        for (const Vector3& c : geometry.centroids)
            for (size_t i = 0; i < 3; ++i)
                low[i] = std::min(low[i], c[i]);
        const int cellsPerRadius = 2;
        const double cellSize = radius / cellsPerRadius;
        const auto cellOf = [&](const Vector3& c, int64_t* cell) {
            for (size_t i = 0; i < 3; ++i)
                cell[i] = (int64_t)std::floor((c[i] - low[i]) / cellSize);
        };
        const auto keyOf = [](const int64_t* cell) {
            return ((uint64_t)(cell[0] & 0x1FFFFF) << 42) | ((uint64_t)(cell[1] & 0x1FFFFF) << 21) | (uint64_t)(cell[2] & 0x1FFFFF);
        };
        std::vector<std::pair<uint64_t, size_t>> keyed(faceCount);
        for (size_t f = 0; f < faceCount; ++f) {
            int64_t cell[3];
            cellOf(geometry.centroids[f], cell);
            keyed[f] = { keyOf(cell), f };
        }
        tbb::parallel_sort(keyed.begin(), keyed.end());
        // Cells in key order: their range in `keyed`, and a cone holding the
        // normals of their triangles (axis, half-angle).
        std::unordered_map<uint64_t, size_t> cells;
        std::vector<std::pair<size_t, size_t>> cellRanges;
        for (size_t i = 0; i < keyed.size();) {
            size_t j = i + 1;
            while (j < keyed.size() && keyed[j].first == keyed[i].first)
                ++j;
            cells[keyed[i].first] = cellRanges.size();
            cellRanges.push_back({ i, j });
            i = j;
        }
        std::vector<Vector3> cellAxes(cellRanges.size());
        std::vector<double> cellAngles(cellRanges.size(), 0.0);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, cellRanges.size()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t c = range.begin(); c != range.end(); ++c) {
                Vector3 sum;
                for (size_t k = cellRanges[c].first; k < cellRanges[c].second; ++k)
                    sum += geometry.normals[keyed[k].second];
                const double length = sum.length();
                if (!(length > 0.0)) {
                    cellAngles[c] = M_PI_VALUE;
                    continue;
                }
                cellAxes[c] = sum / length;
                double angle = 0.0;
                for (size_t k = cellRanges[c].first; k < cellRanges[c].second; ++k) {
                    const Vector3& n = geometry.normals[keyed[k].second];
                    if (hasNormal(n))
                        angle = std::max(angle, std::acos(std::max(-1.0, std::min(1.0, Vector3::dotProduct(n, cellAxes[c])))));
                }
                cellAngles[c] = angle;
            }
        });
        std::vector<double> cellCosines(cellRanges.size()), cellSines(cellRanges.size());
        for (size_t c = 0; c < cellRanges.size(); ++c) {
            cellCosines[c] = std::cos(cellAngles[c]);
            cellSines[c] = std::sin(cellAngles[c]);
        }
        // Each cell's neighbours, itself included.
        std::vector<std::vector<size_t>> cellNeighbours(cellRanges.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, cellRanges.size()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t c = range.begin(); c != range.end(); ++c) {
                int64_t cell[3];
                cellOf(geometry.centroids[keyed[cellRanges[c].first].second], cell);
                const int64_t reach = cellsPerRadius;
                for (int64_t dx = -reach; dx <= reach; ++dx)
                    for (int64_t dy = -reach; dy <= reach; ++dy)
                        for (int64_t dz = -reach; dz <= reach; ++dz) {
                            const int64_t neighbour[3] = { cell[0] + dx, cell[1] + dy, cell[2] + dz };
                            auto found = cells.find(keyOf(neighbour));
                            if (found != cells.end())
                                cellNeighbours[c].push_back(found->second);
                        }
            }
        });

        // A triangle is flagged only if its disc points against it by more
        // than rounding: the disc of a small closed part sums to zero.
        const double tolerance = 1e-9;
        // A hair wider than the radius, so the Euclidean disc holds every
        // triangle the geodesic one reaches despite rounding (path lengths
        // are sums of square roots).
        const double radiusSquared = radius * radius * (1.0 + 1e-9);
        // With a focus: the triangles to test, the others keep their flag.
        std::vector<char> tested(faceCount, focus ? 0 : 1);
        if (focus) {
            tbb::enumerable_thread_specific<std::vector<size_t>> nearLists;
            tbb::parallel_for(tbb::blocked_range<size_t>(0, focus->size()), [&](const tbb::blocked_range<size_t>& range) {
                std::vector<size_t>& list = nearLists.local();
                for (size_t i = range.begin(); i != range.end(); ++i) {
                    const Vector3& p = (*focus)[i];
                    int64_t cell[3];
                    cellOf(p, cell);
                    const int64_t reach = cellsPerRadius;
                    for (int64_t dx = -reach; dx <= reach; ++dx)
                        for (int64_t dy = -reach; dy <= reach; ++dy)
                            for (int64_t dz = -reach; dz <= reach; ++dz) {
                                const int64_t neighbour[3] = { cell[0] + dx, cell[1] + dy, cell[2] + dz };
                                const auto found = cells.find(keyOf(neighbour));
                                if (found == cells.end())
                                    continue;
                                for (size_t k = cellRanges[found->second].first; k < cellRanges[found->second].second; ++k) {
                                    const size_t g = keyed[k].second;
                                    if ((geometry.centroids[g] - p).lengthSquared() <= radius * radius * (1.0 + 1e-9))
                                        list.push_back(g);
                                }
                            }
                }
            });
            for (const std::vector<size_t>& list : nearLists)
                for (size_t f : list) {
                    tested[f] = 1;
                    for (size_t k = topology.faceFaceStart[f]; k < topology.faceFaceStart[f + 1]; ++k)
                        tested[topology.faceFaces[k]] = 1;
                }
            for (size_t f = 0; f < faceCount; ++f)
                if (tested[f])
                    folded[f] = 0;
        }
        std::vector<char> candidate(faceCount, 0);
        std::vector<double> bound(faceCount, 0.0);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, faceCount), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t f = range.begin(); f != range.end(); ++f) {
                if (!tested[f] || !hasNormal(geometry.normals[f]))
                    continue;
                const Vector3& c = geometry.centroids[f];
                int64_t cell[3];
                cellOf(c, cell);
                const size_t home = cells.find(keyOf(cell))->second;
                // A lower bound of n . (sum over the geodesic disc): the
                // geodesic disc lies in the Euclidean one plus the edge
                // neighbours and holds this triangle, so it gets at least
                // its own area and every negative term of those.  Only where
                // the bound is negative can the triangle be flagged, so
                // stacked layers (a pleat, the far side of a thin part) are
                // tested, not guessed.
                const Vector3& n = geometry.normals[f];
                double lower = Vector3::dotProduct(n, geometry.areas[f]);
                for (size_t a : cellNeighbours[home]) {
                    // A cell whose cone of normals lies within 90 degrees of
                    // this normal adds nothing negative: cos(angle to the
                    // axis + half-angle) > 0.
                    const double cosine = Vector3::dotProduct(n, cellAxes[a]);
                    const double sine = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
                    if (cellAngles[a] < 0.5 * M_PI_VALUE && cosine * cellCosines[a] - sine * cellSines[a] > 1e-9)
                        continue;
                    for (size_t k = cellRanges[a].first; k < cellRanges[a].second; ++k) {
                        const size_t g = keyed[k].second;
                        if (g != f && (geometry.centroids[g] - c).lengthSquared() <= radiusSquared)
                            lower += std::min(0.0, Vector3::dotProduct(n, geometry.areas[g]));
                    }
                }
                for (size_t k = topology.faceFaceStart[f]; k < topology.faceFaceStart[f + 1]; ++k) {
                    const size_t g = topology.faceFaces[k];
                    if ((geometry.centroids[g] - c).lengthSquared() > radiusSquared)
                        lower += std::min(0.0, Vector3::dotProduct(n, geometry.areas[g]));
                }
                bound[f] = lower;
                candidate[f] = lower < 0.0;
            }
        });

        tbb::enumerable_thread_specific<DetectScratch> scratch([&]() {
            DetectScratch s;
            s.distance.assign(faceCount, std::numeric_limits<double>::infinity());
            return s;
        });
        tbb::parallel_for(tbb::blocked_range<size_t>(0, faceCount), [&](const tbb::blocked_range<size_t>& range) {
            DetectScratch& s = scratch.local();
            typedef std::pair<double, size_t> Item;
            std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
            for (size_t start = range.begin(); start != range.end(); ++start) {
                if (!candidate[start])
                    continue;
                // The bound rises by the positive terms as the disc fills;
                // once it is above -tolerance * area so far the triangle
                // cannot be flagged.
                const Vector3& n = geometry.normals[start];
                double lower = bound[start];
                Vector3 sum;
                double mass = 0.0;
                s.distance[start] = 0.0;
                s.touched.push_back(start);
                heap.push({ 0.0, start });
                while (!heap.empty()) {
                    const Item item = heap.top();
                    heap.pop();
                    const size_t f = item.second;
                    if (item.first > s.distance[f])
                        continue;
                    sum += geometry.areas[f];
                    mass += geometry.areas[f].length();
                    if (f != start) {
                        lower += std::max(0.0, Vector3::dotProduct(n, geometry.areas[f]));
                        if (lower >= -tolerance * mass)
                            break;
                    }
                    for (size_t k = topology.faceFaceStart[f]; k < topology.faceFaceStart[f + 1]; ++k) {
                        const size_t g = topology.faceFaces[k];
                        const double next = item.first + (geometry.centroids[g] - geometry.centroids[f]).length();
                        // The triangles sharing an edge with the start always
                        // count, for triangles larger than the radius.
                        if ((next <= radius || f == start) && next < s.distance[g]) {
                            if (std::isinf(s.distance[g]))
                                s.touched.push_back(g);
                            s.distance[g] = next;
                            heap.push({ next, g });
                        }
                    }
                }
                for (size_t g : s.touched)
                    s.distance[g] = std::numeric_limits<double>::infinity();
                s.touched.clear();
                folded[start] = lower < -tolerance * mass && Vector3::dotProduct(n, sum) < -tolerance * mass;
                while (!heap.empty())
                    heap.pop();
            }
        });
        return folded;
    }

    std::vector<std::vector<size_t>> clustersOf(const Topology& topology, const std::vector<char>& folded);

    // The sign test's flags, less the spots that do not turn over: a fold
    // has an edge across which a flagged triangle faces away from its
    // neighbour by more than 90 degrees.  Smooth surface that bends back
    // within the radius (a vine or tube thinner than it, an overhang) fails
    // the sign test too, but has no such edge, and is not a fold.
    std::vector<char> detectFolds(const Topology& topology, const Geometry& geometry, double radius,
        const std::vector<Vector3>* focus = nullptr, const std::vector<char>* previous = nullptr)
    {
        std::vector<char> folded = signTest(topology, geometry, radius, focus, previous);
        for (const std::vector<size_t>& cluster : clustersOf(topology, folded)) {
            bool turned = false;
            for (size_t i = 0; i < cluster.size() && !turned; ++i) {
                const size_t f = cluster[i];
                if (!hasNormal(geometry.normals[f]))
                    continue;
                for (size_t k = topology.faceFaceStart[f]; k < topology.faceFaceStart[f + 1] && !turned; ++k) {
                    const size_t g = topology.faceFaces[k];
                    turned = hasNormal(geometry.normals[g]) && Vector3::dotProduct(geometry.normals[f], geometry.normals[g]) < 0.0;
                }
            }
            if (!turned)
                for (size_t f : cluster)
                    folded[f] = 0;
        }
        return folded;
    }

    std::vector<Face> toFaces(const std::vector<std::vector<size_t>>& triangles)
    {
        std::vector<Face> faces;
        faces.reserve(triangles.size());
        for (const auto& t : triangles)
            faces.push_back({ t[0], t[1], t[2] });
        return faces;
    }

    double signedArea(const Vector2& a, const Vector2& b, const Vector2& c)
    {
        return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
    }

    // Distance from p to triangle abc (Ericson, Real-Time Collision
    // Detection, 5.1.5).
    double pointTriangleDistance(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c)
    {
        const Vector3 ab = b - a, ac = c - a, ap = p - a;
        const double d1 = Vector3::dotProduct(ab, ap), d2 = Vector3::dotProduct(ac, ap);
        if (d1 <= 0.0 && d2 <= 0.0)
            return ap.length();
        const Vector3 bp = p - b;
        const double d3 = Vector3::dotProduct(ab, bp), d4 = Vector3::dotProduct(ac, bp);
        if (d3 >= 0.0 && d4 <= d3)
            return bp.length();
        const double vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
            return (p - (a + (d1 / (d1 - d3)) * ab)).length();
        const Vector3 cp = p - c;
        const double d5 = Vector3::dotProduct(ab, cp), d6 = Vector3::dotProduct(ac, cp);
        if (d6 >= 0.0 && d5 <= d6)
            return cp.length();
        const double vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
            return (p - (a + (d2 / (d2 - d6)) * ac)).length();
        const double va = d3 * d6 - d5 * d4;
        if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
            return (p - (b + ((d4 - d3) / ((d4 - d3) + (d5 - d6))) * (c - b))).length();
        const double denominator = 1.0 / (va + vb + vc);
        return (p - (a + ab * (vb * denominator) + ac * (vc * denominator))).length();
    }

    struct PatchResult {
        bool ok = false;
        std::vector<size_t> moved;
        std::vector<Vector3> positions;
        // Free and fixed vertices: two patches that share one are not moved
        // in the same pass.
        std::vector<size_t> touched;
        double offset = 0.0;
        // Spike tips lowered instead of an untangled patch.
        size_t spikes = 0;
    };

    struct PatchScratch {
        Marks freeVertices, fixedVertices, reached, patchFaces, patchEdges, regionVertices, lockKnown, lockValue, zone;
        std::vector<size_t> local;
        std::vector<double> distance;
    };

    class Repairer {
    public:
        Repairer(const Topology& topology, const FoldRepair::Options& options, double radius,
            const std::vector<Vector3>& original)
            : m_topology(topology)
            , m_options(options)
            , m_radius(radius)
            , m_original(original)
        {
        }

        // Whether triangle f is flat on its side of the edge it shares with
        // g: its other neighbours across edges face within half the crease
        // angle of it.  A hard-surface crease runs between flat sheets; the
        // crumpled skin around a sculpt fold has sharp edges everywhere.
        bool flatBeside(size_t f, size_t g, const Geometry& geometry) const
        {
            const Topology& t = m_topology;
            const double flatCosine = std::cos(0.5 * m_options.creaseDegrees * 3.14159265358979323846 / 180.0);
            for (size_t k = t.faceFaceStart[f]; k < t.faceFaceStart[f + 1]; ++k) {
                const size_t h = t.faceFaces[k];
                if (h == g)
                    continue;
                if (Vector3::dotProduct(geometry.normals[f], geometry.normals[h]) < flatCosine)
                    return false;
            }
            return true;
        }

        // An unflagged, well-shaped triangle that stands steep over the
        // chart.  Slivers do not count, their normals say little (a twist
        // shears triangles into slivers at any angle).
        bool steep(size_t f, const Geometry& geometry, const std::vector<char>& folded, const Vector3& normal) const
        {
            return !folded[f] && hasNormal(geometry.normals[f]) && geometry.shapes[f] >= 0.3
                && std::abs(Vector3::dotProduct(geometry.normals[f], normal)) < m_options.steepCosine;
        }

        bool boundaryVertex(size_t v) const
        {
            const Topology& t = m_topology;
            for (size_t k = t.vertexVertexStart[v]; k < t.vertexVertexStart[v + 1]; ++k)
                if (2 != t.edges[t.vertexEdges[k]].count)
                    return true;
            return false;
        }

        // A vertex stays fixed in the chart if it is on an open boundary or
        // a non-manifold edge, or, outside the zone around the fold, on a
        // crease that is not part of the fold: an edge whose triangles'
        // normals differ by more than the crease angle and less than the
        // fold angle, neither flagged nor turned over in the chart, both
        // flat beside it.
        bool lockedVertex(size_t v, const Geometry& geometry, const std::vector<char>& folded, const Vector3& normal, bool inZone) const
        {
            const Topology& t = m_topology;
            const double creaseCosine = std::cos(m_options.creaseDegrees * 3.14159265358979323846 / 180.0);
            const double foldCosine = std::cos(m_options.foldDegrees * 3.14159265358979323846 / 180.0);
            for (size_t k = t.vertexVertexStart[v]; k < t.vertexVertexStart[v + 1]; ++k) {
                const auto& edge = t.edges[t.vertexEdges[k]];
                if (2 != edge.count)
                    return true;
                const size_t f = edge.faces[0], g = edge.faces[1];
                if (folded[f] || folded[g] || !hasNormal(geometry.normals[f]) || !hasNormal(geometry.normals[g]))
                    continue;
                if (Vector3::dotProduct(geometry.normals[f], normal) < 0.0 || Vector3::dotProduct(geometry.normals[g], normal) < 0.0)
                    continue;
                const double cosine = Vector3::dotProduct(geometry.normals[f], geometry.normals[g]);
                if (!inZone && cosine < creaseCosine && cosine > foldCosine && flatBeside(f, g, geometry) && flatBeside(g, f, geometry))
                    return true;
            }
            return false;
        }

        PatchResult repairCluster(const std::vector<Vector3>& vertices, const Geometry& geometry,
            const std::vector<char>& folded, const std::vector<size_t>& seedFaces, PatchScratch& s) const
        {
            const Topology& t = m_topology;
            const size_t nv = t.vertexCount;
            PatchResult best;
            s.freeVertices.reset(nv);
            s.local.resize(nv);
            std::vector<size_t> seeds;
            for (size_t f : seedFaces)
                for (size_t v : t.faces[f])
                    if (!s.freeVertices.has(v)) {
                        s.freeVertices.set(v);
                        seeds.push_back(v);
                    }

            // The detector's mean normal around the cluster: triangles within
            // the radius of it over the surface.
            Vector3 nearNormal;
            {
                std::vector<size_t> reached = verticesWithin(vertices, seeds, m_radius, s);
                s.reached.reset(nv);
                for (size_t v : reached)
                    s.reached.set(v);
                s.patchFaces.reset(t.faces.size());
                for (size_t v : reached)
                    for (size_t k = t.vertexFaceStart[v]; k < t.vertexFaceStart[v + 1]; ++k) {
                        const size_t f = t.vertexFaces[k];
                        if (s.patchFaces.has(f))
                            continue;
                        s.patchFaces.set(f);
                        if (s.reached.has(t.faces[f][0]) && s.reached.has(t.faces[f][1]) && s.reached.has(t.faces[f][2]))
                            nearNormal += geometry.areas[f];
                    }
            }
            const Vector3 lockNormal = nearNormal.length() > 0.0 ? nearNormal.normalized() : Vector3();
            // Within half a radius of the fold no crease is locked: the
            // crumpled skin there is part of it.
            {
                const std::vector<size_t> zone = verticesWithin(vertices, seeds, 0.5 * m_radius, s);
                s.zone.reset(nv);
                for (size_t v : zone)
                    s.zone.set(v);
            }
            s.lockKnown.reset(nv);
            s.lockValue.reset(nv);
            const auto locked = [&](size_t v) {
                if (!s.lockKnown.has(v)) {
                    s.lockKnown.set(v);
                    if (lockedVertex(v, geometry, folded, lockNormal, s.zone.has(v)))
                        s.lockValue.set(v);
                }
                return s.lockValue.has(v);
            };
            s.freeVertices.reset(nv);
            std::vector<size_t> freeList;
            // The fold's own vertices are free unless on a boundary.
            for (size_t v : seeds)
                if (!boundaryVertex(v)) {
                    s.freeVertices.set(v);
                    freeList.push_back(v);
                }

            for (size_t ring = 1; ring <= m_options.maximumRings; ++ring) {
                // One more ring.
                const size_t freeBefore = freeList.size();
                for (size_t i = 0; i < freeBefore; ++i) {
                    const size_t v = freeList[i];
                    for (size_t k = t.vertexVertexStart[v]; k < t.vertexVertexStart[v + 1]; ++k) {
                        const size_t u = t.vertexVertices[k];
                        if (!s.freeVertices.has(u) && !locked(u)) {
                            s.freeVertices.set(u);
                            freeList.push_back(u);
                        }
                    }
                }
                if (freeList.empty()) {
                    break;
                }
                s.fixedVertices.reset(nv);
                std::vector<size_t> fixedList;
                for (size_t v : freeList)
                    for (size_t k = t.vertexVertexStart[v]; k < t.vertexVertexStart[v + 1]; ++k) {
                        const size_t u = t.vertexVertices[k];
                        if (!s.freeVertices.has(u) && !s.fixedVertices.has(u)) {
                            s.fixedVertices.set(u);
                            fixedList.push_back(u);
                        }
                    }
                std::sort(freeList.begin(), freeList.end());
                std::sort(fixedList.begin(), fixedList.end());

                // Patch: every triangle with a free corner.
                s.patchFaces.reset(t.faces.size());
                std::vector<size_t> patch;
                for (size_t v : freeList)
                    for (size_t k = t.vertexFaceStart[v]; k < t.vertexFaceStart[v + 1]; ++k) {
                        const size_t f = t.vertexFaces[k];
                        if (!s.patchFaces.has(f)) {
                            s.patchFaces.set(f);
                            patch.push_back(f);
                        }
                    }
                std::sort(patch.begin(), patch.end());

                // A disk on manifold edges: Euler characteristic 1.
                s.patchEdges.reset(t.edges.size());
                size_t edgeCount = 0;
                bool manifold = true;
                for (size_t f : patch)
                    for (size_t i = 0; i < 3; ++i) {
                        const size_t e = t.faceEdges[3 * f + i];
                        if (t.edges[e].count > 2)
                            manifold = false;
                        if (!s.patchEdges.has(e)) {
                            s.patchEdges.set(e);
                            ++edgeCount;
                        }
                    }
                if (!manifold) {
                    continue;
                }
                if ((long long)(freeList.size() + fixedList.size()) - (long long)edgeCount + (long long)patch.size() != 1) {
                    continue;
                }

                // A fold just outside joins the patch.
                bool grew = false;
                for (size_t v : fixedList)
                    for (size_t k = t.vertexFaceStart[v]; k < t.vertexFaceStart[v + 1]; ++k) {
                        const size_t f = t.vertexFaces[k];
                        if (s.patchFaces.has(f) || !folded[f])
                            continue;
                        for (size_t u : t.faces[f])
                            if (!s.freeVertices.has(u) && !locked(u)) {
                                s.freeVertices.set(u);
                                freeList.push_back(u);
                                grew = true;
                            }
                    }
                if (grew && ring < m_options.maximumRings)
                    continue;
                if (grew)
                    break;

                PatchResult result = solvePatch(vertices, geometry, folded, freeList, fixedList, patch, nearNormal, s);
                if (!result.ok)
                    continue;
                const std::pair<size_t, size_t> flags = flagsAround(vertices, result, freeList, s);
                // Fewer flags around the patch, none new outside it.
                if (flags.first == std::numeric_limits<size_t>::max())
                    continue;
                if (0 == flags.second) {
                    return result;
                }
                if (!best.ok)
                    best = std::move(result);
            }
            return best;
        }

        // Interior vertices within the radius of the cluster whose angle sum
        // is below spikeAngleSum: needles, a single vertex pulled out of the
        // surface.  Each tip, then all of them together, goes down to the
        // mean of its ring, not more than maximumSpikeDrop; kept as for a
        // patch if the detector then flags fewer triangles around it and
        // none it did not flag outside the tips' triangles.  A shape change,
        // so only on request.
        PatchResult flattenSpikes(const std::vector<Vector3>& vertices, const std::vector<size_t>& seedFaces, PatchScratch& s) const
        {
            const Topology& t = m_topology;
            PatchResult none;
            // Vertices within the radius of the spot: a fold on a needle's
            // flank need not touch its tip.
            std::vector<size_t> seeds;
            for (size_t f : seedFaces)
                for (size_t v : t.faces[f])
                    seeds.push_back(v);
            const std::vector<size_t> corners = verticesWithin(vertices, seeds, m_radius, s);
            std::vector<size_t> tips;
            std::vector<Vector3> lowered;
            for (size_t v : corners) {
                if (boundaryVertex(v))
                    continue;
                double angles = 0.0;
                for (size_t k = t.vertexFaceStart[v]; k < t.vertexFaceStart[v + 1]; ++k) {
                    const Face& face = t.faces[t.vertexFaces[k]];
                    const size_t i = face[0] == v ? 0 : (face[1] == v ? 1 : 2);
                    const Vector3 a = vertices[face[(i + 1) % 3]] - vertices[v], b = vertices[face[(i + 2) % 3]] - vertices[v];
                    const double la = a.length(), lb = b.length();
                    if (!(la > 0.0) || !(lb > 0.0)) {
                        angles = std::numeric_limits<double>::infinity();
                        break;
                    }
                    angles += std::acos(std::max(-1.0, std::min(1.0, Vector3::dotProduct(a, b) / (la * lb))));
                }
                if (!(angles < m_options.spikeAngleSum))
                    continue;
                Vector3 mean;
                const size_t ring = t.vertexVertexStart[v + 1] - t.vertexVertexStart[v];
                for (size_t k = t.vertexVertexStart[v]; k < t.vertexVertexStart[v + 1]; ++k)
                    mean += vertices[t.vertexVertices[k]];
                mean = mean / (double)ring;
                if ((mean - vertices[v]).length() > m_options.maximumSpikeDrop * m_radius)
                    continue;
                tips.push_back(v);
                lowered.push_back(mean);
            }
            if (tips.empty())
                return none;

            // Each tip alone, then all together, the most flags removed wins.
            std::vector<std::vector<size_t>> trials;
            for (size_t i = 0; i < tips.size(); ++i)
                trials.push_back({ i });
            if (tips.size() > 1) {
                trials.emplace_back();
                for (size_t i = 0; i < tips.size(); ++i)
                    trials.back().push_back(i);
            }
            PatchResult best;
            size_t bestRemoved = 0;
            for (const std::vector<size_t>& trial : trials) {
                PatchResult result;
                std::vector<size_t> freeList;
                for (size_t i : trial) {
                    result.moved.push_back(tips[i]);
                    result.positions.push_back(lowered[i]);
                    freeList.push_back(tips[i]);
                }
                // A tip next to another tip of the same trial would move on
                // a ring that moves too.
                bool adjacent = false;
                for (size_t i : trial)
                    for (size_t k = t.vertexVertexStart[tips[i]]; k < t.vertexVertexStart[tips[i] + 1]; ++k)
                        adjacent = adjacent || std::binary_search(freeList.begin(), freeList.end(), t.vertexVertices[k]);
                if (adjacent)
                    continue;
                s.patchFaces.reset(t.faces.size());
                std::vector<size_t> touched;
                for (size_t v : freeList) {
                    touched.push_back(v);
                    for (size_t k = t.vertexFaceStart[v]; k < t.vertexFaceStart[v + 1]; ++k)
                        s.patchFaces.set(t.vertexFaces[k]);
                    for (size_t k = t.vertexVertexStart[v]; k < t.vertexVertexStart[v + 1]; ++k)
                        touched.push_back(t.vertexVertices[k]);
                }
                const std::pair<size_t, size_t> flags = flagsAround(vertices, result, freeList, s);
                if (flags.first == std::numeric_limits<size_t>::max() || flags.first - flags.second <= bestRemoved)
                    continue;
                bestRemoved = flags.first - flags.second;
                std::sort(touched.begin(), touched.end());
                touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
                result.touched = touched;
                result.ok = true;
                result.spikes = trial.size();
                best = result;
            }
            return best;
        }

    private:
        // Vertices within `limit` of `sources` along edges (Dijkstra).
        std::vector<size_t> verticesWithin(const std::vector<Vector3>& vertices, const std::vector<size_t>& sources,
            double limit, PatchScratch& s) const
        {
            const Topology& t = m_topology;
            s.reached.reset(t.vertexCount);
            s.distance.resize(t.vertexCount);
            typedef std::pair<double, size_t> Item;
            std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
            std::vector<size_t> out;
            for (size_t v : sources) {
                if (!s.reached.has(v)) {
                    s.reached.set(v);
                    s.distance[v] = 0.0;
                    heap.push({ 0.0, v });
                }
            }
            while (!heap.empty()) {
                const Item item = heap.top();
                heap.pop();
                const size_t v = item.second;
                if (item.first > s.distance[v])
                    continue;
                out.push_back(v);
                for (size_t k = t.vertexVertexStart[v]; k < t.vertexVertexStart[v + 1]; ++k) {
                    const size_t u = t.vertexVertices[k];
                    const double next = item.first + (vertices[u] - vertices[v]).length();
                    if (next > limit)
                        continue;
                    if (!s.reached.has(u) || next < s.distance[u]) {
                        s.reached.set(u);
                        s.distance[u] = next;
                        heap.push({ next, u });
                    }
                }
            }
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return out;
        }

        PatchResult solvePatch(const std::vector<Vector3>& vertices, const Geometry& geometry,
            const std::vector<char>& folded, const std::vector<size_t>& freeList, const std::vector<size_t>& fixedList,
            const std::vector<size_t>& patch, const Vector3& nearNormal, PatchScratch& s) const
        {
            const Topology& t = m_topology;
            PatchResult result;
            Vector3 normal = nearNormal;
            double magnitude = 0.0;
            for (size_t f : patch) {
                normal += geometry.areas[f];
                magnitude += geometry.areas[f].length();
            }
            if (!(normal.length() > 1e-12 * magnitude))
                return result;
            normal.normalize();
            // The unfolded part must be close to a height field over the
            // chart: a patch that is mostly steep (round a spike's flank or
            // an edge-on flap) is left alone.  A few steep triangles are
            // fine, the far side of a crease the fold touches.
            {
                double steepArea = 0.0, area = 0.0;
                for (size_t f : patch) {
                    area += geometry.areas[f].length();
                    if (steep(f, geometry, folded, normal))
                        steepArea += geometry.areas[f].length();
                }
                if (steepArea > m_options.maximumSteepShare * area)
                    return result;
            }
            Vector3 e1 = Vector3::crossProduct(normal, std::abs(normal.x()) < 0.9 ? Vector3(1.0, 0.0, 0.0) : Vector3(0.0, 1.0, 0.0));
            e1.normalize();
            const Vector3 e2 = Vector3::crossProduct(normal, e1);

            // Chart of the current and of the original surface, in the
            // patch's own numbering.
            std::vector<size_t> patchVertices;
            patchVertices.reserve(freeList.size() + fixedList.size());
            std::merge(freeList.begin(), freeList.end(), fixedList.begin(), fixedList.end(), std::back_inserter(patchVertices));
            Vector3 origin;
            for (size_t v : patchVertices)
                origin += vertices[v];
            origin = origin / (double)patchVertices.size();
            const size_t n = patchVertices.size();
            std::vector<Vector2> uv(n), originalUv(n);
            std::vector<double> height(n), originalHeight(n);
            for (size_t i = 0; i < n; ++i) {
                const size_t v = patchVertices[i];
                s.local[v] = i;
                const Vector3 d = vertices[v] - origin, d0 = m_original[v] - origin;
                uv[i] = Vector2(Vector3::dotProduct(d, e1), Vector3::dotProduct(d, e2));
                height[i] = Vector3::dotProduct(d, normal);
                originalUv[i] = Vector2(Vector3::dotProduct(d0, e1), Vector3::dotProduct(d0, e2));
                originalHeight[i] = Vector3::dotProduct(d0, normal);
            }
            std::vector<long long> variable(n, -1);
            size_t variableCount = 0;
            for (size_t i = 0; i < n; ++i) {
                const size_t v = patchVertices[i];
                if (s.freeVertices.has(v))
                    variable[i] = (long long)variableCount++;
            }
            if (0 == variableCount)
                return result;

            std::vector<std::array<size_t, 3>> local(patch.size());
            std::vector<char> active(patch.size(), 0);
            std::vector<std::array<Vector2, 3>> targets(patch.size());
            double totalArea = 0.0;
            for (size_t k = 0; k < patch.size(); ++k) {
                const Face& face = t.faces[patch[k]];
                for (size_t i = 0; i < 3; ++i)
                    local[k][i] = s.local[face[i]];
                const double area3d = geometry.areas[patch[k]].length();
                if (!(area3d > 0.0))
                    continue;
                active[k] = 1;
                const Vector2 &a = uv[local[k][0]], &b = uv[local[k][1]], &c = uv[local[k][2]];
                if (signedArea(a, b, c) > 1e-6 * area3d) {
                    targets[k] = { a, b, c };
                } else {
                    // Folded or edge-on in the chart: its own 3D shape, laid
                    // flat with its first side along that side's direction
                    // in the chart.
                    const Vector3& p0 = vertices[face[0]];
                    const Vector3& p1 = vertices[face[1]];
                    const Vector3& p2 = vertices[face[2]];
                    const double side = (p1 - p0).length();
                    Vector2 direction = b - a;
                    if (!(direction.length() > 1e-9 * side))
                        direction = Vector2(1.0, 0.0);
                    direction.normalize();
                    const Vector2 across(-direction.y(), direction.x());
                    const double along = Vector3::dotProduct(p2 - p0, p1 - p0) / side;
                    const double up = 2.0 * area3d / side;
                    targets[k] = { a, a + side * direction, a + along * direction + up * across };
                }
                totalArea += signedArea(targets[k][0], targets[k][1], targets[k][2]);
            }
            if (!(totalArea > 0.0))
                return result;

            Untangler::Options untanglerOptions;
            untanglerOptions.alignment = 1.0;
            Untangler untangler(2 * variableCount, untanglerOptions);
            for (size_t k = 0; k < patch.size(); ++k) {
                if (!active[k])
                    continue;
                std::array<Untangler::Coordinate, 6> corners;
                for (size_t i = 0; i < 3; ++i) {
                    const size_t p = local[k][i];
                    for (size_t axis = 0; axis < 2; ++axis) {
                        Untangler::Coordinate& coordinate = corners[2 * i + axis];
                        if (variable[p] >= 0)
                            coordinate.terms.push_back({ 2 * (size_t)variable[p] + axis, 1.0 });
                        else
                            coordinate.constant = uv[p][axis];
                    }
                }
                const double weight = signedArea(targets[k][0], targets[k][1], targets[k][2]) / totalArea * (double)patch.size();
                untangler.addTriangle(targets[k], weight, corners);
            }
            std::vector<double> x(2 * variableCount);
            for (size_t i = 0; i < n; ++i)
                if (variable[i] >= 0) {
                    x[2 * variable[i]] = uv[i].x();
                    x[2 * variable[i] + 1] = uv[i].y();
                }
            Untangler::Stats stats;
            if (!untangler.untangle(&x, &stats)) {
                return result;
            }

            std::vector<Vector2> moved = uv;
            for (size_t i = 0; i < n; ++i)
                if (variable[i] >= 0)
                    moved[i] = Vector2(x[2 * variable[i]], x[2 * variable[i] + 1]);
            // Triangles of each patch vertex, in the patch.
            std::vector<std::vector<size_t>> around(n);
            for (size_t k = 0; k < patch.size(); ++k)
                if (active[k])
                    for (size_t i = 0; i < 3; ++i)
                        around[local[k][i]].push_back(k);
            const auto positive = [&](size_t i) {
                for (size_t k : around[i])
                    if (!(signedArea(moved[local[k][0]], moved[local[k][1]], moved[local[k][2]]) > 0.0))
                        return false;
                return true;
            };
            for (size_t k = 0; k < patch.size(); ++k)
                if (active[k] && !(signedArea(moved[local[k][0]], moved[local[k][1]], moved[local[k][2]]) > 0.0))
                    return result;
            // Each vertex back towards its old place as far as its triangles
            // stay positive, so that away from the fold vertices return home.
            for (size_t sweep = 0; sweep < 20; ++sweep) {
                bool any = false;
                for (size_t i = 0; i < n; ++i) {
                    if (variable[i] < 0)
                        continue;
                    const Vector2 delta = uv[i] - moved[i];
                    if (0.0 == delta.x() && 0.0 == delta.y())
                        continue;
                    const Vector2 before = moved[i];
                    for (double alpha : { 1.0, 0.5, 0.25, 0.125 }) {
                        moved[i] = alpha == 1.0 ? uv[i] : before + alpha * delta;
                        if (positive(i)) {
                            any = true;
                            break;
                        }
                        moved[i] = before;
                    }
                }
                if (!any)
                    break;
            }

            // Onto the original surface: the original triangle under the new
            // chart position that is positive in the original chart (the
            // unfolded sheet), the one nearest the vertex's own height.
            for (size_t i = 0; i < n; ++i) {
                if (variable[i] < 0 || (moved[i].x() == uv[i].x() && moved[i].y() == uv[i].y()))
                    continue;
                const Vector2& p = moved[i];
                double bestGap = std::numeric_limits<double>::infinity();
                Vector3 position;
                for (size_t k = 0; k < patch.size(); ++k) {
                    if (!active[k])
                        continue;
                    const Vector2 &a = originalUv[local[k][0]], &b = originalUv[local[k][1]], &c = originalUv[local[k][2]];
                    const double area = signedArea(a, b, c);
                    if (!(area > 0.0))
                        continue;
                    const double l1 = signedArea(a, p, c) / area, l2 = signedArea(a, b, p) / area, l0 = 1.0 - l1 - l2;
                    if (l0 < -1e-9 || l1 < -1e-9 || l2 < -1e-9)
                        continue;
                    const double h = l0 * originalHeight[local[k][0]] + l1 * originalHeight[local[k][1]] + l2 * originalHeight[local[k][2]];
                    const double gap = std::abs(h - height[i]);
                    if (gap < bestGap) {
                        bestGap = gap;
                        const Face& face = t.faces[patch[k]];
                        position = l0 * m_original[face[0]] + l1 * m_original[face[1]] + l2 * m_original[face[2]];
                    }
                }
                if (std::isinf(bestGap)) {
                    position = origin + p.x() * e1 + p.y() * e2 + height[i] * normal;
                    double offset = std::numeric_limits<double>::infinity();
                    for (size_t k = 0; k < patch.size(); ++k) {
                        const Face& face = t.faces[patch[k]];
                        offset = std::min(offset, pointTriangleDistance(position, m_original[face[0]], m_original[face[1]], m_original[face[2]]));
                    }
                    result.offset = std::max(result.offset, offset);
                }
                result.moved.push_back(patchVertices[i]);
                result.positions.push_back(position);
            }
            if (result.moved.empty() || result.offset > m_options.maximumOffset * m_radius)
                return result;

            // Every triangle faces the chart normal in 3D too.
            std::vector<Vector3> after(n);
            for (size_t i = 0; i < n; ++i)
                after[i] = vertices[patchVertices[i]];
            for (size_t m = 0; m < result.moved.size(); ++m)
                after[s.local[result.moved[m]]] = result.positions[m];
            for (size_t k = 0; k < patch.size(); ++k) {
                if (!active[k])
                    continue;
                const Vector3 area = Vector3::crossProduct(after[local[k][1]] - after[local[k][0]], after[local[k][2]] - after[local[k][0]]);
                if (!(Vector3::dotProduct(area, normal) > 0.0))
                    return result;
            }
            result.touched = patchVertices;
            result.ok = true;
            return result;
        }

        // Flags of the detector on the surface within twice the radius of
        // the free vertices, before and after the patch moves: the patch
        // fails (first = max) unless there are fewer and none new outside the
        // patch; otherwise (before, after).
        std::pair<size_t, size_t> flagsAround(const std::vector<Vector3>& vertices, const PatchResult& result,
            const std::vector<size_t>& freeList, PatchScratch& s) const
        {
            const Topology& t = m_topology;
            const std::vector<size_t> region = verticesWithin(vertices, freeList, 2.0 * m_radius, s);
            s.regionVertices.reset(t.vertexCount);
            for (size_t v : region)
                s.regionVertices.set(v);
            std::vector<size_t> regionFaces;
            for (size_t v : region)
                for (size_t k = t.vertexFaceStart[v]; k < t.vertexFaceStart[v + 1]; ++k) {
                    const size_t f = t.vertexFaces[k];
                    const Face& face = t.faces[f];
                    if (s.regionVertices.has(face[0]) && s.regionVertices.has(face[1]) && s.regionVertices.has(face[2]))
                        regionFaces.push_back(f);
                }
            std::sort(regionFaces.begin(), regionFaces.end());
            regionFaces.erase(std::unique(regionFaces.begin(), regionFaces.end()), regionFaces.end());
            for (size_t i = 0; i < region.size(); ++i)
                s.local[region[i]] = i;
            std::vector<Face> faces(regionFaces.size());
            for (size_t k = 0; k < regionFaces.size(); ++k)
                for (size_t i = 0; i < 3; ++i)
                    faces[k][i] = s.local[t.faces[regionFaces[k]][i]];
            std::vector<Vector3> before(region.size());
            for (size_t i = 0; i < region.size(); ++i)
                before[i] = vertices[region[i]];
            std::vector<Vector3> after = before;
            for (size_t m = 0; m < result.moved.size(); ++m)
                after[s.local[result.moved[m]]] = result.positions[m];
            const Topology sub(region.size(), faces);
            const std::vector<char> flagsBefore = detectFolds(sub, Geometry(before, sub.faces), m_radius);
            const std::vector<char> flagsAfter = detectFolds(sub, Geometry(after, sub.faces), m_radius);
            size_t countBefore = 0, countAfter = 0;
            for (size_t k = 0; k < faces.size(); ++k) {
                countBefore += flagsBefore[k];
                countAfter += flagsAfter[k];
                if (flagsAfter[k] && !flagsBefore[k] && !s.patchFaces.has(regionFaces[k]))
                    return { std::numeric_limits<size_t>::max(), 0 };
            }
            if (countAfter >= countBefore)
                return { std::numeric_limits<size_t>::max(), 0 };
            return { countBefore, countAfter };
        }

        const Topology& m_topology;
        const FoldRepair::Options& m_options;
        double m_radius;
        const std::vector<Vector3>& m_original;
    };

    // Clusters of flagged triangles that share a vertex, each sorted, in
    // the order of their first triangle.
    std::vector<std::vector<size_t>> clustersOf(const Topology& topology, const std::vector<char>& folded)
    {
        std::vector<size_t> parent(topology.vertexCount);
        std::iota(parent.begin(), parent.end(), (size_t)0);
        const auto root = [&](size_t v) {
            while (parent[v] != v)
                v = parent[v] = parent[parent[v]];
            return v;
        };
        for (size_t f = 0; f < folded.size(); ++f) {
            if (!folded[f])
                continue;
            const Face& face = topology.faces[f];
            for (size_t i = 1; i < 3; ++i) {
                const size_t a = root(face[0]), b = root(face[i]);
                if (a != b)
                    parent[std::max(a, b)] = std::min(a, b);
            }
        }
        std::unordered_map<size_t, size_t> index;
        std::vector<std::vector<size_t>> clusters;
        for (size_t f = 0; f < folded.size(); ++f) {
            if (!folded[f])
                continue;
            auto inserted = index.insert({ root(topology.faces[f][0]), clusters.size() });
            if (inserted.second)
                clusters.emplace_back();
            clusters[inserted.first->second].push_back(f);
        }
        return clusters;
    }
}

double FoldRepair::defaultRadius(const std::vector<Vector3>& vertices, const Options& options)
{
    if (options.radius > 0.0)
        return options.radius;
    if (vertices.empty())
        return 0.0;
    Vector3 low = vertices[0], high = vertices[0];
    for (const Vector3& v : vertices)
        for (size_t i = 0; i < 3; ++i) {
            low[i] = std::min(low[i], v[i]);
            high[i] = std::max(high[i], v[i]);
        }
    return options.radiusFraction * (high - low).length();
}

std::vector<char> FoldRepair::detect(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double radius)
{
    const Topology topology(vertices.size(), toFaces(triangles));
    return detectFolds(topology, Geometry(vertices, topology.faces), radius);
}

namespace {

    FoldRepair::Stats surveyWith(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles, double radius)
    {
        typedef FoldRepair::Stats Stats;
        const auto start = std::chrono::steady_clock::now();
        Stats stats;
        stats.radius = radius;
        if (triangles.empty() || !(stats.radius > 0.0))
            return stats;
        const Topology topology(vertices.size(), toFaces(triangles));
        const Geometry geometry(vertices, topology.faces);
        const std::vector<char> folded = detectFolds(topology, geometry, stats.radius);
        stats.foldedTriangles = stats.foldedTrianglesAfter = (size_t)std::count(folded.begin(), folded.end(), 1);
        stats.clusters = stats.clustersAfter = clustersOf(topology, folded).size();
        stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return stats;
    }

    // Whether a vertex marked in `moved` lies within reach of the cluster's
    // patches or of what they depend on: `rings` rings around it (patch,
    // margin, locks) or `limit` along edges (flags and mean normal).
    bool movedNear(const Topology& topology, const std::vector<Vector3>& vertices, const std::vector<char>& moved,
        const std::vector<size_t>& cluster, size_t rings, double limit)
    {
        std::vector<size_t> sources;
        for (size_t f : cluster)
            for (size_t v : topology.faces[f])
                sources.push_back(v);
        std::sort(sources.begin(), sources.end());
        sources.erase(std::unique(sources.begin(), sources.end()), sources.end());

        std::unordered_map<size_t, char> inRings;
        std::vector<size_t> frontier = sources;
        for (size_t v : frontier)
            inRings[v] = 1;
        for (size_t ring = 0; ring < rings; ++ring) {
            std::vector<size_t> next;
            for (size_t v : frontier)
                for (size_t k = topology.vertexVertexStart[v]; k < topology.vertexVertexStart[v + 1]; ++k)
                    if (inRings.emplace(topology.vertexVertices[k], 1).second)
                        next.push_back(topology.vertexVertices[k]);
            frontier.swap(next);
        }
        for (const auto& item : inRings)
            if (moved[item.first])
                return true;

        std::unordered_map<size_t, double> distance;
        typedef std::pair<double, size_t> Item;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
        for (size_t v : sources) {
            distance[v] = 0.0;
            heap.push({ 0.0, v });
        }
        while (!heap.empty()) {
            const Item item = heap.top();
            heap.pop();
            const size_t v = item.second;
            if (item.first > distance[v])
                continue;
            if (moved[v])
                return true;
            for (size_t k = topology.vertexVertexStart[v]; k < topology.vertexVertexStart[v + 1]; ++k) {
                const size_t u = topology.vertexVertices[k];
                const double d = item.first + (vertices[u] - vertices[v]).length();
                if (d > limit)
                    continue;
                const auto found = distance.find(u);
                if (found == distance.end() || d < found->second) {
                    distance[u] = d;
                    heap.push({ d, u });
                }
            }
        }
        return false;
    }

    FoldRepair::Stats repairWith(std::vector<Vector3>* vertices,
        const std::vector<std::vector<size_t>>& triangles, const FoldRepair::Options& options, double radius)
    {
        typedef FoldRepair::Stats Stats;
        const auto start = std::chrono::steady_clock::now();
        Stats stats;
        stats.radius = radius;
        if (triangles.empty() || !(stats.radius > 0.0))
            return stats;
        const Topology topology(vertices->size(), toFaces(triangles));
        const std::vector<Vector3> original = *vertices;
        std::vector<Vector3>& current = *vertices;
        const Repairer repairer(topology, options, stats.radius, original);

        std::vector<char> folded = detectFolds(topology, Geometry(current, topology.faces), stats.radius);
        std::vector<std::vector<size_t>> clusters = clustersOf(topology, folded);
        stats.foldedTriangles = (size_t)std::count(folded.begin(), folded.end(), 1);
        stats.clusters = clusters.size();
        tbb::enumerable_thread_specific<PatchScratch> scratch;
        // Clusters that failed in the last pass, and the vertices it moved: a
        // cluster that failed and has nothing moved near it would fail again.
        std::set<std::vector<size_t>> failed;
        std::vector<char> moved(topology.vertexCount, 0);
        // Spikes only once untangling alone moves nothing more, so a tip is
        // lowered only where no patch would do; then as many passes again.
        bool spikePhase = false;
        size_t passesLeft = options.passes;
        for (; passesLeft > 0 && !clusters.empty(); --passesLeft) {
            // Every cluster against the same positions, so the result does not
            // depend on the threads; then applied in cluster order, and a patch
            // that overlaps one applied before waits for the next pass.
            const Geometry geometry(current, topology.faces);
            std::vector<PatchResult> results(clusters.size());
            tbb::parallel_for(tbb::blocked_range<size_t>(0, clusters.size(), 1), [&](const tbb::blocked_range<size_t>& range) {
                // Isolated: while this thread waits inside the untangler's or the
                // detector's own parallel loops it must not pick up another
                // cluster, which would reuse its scratch.
                tbb::this_task_arena::isolate([&]() {
                    PatchScratch& s = scratch.local();
                    for (size_t k = range.begin(); k != range.end(); ++k) {
                        if (failed.count(clusters[k])
                            && !movedNear(topology, current, moved, clusters[k], options.maximumRings + 2, 3.0 * stats.radius))
                            continue;
                        results[k] = repairer.repairCluster(current, geometry, folded, clusters[k], s);
                        if (!results[k].ok && spikePhase)
                            results[k] = repairer.flattenSpikes(current, clusters[k], s);
                    }
                });
            });
            std::vector<char> used(topology.vertexCount, 0);
            std::fill(moved.begin(), moved.end(), 0);
            failed.clear();
            bool any = false;
            for (size_t k = 0; k < results.size(); ++k) {
                const PatchResult& result = results[k];
                if (!result.ok) {
                    failed.insert(clusters[k]);
                    continue;
                }
                bool overlaps = false;
                for (size_t v : result.touched)
                    overlaps = overlaps || used[v];
                if (overlaps)
                    continue;
                for (size_t v : result.touched)
                    used[v] = 1;
                for (size_t m = 0; m < result.moved.size(); ++m) {
                    if (result.spikes)
                        stats.maximumSpikeDrop = std::max(stats.maximumSpikeDrop, (result.positions[m] - original[result.moved[m]]).length());
                    current[result.moved[m]] = result.positions[m];
                    moved[result.moved[m]] = 1;
                }
                stats.maximumOffset = std::max(stats.maximumOffset, result.offset);
                if (result.spikes)
                    stats.flattenedSpikes += result.spikes;
                else
                    ++stats.repairedPatches;
                any = true;
            }
            if (!any) {
                if (!options.flattenSpikes || spikePhase)
                    break;
                spikePhase = true;
                passesLeft = options.passes + 1;
                failed.clear();
                continue;
            }
            // Flags change only near the moved triangles.
            std::vector<Vector3> focus;
            std::vector<char> movedFace(topology.faces.size(), 0);
            for (size_t v = 0; v < topology.vertexCount; ++v) {
                if (!moved[v])
                    continue;
                for (size_t k = topology.vertexFaceStart[v]; k < topology.vertexFaceStart[v + 1]; ++k)
                    movedFace[topology.vertexFaces[k]] = 1;
            }
            const Geometry after(current, topology.faces);
            for (size_t f = 0; f < topology.faces.size(); ++f)
                if (movedFace[f]) {
                    focus.push_back(geometry.centroids[f]);
                    focus.push_back(after.centroids[f]);
                }
            folded = detectFolds(topology, after, stats.radius, &focus, &folded);
            clusters = clustersOf(topology, folded);
        }
        stats.foldedTrianglesAfter = (size_t)std::count(folded.begin(), folded.end(), 1);
        stats.clustersAfter = clusters.size();
        for (size_t v = 0; v < current.size(); ++v) {
            const double move = (current[v] - original[v]).length();
            if (move > 0.0) {
                ++stats.movedVertices;
                stats.maximumMove = std::max(stats.maximumMove, move);
            }
        }
        // Both as fractions of the radius.
        stats.maximumOffset /= stats.radius;
        stats.maximumMove /= stats.radius;
        stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return stats;
    }

    // Parts that share no vertex, each its triangles in input order.
    std::vector<std::vector<size_t>> partsOf(size_t vertexCount, const std::vector<std::vector<size_t>>& triangles)
    {
        std::vector<size_t> parent(vertexCount);
        std::iota(parent.begin(), parent.end(), (size_t)0);
        const auto root = [&](size_t v) {
            while (parent[v] != v)
                v = parent[v] = parent[parent[v]];
            return v;
        };
        for (const std::vector<size_t>& triangle : triangles)
            for (size_t i = 1; i < triangle.size(); ++i) {
                const size_t a = root(triangle[0]), b = root(triangle[i]);
                if (a != b)
                    parent[std::max(a, b)] = std::min(a, b);
            }
        std::unordered_map<size_t, size_t> index;
        std::vector<std::vector<size_t>> parts;
        for (size_t t = 0; t < triangles.size(); ++t) {
            auto inserted = index.insert({ root(triangles[t][0]), parts.size() });
            if (inserted.second)
                parts.emplace_back();
            parts[inserted.first->second].push_back(t);
        }
        return parts;
    }

    // One part as a mesh of its own: its vertices in input order.
    struct Part {
        std::vector<size_t> vertices;
        std::vector<Vector3> positions;
        std::vector<std::vector<size_t>> triangles;
    };

    Part partOf(const std::vector<Vector3>& vertices, const std::vector<std::vector<size_t>>& triangles,
        const std::vector<size_t>& part)
    {
        Part p;
        for (size_t t : part)
            for (size_t v : triangles[t])
                p.vertices.push_back(v);
        std::sort(p.vertices.begin(), p.vertices.end());
        p.vertices.erase(std::unique(p.vertices.begin(), p.vertices.end()), p.vertices.end());
        std::unordered_map<size_t, size_t> local;
        for (size_t i = 0; i < p.vertices.size(); ++i) {
            local[p.vertices[i]] = i;
            p.positions.push_back(vertices[p.vertices[i]]);
        }
        for (size_t t : part) {
            std::vector<size_t> triangle;
            for (size_t v : triangles[t])
                triangle.push_back(local[v]);
            p.triangles.push_back(triangle);
        }
        return p;
    }

    void add(FoldRepair::Stats* total, const FoldRepair::Stats& part)
    {
        total->radius = std::max(total->radius, part.radius);
        total->foldedTriangles += part.foldedTriangles;
        total->clusters += part.clusters;
        total->repairedPatches += part.repairedPatches;
        total->foldedTrianglesAfter += part.foldedTrianglesAfter;
        total->clustersAfter += part.clustersAfter;
        total->movedVertices += part.movedVertices;
        total->maximumOffset = std::max(total->maximumOffset, part.maximumOffset);
        total->maximumMove = std::max(total->maximumMove, part.maximumMove);
        total->flattenedSpikes += part.flattenedSpikes;
        total->maximumSpikeDrop = std::max(total->maximumSpikeDrop, part.maximumSpikeDrop);
    }

    // Each part with its own radius, so that joining a long blade to a vine,
    // or a small part to a big one, does not change what is found on either.
    // A given radius applies to all; a single part is run in place.
    template <typename Run>
    FoldRepair::Stats byPart(const std::vector<Vector3>& vertices, const std::vector<std::vector<size_t>>& triangles,
        const FoldRepair::Options& options, Run run)
    {
        const auto start = std::chrono::steady_clock::now();
        FoldRepair::Stats total;
        const std::vector<std::vector<size_t>> parts = partsOf(vertices.size(), triangles);
        if (options.radius > 0.0 || parts.size() <= 1) {
            std::vector<Vector3> used;
            for (const std::vector<size_t>& triangle : triangles)
                for (size_t v : triangle)
                    used.push_back(vertices[v]);
            total = run(nullptr, FoldRepair::defaultRadius(used, options));
        } else {
            for (const std::vector<size_t>& part : parts) {
                Part p = partOf(vertices, triangles, part);
                add(&total, run(&p, FoldRepair::defaultRadius(p.positions, options)));
            }
        }
        total.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return total;
    }

}

FoldRepair::Stats FoldRepair::survey(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    const Options& options)
{
    return byPart(vertices, triangles, options, [&](Part* part, double radius) {
        return part ? surveyWith(part->positions, part->triangles, radius) : surveyWith(vertices, triangles, radius);
    });
}

FoldRepair::Stats FoldRepair::repair(std::vector<Vector3>* vertices,
    const std::vector<std::vector<size_t>>& triangles,
    const Options& options)
{
    return byPart(*vertices, triangles, options, [&](Part* part, double radius) {
        if (!part)
            return repairWith(vertices, triangles, options, radius);
        const Stats stats = repairWith(&part->positions, part->triangles, options, radius);
        for (size_t i = 0; i < part->vertices.size(); ++i)
            (*vertices)[part->vertices[i]] = part->positions[i];
        return stats;
    });
}

std::vector<char> FoldRepair::detect(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    const Options& options)
{
    std::vector<char> flags(triangles.size(), 0);
    const std::vector<std::vector<size_t>> parts = partsOf(vertices.size(), triangles);
    if (options.radius > 0.0 || parts.size() <= 1) {
        std::vector<Vector3> used;
        for (const std::vector<size_t>& triangle : triangles)
            for (size_t v : triangle)
                used.push_back(vertices[v]);
        return detect(vertices, triangles, defaultRadius(used, options));
    }
    for (const std::vector<size_t>& part : parts) {
        const Part p = partOf(vertices, triangles, part);
        const std::vector<char> partFlags = detect(p.positions, p.triangles, defaultRadius(p.positions, options));
        for (size_t i = 0; i < part.size(); ++i)
            flags[part[i]] = partFlags[i];
    }
    return flags;
}

}
