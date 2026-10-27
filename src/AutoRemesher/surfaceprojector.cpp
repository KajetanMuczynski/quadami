/*
 *  Copyright (C) 2026 Quadami contributors
 *  Portions Copyright (c) 2026 Jeremy HU <jeremy-at-dust3d dot org>, MIT License,
 *  see LICENSE-MIT-upstream (closestPointOnTriangle, from upstream quadextractor.cpp).
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
#include <AutoRemesher/SurfaceProjector>
#include <algorithm>
#include <axisalignedboundingbox.h>
#include <axisalignedboundingboxtree.h>
#include <cmath>
#include <limits>
#include <utility>
#if defined(__has_include)
#if __has_include(<oneapi/tbb/blocked_range.h>)
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#else
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#endif
#else
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#endif

namespace AutoRemesher {

namespace {

    // A vertex may land this far, in mean output edges; T's sagitta is a small
    // fraction of its edge, which is shorter than the output's.
    const double maxMoveInEdges = 0.5;
    // How far the closest-point search looks, so that a match beyond the
    // limit above is told apart from no match at all.
    const double searchRadiusInEdges = 4.0;
    // A face may not drop below this scaled Jacobian unless it already was.
    const double minScaledJacobian = 0.1;
    // cos 60 degrees: the largest turn of a face's normal a move may cause.
    const double minNormalTurnCosine = 0.5;

    ::Vector3 toBoxVector(const Vector3& v)
    {
        return ::Vector3(v.x(), v.y(), v.z());
    }

    double boxDistanceSquared(const AxisAlignedBoudingBox& box, const Vector3& p)
    {
        double sum = 0.0;
        for (size_t i = 0; i < 3; ++i) {
            const double below = box.lowerBound()[i] - p[i];
            const double above = p[i] - box.upperBound()[i];
            const double gap = std::max(0.0, std::max(below, above));
            sum += gap * gap;
        }
        return sum;
    }

    // Exact nearest search: visits the nearer child first and skips every
    // node whose box is farther than the best candidate so far.
    template <typename LeafDistanceSquared>
    void nearestInTree(const AxisAlignedBoudingBoxTree::Node* node, const Vector3& p,
        double* bestDistanceSquared, const LeafDistanceSquared& leafDistanceSquared)
    {
        if (nullptr == node || boxDistanceSquared(node->boundingBox, p) >= *bestDistanceSquared)
            return;
        if (node->isLeaf()) {
            for (const auto& index : node->boxIndices)
                leafDistanceSquared(index, bestDistanceSquared);
            return;
        }
        const AxisAlignedBoudingBoxTree::Node* first = node->left;
        const AxisAlignedBoudingBoxTree::Node* second = node->right;
        if (nullptr != first && nullptr != second
            && boxDistanceSquared(second->boundingBox, p) < boxDistanceSquared(first->boundingBox, p))
            std::swap(first, second);
        nearestInTree(first, p, bestDistanceSquared, leafDistanceSquared);
        nearestInTree(second, p, bestDistanceSquared, leafDistanceSquared);
    }

    Vector3 newellNormal(const std::vector<Vector3>& points, const std::vector<size_t>& face)
    {
        Vector3 normal;
        for (size_t i = 0; i < face.size(); ++i) {
            const Vector3& a = points[face[i]];
            const Vector3& b = points[face[(i + 1) % face.size()]];
            normal += Vector3((a.y() - b.y()) * (a.z() + b.z()),
                (a.z() - b.z()) * (a.x() + b.x()),
                (a.x() - b.x()) * (a.y() + b.y()));
        }
        return normal;
    }

    // Smallest corner scaled Jacobian w.r.t. the face's own normal, as
    // tools/bench/metrics.py measures it; <= 0 means folded or non-convex.
    double minCornerScaledJacobian(const std::vector<Vector3>& points, const std::vector<size_t>& face,
        const Vector3& unitNormal)
    {
        double worst = 1.0;
        for (size_t i = 0; i < face.size(); ++i) {
            const Vector3& corner = points[face[i]];
            const Vector3 next = points[face[(i + 1) % face.size()]] - corner;
            const Vector3 previous = points[face[(i + face.size() - 1) % face.size()]] - corner;
            const double lengths = next.length() * previous.length();
            const double jacobian = lengths > 0.0
                ? Vector3::dotProduct(Vector3::crossProduct(next, previous), unitNormal) / lengths
                : -1.0;
            worst = std::min(worst, jacobian);
        }
        return worst;
    }

    // A vertex this close to an input crease, in mean output edges, is taken
    // to belong to it.
    const double creaseReachInEdges = 0.25;
    // cos 45 degrees: a vertex with a face turned further from its normal is
    // not smoothed.
    const double minBendCosine = 0.70710678118654752;
    // Share of the way to the neighbours' mean a smoothing round moves.
    const double smoothFactor = 0.5;

    // What project() and relax() need to know about the output.
    struct OutputMesh {
        // Each undirected edge once, sorted.
        std::vector<std::pair<size_t, size_t>> edges;
        std::vector<std::vector<size_t>> vertexFaces;
        std::vector<bool> onBoundary;
        double meanEdge = 0.0;
    };

    OutputMesh analyseOutput(const std::vector<Vector3>& positions, const std::vector<std::vector<size_t>>& faces)
    {
        OutputMesh mesh;
        const size_t vertexCount = positions.size();
        mesh.vertexFaces.resize(vertexCount);
        mesh.onBoundary.assign(vertexCount, false);
        std::vector<std::pair<size_t, size_t>> halfEdges;
        for (size_t f = 0; f < faces.size(); ++f) {
            const auto& face = faces[f];
            for (size_t i = 0; i < face.size(); ++i) {
                const size_t a = face[i];
                const size_t b = face[(i + 1) % face.size()];
                halfEdges.emplace_back(std::min(a, b), std::max(a, b));
                mesh.vertexFaces[a].push_back(f);
            }
        }
        std::sort(halfEdges.begin(), halfEdges.end());
        double totalEdgeLength = 0.0;
        for (size_t i = 0; i < halfEdges.size();) {
            size_t j = i;
            while (j < halfEdges.size() && halfEdges[j] == halfEdges[i])
                ++j;
            if (1 == j - i) {
                mesh.onBoundary[halfEdges[i].first] = true;
                mesh.onBoundary[halfEdges[i].second] = true;
            }
            totalEdgeLength += (positions[halfEdges[i].first] - positions[halfEdges[i].second]).length();
            mesh.edges.push_back(halfEdges[i]);
            i = j;
        }
        if (!mesh.edges.empty())
            mesh.meanEdge = totalEdgeLength / mesh.edges.size();
        return mesh;
    }

    // Area-weighted: the Newell normal's length is twice the face's area.
    std::vector<Vector3> vertexNormals(const std::vector<Vector3>& positions,
        const std::vector<std::vector<size_t>>& faces)
    {
        std::vector<Vector3> normals(positions.size());
        for (const auto& face : faces) {
            const Vector3 normal = newellNormal(positions, face);
            for (const size_t v : face)
                normals[v] += normal;
        }
        return normals;
    }

    // `targets` holds the new position of every vertex marked in `moved`
    // (the old one elsewhere), `targetNormals` the normal of the input face
    // it lands on (zero: no facing check). A face is worse if it turns by
    // more than 60 degrees, comes to face away from the input face one of its
    // vertices lands on (having faced that way before), or drops in scaled
    // Jacobian below min(0.1, its value before). Puts back the longest move
    // of each worse face, then checks the faces around it again. Each round
    // puts back at least one vertex and with none moved no face is worse, so
    // this ends. Returns the vertices put back.
    std::vector<size_t> undoWorseningMoves(const std::vector<Vector3>& positions,
        const std::vector<std::vector<size_t>>& faces, const std::vector<std::vector<size_t>>& vertexFaces,
        const std::vector<Vector3>& targetNormals, std::vector<char>* moved, std::vector<Vector3>* targets)
    {
        const size_t vertexCount = positions.size();
        std::vector<Vector3> oldNormals(faces.size());
        std::vector<double> oldJacobians(faces.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, faces.size()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t f = range.begin(); f != range.end(); ++f) {
                oldNormals[f] = newellNormal(positions, faces[f]).normalized();
                oldJacobians[f] = minCornerScaledJacobian(positions, faces[f], oldNormals[f]);
            }
        });
        const auto isWorse = [&](size_t f) {
            const Vector3 normal = newellNormal(*targets, faces[f]).normalized();
            for (const size_t v : faces[f]) {
                if ((*moved)[v] && Vector3::dotProduct(normal, targetNormals[v]) <= 0.0
                    && Vector3::dotProduct(oldNormals[f], targetNormals[v]) > 0.0)
                    return true;
            }
            if (!oldNormals[f].isZero() && Vector3::dotProduct(normal, oldNormals[f]) < minNormalTurnCosine)
                return true;
            return minCornerScaledJacobian(*targets, faces[f], normal)
                < std::min(minScaledJacobian, oldJacobians[f]) - 1e-12;
        };

        std::vector<size_t> undone;
        std::vector<size_t> facesToCheck(faces.size());
        for (size_t f = 0; f < faces.size(); ++f)
            facesToCheck[f] = f;
        std::vector<char> worse(faces.size(), 0);
        std::vector<char> queued(faces.size(), 0);
        std::vector<char> revertedNow(vertexCount, 0);
        while (!facesToCheck.empty()) {
            tbb::parallel_for(tbb::blocked_range<size_t>(0, facesToCheck.size()),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t k = range.begin(); k != range.end(); ++k)
                        worse[facesToCheck[k]] = isWorse(facesToCheck[k]) ? 1 : 0;
                });
            std::vector<size_t> revertedVertices;
            for (const size_t f : facesToCheck) {
                if (!worse[f])
                    continue;
                // A face that already lost a move this round is checked again
                // in the next one, with that move undone.
                bool undoneThisRound = false;
                for (const size_t v : faces[f])
                    undoneThisRound = undoneThisRound || revertedNow[v];
                if (undoneThisRound)
                    continue;
                size_t longest = vertexCount;
                double longestMove = -1.0;
                for (const size_t v : faces[f]) {
                    if (!(*moved)[v])
                        continue;
                    const double move = ((*targets)[v] - positions[v]).lengthSquared();
                    if (move > longestMove) {
                        longestMove = move;
                        longest = v;
                    }
                }
                if (vertexCount == longest)
                    continue;
                (*moved)[longest] = 0;
                (*targets)[longest] = positions[longest];
                revertedVertices.push_back(longest);
                revertedNow[longest] = 1;
            }
            for (const size_t v : revertedVertices)
                revertedNow[v] = 0;
            std::vector<size_t> nextFaces;
            for (const size_t v : revertedVertices) {
                for (const size_t f : vertexFaces[v]) {
                    if (!queued[f]) {
                        queued[f] = 1;
                        nextFaces.push_back(f);
                    }
                }
            }
            for (const size_t f : nextFaces)
                queued[f] = 0;
            std::sort(nextFaces.begin(), nextFaces.end());
            facesToCheck = std::move(nextFaces);
            undone.insert(undone.end(), revertedVertices.begin(), revertedVertices.end());
        }
        return undone;
    }

}

Vector3 closestPointOnTriangle(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c)
{
    const auto ab = b - a;
    const auto ac = c - a;
    const auto ap = p - a;
    double d1 = Vector3::dotProduct(ab, ap);
    double d2 = Vector3::dotProduct(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0)
        return a;

    const auto bp = p - b;
    double d3 = Vector3::dotProduct(ab, bp);
    double d4 = Vector3::dotProduct(ac, bp);
    if (d3 >= 0.0 && d4 <= d3)
        return b;

    double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        double denom = d1 - d3;
        double v = 0.0 != denom ? d1 / denom : 0.0;
        return a + v * ab;
    }

    const auto cp = p - c;
    double d5 = Vector3::dotProduct(ab, cp);
    double d6 = Vector3::dotProduct(ac, cp);
    if (d6 >= 0.0 && d5 <= d6)
        return c;

    double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        double denom = d2 - d6;
        double w = 0.0 != denom ? d2 / denom : 0.0;
        return a + w * ac;
    }

    double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        double denom = (d4 - d3) + (d5 - d6);
        double w = 0.0 != denom ? (d4 - d3) / denom : 0.0;
        return b + w * (c - b);
    }

    double denom = va + vb + vc;
    if (0.0 == denom)
        return a;
    double v = vb / denom;
    double w = vc / denom;
    return a + ab * v + ac * w;
}

Vector3 closestPointOnSegment(const Vector3& p, const Vector3& a, const Vector3& b)
{
    const Vector3 ab = b - a;
    const double lengthSquared = ab.lengthSquared();
    if (lengthSquared <= 0.0)
        return a;
    const double t = std::max(0.0, std::min(1.0, Vector3::dotProduct(p - a, ab) / lengthSquared));
    return a + t * ab;
}

SurfaceProjector::SurfaceProjector(const std::vector<Vector3>* vertices,
    const std::vector<std::vector<size_t>>* triangles, double sharpEdgeDegrees)
    : m_vertices(vertices)
    , m_triangles(triangles)
{
    const auto& points = *m_vertices;
    m_triangleNormals.resize(m_triangles->size());
    m_triangleBoxes.reset(new std::vector<AxisAlignedBoudingBox>(m_triangles->size()));
    std::vector<size_t> triangleIndices(m_triangles->size());
    AxisAlignedBoudingBox triangleGroupBox;
    for (size_t i = 0; i < m_triangles->size(); ++i) {
        const auto& triangle = (*m_triangles)[i];
        m_triangleNormals[i] = Vector3::crossProduct(points[triangle[1]] - points[triangle[0]],
            points[triangle[2]] - points[triangle[0]])
                                   .normalized();
        for (size_t k = 0; k < 3; ++k) {
            (*m_triangleBoxes)[i].update(toBoxVector(points[triangle[k]]));
            triangleGroupBox.update(toBoxVector(points[triangle[k]]));
        }
        (*m_triangleBoxes)[i].updateCenter();
        triangleIndices[i] = i;
    }
    triangleGroupBox.updateCenter();

    // The side list is sorted and dropped before the tree is built, so the
    // two never take memory at the same time.
    // (edge, triangle) for every triangle side
    std::vector<std::pair<std::pair<size_t, size_t>, size_t>> edges;
    edges.reserve(m_triangles->size() * 3);
    for (size_t i = 0; i < m_triangles->size(); ++i) {
        const auto& triangle = (*m_triangles)[i];
        for (size_t k = 0; k < 3; ++k) {
            const size_t a = triangle[k];
            const size_t b = triangle[(k + 1) % 3];
            edges.emplace_back(std::make_pair(std::min(a, b), std::max(a, b)), i);
        }
    }
    // 0 turns creases off, as it turns off the sharp-edge handling of the
    // decimation; at 180 no edge can be sharper.
    const bool findCreases = sharpEdgeDegrees > 0.0 && sharpEdgeDegrees < 180.0;
    const double creaseCosine = std::cos(sharpEdgeDegrees * M_PI / 180.0);
    std::sort(edges.begin(), edges.end());
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j].first == edges[i].first)
            ++j;
        if (1 == j - i)
            m_boundary.edges.push_back(edges[i].first);
        else if (2 == j - i && findCreases) {
            const Vector3& a = m_triangleNormals[edges[i].second];
            const Vector3& b = m_triangleNormals[edges[i + 1].second];
            if (!a.isZero() && !b.isZero() && Vector3::dotProduct(a, b) < creaseCosine)
                m_creases.edges.push_back(edges[i].first);
        }
        i = j;
    }
    std::vector<std::pair<std::pair<size_t, size_t>, size_t>>().swap(edges);

    m_triangleTree.reset(new AxisAlignedBoudingBoxTree(m_triangleBoxes.get(), triangleIndices, triangleGroupBox));
    buildEdgeTree(&m_boundary);
    buildEdgeTree(&m_creases);
}

SurfaceProjector::~SurfaceProjector() = default;

void SurfaceProjector::buildEdgeTree(EdgeTree* edgeTree) const
{
    if (edgeTree->edges.empty())
        return;
    const auto& points = *m_vertices;
    edgeTree->boxes.reset(new std::vector<AxisAlignedBoudingBox>(edgeTree->edges.size()));
    std::vector<size_t> edgeIndices(edgeTree->edges.size());
    AxisAlignedBoudingBox edgeGroupBox;
    for (size_t i = 0; i < edgeTree->edges.size(); ++i) {
        for (const size_t v : { edgeTree->edges[i].first, edgeTree->edges[i].second }) {
            (*edgeTree->boxes)[i].update(toBoxVector(points[v]));
            edgeGroupBox.update(toBoxVector(points[v]));
        }
        (*edgeTree->boxes)[i].updateCenter();
        edgeIndices[i] = i;
    }
    edgeGroupBox.updateCenter();
    edgeTree->tree.reset(new AxisAlignedBoudingBoxTree(edgeTree->boxes.get(), edgeIndices, edgeGroupBox));
}

double SurfaceProjector::distance(const Vector3& position, double maxDistance) const
{
    const auto& points = *m_vertices;
    double best = maxDistance * maxDistance;
    nearestInTree(m_triangleTree->root(), position, &best, [&](size_t index, double* bestDistanceSquared) {
        const auto& triangle = (*m_triangles)[index];
        const double distanceSquared = (closestPointOnTriangle(position,
                                            points[triangle[0]], points[triangle[1]], points[triangle[2]])
            - position)
                                           .lengthSquared();
        if (distanceSquared < *bestDistanceSquared)
            *bestDistanceSquared = distanceSquared;
    });
    return std::sqrt(best);
}

bool SurfaceProjector::closestFacingPoint(const Vector3& position, const Vector3& normal,
    double maxDistance, Vector3* closest, Vector3* closestNormal) const
{
    const auto& points = *m_vertices;
    double best = maxDistance * maxDistance;
    bool found = false;
    nearestInTree(m_triangleTree->root(), position, &best, [&](size_t index, double* bestDistanceSquared) {
        if (Vector3::dotProduct(m_triangleNormals[index], normal) <= 0.0)
            return;
        const auto& triangle = (*m_triangles)[index];
        const Vector3 candidate = closestPointOnTriangle(position,
            points[triangle[0]], points[triangle[1]], points[triangle[2]]);
        const double distanceSquared = (candidate - position).lengthSquared();
        if (distanceSquared < *bestDistanceSquared) {
            *bestDistanceSquared = distanceSquared;
            *closest = candidate;
            *closestNormal = m_triangleNormals[index];
            found = true;
        }
    });
    return found;
}

bool SurfaceProjector::closestEdgePoint(const EdgeTree& edgeTree, const Vector3& position, double maxDistance,
    Vector3* closest) const
{
    if (!edgeTree.tree)
        return false;
    const auto& points = *m_vertices;
    double best = maxDistance * maxDistance;
    bool found = false;
    nearestInTree(edgeTree.tree->root(), position, &best, [&](size_t index, double* bestDistanceSquared) {
        const auto& edge = edgeTree.edges[index];
        const Vector3 candidate = closestPointOnSegment(position, points[edge.first], points[edge.second]);
        const double distanceSquared = (candidate - position).lengthSquared();
        if (distanceSquared < *bestDistanceSquared) {
            *bestDistanceSquared = distanceSquared;
            *closest = candidate;
            found = true;
        }
    });
    return found;
}

SurfaceProjector::Result SurfaceProjector::project(std::vector<Vector3>* points,
    const std::vector<std::vector<size_t>>& faces) const
{
    Result result;
    auto& positions = *points;
    const size_t vertexCount = positions.size();
    if (0 == vertexCount || faces.empty() || m_triangles->empty())
        return result;

    const OutputMesh mesh = analyseOutput(positions, faces);
    if (!(mesh.meanEdge > 0.0))
        return result;
    const double maxMove = maxMoveInEdges * mesh.meanEdge;
    const double searchRadius = searchRadiusInEdges * mesh.meanEdge;
    const std::vector<Vector3> normals = vertexNormals(positions, faces);

    enum class Status : unsigned char { Unused,
        Unmatched,
        Far,
        Folding,
        Moved };
    std::vector<Status> status(vertexCount, Status::Unused);
    std::vector<Vector3> targets(positions);
    // Normal of the input face each vertex lands on; zero on the boundary.
    std::vector<Vector3> targetNormals(vertexCount);
    std::vector<char> moved(vertexCount, 0);
    // Each vertex only reads the input and its own old position, so the result
    // does not depend on the thread count.
    tbb::parallel_for(tbb::blocked_range<size_t>(0, vertexCount), [&](const tbb::blocked_range<size_t>& range) {
        for (size_t v = range.begin(); v != range.end(); ++v) {
            if (mesh.vertexFaces[v].empty())
                continue;
            Vector3 closest;
            const bool found = mesh.onBoundary[v]
                ? closestEdgePoint(m_boundary, positions[v], searchRadius, &closest)
                : closestFacingPoint(positions[v], normals[v], searchRadius, &closest, &targetNormals[v]);
            if (!found)
                status[v] = Status::Unmatched;
            else if ((closest - positions[v]).length() > maxMove)
                status[v] = Status::Far;
            else {
                status[v] = Status::Moved;
                targets[v] = closest;
                moved[v] = 1;
            }
        }
    });
    for (const size_t v : undoWorseningMoves(positions, faces, mesh.vertexFaces, targetNormals, &moved, &targets))
        status[v] = Status::Folding;

    double moveSum = 0.0;
    for (size_t v = 0; v < vertexCount; ++v) {
        if (Status::Unused == status[v])
            continue;
        ++result.vertices;
        if (Status::Unmatched == status[v])
            ++result.keptUnmatched;
        else if (Status::Far == status[v])
            ++result.keptFar;
        else if (Status::Folding == status[v])
            ++result.keptFolding;
        else if (Status::Moved == status[v]) {
            const double move = (targets[v] - positions[v]).length() / mesh.meanEdge;
            moveSum += move;
            result.maxMove = std::max(result.maxMove, move);
            ++result.projected;
        }
    }
    if (result.projected > 0)
        result.meanMove = moveSum / result.projected;
    positions = std::move(targets);
    return result;
}

SurfaceProjector::RelaxResult SurfaceProjector::relax(std::vector<Vector3>* points,
    const std::vector<std::vector<size_t>>& faces, size_t iterations) const
{
    RelaxResult result;
    auto& positions = *points;
    const size_t vertexCount = positions.size();
    if (0 == vertexCount || faces.empty() || m_triangles->empty())
        return result;

    const OutputMesh mesh = analyseOutput(positions, faces);
    if (!(mesh.meanEdge > 0.0))
        return result;
    const double maxMove = maxMoveInEdges * mesh.meanEdge;
    const double searchRadius = searchRadiusInEdges * mesh.meanEdge;
    const double creaseReach = creaseReachInEdges * mesh.meanEdge;

    std::vector<char> movable(vertexCount, 0);
    for (size_t v = 0; v < vertexCount; ++v)
        movable[v] = !mesh.vertexFaces[v].empty() && !mesh.onBoundary[v];
    for (const auto& face : faces) {
        if (4 != face.size()) {
            for (const size_t v : face)
                movable[v] = 0;
        }
    }
    std::vector<std::vector<size_t>> neighbours(vertexCount);
    for (const auto& edge : mesh.edges) {
        neighbours[edge.first].push_back(edge.second);
        neighbours[edge.second].push_back(edge.first);
    }

    // Onto the creases first, so the smoothing works around them.
    std::vector<char> nearCrease(vertexCount, 0);
    if (m_creases.tree) {
        std::vector<Vector3> targets(positions);
        std::vector<Vector3> noNormals(vertexCount);
        std::vector<char> moved(vertexCount, 0);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, vertexCount), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t v = range.begin(); v != range.end(); ++v) {
                Vector3 closest;
                if (!movable[v] || !closestEdgePoint(m_creases, positions[v], creaseReach, &closest))
                    continue;
                nearCrease[v] = 1;
                targets[v] = closest;
                moved[v] = 1;
            }
        });
        result.snapFolding = undoWorseningMoves(positions, faces, mesh.vertexFaces, noNormals, &moved, &targets).size();
        for (size_t v = 0; v < vertexCount; ++v)
            result.snapped += moved[v] ? 1 : 0;
        positions = std::move(targets);
    }

    // A vertex whose faces bend away from its normal sits on a fold of the
    // output the input has no crease for, mostly a thin rim the quads bridge:
    // smoothing there pulls the outermost vertices inwards and cuts the rim.
    std::vector<char> bent(vertexCount, 0);
    {
        const std::vector<Vector3> normals = vertexNormals(positions, faces);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, vertexCount), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t v = range.begin(); v != range.end(); ++v) {
                const Vector3 unitNormal = normals[v].normalized();
                for (const size_t f : mesh.vertexFaces[v]) {
                    if (Vector3::dotProduct(newellNormal(positions, faces[f]).normalized(), unitNormal) < minBendCosine) {
                        bent[v] = 1;
                        break;
                    }
                }
            }
        });
    }
    std::vector<char> smoothable(vertexCount, 0);
    for (size_t v = 0; v < vertexCount; ++v) {
        smoothable[v] = movable[v] && !nearCrease[v] && !bent[v];
        result.free += smoothable[v] ? 1 : 0;
    }
    if (0 == result.free)
        return result;
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        const std::vector<Vector3> normals = vertexNormals(positions, faces);
        std::vector<Vector3> targets(positions);
        std::vector<Vector3> targetNormals(vertexCount);
        std::vector<char> moved(vertexCount, 0);
        // Reads the old positions only (Jacobi), so the result does not
        // depend on the thread count.
        tbb::parallel_for(tbb::blocked_range<size_t>(0, vertexCount), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t v = range.begin(); v != range.end(); ++v) {
                if (!smoothable[v] || neighbours[v].empty())
                    continue;
                Vector3 centre;
                for (const size_t n : neighbours[v])
                    centre += positions[n];
                centre /= (double)neighbours[v].size();
                const Vector3 unitNormal = normals[v].normalized();
                Vector3 step = smoothFactor * (centre - positions[v]);
                step = step - Vector3::dotProduct(step, unitNormal) * unitNormal;
                Vector3 closest, closestNormal;
                if (!closestFacingPoint(positions[v] + step, normals[v], searchRadius, &closest, &closestNormal)
                    || (closest - positions[v]).length() > maxMove)
                    continue;
                targets[v] = closest;
                targetNormals[v] = closestNormal;
                moved[v] = 1;
            }
        });
        result.keptFolding += undoWorseningMoves(positions, faces, mesh.vertexFaces, targetNormals, &moved, &targets).size();
        for (size_t v = 0; v < vertexCount; ++v)
            result.moves += moved[v] ? 1 : 0;
        positions = std::move(targets);
    }
    return result;
}

}
