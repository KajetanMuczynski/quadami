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
#ifndef AUTO_REMESHER_FOLD_REPAIR_H
#define AUTO_REMESHER_FOLD_REPAIR_H

#include <AutoRemesher/Vector3>
#include <cstddef>
#include <vector>

namespace AutoRemesher {

// Folded-over input: triangles a brush stroke turned upside down (a twist,
// a crease pinched past its own line, a pleat).  On sculpts nearly every
// non-quad, folded quad and hole of the output sits on one, because the
// isotropic remesh, the sharp-edge test and the map all inherit the fold
// (research/sculpt-defects-2026-10-21.md).
//
// detect() flags a triangle whose normal points against the area-weighted
// mean normal of the surface within geodesic distance `radius` of it, by
// more than rounding (distances between centroids of edge-adjacent
// triangles; the triangles sharing an edge with it always count).
// Geodesic, so the far side of a thin part or another island nearby does
// not count.  A fold wider than about the radius is not seen: its middle
// agrees with its own neighbourhood.  A spot (flagged triangles sharing a
// vertex) counts only if it turns over: one of its triangles faces away
// from a neighbour across an edge by more than 90 degrees.  Smooth surface
// that bends back within the radius (a vine or tube thinner than it, an
// overhang) is not a fold.  Parts that share no vertex are tested each with
// its own radius, so joining parts changes nothing on either.
//
// repair() untangles each cluster of flagged triangles in a local chart and
// slides the moved vertices back onto the original surface.  Only vertex
// positions change: no triangle is added, removed or reconnected, so the
// topology stays as it was.  Per cluster, k rings around it (k = 1, 2, ...)
// are freed and laid out on the plane of the detector's mean normal there;
// the ring outside them, open boundaries and the vertices of crease edges
// that are not part of the fold stay fixed.  The free vertices are moved by
// the Untangler, with each triangle's target its own chart shape (or, where
// it is folded, its 3D shape laid flat), so only the fold has to move, then
// each moves back towards its old place as far as its triangles stay
// positive, and finally onto the original triangle under its new chart
// position.  A patch is kept only if it is a disk on manifold edges, it is
// mostly not steep over the chart, every triangle ends up positive, no
// vertex ends up further than maximumOffset from the original surface, and
// the detector flags fewer triangles around it than before and none it did
// not flag before.  Otherwise that spot is left as it was.
class FoldRepair {
public:
    struct Options {
        // Detector radius; <= 0 takes radiusFraction of the bounding box
        // diagonal of each part.
        double radius = 0.0;
        double radiusFraction = 0.01;
        size_t maximumRings = 8;
        // At most this many passes; the repair stops once a pass moves
        // nothing.  A patch that overlaps one applied in the same pass, or a
        // spot a patch only made smaller, waits for a later pass: folds in
        // a twist pleat can take 7.
        size_t passes = 10;
        // An edge whose triangles' normals differ by more than creaseDegrees
        // and less than foldDegrees, neither triangle flagged, is a crease:
        // its vertices stay fixed.  Beyond foldDegrees one triangle is
        // turned over against the other, so the edge is part of a fold.
        double creaseDegrees = 60.0;
        double foldDegrees = 150.0;
        // An unflagged, well-shaped triangle that faces the chart normal by
        // less than this cosine stands steep; a patch whose area is more
        // than maximumSteepShare steep (a spike's flank, an edge-on flap) is
        // left alone.
        double steepCosine = 0.5;
        double maximumSteepShare = 0.25;
        // Largest distance of a moved vertex from the original surface, as a
        // fraction of the radius.
        double maximumOffset = 0.25;
        // Off by default, since it changes the shape: where a spot cannot be
        // untangled, lower the tips of needles within the radius of it
        // (interior vertices with an angle sum below spikeAngleSum, flat is
        // 2 pi) to
        // the mean of their ring, if that is at most maximumSpikeDrop (a
        // fraction of the radius) away.  Kept under the same detector test
        // as a patch; maximumOffset does not apply.
        bool flattenSpikes = false;
        double spikeAngleSum = 3.14159265358979323846;
        double maximumSpikeDrop = 2.0;
    };

    struct Stats {
        // The largest radius over the parts.
        double radius = 0.0;
        // Before the repair: flagged triangles and their clusters (sharing
        // a vertex).
        size_t foldedTriangles = 0;
        size_t clusters = 0;
        // Patches moved, over all passes.
        size_t repairedPatches = 0;
        // After the repair.
        size_t foldedTrianglesAfter = 0;
        size_t clustersAfter = 0;
        size_t movedVertices = 0;
        // Largest distance of a moved vertex from the original surface, and
        // largest move, as fractions of the radius of its part.
        double maximumOffset = 0.0;
        double maximumMove = 0.0;
        // Spike tips lowered, and the largest drop, in model units.
        size_t flattenedSpikes = 0;
        double maximumSpikeDrop = 0.0;
        double seconds = 0.0;
    };

    static double defaultRadius(const std::vector<Vector3>& vertices, const Options& options);

    // One flag per triangle.  Triangles must have three corners.  This one
    // tests every part with the given radius.
    static std::vector<char> detect(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles,
        double radius);
    // Each part with its radius from the options, as survey() and repair().
    static std::vector<char> detect(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles,
        const Options& options);

    // Detection only: the counts repair() would start from, nothing moved.
    static Stats survey(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles,
        const Options& options);

    static Stats repair(std::vector<Vector3>* vertices,
        const std::vector<std::vector<size_t>>& triangles,
        const Options& options);
};

}

#endif
