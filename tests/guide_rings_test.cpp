/*
 *  Copyright (C) 2026 Quadami contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

// Padding of guide rings (guiderings.h, TODO 5.7).
//
// A torus of quads with a guide ring around its tube. Clean, the ring is an
// edge loop already and padding leaves the mesh alone. With a cone merged
// into the ring (a quad next to it collapsed along its diagonal: what the
// extraction makes of a cone less than half a row from a guide) the loop
// ends at two poles; padded, it closes, the mesh stays a closed manifold
// torus of quads, and the poles sit one row off the ring. The same with the
// merged vertex left off the guide, where nothing pinned it.

#include <AutoRemesher/FeatureEdges>
#include <AutoRemesher/GuideRings>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

using namespace AutoRemesher;

namespace {

const size_t around = 16, along = 12;

size_t at(size_t i, size_t j)
{
    return (j % along) * around + i % around;
}

struct Mesh {
    std::vector<Vector3> positions;
    std::vector<std::vector<size_t>> faces;
};

Mesh torus()
{
    Mesh mesh;
    for (size_t j = 0; j < along; ++j)
        for (size_t i = 0; i < around; ++i) {
            const double u = 2.0 * M_PI * j / along, v = 2.0 * M_PI * i / around;
            mesh.positions.push_back(Vector3((3.0 + std::cos(v)) * std::cos(u), (3.0 + std::cos(v)) * std::sin(u), std::sin(v)));
        }
    for (size_t j = 0; j < along; ++j)
        for (size_t i = 0; i < around; ++i)
            mesh.faces.push_back({ at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1) });
    return mesh;
}

// T: the same torus in triangles, and the ring j = ringRow as hard edges.
const size_t ringRow = 4;

void triangleMesh(std::vector<Vector3>* vertices, std::vector<std::vector<size_t>>* triangles, FeatureEdgeSet* set)
{
    const Mesh quads = torus();
    *vertices = quads.positions;
    for (const auto& face : quads.faces) {
        triangles->push_back({ face[0], face[1], face[2] });
        triangles->push_back({ face[0], face[2], face[3] });
    }
    for (size_t i = 0; i < around; ++i) {
        const size_t a = at(i, ringRow), b = at(i + 1, ringRow);
        set->edges.push_back({ a < b ? a : b, a < b ? b : a });
        set->flags.push_back(FeatureEdgeSet::Hard);
    }
    set->normalize();
}

// Collapses the quad above the ring edge (i, i + 1) along its diagonal: the
// vertex above i + 1 becomes ring vertex i (a cone that sat there merges into
// the ring).
void mergeConeIntoRing(Mesh* mesh, size_t i, bool leaveOffRing)
{
    const size_t ringVertex = at(i, ringRow), above = at(i + 1, ringRow + 1);
    std::vector<std::vector<size_t>> faces;
    for (auto face : mesh->faces) {
        for (size_t& v : face)
            if (v == above)
                v = ringVertex;
        if (std::set<size_t>(face.begin(), face.end()).size() == 4)
            faces.push_back(face);
    }
    mesh->faces = faces;
    if (leaveOffRing)
        mesh->positions[ringVertex] = mesh->positions[ringVertex] + 0.3 * (mesh->positions[above] - mesh->positions[ringVertex]);
}

// Closed manifold, every face a quad, Euler characteristic 0.
bool closedQuadTorus(const Mesh& mesh)
{
    std::map<std::pair<size_t, size_t>, int> halfEdges;
    std::set<size_t> used;
    for (const auto& face : mesh.faces) {
        if (4 != face.size())
            return false;
        for (size_t k = 0; k < 4; ++k) {
            ++halfEdges[{ face[k], face[(k + 1) % 4] }];
            used.insert(face[k]);
        }
    }
    for (const auto& item : halfEdges)
        if (1 != item.second || !halfEdges.count({ item.first.second, item.first.first }))
            return false;
    return (long)used.size() - (long)(halfEdges.size() / 2) + (long)mesh.faces.size() == 0;
}

// Blender's Alt+click from the edge a -> b: through every vertex with four
// edges on the edge opposite the incoming one; the number of edges when it
// comes back to a -> b, else 0.
size_t edgeLoopLength(const Mesh& mesh, size_t a, size_t b)
{
    std::map<std::pair<size_t, size_t>, size_t> faceOf;
    for (size_t f = 0; f < mesh.faces.size(); ++f)
        for (size_t k = 0; k < mesh.faces[f].size(); ++k)
            faceOf[{ mesh.faces[f][k], mesh.faces[f][(k + 1) % mesh.faces[f].size()] }] = f;
    size_t previous = a, current = b, length = 1;
    for (size_t step = 0; step < 4 * mesh.positions.size(); ++step) {
        // The fan around `current` starting at `previous`.
        std::vector<size_t> fan = { previous };
        size_t spoke = previous;
        for (size_t k = 0; k < 16; ++k) {
            const auto found = faceOf.find({ current, spoke });
            if (found == faceOf.end())
                return 0;
            const auto& face = mesh.faces[found->second];
            size_t index = 0;
            while (face[index] != current)
                ++index;
            spoke = face[(index + face.size() - 1) % face.size()];
            if (spoke == previous)
                break;
            fan.push_back(spoke);
        }
        if (4 != fan.size())
            return 0;
        previous = current;
        current = fan[2];
        if (previous == a && current == b)
            return length;
        ++length;
    }
    return 0;
}

size_t poles(const Mesh& mesh)
{
    std::map<size_t, std::set<size_t>> neighbours;
    for (const auto& face : mesh.faces)
        for (size_t k = 0; k < face.size(); ++k) {
            neighbours[face[k]].insert(face[(k + 1) % face.size()]);
            neighbours[face[(k + 1) % face.size()]].insert(face[k]);
        }
    size_t count = 0;
    for (const auto& item : neighbours)
        count += 4 != item.second.size();
    return count;
}

bool check(const char* name, bool condition)
{
    std::printf("%-58s %s\n", name, condition ? "ok" : "FAIL");
    return condition;
}

}

int main()
{
    std::vector<Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;
    FeatureEdgeSet set;
    triangleMesh(&vertices, &triangles, &set);
    const size_t a = at(8, ringRow), b = at(9, ringRow);
    bool ok = true;

    {
        Mesh mesh = torus();
        const Mesh before = mesh;
        const GuideRingStats stats = padGuideRings(&mesh.positions, &mesh.faces, set, vertices, triangles, true);
        ok = check("clean ring: found, straight", 1 == stats.loops && 1 == stats.cycles && 1 == stats.straightBefore && 0 == stats.poles) && ok;
        ok = check("clean ring: nothing added", 0 == stats.addedFaces && mesh.faces == before.faces && mesh.positions.size() == before.positions.size()) && ok;
    }
    for (const bool offRing : { false, true }) {
        Mesh mesh = torus();
        mergeConeIntoRing(&mesh, 2, offRing);
        std::printf("cone merged into the ring%s\n", offRing ? ", left off the guide" : "");
        ok = check("  before: closed quad torus, loop on the ring broken", closedQuadTorus(mesh) && 0 == edgeLoopLength(mesh, a, b)) && ok;
        const size_t polesBefore = poles(mesh);
        Mesh report = mesh;
        const GuideRingStats found = padGuideRings(&report.positions, &report.faces, set, vertices, triangles, false);
        ok = check("  report: ring found with its two poles, mesh untouched",
                 1 == found.cycles && 2 == found.poles && 0 == found.straightBefore && 0 == found.addedFaces
                     && (offRing ? 1u : 0u) == found.bridged && report.faces == mesh.faces)
            && ok;
        const GuideRingStats stats = padGuideRings(&mesh.positions, &mesh.faces, set, vertices, triangles, true);
        ok = check("  padded: one side, one quad per ring edge", 1 == stats.paddedSides && around == stats.addedFaces && 1 == stats.straightAfter) && ok;
        ok = check("  padded: still a closed quad torus", closedQuadTorus(mesh)) && ok;
        ok = check("  padded: the edge loop on the ring closes", around == edgeLoopLength(mesh, a, b)) && ok;
        ok = check("  padded: no pole added", poles(mesh) == polesBefore) && ok;
        bool onRing = true;
        for (size_t i = 0; i < around; ++i) {
            // The ring's vertices are where T's are.
            onRing = onRing && (mesh.positions[at(i, ringRow)] - vertices[at(i, ringRow)]).length() < (offRing && 2 == i ? 0.3 : 1e-12);
        }
        ok = check("  padded: ring vertices on the guide", onRing) && ok;
    }
    return ok ? 0 : 1;
}
