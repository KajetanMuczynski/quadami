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

// The counts behind the crease slide choice (TODO 2.35): an island is finished
// with and without crease slides, both go onto the input, and the one with
// fewer folded faces is kept, then fewer folded or twisted ones.  A quad
// with a corner turned against its normal counts in both; a twisted one only
// in the second when its corners all turn the same way.  revertAtBadFaces()
// puts back only the vertices of the faces left bad.

#include <AutoRemesher/QuadExtractor>
#include <cstdio>
#include <vector>

using AutoRemesher::QuadExtractor;
using AutoRemesher::Vector3;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

int main()
{
    // 0-3: a unit square; 4-7: a bow tie (corners 2 and 3 swapped, so its
    // sides cross); 8-11: a dart whose corner 10 lies inside the triangle of
    // the other three (turned at 10).
    const std::vector<Vector3> positions = {
        Vector3(0, 0, 0),
        Vector3(1, 0, 0),
        Vector3(1, 1, 0),
        Vector3(0, 1, 0),
        Vector3(0, 0, 0),
        Vector3(1, 0, 0),
        Vector3(0, 1, 0),
        Vector3(1, 1, 0),
        Vector3(0, 0, 0),
        Vector3(2, 0, 0),
        Vector3(0.5, 0.5, 0),
        Vector3(0, 2, 0),
    };
    const std::vector<std::vector<size_t>> square = { { 0, 1, 2, 3 } };
    const std::vector<std::vector<size_t>> bowTie = { { 4, 5, 6, 7 } };
    const std::vector<std::vector<size_t>> dart = { { 8, 9, 10, 11 } };

    check(0 == QuadExtractor::foldedFaceCount(positions, square), "square: not folded");
    check(0 == QuadExtractor::foldedOrTwistedFaceCount(positions, square), "square: not folded or twisted");
    check(1 == QuadExtractor::foldedOrTwistedFaceCount(positions, dart), "dart: folded or twisted");
    check(1 == QuadExtractor::foldedFaceCount(positions, dart), "dart: folded");
    check(1 == QuadExtractor::foldedOrTwistedFaceCount(positions, bowTie), "bow tie: folded or twisted");

    const std::vector<std::vector<size_t>> all = { square[0], bowTie[0], dart[0] };
    check(QuadExtractor::foldedFaceCount(positions, all) <= QuadExtractor::foldedOrTwistedFaceCount(positions, all),
        "folded faces are a subset of the folded or twisted ones");
    check(2 == QuadExtractor::foldedOrTwistedFaceCount(positions, all), "three quads: two bad");

    // Slid: corner 2 of the square crosses to the far side (folded) and
    // corner 5 of a second square moves a little (still fine).
    std::vector<Vector3> fallback = { Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(1, 1, 0), Vector3(0, 1, 0),
        Vector3(2, 0, 0), Vector3(3, 0, 0), Vector3(3, 1, 0), Vector3(2, 1, 0) };
    std::vector<Vector3> slid = fallback;
    slid[2] = Vector3(-0.5, -0.5, 0);
    slid[5] = Vector3(3.2, 0, 0);
    const std::vector<std::vector<size_t>> two = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 } };
    check(1 == QuadExtractor::foldedFaceCount(slid, two), "slid: one folded");
    check(1 == QuadExtractor::revertAtBadFaces(&slid, fallback, two, 3), "one vertex put back");
    check(0 == QuadExtractor::foldedOrTwistedFaceCount(slid, two), "reverted: none bad");
    check(-0.5 != slid[2].x() && 3.2 == slid[5].x(), "only the bad face's vertex is put back");

    if (0 == failures)
        std::printf("slide_choice: all checks passed\n");
    return 0 == failures ? 0 : 1;
}
