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
#ifndef AUTO_REMESHER_GUIDE_RINGS_H
#define AUTO_REMESHER_GUIDE_RINGS_H

#include <AutoRemesher/FeatureEdges>
#include <AutoRemesher/Vector3>
#include <cstddef>
#include <vector>

namespace AutoRemesher {

// Closed guide loops in the extracted mesh (TODO 5.7, research prototype).
//
// A guide ring is a closed chain of T's hard edges through a smooth surface.
// The extractor puts output vertices on it (the isoline constraint), so the
// ring shows in the output as a cycle of edges between those vertices. An
// edge loop in the modelling sense (Blender's Alt+click) runs through a
// vertex only when it has four edges, two of them on the ring and one on
// each side: with kL and kR the edges that leave the ring on its left and
// right, (kL, kR) = (1, 1). A cone on the ring or within less than a row of
// it gives another pair and ends the loop there.
//
// padGuideRings() classifies every ring vertex, and with `pad` inserts one
// row of quads along each side of a ring that has a vertex with k != 1 on
// that side (pillowing): every ring vertex p gets a copy p' that takes over
// p's faces on that side, and a quad (p, next, next', p') joins each ring
// edge to its copy. Afterwards p has exactly the two ring edges, the edge
// to p' and its edges on the other side, so a ring padded on both sides is
// (1, 1) everywhere whatever it was; p' has kL + 3 edges, i.e. the cones
// move from the ring to the copy, one row off, and none is added unless
// the ring bends at a regular vertex ((0, 2) becomes a 3 and a 5).
//
// Works on any polygon mesh; a ring whose cycle is broken (two consecutive
// ring vertices not joined by an output edge) or that has a vertex without
// a closed fan of faces is left alone and counted.
struct GuideRingStats {
    // Closed smooth chains of T's hard edges.
    size_t loops = 0;
    // Loops with a closed cycle of output edges through their vertices.
    size_t cycles = 0;
    // Loops whose output vertices are not all joined in order, and the
    // number of missing joins.
    size_t brokenLoops = 0;
    size_t gaps = 0;
    // Ring vertices found off T's edge between two that are on it, and the
    // largest distance of one from the loop, in units of the distance
    // between the two it joins.
    size_t bridged = 0;
    double bridgeDistance = 0.0;
    // Loops with fewer than three output vertices on them.
    size_t emptyLoops = 0;
    // Over the cycles, before padding: ring vertices, those with kL + kR
    // != 2 (poles), those with four edges but not (1, 1) (bends), those
    // without a closed fan.
    size_t ringVertices = 0;
    size_t poles = 0;
    size_t bends = 0;
    size_t openFans = 0;
    // Cycles that were (1, 1) at every vertex before padding.
    size_t straightBefore = 0;
    // Sides padded and faces added.
    size_t paddedSides = 0;
    size_t addedFaces = 0;
    // Cycles that are (1, 1) at every vertex afterwards.
    size_t straightAfter = 0;
};

// `positions`, `faces`: the extracted mesh, before it is projected onto the
// input. `featureEdges`, `triangleVertices`, `triangles`: T and its set;
// only chains of hard edges count. `smoothDegrees`: a chain is a guide ring
// (and not a marked crease) when the faces of T across each of its edges
// are within this angle.
GuideRingStats padGuideRings(std::vector<Vector3>* positions, std::vector<std::vector<size_t>>* faces,
    const FeatureEdgeSet& featureEdges, const std::vector<Vector3>& triangleVertices,
    const std::vector<std::vector<size_t>>& triangles, bool pad, double smoothDegrees = 40.0);

}

#endif
