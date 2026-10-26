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
#include "inputsanitizer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>
#if defined(__has_include)
#if __has_include(<oneapi/tbb/parallel_sort.h>)
#include <oneapi/tbb/parallel_sort.h>
#else
#include <tbb/parallel_sort.h>
#endif
#else
#include <tbb/parallel_sort.h>
#endif

namespace AutoRemesher {

const double seamWeldRelativeTolerance = 1e-5;

namespace {

    const size_t none = std::numeric_limits<size_t>::max();

    struct UnionFind {
        std::vector<size_t> parent;
        explicit UnionFind(size_t size)
            : parent(size)
        {
            std::iota(parent.begin(), parent.end(), (size_t)0);
        }
        size_t root(size_t v)
        {
            while (parent[v] != v)
                v = parent[v] = parent[parent[v]];
            return v;
        }
        // The lower index becomes the root, so a group's index does not hang
        // on the order of the unions.
        bool join(size_t a, size_t b)
        {
            a = root(a);
            b = root(b);
            if (a == b)
                return false;
            parent[std::max(a, b)] = std::min(a, b);
            return true;
        }
    };

    struct Cell {
        long long x, y, z;
        bool operator==(const Cell& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct CellHash {
        size_t operator()(const Cell& c) const
        {
            uint64_t h = (uint64_t)c.x * 0x9E3779B97F4A7C15ull;
            h ^= (uint64_t)c.y + 0x7F4A7C159E3779B9ull + (h << 6) + (h >> 2);
            h ^= (uint64_t)c.z + 0x94D049BB133111EBull + (h << 6) + (h >> 2);
            return (size_t)h;
        }
    };

    bool isDegenerate(const std::vector<size_t>& t)
    {
        return t[0] == t[1] || t[1] == t[2] || t[2] == t[0];
    }

    // An undirected edge packed into one key, with the triangle it belongs
    // to.  Callers check that vertex indices fit 32 bits.
    struct EdgeUse {
        uint64_t key;
        uint32_t triangle;
        bool operator<(const EdgeUse& other) const
        {
            return key != other.key ? key < other.key : triangle < other.triangle;
        }
    };

    uint64_t edgeKey(size_t a, size_t b)
    {
        return ((uint64_t)std::min(a, b) << 32) | (uint64_t)std::max(a, b);
    }

    std::vector<EdgeUse> sortedEdges(const std::vector<std::vector<size_t>>& triangles,
        const std::vector<size_t>* remap)
    {
        std::vector<EdgeUse> edges;
        edges.reserve(triangles.size() * 3);
        for (size_t t = 0; t < triangles.size(); ++t) {
            std::array<size_t, 3> c = { triangles[t][0], triangles[t][1], triangles[t][2] };
            if (nullptr != remap) {
                for (auto& v : c)
                    v = (*remap)[v];
            }
            for (size_t i = 0; i < 3; ++i) {
                const size_t a = c[i], b = c[(i + 1) % 3];
                if (a != b)
                    edges.push_back({ edgeKey(a, b), (uint32_t)t });
            }
        }
        tbb::parallel_sort(edges.begin(), edges.end());
        return edges;
    }

}

double usedBoundingDiagonal(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles)
{
    Vector3 lower(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    Vector3 upper(-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max());
    bool any = false;
    for (const auto& triangle : triangles) {
        for (const size_t v : triangle) {
            for (size_t i = 0; i < 3; ++i) {
                lower[i] = std::min(lower[i], vertices[v][i]);
                upper[i] = std::max(upper[i], vertices[v][i]);
            }
            any = true;
        }
    }
    return any ? (upper - lower).length() : 0.0;
}

SeamWeldStats weldSeams(const std::vector<Vector3>& vertices,
    std::vector<std::vector<size_t>>& triangles,
    double tolerance)
{
    SeamWeldStats stats;
    if (vertices.size() > 0xffffffffu || triangles.size() > 0xffffffffu)
        return stats;

    // Boundary edges: used by one triangle.  Kept as the triangle walks
    // them, (from, to).
    struct Side {
        size_t from, to, triangle;
    };
    std::vector<Side> boundary;
    {
        const auto edges = sortedEdges(triangles, nullptr);
        for (size_t i = 0; i < edges.size();) {
            size_t j = i + 1;
            while (j < edges.size() && edges[j].key == edges[i].key)
                ++j;
            if (j - i > 2)
                ++stats.nonManifoldEdges;
            else if (1 == j - i) {
                const auto& t = triangles[edges[i].triangle];
                for (size_t k = 0; k < 3; ++k) {
                    const size_t a = t[k], b = t[(k + 1) % 3];
                    if (edgeKey(a, b) == edges[i].key) {
                        boundary.push_back({ a, b, edges[i].triangle });
                        break;
                    }
                }
            }
            i = j;
        }
    }
    if (boundary.empty() || !(tolerance > 0.0) || !std::isfinite(tolerance))
        return stats;

    // Boundary vertices on a grid of the tolerance.  A position that is not
    // finite, or too far out for a cell index, is never near anything.
    std::unordered_map<Cell, std::vector<size_t>, CellHash> grid;
    const double farthestCell = 1e15;
    const auto onGrid = [&](const Vector3& p) {
        for (size_t i = 0; i < 3; ++i) {
            if (!(std::abs(p[i] / tolerance) < farthestCell))
                return false;
        }
        return true;
    };
    const auto cellOf = [&](const Vector3& p) {
        return Cell { (long long)std::floor(p.x() / tolerance), (long long)std::floor(p.y() / tolerance),
            (long long)std::floor(p.z() / tolerance) };
    };
    {
        std::vector<size_t> ends;
        ends.reserve(boundary.size() * 2);
        for (const auto& side : boundary) {
            ends.push_back(side.from);
            ends.push_back(side.to);
        }
        std::sort(ends.begin(), ends.end());
        ends.erase(std::unique(ends.begin(), ends.end()), ends.end());
        for (const size_t v : ends) {
            if (onGrid(vertices[v]))
                grid[cellOf(vertices[v])].push_back(v);
        }
    }
    // The boundary sides leaving and entering each boundary vertex.
    std::unordered_map<size_t, std::vector<size_t>> leaving, entering;
    for (size_t k = 0; k < boundary.size(); ++k) {
        leaving[boundary[k].from].push_back(k);
        entering[boundary[k].to].push_back(k);
    }
    const std::vector<size_t> noSides;
    const auto sidesOf = [&](const std::unordered_map<size_t, std::vector<size_t>>& map, size_t v) -> const std::vector<size_t>& {
        auto found = map.find(v);
        return found == map.end() ? noSides : found->second;
    };
    const auto near = [&](size_t v, std::vector<size_t>& out) {
        out.clear();
        if (!onGrid(vertices[v]))
            return;
        const Cell c = cellOf(vertices[v]);
        for (long long dx = -1; dx <= 1; ++dx) {
            for (long long dy = -1; dy <= 1; ++dy) {
                for (long long dz = -1; dz <= 1; ++dz) {
                    auto found = grid.find(Cell { c.x + dx, c.y + dy, c.z + dz });
                    if (found == grid.end())
                        continue;
                    for (const size_t u : found->second) {
                        if ((vertices[u] - vertices[v]).lengthSquared() <= tolerance * tolerance)
                            out.push_back(u);
                    }
                }
            }
        }
    };

    // Each side's one partner: another boundary side between the same two
    // points, walked either way (a piece turned against its neighbour is
    // oriented later).  None when there are several.
    std::vector<size_t> partner(boundary.size(), none);
    std::vector<unsigned char> reversed(boundary.size(), 0);
    std::vector<size_t> nearFrom;
    for (size_t s = 0; s < boundary.size(); ++s) {
        const Side& side = boundary[s];
        const double length = (vertices[side.from] - vertices[side.to]).length();
        near(side.from, nearFrom);
        size_t found = none;
        bool foundReversed = false;
        bool ambiguous = false;
        const auto consider = [&](size_t k, size_t u, size_t w, bool isReversed) {
            if (k == s || u == w)
                return;
            const double otherLength = (vertices[boundary[k].from] - vertices[boundary[k].to]).length();
            const double limit = std::min(tolerance, 0.1 * std::min(length, otherLength));
            if ((vertices[u] - vertices[side.from]).lengthSquared() > limit * limit
                || (vertices[w] - vertices[side.to]).lengthSquared() > limit * limit)
                return;
            if (none != found && found != k)
                ambiguous = true;
            found = k;
            foundReversed = isReversed;
        };
        for (const size_t u : nearFrom) {
            // k walks w -> u, against s, or u -> w, along it.
            for (const size_t k : sidesOf(entering, u))
                consider(k, u, boundary[k].from, true);
            for (const size_t k : sidesOf(leaving, u))
                consider(k, u, boundary[k].to, false);
        }
        if (!ambiguous) {
            partner[s] = found;
            reversed[s] = foundReversed ? 1 : 0;
        }
    }

    UnionFind groups(vertices.size());
    std::vector<size_t> welded;
    for (size_t s = 0; s < boundary.size(); ++s) {
        const size_t k = partner[s];
        if (none == k || k < s || partner[k] != s)
            continue;
        if (reversed[s]) {
            groups.join(boundary[s].from, boundary[k].to);
            groups.join(boundary[s].to, boundary[k].from);
        } else {
            groups.join(boundary[s].from, boundary[k].from);
            groups.join(boundary[s].to, boundary[k].to);
        }
        welded.push_back(s);
    }
    if (welded.empty())
        return stats;

    std::vector<size_t> remap(vertices.size());
    for (size_t v = 0; v < vertices.size(); ++v)
        remap[v] = groups.root(v);

    // Connected parts after the welds, by shared corners.
    UnionFind parts(vertices.size());
    for (const auto& t : triangles) {
        parts.join(remap[t[0]], remap[t[1]]);
        parts.join(remap[t[0]], remap[t[2]]);
    }
    const auto partOfTriangle = [&](size_t t) { return parts.root(remap[triangles[t][0]]); };

    // A part is kept as it was if a weld made a triangle degenerate or an
    // edge non-manifold (more such edges than before).
    std::unordered_map<size_t, long long> nonManifoldGrowth;
    std::unordered_map<size_t, bool> rejected;
    for (size_t t = 0; t < triangles.size(); ++t) {
        const auto& c = triangles[t];
        if (!isDegenerate(c) && (remap[c[0]] == remap[c[1]] || remap[c[1]] == remap[c[2]] || remap[c[2]] == remap[c[0]]))
            rejected[partOfTriangle(t)] = true;
    }
    for (int after = 0; after < 2; ++after) {
        const auto edges = sortedEdges(triangles, after ? &remap : nullptr);
        for (size_t i = 0; i < edges.size();) {
            size_t j = i + 1;
            while (j < edges.size() && edges[j].key == edges[i].key)
                ++j;
            if (j - i > 2)
                nonManifoldGrowth[partOfTriangle(edges[i].triangle)] += after ? 1 : -1;
            i = j;
        }
    }
    for (const auto& it : nonManifoldGrowth) {
        if (it.second > 0)
            rejected[it.first] = true;
    }
    if (!rejected.empty()) {
        for (const auto& it : rejected) {
            if (it.second)
                ++stats.keptParts;
        }
        for (size_t v = 0; v < vertices.size(); ++v) {
            if (remap[v] != v) {
                auto found = rejected.find(parts.root(remap[v]));
                if (found != rejected.end() && found->second)
                    remap[v] = v;
            }
        }
    }

    for (const size_t s : welded) {
        if (remap[boundary[s].from] != boundary[s].from || remap[boundary[s].to] != boundary[s].to
            || remap[boundary[partner[s]].from] != boundary[partner[s]].from
            || remap[boundary[partner[s]].to] != boundary[partner[s]].to)
            ++stats.weldedEdges;
    }
    for (size_t v = 0; v < vertices.size(); ++v) {
        if (remap[v] != v)
            ++stats.weldedVertices;
    }
    if (0 == stats.weldedVertices)
        return stats;
    for (auto& t : triangles) {
        for (auto& corner : t)
            corner = remap[corner];
    }
    return stats;
}

size_t splitPinchedVertices(std::vector<Vector3>& vertices,
    std::vector<std::vector<size_t>>& triangles)
{
    // Corners packed as vertex << 32 | triangle << 2 | corner, sorted, so
    // each vertex's corners are together and in triangle order.
    if (vertices.size() > 0xffffffffu || triangles.size() >= ((size_t)1 << 30))
        return 0;
    std::vector<uint64_t> corners;
    corners.reserve(triangles.size() * 3);
    for (size_t t = 0; t < triangles.size(); ++t) {
        if (isDegenerate(triangles[t]))
            continue;
        for (size_t k = 0; k < 3; ++k)
            corners.push_back(((uint64_t)triangles[t][k] << 32) | ((uint64_t)t << 2) | (uint64_t)k);
    }
    tbb::parallel_sort(corners.begin(), corners.end());
    const auto vertexOf = [&](size_t c) { return (size_t)(corners[c] >> 32); };
    const auto triangleOf = [&](size_t c) { return (size_t)((corners[c] & 0xffffffffu) >> 2); };
    const auto cornerOf = [&](size_t c) { return (size_t)(corners[c] & 3u); };

    size_t added = 0;
    std::vector<size_t> parent;
    std::vector<std::pair<size_t, size_t>> across;
    for (size_t i = 0; i < corners.size();) {
        size_t j = i + 1;
        while (j < corners.size() && vertexOf(j) == vertexOf(i))
            ++j;
        const size_t count = j - i;
        if (count < 2) {
            i = j;
            continue;
        }
        // Two corners of the vertex are in one fan if their triangles share
        // an edge from it: the same far end.
        across.clear();
        for (size_t c = i; c < j; ++c) {
            const auto& t = triangles[triangleOf(c)];
            const size_t k = cornerOf(c);
            across.push_back({ t[(k + 1) % 3], c - i });
            across.push_back({ t[(k + 2) % 3], c - i });
        }
        std::sort(across.begin(), across.end());
        parent.resize(count);
        std::iota(parent.begin(), parent.end(), (size_t)0);
        const auto root = [&](size_t x) {
            while (parent[x] != x)
                x = parent[x] = parent[parent[x]];
            return x;
        };
        size_t fans = count;
        for (size_t a = 1; a < across.size(); ++a) {
            if (across[a].first != across[a - 1].first)
                continue;
            const size_t ra = root(across[a].second), rb = root(across[a - 1].second);
            if (ra != rb) {
                parent[std::max(ra, rb)] = std::min(ra, rb);
                --fans;
            }
        }
        if (fans > 1) {
            // Corners are sorted by triangle, so root 0 is the fan with the
            // lowest triangle; it keeps the vertex.
            std::vector<size_t> copyOf(count, none);
            const size_t vertex = vertexOf(i);
            const Vector3 position = vertices[vertex];
            for (size_t c = 0; c < count; ++c) {
                const size_t r = root(c);
                if (0 == r)
                    continue;
                if (none == copyOf[r]) {
                    copyOf[r] = vertices.size();
                    vertices.push_back(position);
                    ++added;
                }
                triangles[triangleOf(i + c)][cornerOf(i + c)] = copyOf[r];
            }
        }
        i = j;
    }
    return added;
}

size_t removeDuplicateTriangles(std::vector<std::vector<size_t>>& triangles)
{
    std::vector<std::array<size_t, 4>> keys(triangles.size());
    for (size_t t = 0; t < triangles.size(); ++t) {
        std::array<size_t, 4> key = { triangles[t][0], triangles[t][1], triangles[t][2], t };
        std::sort(key.begin(), key.begin() + 3);
        keys[t] = key;
    }
    tbb::parallel_sort(keys.begin(), keys.end());
    std::vector<unsigned char> duplicate(triangles.size(), 0);
    size_t removed = 0;
    for (size_t i = 1; i < keys.size(); ++i) {
        if (keys[i][0] == keys[i - 1][0] && keys[i][1] == keys[i - 1][1] && keys[i][2] == keys[i - 1][2]) {
            duplicate[keys[i][3]] = 1;
            ++removed;
        }
    }
    if (0 == removed)
        return 0;
    size_t kept = 0;
    for (size_t t = 0; t < triangles.size(); ++t) {
        if (duplicate[t])
            continue;
        if (kept != t)
            triangles[kept] = std::move(triangles[t]);
        ++kept;
    }
    triangles.resize(kept);
    return removed;
}

}
