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
#include <AutoRemesher/ThinParts>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace AutoRemesher {

namespace {

    size_t findRoot(std::vector<size_t>& parent, size_t v)
    {
        while (parent[v] != v) {
            parent[v] = parent[parent[v]];
            v = parent[v];
        }
        return v;
    }

}

ThinParts measureThinParts(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles, double quadEdge)
{
    ThinParts result;
    const size_t vertexCount = vertices.size();
    if (0 == vertexCount || !(quadEdge > 0.0))
        return result;

    // Area-weighted vertex normals and a third of each triangle's area per corner.
    std::vector<Vector3> normals(vertexCount);
    std::vector<double> areas(vertexCount, 0.0);
    for (const auto& triangle : triangles) {
        if (3 != triangle.size())
            continue;
        const Vector3 cross = Vector3::crossProduct(vertices[triangle[1]] - vertices[triangle[0]],
            vertices[triangle[2]] - vertices[triangle[0]]);
        const double area = 0.5 * cross.length();
        result.area += area;
        for (size_t corner : triangle) {
            normals[corner] += cross;
            areas[corner] += area / 3.0;
        }
    }
    for (auto& normal : normals)
        normal.normalize();

    // Vertices on a grid of cells one quad edge wide: the ball of radius
    // quadEdge around a vertex lies in its cell and the 26 around it.
    Vector3 low = vertices[0];
    for (const Vector3& v : vertices)
        for (size_t i = 0; i < 3; ++i)
            low[i] = std::min(low[i], v[i]);
    const auto cellOf = [&](const Vector3& v, int64_t* cell) {
        for (size_t i = 0; i < 3; ++i)
            cell[i] = (int64_t)std::floor((v[i] - low[i]) / quadEdge);
    };
    const auto keyOf = [](int64_t x, int64_t y, int64_t z) {
        return ((uint64_t)(x & 0x1FFFFF) << 42) | ((uint64_t)(y & 0x1FFFFF) << 21) | (uint64_t)(z & 0x1FFFFF);
    };
    std::unordered_map<uint64_t, std::vector<size_t>> cells;
    for (size_t v = 0; v < vertexCount; ++v) {
        int64_t cell[3];
        cellOf(vertices[v], cell);
        cells[keyOf(cell[0], cell[1], cell[2])].push_back(v);
    }

    const double radiusSquared = quadEdge * quadEdge;
    std::vector<double> thickness(vertexCount, std::numeric_limits<double>::infinity());
    for (size_t v = 0; v < vertexCount; ++v) {
        if (!(normals[v].lengthSquared() > 0.0))
            continue;
        int64_t cell[3];
        cellOf(vertices[v], cell);
        double nearest = std::numeric_limits<double>::infinity();
        for (int64_t dx = -1; dx <= 1; ++dx)
            for (int64_t dy = -1; dy <= 1; ++dy)
                for (int64_t dz = -1; dz <= 1; ++dz) {
                    const auto found = cells.find(keyOf(cell[0] + dx, cell[1] + dy, cell[2] + dz));
                    if (found == cells.end())
                        continue;
                    for (size_t other : found->second) {
                        const double distanceSquared = (vertices[other] - vertices[v]).lengthSquared();
                        if (distanceSquared <= radiusSquared && distanceSquared < nearest
                            && Vector3::dotProduct(normals[other], normals[v]) < -0.5)
                            nearest = distanceSquared;
                    }
                }
        if (nearest < std::numeric_limits<double>::infinity())
            thickness[v] = std::sqrt(nearest);
    }

    // Spots: thin vertices joined by triangle edges.
    std::vector<size_t> parent(vertexCount);
    std::iota(parent.begin(), parent.end(), (size_t)0);
    const auto isThin = [&](size_t v) { return thickness[v] < std::numeric_limits<double>::infinity(); };
    for (const auto& triangle : triangles) {
        for (size_t i = 0; i < triangle.size(); ++i) {
            const size_t a = triangle[i];
            const size_t b = triangle[(i + 1) % triangle.size()];
            if (isThin(a) && isThin(b))
                parent[findRoot(parent, a)] = findRoot(parent, b);
        }
    }
    std::unordered_map<size_t, double> spotAreas;
    for (size_t v = 0; v < vertexCount; ++v)
        if (isThin(v))
            spotAreas[findRoot(parent, v)] += areas[v];
    for (const auto& spot : spotAreas)
        if (spot.second >= radiusSquared)
            ++result.spots;
    for (size_t v = 0; v < vertexCount; ++v) {
        if (!isThin(v) || spotAreas[findRoot(parent, v)] < radiusSquared)
            continue;
        result.thinArea += areas[v];
        const double across = quadEdge / std::max(thickness[v], quadEdge * 1e-3);
        result.growth.push_back({ across * across, areas[v] });
    }
    return result;
}

double medianThinGrowth(std::vector<std::pair<double, double>> growth)
{
    if (growth.empty())
        return 0.0;
    std::sort(growth.begin(), growth.end());
    double total = 0.0;
    for (const auto& g : growth)
        total += g.second;
    double sum = 0.0;
    for (const auto& g : growth) {
        sum += g.second;
        if (sum >= 0.5 * total)
            return g.first;
    }
    return growth.back().first;
}

}
