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
#include <AutoRemesher/NestedDissection>

#include <Eigen/OrderingMethods>
#include <Eigen/Sparse>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <queue>
#include <random>
#include <tbb/parallel_for.h>
#include <tbb/parallel_invoke.h>
#include <utility>

namespace AutoRemesher {
namespace {
    // Settings of the 2026-11-06 prototype (research note high-targets §7),
    // tuned on two curl systems and checked on two held-out ones: within
    // 1.10-1.12x of METIS's fill everywhere. More directions, cuts and
    // refined candidates reach 1.04-1.06x at about four times the time.

    // A part of at most this many groups is ordered by minimum degree.
    const int leafGroups = 300;
    // Bisections deeper than this fall back to minimum degree.
    const int maximumDepth = 60;
    // Cut directions on the unit sphere, besides the part's three principal
    // axes and its two graph-distance keys.
    const int sphereDirections = 6;
    // Straight cuts per key, at evenly spaced fractions in
    // [firstCutFraction, 1 - firstCutFraction] of the sorted groups.
    const int cutsPerKey = 7;
    const double firstCutFraction = 0.3;
    // The best cuts by separator size relative to the smaller side get the
    // refinement; the smallest refined separator wins.
    const int refinedCuts = 4;
    // Refinement keeps each side at most (1 + imbalance) / 2 of the part.
    const double imbalance = 0.05;
    const int refinementPasses = 10;
    const int refinementMovesWithoutGain = 2000;
    // Parts at least this large bisect their halves on separate threads.
    const int parallelGroups = 20000;

    const char sideA = 0;
    const char sideB = 1;
    const char separator = 2;

    // The unknowns and their pattern, shared read-only by every part.
    struct System {
        const int* columnStart;
        const int* rows;
        // Unknowns of group g: groupColumns[groupStart[g], groupStart[g + 1]).
        std::vector<int> groupStart;
        std::vector<int> groupColumns;
        std::vector<Vector3> positions;
        std::vector<int>* order;
    };

    // A part: groups with local indices and the graph between them.
    struct Part {
        std::vector<int> group;
        std::vector<int> start;
        std::vector<int> neighbours;

        int size() const
        {
            return (int)group.size();
        }
    };

    int unknownCount(const System& system, const std::vector<int>& groups)
    {
        int count = 0;
        for (int g : groups)
            count += system.groupStart[g + 1] - system.groupStart[g];
        return count;
    }

    // Minimum degree on the unknowns of `groups`, written to order[offset...].
    void orderByMinimumDegree(const System& system, const std::vector<int>& groups, int offset)
    {
        std::vector<int> columns;
        for (int g : groups)
            for (int k = system.groupStart[g]; k < system.groupStart[g + 1]; ++k)
                columns.push_back(system.groupColumns[k]);
        std::sort(columns.begin(), columns.end());
        const int count = (int)columns.size();
        if (count <= 1) {
            if (1 == count)
                (*system.order)[offset] = columns[0];
            return;
        }
        std::vector<Eigen::Triplet<double>> entries;
        for (int i = 0; i < count; ++i) {
            entries.emplace_back(i, i, 1.0);
            const int column = columns[i];
            for (int p = system.columnStart[column]; p < system.columnStart[column + 1]; ++p) {
                const int row = system.rows[p];
                if (row == column)
                    continue;
                const auto found = std::lower_bound(columns.begin(), columns.end(), row);
                if (found != columns.end() && *found == row)
                    entries.emplace_back((int)(found - columns.begin()), i, 1.0);
            }
        }
        Eigen::SparseMatrix<double> pattern(count, count);
        pattern.setFromTriplets(entries.begin(), entries.end());
        Eigen::PermutationMatrix<Eigen::Dynamic, Eigen::Dynamic, int> permutation;
        Eigen::AMDOrdering<int> ordering;
        ordering(pattern, permutation);
        for (int k = 0; k < count; ++k)
            (*system.order)[offset + k] = columns[permutation.indices()[k]];
    }

    void orderByMinimumDegree(const System& system, const Part& part, int offset)
    {
        orderByMinimumDegree(system, part.group, offset);
    }

    // The part made of the vertices of `part` that `keep` lists, in that order.
    Part subPart(const Part& part, const std::vector<int>& keep)
    {
        std::vector<int> local(part.size(), -1);
        for (int i = 0; i < (int)keep.size(); ++i)
            local[keep[i]] = i;
        Part result;
        result.group.reserve(keep.size());
        result.start.reserve(keep.size() + 1);
        result.start.push_back(0);
        for (int v : keep) {
            result.group.push_back(part.group[v]);
            for (int p = part.start[v]; p < part.start[v + 1]; ++p)
                if (local[part.neighbours[p]] >= 0)
                    result.neighbours.push_back(local[part.neighbours[p]]);
            result.start.push_back((int)result.neighbours.size());
        }
        return result;
    }

    // Connected components, numbered in order of their smallest vertex.
    int components(const Part& part, std::vector<int>* component)
    {
        component->assign(part.size(), -1);
        int count = 0;
        std::vector<int> queue;
        for (int seed = 0; seed < part.size(); ++seed) {
            if ((*component)[seed] >= 0)
                continue;
            queue.assign(1, seed);
            (*component)[seed] = count;
            for (size_t head = 0; head < queue.size(); ++head) {
                const int v = queue[head];
                for (int p = part.start[v]; p < part.start[v + 1]; ++p) {
                    const int w = part.neighbours[p];
                    if ((*component)[w] < 0) {
                        (*component)[w] = count;
                        queue.push_back(w);
                    }
                }
            }
            ++count;
        }
        return count;
    }

    // Breadth-first distances from `source`; returns the last vertex reached.
    int distances(const Part& part, int source, std::vector<int>* level)
    {
        level->assign(part.size(), -1);
        std::vector<int> queue(1, source);
        (*level)[source] = 0;
        int last = source;
        for (size_t head = 0; head < queue.size(); ++head) {
            const int v = queue[head];
            last = v;
            for (int p = part.start[v]; p < part.start[v + 1]; ++p) {
                const int w = part.neighbours[p];
                if ((*level)[w] < 0) {
                    (*level)[w] = (*level)[v] + 1;
                    queue.push_back(w);
                }
            }
        }
        return last;
    }

    // Replaces the edge cut between sides A and B by a minimum vertex cover
    // of the cut edges (Hopcroft-Karp matching, then Koenig's theorem).
    void coverCut(const Part& part, std::vector<char>* side)
    {
        std::vector<char>& s = *side;
        const int m = part.size();
        std::vector<int> boundaryA, boundaryB, boundaryIndex(m, -1);
        for (int v = 0; v < m; ++v) {
            for (int p = part.start[v]; p < part.start[v + 1]; ++p) {
                if (s[part.neighbours[p]] != s[v]) {
                    std::vector<int>& boundary = sideA == s[v] ? boundaryA : boundaryB;
                    boundaryIndex[v] = (int)boundary.size();
                    boundary.push_back(v);
                    break;
                }
            }
        }
        const int countA = (int)boundaryA.size();
        const int countB = (int)boundaryB.size();
        std::vector<int> edgeStart(countA + 1, 0), edges;
        for (int a = 0; a < countA; ++a) {
            const int v = boundaryA[a];
            for (int p = part.start[v]; p < part.start[v + 1]; ++p)
                if (sideB == s[part.neighbours[p]])
                    edges.push_back(boundaryIndex[part.neighbours[p]]);
            edgeStart[a + 1] = (int)edges.size();
        }
        std::vector<int> matchA(countA, -1), matchB(countB, -1), layer(countA);
        auto layered = [&]() {
            std::vector<int> queue;
            bool free = false;
            for (int a = 0; a < countA; ++a) {
                layer[a] = matchA[a] < 0 ? 0 : -1;
                if (matchA[a] < 0)
                    queue.push_back(a);
            }
            for (size_t head = 0; head < queue.size(); ++head) {
                const int a = queue[head];
                for (int e = edgeStart[a]; e < edgeStart[a + 1]; ++e) {
                    const int next = matchB[edges[e]];
                    if (next < 0)
                        free = true;
                    else if (layer[next] < 0) {
                        layer[next] = layer[a] + 1;
                        queue.push_back(next);
                    }
                }
            }
            return free;
        };
        // Depth-first search for an augmenting path along the layers, without
        // recursion: stack of (vertex of A, next edge to try).
        auto augment = [&](int root) {
            std::vector<std::pair<int, int>> stack(1, std::make_pair(root, edgeStart[root]));
            while (!stack.empty()) {
                const int a = stack.back().first;
                int& e = stack.back().second;
                if (e >= edgeStart[a + 1]) {
                    layer[a] = -1;
                    stack.pop_back();
                    continue;
                }
                const int b = edges[e++];
                const int next = matchB[b];
                if (next < 0) {
                    for (const auto& step : stack) {
                        const int matched = edges[step.second - 1];
                        matchA[step.first] = matched;
                        matchB[matched] = step.first;
                    }
                    return;
                }
                if (layer[next] == layer[a] + 1)
                    stack.push_back(std::make_pair(next, edgeStart[next]));
            }
        };
        while (layered())
            for (int a = 0; a < countA; ++a)
                if (matchA[a] < 0)
                    augment(a);
        // Koenig: Z = reachable from unmatched A by alternating paths; the
        // cover is (A outside Z) plus (B inside Z).
        std::vector<char> reachedA(countA, 0), reachedB(countB, 0);
        std::vector<int> queue;
        for (int a = 0; a < countA; ++a)
            if (matchA[a] < 0) {
                reachedA[a] = 1;
                queue.push_back(a);
            }
        for (size_t head = 0; head < queue.size(); ++head) {
            const int a = queue[head];
            for (int e = edgeStart[a]; e < edgeStart[a + 1]; ++e) {
                const int b = edges[e];
                if (reachedB[b] || matchA[a] == b)
                    continue;
                reachedB[b] = 1;
                const int next = matchB[b];
                if (next >= 0 && !reachedA[next]) {
                    reachedA[next] = 1;
                    queue.push_back(next);
                }
            }
        }
        for (int a = 0; a < countA; ++a)
            if (!reachedA[a] && edgeStart[a + 1] > edgeStart[a])
                s[boundaryA[a]] = separator;
        for (int b = 0; b < countB; ++b)
            if (reachedB[b])
                s[boundaryB[b]] = separator;
    }

    struct Move {
        int gain;
        int tie;
        int vertex;
        int tag;
        bool operator<(const Move& other) const
        {
            if (gain != other.gain)
                return gain < other.gain;
            if (tie != other.tie)
                return tie < other.tie;
            if (vertex != other.vertex)
                return vertex < other.vertex;
            return tag < other.tag;
        }
    };

    // Fiduccia-Mattheyses refinement of a vertex separator: moves separator
    // vertices to a side (pulling that vertex's neighbours on the other side
    // into the separator) while the separator shrinks, keeping the balance,
    // and rolls back to the smallest separator seen.
    void refine(const Part& part, std::vector<char>* side)
    {
        std::vector<char>& s = *side;
        const int m = part.size();
        int count[3] = { 0, 0, 0 };
        for (char c : s)
            ++count[(int)c];
        const int maximumSide = (int)((1.0 + imbalance) * 0.5 * m);
        std::vector<int> version(m, 0);
        std::vector<char> locked(m, 0);
        std::mt19937 random(12345);
        auto gainTo = [&](int v, int target) {
            int gain = 1;
            for (int p = part.start[v]; p < part.start[v + 1]; ++p)
                if (s[part.neighbours[p]] == 1 - target)
                    --gain;
            return gain;
        };
        for (int pass = 0; pass < refinementPasses; ++pass) {
            std::priority_queue<Move> queue;
            auto push = [&](int v) {
                for (int target = 0; target < 2; ++target)
                    queue.push(Move { gainTo(v, target), (int)(random() & 0xffff), v, target | (version[v] << 1) });
            };
            for (int v = 0; v < m; ++v)
                if (separator == s[v])
                    push(v);
            std::vector<std::pair<int, char>> log;
            int bestSeparator = count[separator];
            int bestImbalance = std::abs(count[sideA] - count[sideB]);
            size_t bestLog = 0;
            int sinceBest = 0;
            std::fill(locked.begin(), locked.end(), 0);
            std::vector<int> joined;
            while (!queue.empty() && sinceBest < refinementMovesWithoutGain) {
                Move move = queue.top();
                queue.pop();
                const int v = move.vertex;
                const int target = move.tag & 1;
                if ((move.tag >> 1) != version[v] || separator != s[v] || locked[v])
                    continue;
                const int gain = gainTo(v, target);
                if (gain != move.gain) {
                    move.gain = gain;
                    move.tie = (int)(random() & 0xffff);
                    queue.push(move);
                    continue;
                }
                const int pulled = 1 - gain;
                if (count[target] + 1 > maximumSide)
                    continue;
                if (count[1 - target] - pulled < 1)
                    continue;
                log.push_back(std::make_pair(v, separator));
                s[v] = (char)target;
                --count[separator];
                ++count[target];
                locked[v] = 1;
                ++version[v];
                joined.clear();
                for (int p = part.start[v]; p < part.start[v + 1]; ++p) {
                    const int w = part.neighbours[p];
                    if (s[w] == 1 - target) {
                        log.push_back(std::make_pair(w, s[w]));
                        --count[1 - target];
                        s[w] = separator;
                        ++count[separator];
                        ++version[w];
                        joined.push_back(w);
                    }
                }
                for (int p = part.start[v]; p < part.start[v + 1]; ++p) {
                    const int w = part.neighbours[p];
                    if (separator == s[w] && !locked[w]) {
                        ++version[w];
                        push(w);
                    }
                }
                for (int w : joined) {
                    if (!locked[w])
                        push(w);
                    for (int p = part.start[w]; p < part.start[w + 1]; ++p) {
                        const int u = part.neighbours[p];
                        if (separator == s[u] && !locked[u] && u != w) {
                            ++version[u];
                            push(u);
                        }
                    }
                }
                const int currentImbalance = std::abs(count[sideA] - count[sideB]);
                if (count[separator] < bestSeparator
                    || (count[separator] == bestSeparator && currentImbalance < bestImbalance)) {
                    bestSeparator = count[separator];
                    bestImbalance = currentImbalance;
                    bestLog = log.size();
                    sinceBest = 0;
                } else {
                    ++sinceBest;
                }
            }
            for (size_t k = log.size(); k > bestLog; --k) {
                const int v = log[k - 1].first;
                --count[(int)s[v]];
                s[v] = log[k - 1].second;
                ++count[(int)s[v]];
            }
            if (0 == bestLog)
                break;
        }
    }

    // Sides of the straight cut `cut` (0 .. cutsPerKey - 1) along `key`.
    void cutAlong(const std::vector<std::pair<double, int>>& sortedKey, int cut, std::vector<char>* side)
    {
        const int m = (int)sortedKey.size();
        const double fraction = cutsPerKey == 1 ? 0.5
                                                : firstCutFraction + (1.0 - 2.0 * firstCutFraction) * cut / (cutsPerKey - 1);
        const int split = (int)(fraction * m);
        side->resize(m);
        for (int r = 0; r < m; ++r)
            (*side)[sortedKey[r].second] = r < split ? sideA : sideB;
    }

    // Sort keys of the part: projections on its principal axes and on fixed
    // directions, then two breadth-first distance keys.
    std::vector<std::vector<std::pair<double, int>>> cutKeys(const System& system, const Part& part)
    {
        const int m = part.size();
        double centre[3] = { 0.0, 0.0, 0.0 };
        for (int g : part.group)
            for (int k = 0; k < 3; ++k)
                centre[k] += system.positions[g][k];
        for (int k = 0; k < 3; ++k)
            centre[k] /= m;
        double covariance[3][3] = { { 0.0 } };
        for (int g : part.group) {
            double d[3];
            for (int k = 0; k < 3; ++k)
                d[k] = system.positions[g][k] - centre[k];
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b)
                    covariance[a][b] += d[a] * d[b];
        }
        std::vector<Vector3> directions;
        // Principal axes by power iteration with deflation.
        for (int e = 0; e < 3; ++e) {
            double axis[3] = { 1.0, 0.7, 0.3 };
            double eigenvalue = 0.0;
            for (int iteration = 0; iteration < 80; ++iteration) {
                double y[3];
                for (int a = 0; a < 3; ++a)
                    y[a] = covariance[a][0] * axis[0] + covariance[a][1] * axis[1] + covariance[a][2] * axis[2];
                const double norm = std::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
                if (0.0 == norm)
                    break;
                eigenvalue = norm;
                for (int a = 0; a < 3; ++a)
                    axis[a] = y[a] / norm;
            }
            directions.push_back(Vector3(axis[0], axis[1], axis[2]));
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b)
                    covariance[a][b] -= eigenvalue * axis[a] * axis[b];
        }
        // Fibonacci points on the sphere.
        for (int i = 0; i < sphereDirections; ++i) {
            const double y = 1.0 - (i + 0.5) / sphereDirections;
            const double r = std::sqrt(1.0 - y * y);
            const double angle = 2.399963229728653 * i;
            directions.push_back(Vector3(r * std::cos(angle), y, r * std::sin(angle)));
        }
        std::vector<std::vector<std::pair<double, int>>> keys(directions.size() + 2);
        for (size_t d = 0; d < directions.size(); ++d) {
            std::vector<std::pair<double, int>>& key = keys[d];
            key.resize(m);
            for (int v = 0; v < m; ++v) {
                const Vector3& p = system.positions[part.group[v]];
                key[v] = std::make_pair((p[0] - centre[0]) * directions[d][0]
                        + (p[1] - centre[1]) * directions[d][1] + (p[2] - centre[2]) * directions[d][2],
                    v);
            }
        }
        // Distance from one end of a pseudo-diameter, and the difference of
        // the distances from both ends.
        std::vector<int> level, otherLevel;
        const int first = distances(part, 0, &level);
        const int second = distances(part, first, &level);
        distances(part, second, &otherLevel);
        std::vector<std::pair<double, int>>& fromEnd = keys[directions.size()];
        std::vector<std::pair<double, int>>& between = keys[directions.size() + 1];
        fromEnd.resize(m);
        between.resize(m);
        for (int v = 0; v < m; ++v) {
            fromEnd[v] = std::make_pair((double)level[v], v);
            between[v] = std::make_pair((double)(level[v] - otherLevel[v]), v);
        }
        tbb::parallel_for(size_t(0), keys.size(), [&](size_t k) { std::sort(keys[k].begin(), keys[k].end()); });
        return keys;
    }

    // Splits a connected part into A, B and a separator; false when no cut
    // leaves both sides non-empty.
    bool bisect(const System& system, const Part& part, std::vector<char>* side)
    {
        const std::vector<std::vector<std::pair<double, int>>> keys = cutKeys(system, part);
        const int cutCount = (int)keys.size() * cutsPerKey;
        // Score every cut; the sides are rebuilt for the few that are refined.
        std::vector<double> score(cutCount, -1.0);
        tbb::parallel_for(0, cutCount, [&](int c) {
            std::vector<char> s;
            cutAlong(keys[c / cutsPerKey], c % cutsPerKey, &s);
            coverCut(part, &s);
            int count[3] = { 0, 0, 0 };
            for (char x : s)
                ++count[(int)x];
            if (count[sideA] > 0 && count[sideB] > 0)
                score[c] = (double)count[separator] / std::min(count[sideA], count[sideB]);
        });
        std::vector<int> ranked;
        for (int c = 0; c < cutCount; ++c)
            if (score[c] >= 0.0)
                ranked.push_back(c);
        if (ranked.empty())
            return false;
        std::stable_sort(ranked.begin(), ranked.end(), [&](int a, int b) { return score[a] < score[b]; });
        ranked.resize(std::min((int)ranked.size(), refinedCuts));
        std::vector<std::vector<char>> sides(ranked.size());
        std::vector<int> separatorSize(ranked.size());
        tbb::parallel_for(size_t(0), ranked.size(), [&](size_t r) {
            cutAlong(keys[ranked[r] / cutsPerKey], ranked[r] % cutsPerKey, &sides[r]);
            coverCut(part, &sides[r]);
            refine(part, &sides[r]);
            separatorSize[r] = (int)std::count(sides[r].begin(), sides[r].end(), separator);
        });
        size_t best = 0;
        for (size_t r = 1; r < ranked.size(); ++r)
            if (separatorSize[r] < separatorSize[best])
                best = r;
        side->swap(sides[best]);
        return true;
    }

    void dissect(const System& system, const Part& part, int offset, int depth)
    {
        if (part.size() <= leafGroups || depth > maximumDepth) {
            orderByMinimumDegree(system, part, offset);
            return;
        }
        std::vector<int> component;
        const int componentCount = components(part, &component);
        if (componentCount > 1) {
            // Parts are independent: large ones are dissected one after
            // another, the small ones are ordered together at the end.
            std::vector<std::vector<int>> members(componentCount);
            for (int v = 0; v < part.size(); ++v)
                members[component[v]].push_back(v);
            std::vector<int> small;
            for (const std::vector<int>& keep : members) {
                if ((int)keep.size() <= leafGroups) {
                    small.insert(small.end(), keep.begin(), keep.end());
                    continue;
                }
                const Part child = subPart(part, keep);
                dissect(system, child, offset, depth);
                offset += unknownCount(system, child.group);
            }
            std::vector<int> smallGroups;
            smallGroups.reserve(small.size());
            for (int v : small)
                smallGroups.push_back(part.group[v]);
            orderByMinimumDegree(system, smallGroups, offset);
            return;
        }
        std::vector<char> side;
        if (!bisect(system, part, &side)) {
            orderByMinimumDegree(system, part, offset);
            return;
        }
        std::vector<int> keepA, keepB, keepSeparator;
        for (int v = 0; v < part.size(); ++v)
            (sideA == side[v] ? keepA : sideB == side[v] ? keepB
                                                         : keepSeparator)
                .push_back(v);
        if (keepA.empty() || keepB.empty()) {
            orderByMinimumDegree(system, part, offset);
            return;
        }
        std::vector<char>().swap(side);
        const Part a = subPart(part, keepA);
        const Part b = subPart(part, keepB);
        std::vector<int> separatorGroups;
        separatorGroups.reserve(keepSeparator.size());
        for (int v : keepSeparator)
            separatorGroups.push_back(part.group[v]);
        const int offsetB = offset + unknownCount(system, a.group);
        const int offsetSeparator = offsetB + unknownCount(system, b.group);
        if (part.size() >= parallelGroups) {
            tbb::parallel_invoke([&]() { dissect(system, a, offset, depth + 1); },
                [&]() { dissect(system, b, offsetB, depth + 1); });
        } else {
            dissect(system, a, offset, depth + 1);
            dissect(system, b, offsetB, depth + 1);
        }
        orderByMinimumDegree(system, separatorGroups, offsetSeparator);
    }
}

std::vector<int> nestedDissectionOrder(int columns, const int* columnStart, const int* rows,
    const std::vector<int>& groupOfColumn, const std::vector<Vector3>& groupPositions)
{
    if (columns <= 0 || (int)groupOfColumn.size() != columns)
        return std::vector<int>();
    const int inputGroups = (int)groupPositions.size();
    for (int g : groupOfColumn)
        if (g < 0 || g >= inputGroups)
            return std::vector<int>();
    for (int p = 0; p < columnStart[columns]; ++p)
        if (rows[p] < 0 || rows[p] >= columns)
            return std::vector<int>();

    // Number the groups that hold unknowns, in input order.
    std::vector<int> groupIndex(inputGroups, -1);
    System system;
    system.columnStart = columnStart;
    system.rows = rows;
    for (int c = 0; c < columns; ++c) {
        const int g = groupOfColumn[c];
        if (groupIndex[g] < 0) {
            groupIndex[g] = (int)system.positions.size();
            system.positions.push_back(groupPositions[g]);
        }
    }
    const int groups = (int)system.positions.size();
    system.groupStart.assign(groups + 1, 0);
    for (int c = 0; c < columns; ++c)
        ++system.groupStart[groupIndex[groupOfColumn[c]] + 1];
    for (int g = 0; g < groups; ++g)
        system.groupStart[g + 1] += system.groupStart[g];
    system.groupColumns.resize(columns);
    {
        std::vector<int> fill(system.groupStart.begin(), system.groupStart.end() - 1);
        for (int c = 0; c < columns; ++c)
            system.groupColumns[fill[groupIndex[groupOfColumn[c]]]++] = c;
    }

    // The graph of groups: g and h are adjacent when an unknown of g and one
    // of h share an entry. Built in one direction, then symmetrised.
    Part root;
    root.group.resize(groups);
    for (int g = 0; g < groups; ++g)
        root.group[g] = g;
    {
        std::vector<int> start(groups + 1, 0), adjacent, scratch;
        for (int g = 0; g < groups; ++g) {
            scratch.clear();
            for (int k = system.groupStart[g]; k < system.groupStart[g + 1]; ++k) {
                const int c = system.groupColumns[k];
                for (int p = columnStart[c]; p < columnStart[c + 1]; ++p) {
                    const int h = groupIndex[groupOfColumn[rows[p]]];
                    if (h != g)
                        scratch.push_back(h);
                }
            }
            std::sort(scratch.begin(), scratch.end());
            scratch.erase(std::unique(scratch.begin(), scratch.end()), scratch.end());
            adjacent.insert(adjacent.end(), scratch.begin(), scratch.end());
            start[g + 1] = (int)adjacent.size();
        }
        std::vector<int> reverseStart(groups + 1, 0), reverse(adjacent.size());
        for (int h : adjacent)
            ++reverseStart[h + 1];
        for (int g = 0; g < groups; ++g)
            reverseStart[g + 1] += reverseStart[g];
        {
            std::vector<int> fill(reverseStart.begin(), reverseStart.end() - 1);
            for (int g = 0; g < groups; ++g)
                for (int p = start[g]; p < start[g + 1]; ++p)
                    reverse[fill[adjacent[p]]++] = g;
        }
        root.start.assign(1, 0);
        root.start.reserve(groups + 1);
        root.neighbours.reserve(adjacent.size());
        for (int g = 0; g < groups; ++g) {
            const size_t first = root.neighbours.size();
            std::merge(adjacent.begin() + start[g], adjacent.begin() + start[g + 1],
                reverse.begin() + reverseStart[g], reverse.begin() + reverseStart[g + 1],
                std::back_inserter(root.neighbours));
            root.neighbours.erase(std::unique(root.neighbours.begin() + first, root.neighbours.end()),
                root.neighbours.end());
            root.start.push_back((int)root.neighbours.size());
        }
    }

    std::vector<int> order(columns, -1);
    system.order = &order;
    dissect(system, root, 0, 0);
    return order;
}

}
