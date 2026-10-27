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

// Seam welding and duplicate-triangle removal (TODO 2.7,
// research/input-sanitizer-2026-10-26.md).  A clean mesh must stay as it is,
// an unwelded one must come back whole, and nothing may become non-manifold.

#include <AutoRemesher/InputSanitizer>
#include <AutoRemesher/Remesh>
#include <AutoRemesher/Vector3>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

using AutoRemesher::Vector3;

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        ++failures;
}

struct Mesh {
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;
};

// An n x n grid of unit squares, two triangles each, in the plane z = 0.
Mesh grid(size_t n)
{
    Mesh mesh;
    for (size_t j = 0; j <= n; ++j)
        for (size_t i = 0; i <= n; ++i)
            mesh.vertices.push_back(Vector3((double)i, (double)j, 0.0));
    const auto at = [&](size_t i, size_t j) { return j * (n + 1) + i; };
    for (size_t j = 0; j < n; ++j) {
        for (size_t i = 0; i < n; ++i) {
            mesh.triangles.push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1) });
            mesh.triangles.push_back({ at(i, j), at(i + 1, j + 1), at(i, j + 1) });
        }
    }
    return mesh;
}

// Gives every triangle its own three vertices: a triangle soup.
Mesh soup(const Mesh& mesh, double jitter)
{
    Mesh out;
    size_t k = 0;
    for (const auto& t : mesh.triangles) {
        std::vector<size_t> corners;
        for (const size_t v : t) {
            const double s = jitter * ((double)(k++ % 7) - 3.0) / 3.0;
            out.vertices.push_back(mesh.vertices[v] + Vector3(s, -s, 0.5 * s));
            corners.push_back(out.vertices.size() - 1);
        }
        out.triangles.push_back(corners);
    }
    return out;
}

struct Topology {
    size_t boundaryEdges = 0;
    size_t nonManifoldEdges = 0;
};

Topology topology(const std::vector<std::vector<size_t>>& faces)
{
    Topology result;
    std::map<std::pair<size_t, size_t>, size_t> edgeUse;
    for (const auto& face : faces)
        for (size_t i = 0; i < face.size(); ++i)
            ++edgeUse[std::make_pair(std::min(face[i], face[(i + 1) % face.size()]),
                std::max(face[i], face[(i + 1) % face.size()]))];
    for (const auto& it : edgeUse) {
        if (1 == it.second)
            ++result.boundaryEdges;
        else if (it.second > 2)
            ++result.nonManifoldEdges;
    }
    return result;
}

} // namespace

int main()
{
    const double tolerance = 1e-5 * std::sqrt(2.0) * 4.0;
    {
        Mesh mesh = grid(4);
        const auto before = mesh.triangles;
        const auto stats = AutoRemesher::weldSeams(mesh.vertices, mesh.triangles, tolerance);
        check(0 == stats.weldedEdges && before == mesh.triangles, "clean grid: unchanged");
        check(0 == AutoRemesher::removeDuplicateTriangles(mesh.triangles) && before == mesh.triangles,
            "clean grid: no duplicates");
    }
    {
        Mesh mesh = soup(grid(4), 0.0);
        const auto stats = AutoRemesher::weldSeams(mesh.vertices, mesh.triangles, tolerance);
        const Topology t = topology(mesh.triangles);
        check(40 == stats.weldedEdges, "exact soup: every interior edge welded (40)");
        check(16 == t.boundaryEdges && 0 == t.nonManifoldEdges, "exact soup: only the outline stays open");
    }
    {
        Mesh mesh = soup(grid(4), 1e-6);
        const auto stats = AutoRemesher::weldSeams(mesh.vertices, mesh.triangles, tolerance);
        check(40 == stats.weldedEdges && 16 == topology(mesh.triangles).boundaryEdges,
            "soup with gaps under the tolerance: welded");
    }
    {
        Mesh mesh = soup(grid(4), 1e-3);
        const auto before = mesh.triangles;
        const auto stats = AutoRemesher::weldSeams(mesh.vertices, mesh.triangles, tolerance);
        check(0 == stats.weldedEdges && before == mesh.triangles, "soup with gaps over the tolerance: unchanged");
    }
    {
        // Three sheets meeting along one line: every edge on it has two
        // candidate partners, so none is welded.
        Mesh mesh;
        mesh.vertices = { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 },
            { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        mesh.triangles = { { 0, 2, 1 }, { 3, 4, 5 }, { 6, 7, 8 } };
        const auto before = mesh.triangles;
        const auto stats = AutoRemesher::weldSeams(mesh.vertices, mesh.triangles, tolerance);
        check(0 == stats.weldedEdges && before == mesh.triangles, "three sheets on one line: unchanged");
    }
    {
        // An open sheet exported double-sided, with its own vertices per
        // side: the rims coincide but both faces lie on the same side of
        // them, so they stay apart instead of closing into a flat pillow.
        Mesh mesh = grid(4);
        const size_t n = mesh.vertices.size();
        const size_t m = mesh.triangles.size();
        for (size_t v = 0; v < n; ++v)
            mesh.vertices.push_back(mesh.vertices[v]);
        for (size_t t = 0; t < m; ++t) {
            const auto& f = mesh.triangles[t];
            mesh.triangles.push_back({ f[0] + n, f[2] + n, f[1] + n });
        }
        const auto before = mesh.triangles;
        const auto stats = AutoRemesher::weldSeams(mesh.vertices, mesh.triangles, tolerance);
        check(0 == stats.weldedEdges && before == mesh.triangles, "double-sided sheet, own vertices: unchanged");
    }
    {
        // A sheet folded flat onto itself along an unwelded edge: same side,
        // not welded; the same edge opened to 90 degrees is a seam.
        Mesh flat;
        flat.vertices = { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 0, 1, 0 }, { 0.5, 0.3, 0 } };
        flat.triangles = { { 0, 2, 1 }, { 3, 4, 5 } };
        check(0 == AutoRemesher::weldSeams(flat.vertices, flat.triangles, tolerance).weldedEdges,
            "sheet folded flat: unchanged");
        Mesh crease = flat;
        crease.vertices[5] = Vector3(0, 0.3, 0.5);
        check(1 == AutoRemesher::weldSeams(crease.vertices, crease.triangles, tolerance).weldedEdges,
            "seam at a right angle: welded");
    }
    {
        // A sliver edge shorter than ten times the gap is not welded across.
        Mesh mesh;
        mesh.vertices = { { 0, 0, 0 }, { 1e-5, 0, 0 }, { 0, 1, 0 }, { 0, 2e-6, 0 }, { 1e-5, 2e-6, 0 }, { 0, -1, 0 } };
        mesh.triangles = { { 0, 1, 2 }, { 4, 3, 5 } };
        const auto stats = AutoRemesher::weldSeams(mesh.vertices, mesh.triangles, tolerance);
        check(0 == stats.weldedEdges, "edge shorter than ten gaps: unchanged");
    }
    {
        Mesh mesh = grid(2);
        const auto first = mesh.triangles;
        mesh.triangles.push_back(first[3]);
        mesh.triangles.push_back({ first[5][0], first[5][2], first[5][1] });
        check(2 == AutoRemesher::removeDuplicateTriangles(mesh.triangles) && first == mesh.triangles,
            "duplicates in either winding removed, first kept in place");
    }
    {
        // Two grids sharing one corner: the shared vertex is split, and only
        // the second grid's corners move to the new vertex.
        Mesh a = grid(2), b = grid(2);
        Mesh mesh = a;
        const size_t offset = mesh.vertices.size();
        for (auto v : b.vertices)
            mesh.vertices.push_back(Vector3(-v.x(), -v.y(), v.z()));
        for (auto t : b.triangles) {
            for (auto& c : t)
                c = 0 == c ? 0 : c + offset;
            mesh.triangles.push_back({ t[0], t[2], t[1] });
        }
        const auto before = mesh.triangles;
        const size_t vertexCount = mesh.vertices.size();
        check(1 == AutoRemesher::splitPinchedVertices(mesh.vertices, mesh.triangles), "pinch: one vertex added");
        bool firstKept = true, secondMoved = true;
        for (size_t t = 0; t < before.size(); ++t) {
            for (size_t k = 0; k < 3; ++k) {
                if (0 != before[t][k])
                    continue;
                if (t < a.triangles.size())
                    firstKept = firstKept && 0 == mesh.triangles[t][k];
                else
                    secondMoved = secondMoved && vertexCount == mesh.triangles[t][k];
            }
        }
        check(firstKept && secondMoved, "pinch: first fan keeps the vertex, second gets the copy");
        Mesh clean = grid(4);
        const auto cleanBefore = clean.triangles;
        check(0 == AutoRemesher::splitPinchedVertices(clean.vertices, clean.triangles) && cleanBefore == clean.triangles,
            "clean grid: no vertex split");
    }
    {
        // Through remesh(): a soup of a closed box comes back closed.
        Mesh box;
        box.vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } };
        box.triangles = { { 0, 2, 1 }, { 0, 3, 2 }, { 4, 5, 6 }, { 4, 6, 7 }, { 0, 1, 5 }, { 0, 5, 4 },
            { 1, 2, 6 }, { 1, 6, 5 }, { 2, 3, 7 }, { 2, 7, 6 }, { 3, 0, 4 }, { 3, 4, 7 } };
        const Mesh pieces = soup(box, 0.0);
        AutoRemesher::RemeshRequest request;
        for (const auto& v : pieces.vertices) {
            request.positions.push_back(v.x());
            request.positions.push_back(v.y());
            request.positions.push_back(v.z());
        }
        for (const auto& t : pieces.triangles)
            for (const size_t i : t)
                request.triangles.push_back((uint32_t)i);
        request.parameters.targetQuadCount = 200;
        const AutoRemesher::RemeshResult result = AutoRemesher::remesh(request);
        double welded = -1;
        for (const auto& item : result.report.diagnostics) {
            if (item.first == "input_welded_edges")
                welded = item.second;
        }
        check(AutoRemesher::RemeshStatus::Ok == result.status, "box soup remesh: ok");
        check(18 == welded, "box soup remesh: 18 edges welded");
        check(0 == result.report.boundaryEdgeCount && 0 == result.report.nonManifoldEdgeCount,
            "box soup remesh: output closed and manifold");
    }
    return failures ? 1 : 0;
}
