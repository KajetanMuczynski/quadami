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

// The apex of Blender's default cone (angle deficit 199 degrees) came out
// cut off about one quad deep, with four valence-3 vertices around a small
// cap (TODO 2.23): the triangle remesh collapsed and relaxed the apex, and
// the extraction merged and smoothed the valence-2 vertex the field put
// there. Checked here: the output keeps a vertex at the apex, shared by two
// quads, on the default cone (deficit 199 degrees) and on a wider one (175).

#include <AutoRemesher/AutoRemesher>
#include <cmath>
#include <cstdio>
#include <vector>

using AutoRemesher::Vector3;

namespace {

struct Mesh {
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;
};

// A cone like Blender's primitive_cone_add: `segments` rim vertices of
// radius `radius` at z = -1, the apex at z = 1, the base an n-gon split into
// a fan from its first vertex.
Mesh cone(size_t segments, double radius)
{
    Mesh mesh;
    for (size_t i = 0; i < segments; ++i) {
        const double angle = 2.0 * M_PI * i / segments;
        mesh.vertices.push_back(Vector3(radius * std::sin(angle), radius * std::cos(angle), -1.0));
    }
    const size_t apex = mesh.vertices.size();
    mesh.vertices.push_back(Vector3(0.0, 0.0, 1.0));
    for (size_t i = 0; i < segments; ++i)
        mesh.triangles.push_back({ (i + 1) % segments, i, apex });
    for (size_t i = 1; i + 1 < segments; ++i)
        mesh.triangles.push_back({ 0, i, i + 1 });
    return mesh;
}

double area(const Mesh& mesh)
{
    double sum = 0.0;
    for (const auto& t : mesh.triangles)
        sum += 0.5 * Vector3::crossProduct(mesh.vertices[t[1]] - mesh.vertices[t[0]], mesh.vertices[t[2]] - mesh.vertices[t[0]]).length();
    return sum;
}

int check(const char* name, const Mesh& input, size_t targetQuads)
{
    AutoRemesher::AutoRemesher remesher(input.vertices, input.triangles);
    remesher.setTargetTriangleCount(2 * targetQuads);
    if (!remesher.remesh()) {
        std::printf("%s %zu: remesh failed\n", name, targetQuads);
        return 1;
    }
    const Vector3 apex(0.0, 0.0, 1.0);
    const std::vector<Vector3>& vertices = remesher.remeshedVertices();
    size_t nearest = 0;
    for (size_t v = 1; v < vertices.size(); ++v)
        if ((vertices[v] - apex).length() < (vertices[nearest] - apex).length())
            nearest = v;
    size_t faces = 0;
    for (const auto& face : remesher.remeshedQuads())
        for (size_t v : face)
            if (v == nearest)
                ++faces;
    // In planned quad edges, h = sqrt(area / target).
    const double gap = (vertices[nearest] - apex).length() / std::sqrt(area(input) / targetQuads);
    std::printf("%s %zu: nearest output vertex %.3f h from the apex, in %zu faces\n", name, targetQuads, gap, faces);
    // Before the fix (TODO 2.23): 1.0 h on the default cone, 0.7 h on the
    // wide one, a regular vertex in 4 faces.
    return (gap < 0.05 && 2 == faces) ? 0 : 1;
}

} // namespace

int main()
{
    int failures = 0;
    for (const size_t targetQuads : { 1000, 3000 }) {
        failures += check("cone", cone(32, 1.0), targetQuads);
        failures += check("wide cone", cone(32, 1.2), targetQuads);
    }
    if (0 != failures) {
        std::printf("FAILED: %d checks\n", failures);
        return 1;
    }
    std::printf("PASSED\n");
    return 0;
}
