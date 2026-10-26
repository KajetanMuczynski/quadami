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

// Folded input (FoldRepair).
//
// The detector must leave a clean sphere, a thin closed slab, a thin tube
// and a thin tube bent back on itself alone, flag a twist past one turn and
// a pleat, and find a twist on a small part joined to a big one.  The repair must
// untangle the twist locally with every moved vertex still on the sphere,
// untangle the pleat without moving the sheet's open border, keep the
// vertices of a crease next to a fold where they are, leave clean input
// bit for bit as it was, and give the same result on every run.  Spike
// flattening, only when asked for, lowers a folded needle's tip and leaves
// a needle that does not fold alone.

#include <AutoRemesher/FoldRepair>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

namespace {

using AutoRemesher::FoldRepair;
using AutoRemesher::Vector3;

typedef std::vector<std::vector<size_t>> Triangles;

const double pi = 3.14159265358979323846;

struct Mesh {
    std::vector<Vector3> vertices;
    Triangles triangles;
};

Mesh icosphere(int subdivisions)
{
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    Mesh mesh;
    for (const auto& p : std::vector<Vector3> { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
             { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } })
        mesh.vertices.push_back(p.normalized());
    mesh.triangles = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 },
        { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
        { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    for (int s = 0; s < subdivisions; ++s) {
        std::map<std::pair<size_t, size_t>, size_t> middle;
        const auto mid = [&](size_t a, size_t b) {
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto found = middle.find(key);
            if (found != middle.end())
                return found->second;
            mesh.vertices.push_back((0.5 * (mesh.vertices[a] + mesh.vertices[b])).normalized());
            return middle[key] = mesh.vertices.size() - 1;
        };
        Triangles next;
        for (const auto& f : mesh.triangles) {
            const size_t a = mid(f[0], f[1]), b = mid(f[1], f[2]), c = mid(f[2], f[0]);
            next.push_back({ f[0], a, c });
            next.push_back({ f[1], b, a });
            next.push_back({ f[2], c, b });
            next.push_back({ a, b, c });
        }
        mesh.triangles = next;
    }
    return mesh;
}

// Turns the cap around the north pole by up to `turns` full turns, most at
// the pole: a Rotate brush stroke wound past itself.
void twist(Mesh* mesh, double turns, double radius)
{
    const Vector3 pole(0.0, 0.0, 1.0);
    for (Vector3& v : mesh->vertices) {
        const double d = (v - pole).length();
        if (d >= radius)
            continue;
        const double s = d / radius;
        const double angle = 2.0 * pi * turns * (1.0 - 3.0 * s * s + 2.0 * s * s * s);
        const double x = v.x(), y = v.y();
        v = Vector3(x * std::cos(angle) - y * std::sin(angle), x * std::sin(angle) + y * std::cos(angle), v.z());
    }
}

// A closed box of n x n squares per side, each split in two.
Mesh box(size_t n, const Vector3& size)
{
    Mesh mesh;
    std::map<std::array<long, 3>, size_t> index;
    const auto vertex = [&](long i, long j, long k) {
        const std::array<long, 3> key { i, j, k };
        auto found = index.find(key);
        if (found != index.end())
            return found->second;
        mesh.vertices.push_back(Vector3(size.x() * ((double)i / n - 0.5), size.y() * ((double)j / n - 0.5), size.z() * ((double)k / n - 0.5)));
        return index[key] = mesh.vertices.size() - 1;
    };
    for (int axis = 0; axis < 3; ++axis)
        for (int side = 0; side < 2; ++side)
            for (size_t a = 0; a < n; ++a)
                for (size_t b = 0; b < n; ++b) {
                    size_t q[4];
                    const long corners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
                    for (int c = 0; c < 4; ++c) {
                        long p[3];
                        p[axis] = side ? (long)n : 0;
                        p[(axis + 1) % 3] = (long)a + corners[c][0];
                        p[(axis + 2) % 3] = (long)b + corners[c][1];
                        q[c] = vertex(p[0], p[1], p[2]);
                    }
                    if (side) {
                        mesh.triangles.push_back({ q[0], q[1], q[2] });
                        mesh.triangles.push_back({ q[0], q[2], q[3] });
                    } else {
                        mesh.triangles.push_back({ q[0], q[2], q[1] });
                        mesh.triangles.push_back({ q[0], q[3], q[2] });
                    }
                }
    return mesh;
}

// A closed tube along z of `segments` x `rings`, capped by fans.
Mesh tube(double radius, double length, size_t segments, size_t rings)
{
    Mesh mesh;
    for (size_t r = 0; r <= rings; ++r)
        for (size_t s = 0; s < segments; ++s) {
            const double a = 2.0 * pi * s / segments;
            mesh.vertices.push_back(Vector3(radius * std::cos(a), radius * std::sin(a), length * ((double)r / rings - 0.5)));
        }
    const size_t bottom = mesh.vertices.size();
    mesh.vertices.push_back(Vector3(0, 0, -0.5 * length));
    mesh.vertices.push_back(Vector3(0, 0, 0.5 * length));
    for (size_t r = 0; r < rings; ++r)
        for (size_t s = 0; s < segments; ++s) {
            const size_t a = r * segments + s, b = r * segments + (s + 1) % segments;
            mesh.triangles.push_back({ a, b, b + segments });
            mesh.triangles.push_back({ a, b + segments, a + segments });
        }
    for (size_t s = 0; s < segments; ++s) {
        mesh.triangles.push_back({ bottom, (s + 1) % segments, s });
        mesh.triangles.push_back({ bottom + 1, rings * segments + s, rings * segments + (s + 1) % segments });
    }
    return mesh;
}

// An open tube along a hairpin: straight, half a turn of the given radius,
// straight back.  Smooth everywhere (no edge turns more than 360 / segments
// degrees), but it bends back within the detector radius.
Mesh hairpin(double radius, double bend, size_t segments)
{
    std::vector<std::pair<Vector3, Vector3>> path;
    const size_t straight = 400, around = 200;
    for (size_t i = 0; i < straight; ++i)
        path.push_back({ Vector3(-1.0 + (double)i / straight, 0, 0), Vector3(1, 0, 0) });
    for (size_t i = 0; i <= around; ++i) {
        const double a = pi * i / around;
        path.push_back({ Vector3(bend * std::sin(a), bend - bend * std::cos(a), 0), Vector3(std::cos(a), std::sin(a), 0) });
    }
    for (size_t i = 1; i < straight; ++i)
        path.push_back({ Vector3(-(double)i / straight, 2.0 * bend, 0), Vector3(-1, 0, 0) });
    Mesh mesh;
    for (const auto& point : path) {
        const Vector3 u = Vector3::crossProduct(point.second, Vector3(0, 0, 1)).normalized();
        const Vector3 w = Vector3::crossProduct(point.second, u);
        for (size_t k = 0; k < segments; ++k) {
            const double a = 2.0 * pi * k / segments;
            mesh.vertices.push_back(point.first + radius * (std::cos(a) * u + std::sin(a) * w));
        }
    }
    for (size_t i = 0; i + 1 < path.size(); ++i)
        for (size_t k = 0; k < segments; ++k) {
            const size_t a = i * segments + k, b = i * segments + (k + 1) % segments;
            mesh.triangles.push_back({ a, b, b + segments });
            mesh.triangles.push_back({ a, b + segments, a + segments });
        }
    return mesh;
}

// Both meshes as one, b moved by `offset`.
Mesh join(const Mesh& a, const Mesh& b, const Vector3& offset)
{
    Mesh mesh = a;
    for (const Vector3& v : b.vertices)
        mesh.vertices.push_back(v + offset);
    for (const std::vector<size_t>& t : b.triangles)
        mesh.triangles.push_back({ t[0] + a.vertices.size(), t[1] + a.vertices.size(), t[2] + a.vertices.size() });
    return mesh;
}

bool same(const Vector3& a, const Vector3& b)
{
    return a.x() == b.x() && a.y() == b.y() && a.z() == b.z();
}

bool same(const std::vector<Vector3>& a, const std::vector<Vector3>& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const Vector3& x, const Vector3& y) { return same(x, y); });
}

// A square sheet of n x n squares over [-1, 1]^2 with a Z-shaped pleat
// in its middle: along x the sheet runs forward to 0, back by `width` while
// it climbs by `height`, then forward again on top, so the middle layer is
// upside down.  The pleat fades out towards |y| = 0.5, so it does not reach
// the border.  An over-pinched crease looks like this.
Mesh pleat(size_t n, double width, double height)
{
    Mesh mesh;
    for (size_t j = 0; j <= n; ++j)
        for (size_t i = 0; i <= n; ++i) {
            const double s = -1.0 + 2.0 * i / n, y = -1.0 + 2.0 * j / n;
            const double fade = std::pow(std::max(0.0, 1.0 - 4.0 * y * y), 2.0);
            const double w = width * fade, h = height * fade;
            double x = s, z = 0.0;
            if (s >= 0.0 && s < w) {
                x = -s;
                z = h * s / w;
            } else if (s >= w) {
                x = s - 2.0 * w;
                z = h;
            }
            mesh.vertices.push_back(Vector3(x, y, z));
        }
    for (size_t j = 0; j < n; ++j)
        for (size_t i = 0; i < n; ++i) {
            const size_t a = j * (n + 1) + i, b = a + 1, c = a + n + 2, d = a + n + 1;
            mesh.triangles.push_back({ a, b, c });
            mesh.triangles.push_back({ a, c, d });
        }
    return mesh;
}

size_t count(const std::vector<char>& flags)
{
    return (size_t)std::count(flags.begin(), flags.end(), 1);
}

size_t detected(const Mesh& mesh)
{
    return count(FoldRepair::detect(mesh.vertices, mesh.triangles, FoldRepair::Options()));
}

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("%s: %s\n", condition ? "ok" : "FAILED", what);
    if (!condition)
        ++failures;
}

}

int main()
{
    {
        Mesh sphere = icosphere(4);
        check(0 == detected(sphere), "a clean sphere has no folds");
        const std::vector<Vector3> before = sphere.vertices;
        const FoldRepair::Stats stats = FoldRepair::repair(&sphere.vertices, sphere.triangles, FoldRepair::Options());
        check(0 == stats.foldedTriangles && 0 == stats.movedVertices && same(before, sphere.vertices),
            "repair leaves a clean sphere bit for bit as it was");
    }
    {
        const Mesh slab = box(40, Vector3(1.0, 1.0, 0.002));
        check(0 == detected(slab), "a thin closed slab is not a fold");
    }
    {
        // Circumference 0.006, far under the radius (1 % of the diagonal,
        // 0.01): the disc wraps all the way round.
        const Mesh thin = tube(0.001, 1.0, 12, 400);
        check(0 == detected(thin), "a thin tube is not a fold");
    }
    {
        // Radius 0.3 % of the diagonal, bent back at 1 %: the sign test
        // flags its inner side (as on a sword's vines), but nothing turns
        // over.
        const Mesh bent = hairpin(0.003, 0.01, 12);
        check(0 == detected(bent), "a thin tube bent back on itself is not a fold");
    }
    {
        // A twisted sphere of diameter 2 next to a box 300 across: with the
        // radius of the whole (1 % of about 300) the disc would hold the
        // whole sphere and sum to nothing.
        Mesh small = icosphere(5);
        twist(&small, 3.0, 0.15);
        const size_t alone = detected(small);
        const Mesh joined = join(box(20, Vector3(300.0, 300.0, 300.0)), small, Vector3(400.0, 0.0, 0.0));
        check(alone > 0 && detected(joined) == alone, "a twist on a small part is found next to a big part");
        Mesh repaired = joined;
        const FoldRepair::Stats stats = FoldRepair::repair(&repaired.vertices, repaired.triangles, FoldRepair::Options());
        bool boxKept = true;
        for (size_t v = 0; v < joined.vertices.size() && v < repaired.vertices.size() - small.vertices.size(); ++v)
            boxKept = boxKept && same(joined.vertices[v], repaired.vertices[v]);
        check(stats.foldedTriangles == alone && stats.foldedTrianglesAfter * 4 < alone && boxKept,
            "the twist on the small part is repaired, the big part stays as it was");
    }
    {
        Mesh twisted = icosphere(5);
        twist(&twisted, 3.0, 0.15);
        const size_t before = detected(twisted);
        const std::vector<Vector3> input = twisted.vertices;
        FoldRepair::Stats stats = FoldRepair::repair(&twisted.vertices, twisted.triangles, FoldRepair::Options());
        std::printf("twist: %zu folded -> %zu, %zu patches, %zu moved, offset %g, move %g, %.3f s\n", stats.foldedTriangles,
            stats.foldedTrianglesAfter, stats.repairedPatches, stats.movedVertices, stats.maximumOffset, stats.maximumMove, stats.seconds);
        check(before > 0 && stats.foldedTriangles == before, "a twist past one turn is flagged");
        check(stats.foldedTrianglesAfter * 4 < before, "the repair removes most of the twist's folds");
        double farthest = 0.0, offSphere = 0.0;
        for (size_t v = 0; v < input.size(); ++v) {
            if (same(input[v], twisted.vertices[v]))
                continue;
            farthest = std::max(farthest, (input[v] - Vector3(0, 0, 1)).length());
            offSphere = std::max(offSphere, std::abs(twisted.vertices[v].length() - 1.0));
        }
        check(farthest < 0.3, "only vertices near the twist move");
        check(offSphere < 0.01, "moved vertices stay on the sphere");
        check(detected(twisted) == stats.foldedTrianglesAfter, "the reported count matches the detector");

        std::vector<Vector3> again = input;
        FoldRepair::repair(&again, twisted.triangles, FoldRepair::Options());
        check(same(again, twisted.vertices), "the repair gives the same result on every run");
    }
    {
        Mesh sheet = pleat(200, 0.02, 0.005);
        const std::vector<Vector3> input = sheet.vertices;
        const FoldRepair::Stats stats = FoldRepair::repair(&sheet.vertices, sheet.triangles, FoldRepair::Options());
        std::printf("pleat: %zu folded -> %zu, %zu patches, %zu moved, offset %g\n", stats.foldedTriangles,
            stats.foldedTrianglesAfter, stats.repairedPatches, stats.movedVertices, stats.maximumOffset);
        check(stats.foldedTriangles > 0, "a pleat is flagged");
        check(stats.foldedTrianglesAfter * 4 < stats.foldedTriangles, "the repair removes most of the pleat's folds");
        bool border = true;
        for (size_t v = 0; v < input.size(); ++v)
            if ((std::abs(std::abs(input[v].y()) - 1.0) < 1e-12 || std::abs(input[v].x()) > 0.5) && !same(input[v], sheet.vertices[v]))
                border = false;
        check(border, "the sheet's border and its parts away from the pleat stay where they were");
    }
    {
        // A twist on the top of a box, centred one radius from an edge:
        // its folds reach the crease, which must not move.
        Mesh cube = box(60, Vector3(1.0, 1.0, 1.0));
        const Vector3 centre(0.5 - 0.02, 0.0, 0.5);
        for (Vector3& v : cube.vertices) {
            if (v.z() < 0.5 || std::abs(v.x()) >= 0.5 || std::abs(v.y()) >= 0.5)
                continue;
            const double d = (v - centre).length();
            if (d >= 0.12)
                continue;
            const double s = d / 0.12;
            const double angle = 2.0 * pi * 2.0 * (1.0 - 3.0 * s * s + 2.0 * s * s * s);
            const double x = v.x() - centre.x(), y = v.y() - centre.y();
            v = Vector3(centre.x() + x * std::cos(angle) - y * std::sin(angle), centre.y() + x * std::sin(angle) + y * std::cos(angle), v.z());
        }
        const std::vector<Vector3> input = cube.vertices;
        const FoldRepair::Stats stats = FoldRepair::repair(&cube.vertices, cube.triangles, FoldRepair::Options());
        std::printf("box edge: %zu folded -> %zu, %zu patches, %zu moved, offset %g\n", stats.foldedTriangles,
            stats.foldedTrianglesAfter, stats.repairedPatches, stats.movedVertices, stats.maximumOffset);
        bool edgeKept = true, onTop = true;
        for (size_t v = 0; v < input.size(); ++v) {
            const bool onEdge = std::abs(std::abs(input[v].z()) - 0.5) < 1e-12
                && (std::abs(std::abs(input[v].x()) - 0.5) < 1e-12 || std::abs(std::abs(input[v].y()) - 0.5) < 1e-12);
            if (onEdge && !same(input[v], cube.vertices[v]))
                edgeKept = false;
            if (!same(input[v], cube.vertices[v]) && std::abs(cube.vertices[v].z() - 0.5) > 1e-9)
                onTop = false;
        }
        check(stats.foldedTriangles > 0 && stats.foldedTrianglesAfter < stats.foldedTriangles, "a twist next to a box edge is repaired");
        check(edgeKept, "the box edge does not move");
        check(onTop, "moved vertices stay on the top face");
    }
    {
        // A needle pulled out of a sphere and leaning past its base: its
        // flank folds over and untangling cannot fix it without moving the
        // tip off the surface.  Spike flattening lowers the tip, only when
        // asked for; a needle that does not fold stays.
        Mesh needle = icosphere(6);
        size_t tip = 0;
        for (size_t v = 0; v < needle.vertices.size(); ++v)
            if (needle.vertices[v].z() > needle.vertices[tip].z())
                tip = v;
        Mesh upright = needle;
        upright.vertices[tip] = upright.vertices[tip] * 1.05;
        needle.vertices[tip] = needle.vertices[tip] * 1.02 + Vector3(0.05, 0.0, 0.0);
        const std::vector<Vector3> input = needle.vertices;
        Mesh plain = needle;
        const FoldRepair::Stats off = FoldRepair::repair(&plain.vertices, plain.triangles, FoldRepair::Options());
        FoldRepair::Options flatten;
        flatten.flattenSpikes = true;
        const FoldRepair::Stats on = FoldRepair::repair(&needle.vertices, needle.triangles, flatten);
        std::printf("needle: %zu folded -> %zu without spikes, %zu with, %zu spikes, drop %g\n", off.foldedTriangles,
            off.foldedTrianglesAfter, on.foldedTrianglesAfter, on.flattenedSpikes, on.maximumSpikeDrop);
        check(off.foldedTrianglesAfter > 0 && 0 == off.flattenedSpikes && same(input, plain.vertices),
            "without spike flattening a folded needle is left as it was");
        check(1 == on.flattenedSpikes && 0 == on.foldedTrianglesAfter, "spike flattening lowers the needle and clears its fold");
        bool onlyTip = true;
        for (size_t v = 0; v < input.size(); ++v)
            if (v != tip && !same(input[v], needle.vertices[v]))
                onlyTip = false;
        check(onlyTip && std::abs(on.maximumSpikeDrop - (input[tip] - needle.vertices[tip]).length()) < 1e-12,
            "only the tip moves, by the reported drop");
        const std::vector<Vector3> standing = upright.vertices;
        const FoldRepair::Stats kept = FoldRepair::repair(&upright.vertices, upright.triangles, flatten);
        check(0 == kept.foldedTriangles && 0 == kept.flattenedSpikes && same(standing, upright.vertices),
            "a needle that does not fold stays");
    }
    return failures ? 1 : 0;
}
