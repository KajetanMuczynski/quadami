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
#include "envelope.h"
#include "surfaceprojector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>
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

    struct Grid {
        size_t nx = 0, ny = 0, nz = 0;
        Vector3 origin;
        double cell = 0.0;
        size_t size() const { return nx * ny * nz; }
        size_t index(size_t x, size_t y, size_t z) const { return (z * ny + y) * nx + x; }
        Vector3 position(size_t x, size_t y, size_t z) const
        {
            return origin + Vector3((double)x * cell, (double)y * cell, (double)z * cell);
        }
    };

    // Squared Euclidean distance transform of one line (Felzenszwalb and
    // Huttenlocher, "Distance transforms of sampled functions", 2012), in
    // place: f[q] becomes min_p (q - p)^2 + f[p].  Nodes with nothing to
    // measure to hold farValue, so no infinities enter the arithmetic.
    const double farValue = 1e20;

    void distanceTransform1d(std::vector<double>& f, std::vector<double>& d,
        std::vector<size_t>& v, std::vector<double>& z)
    {
        const size_t n = f.size();
        size_t k = 0;
        v[0] = 0;
        z[0] = -std::numeric_limits<double>::max();
        z[1] = std::numeric_limits<double>::max();
        for (size_t q = 1; q < n; ++q) {
            double s;
            while (true) {
                const size_t p = v[k];
                s = ((f[q] + (double)q * (double)q) - (f[p] + (double)p * (double)p)) / (2.0 * ((double)q - (double)p));
                if (s <= z[k] && k > 0) {
                    --k;
                    continue;
                }
                break;
            }
            if (s <= z[k]) {
                v[0] = q;
                z[1] = std::numeric_limits<double>::max();
                continue;
            }
            ++k;
            v[k] = q;
            z[k] = s;
            z[k + 1] = std::numeric_limits<double>::max();
        }
        k = 0;
        for (size_t q = 0; q < n; ++q) {
            while (z[k + 1] < (double)q)
                ++k;
            const double dq = (double)q - (double)v[k];
            d[q] = std::min(farValue, dq * dq + f[v[k]]);
        }
        f.swap(d);
    }

    // Applies the 1-D transform along one axis of the grid.
    void distanceTransformAxis(std::vector<float>& values, const Grid& grid, int axis)
    {
        const size_t n = 0 == axis ? grid.nx : (1 == axis ? grid.ny : grid.nz);
        const size_t a = 0 == axis ? grid.ny : grid.nx;
        const size_t b = 2 == axis ? grid.ny : grid.nz;
        tbb::parallel_for(tbb::blocked_range<size_t>(0, b),
            [&](const tbb::blocked_range<size_t>& range) {
                std::vector<double> f(n), d(n), z(n + 1);
                std::vector<size_t> v(n);
                for (size_t j = range.begin(); j != range.end(); ++j) {
                    for (size_t i = 0; i < a; ++i) {
                        const auto at = [&](size_t q) {
                            if (0 == axis)
                                return grid.index(q, i, j);
                            if (1 == axis)
                                return grid.index(i, q, j);
                            return grid.index(i, j, q);
                        };
                        for (size_t q = 0; q < n; ++q)
                            f[q] = values[at(q)];
                        distanceTransform1d(f, d, v, z);
                        for (size_t q = 0; q < n; ++q)
                            values[at(q)] = (float)f[q];
                    }
                }
            });
    }

    size_t findRoot(std::vector<size_t>& parent, size_t i)
    {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    }

}

bool buildEnvelope(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    double closingRadius, double offset, double cellSize, size_t maximumCells,
    EnvelopeResult* result)
{
    *result = EnvelopeResult();
    if (!(closingRadius > 0.0) || !(cellSize > 0.0) || !(offset > 0.0) || offset >= closingRadius || maximumCells < 64)
        return false;

    Vector3 lo(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    Vector3 hi(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest());
    size_t used = 0;
    for (const auto& triangle : triangles) {
        if (3 != triangle.size())
            continue;
        ++used;
        for (size_t v : triangle) {
            for (size_t k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], vertices[v][k]);
                hi[k] = std::max(hi[k], vertices[v][k]);
            }
        }
    }
    if (0 == used)
        return false;

    // Two cells of margin beyond the grown part, so the border nodes are
    // outside and the outer flood fill starts there.
    Grid grid;
    grid.cell = cellSize;
    for (int attempt = 0; attempt < 64; ++attempt) {
        const double pad = closingRadius + 2.0 * grid.cell;
        std::array<double, 3> counts;
        for (size_t k = 0; k < 3; ++k)
            counts[k] = std::ceil((hi[k] - lo[k] + 2.0 * pad) / grid.cell) + 1.0;
        const double nodes = counts[0] * counts[1] * counts[2];
        if (nodes <= (double)maximumCells) {
            grid.nx = (size_t)counts[0];
            grid.ny = (size_t)counts[1];
            grid.nz = (size_t)counts[2];
            grid.origin = lo - Vector3(pad, pad, pad);
            break;
        }
        grid.cell *= std::max(1.01, std::cbrt(nodes / (double)maximumCells));
    }
    if (0 == grid.nx || grid.cell * 2.0 >= closingRadius)
        return false;
    result->cellSize = grid.cell;
    result->cells = grid.size();

    // Nodes within closingRadius of a triangle, filled slab by slab along z
    // so that each thread writes its own nodes.
    const size_t slabLayers = 4;
    const size_t slabCount = (grid.nz + slabLayers - 1) / slabLayers;
    std::vector<std::vector<size_t>> slabTriangles(slabCount);
    const double radius2 = closingRadius * closingRadius;
    const auto nodeRange = [&](double low, double high, size_t k, size_t count, size_t* first, size_t* last) {
        const double a = std::ceil((low - closingRadius - grid.origin[k]) / grid.cell);
        const double b = std::floor((high + closingRadius - grid.origin[k]) / grid.cell);
        *first = (size_t)std::max(0.0, a);
        *last = (size_t)std::min((double)count - 1.0, b);
        return a <= b && b >= 0.0 && a <= (double)count - 1.0;
    };
    for (size_t t = 0; t < triangles.size(); ++t) {
        const auto& triangle = triangles[t];
        if (3 != triangle.size())
            continue;
        const double zLow = std::min({ vertices[triangle[0]].z(), vertices[triangle[1]].z(), vertices[triangle[2]].z() });
        const double zHigh = std::max({ vertices[triangle[0]].z(), vertices[triangle[1]].z(), vertices[triangle[2]].z() });
        size_t first, last;
        if (!nodeRange(zLow, zHigh, 2, grid.nz, &first, &last))
            continue;
        for (size_t s = first / slabLayers; s <= last / slabLayers; ++s)
            slabTriangles[s].push_back(t);
    }
    std::vector<uint8_t> inside(grid.size(), 0);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, slabCount),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t s = range.begin(); s != range.end(); ++s) {
                const size_t slabFirst = s * slabLayers;
                const size_t slabLast = std::min(grid.nz - 1, slabFirst + slabLayers - 1);
                for (size_t t : slabTriangles[s]) {
                    const auto& a = vertices[triangles[t][0]];
                    const auto& b = vertices[triangles[t][1]];
                    const auto& c = vertices[triangles[t][2]];
                    size_t x0, x1, y0, y1, z0, z1;
                    if (!nodeRange(std::min({ a.x(), b.x(), c.x() }), std::max({ a.x(), b.x(), c.x() }), 0, grid.nx, &x0, &x1)
                        || !nodeRange(std::min({ a.y(), b.y(), c.y() }), std::max({ a.y(), b.y(), c.y() }), 1, grid.ny, &y0, &y1)
                        || !nodeRange(std::min({ a.z(), b.z(), c.z() }), std::max({ a.z(), b.z(), c.z() }), 2, grid.nz, &z0, &z1))
                        continue;
                    z0 = std::max(z0, slabFirst);
                    z1 = std::min(z1, slabLast);
                    for (size_t z = z0; z <= z1 && z0 <= z1; ++z) {
                        for (size_t y = y0; y <= y1; ++y) {
                            for (size_t x = x0; x <= x1; ++x) {
                                const size_t i = grid.index(x, y, z);
                                if (inside[i])
                                    continue;
                                const Vector3 p = grid.position(x, y, z);
                                if ((p - closestPointOnTriangle(p, a, b, c)).lengthSquared() <= radius2)
                                    inside[i] = 1;
                            }
                        }
                    }
                }
            }
        });

    // Distance of each grown node to the nearest node outside, then the
    // shrink: the field is positive inside the envelope.
    std::vector<float> field(grid.size());
    for (size_t i = 0; i < field.size(); ++i)
        field[i] = inside[i] ? (float)farValue : 0.0f;
    for (int axis = 0; axis < 3; ++axis)
        distanceTransformAxis(field, grid, axis);
    const double shrink = closingRadius - offset;
    for (size_t i = 0; i < field.size(); ++i)
        field[i] = (float)(std::sqrt((double)field[i]) * grid.cell - shrink);

    // Fill cavities: outside nodes the border cannot reach through outside
    // nodes become inside, so only the outer surface is extracted.
    {
        std::vector<uint8_t> reached(grid.size(), 0);
        std::vector<size_t> stack;
        const auto push = [&](size_t x, size_t y, size_t z) {
            const size_t i = grid.index(x, y, z);
            if (!reached[i] && field[i] <= 0.0f) {
                reached[i] = 1;
                stack.push_back(i);
            }
        };
        for (size_t z = 0; z < grid.nz; ++z) {
            for (size_t y = 0; y < grid.ny; ++y) {
                for (size_t x = 0; x < grid.nx; ++x) {
                    if (0 == x || 0 == y || 0 == z || grid.nx - 1 == x || grid.ny - 1 == y || grid.nz - 1 == z)
                        push(x, y, z);
                }
            }
        }
        while (!stack.empty()) {
            const size_t i = stack.back();
            stack.pop_back();
            const size_t x = i % grid.nx;
            const size_t y = (i / grid.nx) % grid.ny;
            const size_t z = i / (grid.nx * grid.ny);
            if (x > 0)
                push(x - 1, y, z);
            if (x + 1 < grid.nx)
                push(x + 1, y, z);
            if (y > 0)
                push(x, y - 1, z);
            if (y + 1 < grid.ny)
                push(x, y + 1, z);
            if (z > 0)
                push(x, y, z - 1);
            if (z + 1 < grid.nz)
                push(x, y, z + 1);
        }
        const float sealed = (float)(0.5 * grid.cell);
        for (size_t i = 0; i < field.size(); ++i) {
            if (!reached[i] && field[i] <= 0.0f)
                field[i] = sealed;
        }
    }
    // No node exactly on the surface, so every crossing is inside an edge.
    const float nudge = (float)(1e-6 * grid.cell);
    for (auto& value : field) {
        if (0.0f == value)
            value = nudge;
    }

    // Marching tetrahedra on the Freudenthal split of each cell: six
    // tetrahedra around the cell's diagonal from corner 0 to corner 7, the
    // same in every cell, so neighbouring cells split their shared face
    // alike and the surface is closed and two-manifold.
    static const int tetrahedra[6][4] = {
        { 0, 1, 3, 7 }, { 0, 1, 5, 7 }, { 0, 2, 3, 7 },
        { 0, 2, 6, 7 }, { 0, 4, 5, 7 }, { 0, 4, 6, 7 }
    };
    std::unordered_map<uint64_t, size_t> crossings;
    std::vector<Vector3> surfaceVertices;
    std::vector<std::array<size_t, 3>> surfaceTriangles;
    const auto crossing = [&](size_t i, size_t j) {
        if (i > j)
            std::swap(i, j);
        const uint64_t key = (uint64_t)i * (uint64_t)grid.size() + (uint64_t)j;
        auto inserted = crossings.insert({ key, surfaceVertices.size() });
        if (inserted.second) {
            const size_t xi = i % grid.nx, yi = (i / grid.nx) % grid.ny, zi = i / (grid.nx * grid.ny);
            const size_t xj = j % grid.nx, yj = (j / grid.nx) % grid.ny, zj = j / (grid.nx * grid.ny);
            const double fi = field[i], fj = field[j];
            const double t = std::min(0.99, std::max(0.01, fi / (fi - fj)));
            const Vector3 pi = grid.position(xi, yi, zi);
            const Vector3 pj = grid.position(xj, yj, zj);
            surfaceVertices.push_back(pi + (pj - pi) * t);
        }
        return inserted.first->second;
    };
    const auto emit = [&](size_t a, size_t b, size_t c, const Vector3& outwards) {
        const Vector3 n = Vector3::crossProduct(surfaceVertices[b] - surfaceVertices[a], surfaceVertices[c] - surfaceVertices[a]);
        if (Vector3::dotProduct(n, outwards) < 0.0)
            std::swap(b, c);
        surfaceTriangles.push_back({ a, b, c });
    };
    for (size_t z = 0; z + 1 < grid.nz; ++z) {
        for (size_t y = 0; y + 1 < grid.ny; ++y) {
            for (size_t x = 0; x + 1 < grid.nx; ++x) {
                size_t corner[8];
                int positive = 0;
                for (int k = 0; k < 8; ++k) {
                    corner[k] = grid.index(x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1));
                    positive += field[corner[k]] > 0.0f ? 1 : 0;
                }
                if (0 == positive || 8 == positive)
                    continue;
                for (const auto& tetrahedron : tetrahedra) {
                    size_t in[4], out[4];
                    int inCount = 0, outCount = 0;
                    for (int k = 0; k < 4; ++k) {
                        const size_t node = corner[tetrahedron[k]];
                        if (field[node] > 0.0f)
                            in[inCount++] = node;
                        else
                            out[outCount++] = node;
                    }
                    if (0 == inCount || 0 == outCount)
                        continue;
                    const auto nodePosition = [&](size_t i) {
                        return grid.position(i % grid.nx, (i / grid.nx) % grid.ny, i / (grid.nx * grid.ny));
                    };
                    Vector3 inCentre, outCentre;
                    for (int k = 0; k < inCount; ++k)
                        inCentre = inCentre + nodePosition(in[k]) / (double)inCount;
                    for (int k = 0; k < outCount; ++k)
                        outCentre = outCentre + nodePosition(out[k]) / (double)outCount;
                    const Vector3 outwards = outCentre - inCentre;
                    if (1 == inCount) {
                        emit(crossing(in[0], out[0]), crossing(in[0], out[1]), crossing(in[0], out[2]), outwards);
                    } else if (3 == inCount) {
                        emit(crossing(out[0], in[0]), crossing(out[0], in[1]), crossing(out[0], in[2]), outwards);
                    } else {
                        const size_t a = crossing(in[0], out[0]);
                        const size_t b = crossing(in[0], out[1]);
                        const size_t c = crossing(in[1], out[1]);
                        const size_t d = crossing(in[1], out[0]);
                        emit(a, b, c, outwards);
                        emit(a, c, d, outwards);
                    }
                }
            }
        }
    }
    if (surfaceTriangles.empty())
        return false;

    // Keep the largest piece (pieces sharing a vertex are one piece).
    std::vector<size_t> parent(surfaceVertices.size());
    std::iota(parent.begin(), parent.end(), 0);
    for (const auto& triangle : surfaceTriangles) {
        const size_t r0 = findRoot(parent, triangle[0]);
        parent[findRoot(parent, triangle[1])] = r0;
        parent[findRoot(parent, triangle[2])] = r0;
    }
    std::vector<size_t> pieceSize(surfaceVertices.size(), 0);
    for (const auto& triangle : surfaceTriangles)
        ++pieceSize[findRoot(parent, triangle[0])];
    const size_t largest = (size_t)(std::max_element(pieceSize.begin(), pieceSize.end()) - pieceSize.begin());
    std::vector<size_t> newIndex(surfaceVertices.size(), std::numeric_limits<size_t>::max());
    for (const auto& triangle : surfaceTriangles) {
        if (findRoot(parent, triangle[0]) != largest) {
            ++result->droppedTriangles;
            continue;
        }
        std::vector<size_t> face(3);
        for (size_t k = 0; k < 3; ++k) {
            size_t& index = newIndex[triangle[k]];
            if (std::numeric_limits<size_t>::max() == index) {
                index = result->vertices.size();
                result->vertices.push_back(surfaceVertices[triangle[k]]);
            }
            face[k] = index;
        }
        result->triangles.push_back(std::move(face));
    }
    return !result->triangles.empty();
}

}
