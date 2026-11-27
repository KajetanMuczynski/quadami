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
#include <AutoRemesher/GuideRings>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <unordered_map>

namespace AutoRemesher {
namespace {
    const size_t none = std::numeric_limits<size_t>::max();
    // As QuadExtractor::kPinnedToleranceOfEdge: an output vertex on an
    // isoline that runs along T's edge is on it up to rounding.
    const double onEdgeTolerance = 1e-6;
    // A chain is a guide ring when at least this share of its length runs
    // between faces of T within the smooth angle.
    const double smoothShare = 0.9;
    // A vertex bridging two ring vertices that are not joined lies within
    // this share of their distance of the loop: a cone the cleanup merged
    // a ring vertex into sits up to a row away.
    const double bridgeReach = 1.0;

    uint64_t directedKey(size_t a, size_t b)
    {
        return ((uint64_t)a << 32) | (uint64_t)(uint32_t)b;
    }

    struct Loop {
        // T's vertices in order, and the arc length at each (one more entry
        // than vertices: the total).
        std::vector<size_t> vertices;
        std::vector<double> arc;
    };

    // The closed chains of hard edges through a smooth part of T.
    std::vector<Loop> smoothHardLoops(const FeatureEdgeSet& set, const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles, double smoothDegrees)
    {
        std::unordered_map<size_t, std::vector<size_t>> neighbours;
        std::unordered_map<uint64_t, double> turn;
        for (size_t i = 0; i < set.edges.size(); ++i) {
            if (0 == (set.flags[i] & FeatureEdgeSet::Hard))
                continue;
            const size_t a = set.edges[i].first, b = set.edges[i].second;
            if (a == b)
                continue;
            neighbours[a].push_back(b);
            neighbours[b].push_back(a);
            turn[directedKey(std::min(a, b), std::max(a, b))] = -1.0;
        }
        if (neighbours.empty())
            return {};
        // The angle between the two faces of T at each hard edge.
        std::unordered_map<uint64_t, Vector3> firstNormal;
        for (const auto& triangle : triangles) {
            if (3 != triangle.size())
                continue;
            for (size_t l = 0; l < 3; ++l) {
                const size_t a = triangle[l], b = triangle[(l + 1) % 3];
                const uint64_t key = directedKey(std::min(a, b), std::max(a, b));
                const auto found = turn.find(key);
                if (found == turn.end())
                    continue;
                const Vector3 normal = Vector3::normal(vertices[triangle[0]], vertices[triangle[1]], vertices[triangle[2]]);
                const auto other = firstNormal.find(key);
                if (other == firstNormal.end())
                    firstNormal[key] = normal;
                else
                    found->second = std::acos(std::max(-1.0, std::min(1.0, Vector3::dotProduct(other->second, normal))));
            }
        }
        std::vector<size_t> starts;
        starts.reserve(neighbours.size());
        for (const auto& item : neighbours)
            starts.push_back(item.first);
        std::sort(starts.begin(), starts.end());
        std::unordered_map<size_t, char> seen;
        std::vector<Loop> loops;
        const double smooth = smoothDegrees * M_PI / 180.0;
        for (const size_t start : starts) {
            if (seen[start])
                continue;
            // The component, and whether every vertex of it has two edges.
            std::vector<size_t> stack = { start }, component;
            bool cycle = true;
            while (!stack.empty()) {
                const size_t v = stack.back();
                stack.pop_back();
                if (seen[v])
                    continue;
                seen[v] = 1;
                component.push_back(v);
                const auto& around = neighbours[v];
                if (2 != around.size() || around[0] == around[1])
                    cycle = false;
                for (const size_t n : around)
                    stack.push_back(n);
            }
            if (!cycle || component.size() < 3)
                continue;
            Loop loop;
            size_t previous = none, current = start;
            double length = 0.0, smoothLength = 0.0;
            do {
                loop.vertices.push_back(current);
                loop.arc.push_back(length);
                const auto& around = neighbours[current];
                const size_t following = around[0] != previous ? around[0] : around[1];
                const double edge = (vertices[following] - vertices[current]).length();
                const double angle = turn[directedKey(std::min(current, following), std::max(current, following))];
                if (angle >= 0.0 && angle <= smooth)
                    smoothLength += edge;
                length += edge;
                previous = current;
                current = following;
            } while (current != start && loop.vertices.size() <= component.size());
            loop.arc.push_back(length);
            if (current != start || !(length > 0.0) || smoothLength < smoothShare * length)
                continue;
            loops.push_back(std::move(loop));
        }
        return loops;
    }

    Vector3 closestOnLoop(const Loop& loop, const std::vector<Vector3>& vertices, const Vector3& point)
    {
        double best = std::numeric_limits<double>::max();
        Vector3 closest = point;
        for (size_t i = 0; i < loop.vertices.size(); ++i) {
            const Vector3& a = vertices[loop.vertices[i]];
            const Vector3 ab = vertices[loop.vertices[(i + 1) % loop.vertices.size()]] - a;
            const double lengthSquared = Vector3::dotProduct(ab, ab);
            double t = lengthSquared > 0.0 ? Vector3::dotProduct(point - a, ab) / lengthSquared : 0.0;
            t = std::max(0.0, std::min(1.0, t));
            const Vector3 candidate = a + ab * t;
            const double distance = (point - candidate).lengthSquared();
            if (distance < best) {
                best = distance;
                closest = candidate;
            }
        }
        return closest;
    }

    double distanceToLoop(const Loop& loop, const std::vector<Vector3>& vertices, const Vector3& point)
    {
        return (point - closestOnLoop(loop, vertices, point)).length();
    }

    struct Fans {
        // Per cycle vertex: its faces on the left and on the right of the
        // cycle's direction, and whether its fan of faces is closed.
        std::vector<std::vector<size_t>> left, right;
        std::vector<char> closed;
        size_t poles = 0, bends = 0, open = 0;
        bool needsLeft = false, needsRight = false;
    };

    class Rings {
    public:
        Rings(std::vector<Vector3>* positions, std::vector<std::vector<size_t>>* faces)
            : m_positions(*positions)
            , m_faces(*faces)
        {
            index();
        }

        void index()
        {
            m_faceOf.clear();
            m_faceOf.reserve(4 * m_faces.size());
            m_incident.assign(m_positions.size(), 0);
            m_neighbours.assign(m_positions.size(), {});
            for (size_t f = 0; f < m_faces.size(); ++f) {
                const auto& face = m_faces[f];
                for (size_t i = 0; i < face.size(); ++i) {
                    m_faceOf[directedKey(face[i], face[(i + 1) % face.size()])] = f;
                    ++m_incident[face[i]];
                    m_neighbours[face[i]].push_back(face[(i + 1) % face.size()]);
                }
            }
            for (auto& around : m_neighbours) {
                std::sort(around.begin(), around.end());
                around.erase(std::unique(around.begin(), around.end()), around.end());
            }
        }

        bool joined(size_t a, size_t b) const
        {
            return m_faceOf.count(directedKey(a, b)) && m_faceOf.count(directedKey(b, a));
        }

        // The faces around `v` from the one left of the edge v -> from, up
        // to the edge to -> v, turning clockwise... in face order: each
        // face is entered across the edge it shares with the one before.
        // Returns false when the fan runs into a boundary or does not
        // reach `to`.
        bool walk(size_t v, size_t from, size_t to, std::vector<size_t>* faces) const
        {
            faces->clear();
            size_t spoke = from;
            for (size_t step = 0; step < 64; ++step) {
                const auto found = m_faceOf.find(directedKey(v, spoke));
                if (found == m_faceOf.end())
                    return false;
                const auto& face = m_faces[found->second];
                const size_t at = std::find(face.begin(), face.end(), v) - face.begin();
                if (at >= face.size())
                    return false;
                faces->push_back(found->second);
                spoke = face[(at + face.size() - 1) % face.size()];
                if (spoke == to)
                    return true;
                if (spoke == from)
                    return false;
            }
            return false;
        }

        Fans classify(const std::vector<size_t>& cycle) const
        {
            const size_t n = cycle.size();
            Fans fans;
            fans.left.resize(n);
            fans.right.resize(n);
            fans.closed.assign(n, 0);
            for (size_t i = 0; i < n; ++i) {
                const size_t v = cycle[i], previous = cycle[(i + n - 1) % n], next = cycle[(i + 1) % n];
                const bool closed = walk(v, next, previous, &fans.left[i]) && walk(v, previous, next, &fans.right[i])
                    && fans.left[i].size() + fans.right[i].size() == m_incident[v];
                fans.closed[i] = closed;
                if (!closed) {
                    ++fans.open;
                    continue;
                }
                // k edges leave the ring on a side that has k + 1 faces.
                const size_t kl = fans.left[i].size() - 1, kr = fans.right[i].size() - 1;
                if (kl + kr != 2)
                    ++fans.poles;
                else if (1 != kl)
                    ++fans.bends;
                fans.needsLeft = fans.needsLeft || 1 != kl;
                fans.needsRight = fans.needsRight || 1 != kr;
            }
            return fans;
        }

        // One row of quads along the side of `cycle` whose faces are
        // `sideFaces` (per cycle vertex): the left side for `forward`.
        size_t pad(const std::vector<size_t>& cycle, const std::vector<std::vector<size_t>>& sideFaces, bool forward)
        {
            const size_t n = cycle.size();
            const size_t base = m_positions.size();
            std::vector<Vector3> copies(n);
            for (size_t i = 0; i < n; ++i) {
                // Half a row in: the mean of the side's face centres.
                Vector3 centre;
                for (const size_t f : sideFaces[i]) {
                    Vector3 faceCentre;
                    for (const size_t v : m_faces[f])
                        faceCentre += m_positions[v];
                    centre += faceCentre / (double)m_faces[f].size();
                }
                copies[i] = centre / (double)sideFaces[i].size();
            }
            for (size_t i = 0; i < n; ++i)
                for (const size_t f : sideFaces[i])
                    for (size_t& v : m_faces[f])
                        if (v == cycle[i])
                            v = base + i;
            m_positions.insert(m_positions.end(), copies.begin(), copies.end());
            for (size_t i = 0; i < n; ++i) {
                const size_t j = (i + 1) % n;
                if (forward)
                    m_faces.push_back({ cycle[i], cycle[j], base + j, base + i });
                else
                    m_faces.push_back({ cycle[j], cycle[i], base + i, base + j });
            }
            return n;
        }

        const std::vector<size_t>& neighbours(size_t v) const
        {
            return m_neighbours[v];
        }

    private:
        std::vector<Vector3>& m_positions;
        std::vector<std::vector<size_t>>& m_faces;
        std::unordered_map<uint64_t, size_t> m_faceOf;
        std::vector<size_t> m_incident;
        // Per vertex, the ends of its outgoing half edges.
        std::vector<std::vector<size_t>> m_neighbours;
    };
}

GuideRingStats padGuideRings(std::vector<Vector3>* positions, std::vector<std::vector<size_t>>* faces,
    const FeatureEdgeSet& featureEdges, const std::vector<Vector3>& triangleVertices,
    const std::vector<std::vector<size_t>>& triangles, bool pad, double smoothDegrees)
{
    GuideRingStats stats;
    if (featureEdges.empty() || positions->empty() || faces->empty())
        return stats;
    const std::vector<Loop> loops = smoothHardLoops(featureEdges, triangleVertices, triangles, smoothDegrees);
    stats.loops = loops.size();
    if (loops.empty())
        return stats;
    if (nullptr != std::getenv("AUTOREMESHER_GUIDE_RINGS_DEBUG")) {
        for (size_t l = 0; l < loops.size(); ++l) {
            std::cerr << "guide chain: loop " << l;
            for (const size_t v : loops[l].vertices)
                std::cerr << " " << triangleVertices[v].x() << "," << triangleVertices[v].y() << "," << triangleVertices[v].z();
            std::cerr << std::endl;
        }
        // The turn of each chain in the surface, per vertex in degrees: the
        // angles of T's triangles at the vertex on one side of the chain,
        // against half of all of them (a straight line over a ridge turns 0).
        // -1 where the fan around the vertex is not a closed disc.
        std::unordered_map<size_t, std::vector<size_t>> around;
        for (const Loop& loop : loops)
            for (const size_t v : loop.vertices)
                around[v];
        for (size_t t = 0; t < triangles.size(); ++t)
            for (const size_t v : triangles[t]) {
                const auto found = around.find(v);
                if (around.end() != found)
                    found->second.push_back(t);
            }
        for (size_t l = 0; l < loops.size(); ++l) {
            const std::vector<size_t>& chain = loops[l].vertices;
            std::cerr << "guide chain turns: loop " << l;
            for (size_t i = 0; i < chain.size(); ++i) {
                const size_t v = chain[i], previous = chain[(i + chain.size() - 1) % chain.size()], next = chain[(i + 1) % chain.size()];
                std::unordered_map<size_t, std::pair<size_t, double>> fan;
                double total = 0.0;
                for (const size_t t : around[v]) {
                    const std::vector<size_t>& triangle = triangles[t];
                    const size_t k = triangle[0] == v ? 0 : (triangle[1] == v ? 1 : 2);
                    const size_t a = triangle[(k + 1) % 3], b = triangle[(k + 2) % 3];
                    const double angle = Vector3::angle(triangleVertices[a] - triangleVertices[v], triangleVertices[b] - triangleVertices[v]);
                    fan[a] = std::make_pair(b, angle);
                    total += angle;
                }
                double side = 0.0;
                size_t at = next, steps = 0;
                while (at != previous && steps <= fan.size()) {
                    const auto found = fan.find(at);
                    if (fan.end() == found)
                        break;
                    side += found->second.second;
                    at = found->second.first;
                    ++steps;
                }
                if (at != previous || total <= 0.0)
                    std::cerr << " -1";
                else
                    std::cerr << " " << std::abs(180.0 - 360.0 * side / total);
            }
            std::cerr << std::endl;
        }
    }

    // The output vertices on each loop, by arc length along it.
    const double meanLength = featureEdgeLength(featureEdges, triangleVertices) / featureEdges.size();
    const std::vector<char> onHard = pointsOnFeatureEdges(*positions, featureEdges, triangleVertices,
        FeatureEdgeSet::Hard, onEdgeTolerance * meanLength);
    std::vector<std::vector<std::pair<double, size_t>>> onLoop(loops.size());
    const double reach = 4.0 * onEdgeTolerance * meanLength;
    for (size_t p = 0; p < positions->size(); ++p) {
        if (!onHard[p])
            continue;
        const Vector3& point = (*positions)[p];
        double best = reach * reach, bestArc = 0.0;
        size_t bestLoop = none;
        for (size_t l = 0; l < loops.size(); ++l) {
            const Loop& loop = loops[l];
            for (size_t i = 0; i < loop.vertices.size(); ++i) {
                const Vector3& a = triangleVertices[loop.vertices[i]];
                const Vector3 ab = triangleVertices[loop.vertices[(i + 1) % loop.vertices.size()]] - a;
                const double lengthSquared = Vector3::dotProduct(ab, ab);
                double t = lengthSquared > 0.0 ? Vector3::dotProduct(point - a, ab) / lengthSquared : 0.0;
                t = std::max(0.0, std::min(1.0, t));
                const Vector3 offset = point - (a + ab * t);
                const double distance = Vector3::dotProduct(offset, offset);
                if (distance < best) {
                    best = distance;
                    bestLoop = l;
                    bestArc = loop.arc[i] + t * (loop.arc[i + 1] - loop.arc[i]);
                }
            }
        }
        if (none != bestLoop)
            onLoop[bestLoop].push_back({ bestArc, p });
    }

    Rings rings(positions, faces);
    std::vector<std::vector<size_t>> cycles;
    // The loop each output vertex is on, and its arc length there.
    std::vector<size_t> loopOf(positions->size(), none);
    std::vector<double> arcOf(positions->size(), 0.0);
    for (size_t l = 0; l < loops.size(); ++l)
        for (const auto& item : onLoop[l]) {
            loopOf[item.second] = l;
            arcOf[item.second] = item.first;
        }
    for (size_t l = 0; l < loops.size(); ++l) {
        auto& on = onLoop[l];
        if (on.size() < 3) {
            ++stats.emptyLoops;
            continue;
        }
        std::sort(on.begin(), on.end());
        // The ring in the output: the edges between the loop's vertices
        // that run along it. An edge across a bend of the ring (a chord)
        // has its middle off the loop.
        std::unordered_map<size_t, std::vector<size_t>> ringEdges;
        std::vector<std::pair<size_t, Vector3>> bridges;
        const double total = loops[l].arc.back();
        const auto arcBetween = [&](size_t a, size_t b) {
            return std::fabs(std::remainder(arcOf[a] - arcOf[b], total));
        };
        for (const auto& item : on) {
            const size_t a = item.second;
            ringEdges[a];
            for (const size_t b : rings.neighbours(a))
                if (b > a && b < loopOf.size() && loopOf[b] == l && rings.joined(a, b)) {
                    ringEdges[a].push_back(b);
                    ringEdges[b].push_back(a);
                }
        }
        // An edge across a bend of the ring (a chord) joins vertices that
        // are not next to each other along the loop: a vertex keeps its two
        // edges to the nearest ones, and an edge has to be kept at both ends.
        {
            std::vector<std::pair<size_t, size_t>> dropped;
            for (auto& item : ringEdges) {
                auto& around = item.second;
                if (around.size() <= 2)
                    continue;
                const size_t a = item.first;
                std::sort(around.begin(), around.end(), [&](size_t x, size_t y) {
                    const double dx = arcBetween(a, x), dy = arcBetween(a, y);
                    return dx < dy || (dx == dy && x < y);
                });
                for (size_t k = 2; k < around.size(); ++k)
                    dropped.push_back({ a, around[k] });
            }
            for (const auto& edge : dropped)
                for (const auto& ends : { std::make_pair(edge.first, edge.second), std::make_pair(edge.second, edge.first) }) {
                    auto& around = ringEdges[ends.first];
                    around.erase(std::remove(around.begin(), around.end(), ends.second), around.end());
                }
        }
        // A ring vertex the smoothing moved off T's edge (it was not on it
        // up to rounding, so nothing held it) is missing: a loose end of
        // the ring is joined to another through the vertex next to both
        // that lies closest to the loop.
        for (const auto& item : on) {
            const size_t a = item.second;
            if (ringEdges[a].size() >= 2)
                continue;
            size_t best = none, bestEnd = none;
            double bestDistance = std::numeric_limits<double>::max(), bestSpan = 0.0;
            for (const size_t w : rings.neighbours(a)) {
                if (w >= loopOf.size() || none != loopOf[w] || onHard[w] || !rings.joined(a, w))
                    continue;
                for (const size_t b : rings.neighbours(w)) {
                    if (b == a || b >= loopOf.size() || loopOf[b] != l || ringEdges[b].size() >= 2 || !rings.joined(w, b))
                        continue;
                    const double span = ((*positions)[a] - (*positions)[b]).length();
                    const double distance = distanceToLoop(loops[l], triangleVertices, (*positions)[w]);
                    if (distance <= bridgeReach * span && distance < bestDistance) {
                        bestDistance = distance;
                        bestSpan = span;
                        best = w;
                        bestEnd = b;
                    }
                }
            }
            if (none == best)
                continue;
            loopOf[best] = l;
            arcOf[best] = arcOf[a] + std::remainder(arcOf[bestEnd] - arcOf[a], total) / 2.0;
            ringEdges[a].push_back(best);
            ringEdges[best].push_back(a);
            ringEdges[best].push_back(bestEnd);
            ringEdges[bestEnd].push_back(best);
            ++stats.bridged;
            stats.bridgeDistance = std::max(stats.bridgeDistance, bestDistance / bestSpan);
            bridges.push_back({ best, closestOnLoop(loops[l], triangleVertices, ((*positions)[a] + (*positions)[bestEnd]) / 2.0) });
        }
        size_t gaps = 0;
        for (const auto& item : ringEdges)
            if (2 != item.second.size())
                ++gaps;
        // Back onto the loop, where the projection after this keeps them.
        if (pad && 0 == gaps)
            for (const auto& bridge : bridges)
                (*positions)[bridge.first] = bridge.second;
        std::vector<size_t> cycle;
        if (0 == gaps) {
            const size_t start = on.front().second;
            size_t previous = none, current = start;
            do {
                cycle.push_back(current);
                const auto& around = ringEdges[current];
                const size_t following = around[0] != previous ? around[0] : around[1];
                previous = current;
                current = following;
            } while (current != start && cycle.size() <= ringEdges.size());
            // More than one cycle on the loop: not a ring.
            if (current != start || cycle.size() != ringEdges.size())
                gaps = 1;
        }
        if (gaps > 0 && nullptr != std::getenv("AUTOREMESHER_GUIDE_RINGS_DEBUG"))
            for (const auto& item : ringEdges)
                if (2 != item.second.size()) {
                    const Vector3& q = (*positions)[item.first];
                    std::cerr << "guide ring: loop " << l << " vertex " << item.first << " at " << q.x() << " " << q.y() << " " << q.z()
                              << " has " << item.second.size() << " ring edges" << std::endl;
                }
        if (gaps > 0) {
            ++stats.brokenLoops;
            stats.gaps += gaps;
            continue;
        }
        // In the loop's direction.
        {
            double turned = 0.0;
            for (size_t i = 0; i < cycle.size(); ++i)
                turned += std::remainder(arcOf[cycle[(i + 1) % cycle.size()]] - arcOf[cycle[i]], total);
            if (turned < 0.0)
                std::reverse(cycle.begin(), cycle.end());
        }
        ++stats.cycles;
        const Fans fans = rings.classify(cycle);
        stats.ringVertices += cycle.size();
        stats.poles += fans.poles;
        stats.bends += fans.bends;
        stats.openFans += fans.open;
        if (0 == fans.open && !fans.needsLeft && !fans.needsRight)
            ++stats.straightBefore;
        if (pad && 0 == fans.open) {
            if (fans.needsLeft) {
                stats.addedFaces += rings.pad(cycle, fans.left, true);
                ++stats.paddedSides;
            }
            if (fans.needsRight) {
                stats.addedFaces += rings.pad(cycle, fans.right, false);
                ++stats.paddedSides;
            }
        }
        cycles.push_back(std::move(cycle));
    }
    if (stats.paddedSides > 0)
        rings.index();
    for (const auto& cycle : cycles) {
        const Fans fans = rings.classify(cycle);
        if (0 == fans.open && !fans.needsLeft && !fans.needsRight)
            ++stats.straightAfter;
    }
    return stats;
}

}
