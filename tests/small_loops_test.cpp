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

// Small crowded closed creases (TODO 2.34): dropSmallClosedLoops drops the
// rims of a perforation and keeps a lone hole's rims, large rims, open
// chains, loops through a corner and hard loops.

#include <AutoRemesher/FeatureEdges>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using AutoRemesher::FeatureEdgeSet;
using AutoRemesher::Vector3;

bool check(bool ok, const char* what)
{
    std::printf("  %s: %s\n", ok ? "ok" : "FAILED", what);
    return ok;
}

// A circle of `n` edges, radius `r`, centre (x, y, z) in a plane z = const,
// appended to `vertices` and `set` with `flags`.
void circle(double x, double y, double z, double r, size_t n, unsigned char flags, std::vector<Vector3>* vertices,
    FeatureEdgeSet* set)
{
    const size_t first = vertices->size();
    for (size_t i = 0; i < n; ++i) {
        const double a = 2.0 * M_PI * (double)i / (double)n;
        vertices->push_back(Vector3(x + r * std::cos(a), y + r * std::sin(a), z));
    }
    for (size_t i = 0; i < n; ++i) {
        const size_t a = first + i, b = first + (i + 1) % n;
        set->edges.push_back({ std::min(a, b), std::max(a, b) });
        set->flags.push_back(flags);
    }
}

size_t dropped(const FeatureEdgeSet& set, const std::vector<Vector3>& vertices)
{
    // Rims under 2.0 long, three or more within their own length.
    size_t count = 0;
    const FeatureEdgeSet kept = AutoRemesher::dropSmallClosedLoops(set, vertices, 2.0, 3, 1.0, &count);
    if (kept.size() + count != set.size())
        return (size_t)-1;
    return count;
}

bool perforationRimsAreDropped()
{
    // 2 x 2 holes of radius 0.3 (rim length 1.88) through a plate 1 thick,
    // 1.25 apart: each rim has its partner and the neighbours' rims near.
    std::vector<Vector3> vertices;
    FeatureEdgeSet set;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (double z : { 0.0, 1.0 })
                circle(1.25 * i, 1.25 * j, z, 0.3, 16, 0, &vertices, &set);
    return check(dropped(set, vertices) == set.size(), "every rim of a 2 x 2 perforation is dropped");
}

bool loneHoleKeepsItsRims()
{
    std::vector<Vector3> vertices;
    FeatureEdgeSet set;
    circle(0, 0, 0, 0.3, 16, 0, &vertices, &set);
    circle(0, 0, 1, 0.3, 16, 0, &vertices, &set);
    bool ok = check(dropped(set, vertices) == 0, "a lone hole's two rims are kept");
    // A second hole far away (10 apart) is not a crowd either.
    circle(10, 0, 0, 0.3, 16, 0, &vertices, &set);
    circle(10, 0, 1, 0.3, 16, 0, &vertices, &set);
    ok = check(dropped(set, vertices) == 0, "two holes 10 apart keep their rims") && ok;
    return ok;
}

bool largeHardAndOpenChainsAreKept()
{
    std::vector<Vector3> vertices;
    FeatureEdgeSet set;
    // Large rims (length 6.3 > 2.0) in a crowd.
    for (int i = 0; i < 3; ++i)
        circle(2.5 * i, 0, 0, 1.0, 32, 0, &vertices, &set);
    bool ok = check(dropped(set, vertices) == 0, "rims longer than the limit are kept");

    std::vector<Vector3> hardVertices;
    FeatureEdgeSet hard;
    for (int i = 0; i < 3; ++i)
        circle(0.7 * i, 0, 0, 0.3, 16, FeatureEdgeSet::Hard, &hardVertices, &hard);
    ok = check(dropped(hard, hardVertices) == 0, "hard (marked) loops are kept") && ok;

    // Open arcs: three small rims with one edge each missing.
    std::vector<Vector3> arcVertices;
    FeatureEdgeSet arcs;
    for (int i = 0; i < 3; ++i) {
        circle(0.7 * i, 0, 0, 0.3, 16, 0, &arcVertices, &arcs);
        arcs.edges.pop_back();
        arcs.flags.pop_back();
    }
    ok = check(dropped(arcs, arcVertices) == 0, "open chains are kept") && ok;

    // Three small rims, one of them with a spur at a vertex (a corner of
    // three crease edges): that one is kept, the two others are not a crowd
    // of three on their own.
    std::vector<Vector3> spurVertices;
    FeatureEdgeSet spur;
    for (int i = 0; i < 3; ++i)
        circle(0.7 * i, 0, 0, 0.3, 16, 0, &spurVertices, &spur);
    spurVertices.push_back(Vector3(0.0, 0.0, 0.5));
    spur.edges.push_back({ 0, spurVertices.size() - 1 });
    spur.flags.push_back(0);
    ok = check(dropped(spur, spurVertices) == 0, "a loop through a corner is kept and does not count") && ok;
    return ok;
}

}

int main()
{
    int failures = 0;
    failures += perforationRimsAreDropped() ? 0 : 1;
    failures += loneHoleKeepsItsRims() ? 0 : 1;
    failures += largeHardAndOpenChainsAreKept() ? 0 : 1;
    std::printf("%s\n", 0 == failures ? "all passed" : "FAILED");
    return 0 == failures ? 0 : 1;
}
