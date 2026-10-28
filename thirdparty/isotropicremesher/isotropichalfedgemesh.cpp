/*
 *  Copyright (c) 2020-2021 Jeremy HU <jeremy-at-dust3d dot org>. All rights reserved. 
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:

 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.

 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include "isotropichalfedgemesh.h"

namespace {

// A triangle is treated as degenerate when its area is negligible compared to
// the square of its longest edge, which is scale independent unlike an
// absolute epsilon.
inline bool isTriangleDegenerate(const Vector3 &a, const Vector3 &b, const Vector3 &c)
{
    const double longestEdgeSquared = std::max(std::max((b - a).lengthSquared(),
        (c - b).lengthSquared()), (a - c).lengthSquared());
    if (longestEdgeSquared <= 0.0)
        return true;
    return Vector3::area(a, b, c) <= 1e-6 * longestEdgeSquared;
}

}

IsotropicHalfedgeMesh::~IsotropicHalfedgeMesh()
{
    while (nullptr != m_vertexAllocLink) {
        Vertex *vertex = m_vertexAllocLink;
        m_vertexAllocLink = vertex->_allocLink;
        delete vertex;
    }
    
    while (nullptr != m_faceAllocLink) {
        Face *face = m_faceAllocLink;
        m_faceAllocLink = face->_allocLink;
        delete face;
    }
    
    while (nullptr != m_halfedgeAllocLink) {
        Halfedge *halfedge = m_halfedgeAllocLink;
        m_halfedgeAllocLink = halfedge->_allocLink;
        delete halfedge;
    }
}

IsotropicHalfedgeMesh::IsotropicHalfedgeMesh(const std::vector<Vector3> &vertices,
    const std::vector<std::vector<size_t>> &faces)
{
    std::vector<Vertex *> halfedgeVertices;
    halfedgeVertices.reserve(vertices.size());
    for (const auto &it: vertices) {
        Vertex *vertex = newVertex();
        vertex->position = it;
        halfedgeVertices.push_back(vertex);
    }
    
    std::vector<Face *> halfedgeFaces;
    halfedgeFaces.reserve(faces.size());
    for (const auto &it: faces) {
        Face *face = newFace();
        halfedgeFaces.push_back(face);
    }

    std::unordered_map<uint64_t, Halfedge *> halfedgeMap;
    for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        const auto &indices = faces[faceIndex];
        if (3 != indices.size()) {
            std::cerr << "Found non-triangle, face count:" << indices.size() << std::endl;
            continue;
        }
        std::vector<Halfedge *> halfedges(indices.size());
        for (size_t i = 0; i < 3; ++i) {
            size_t j = (i + 1) % 3;
            const auto &first = indices[i];
            const auto &second = indices[j];
            
            Vertex *vertex = halfedgeVertices[first];
            ++vertex->initialFaces;
            
            Halfedge *halfedge = newHalfedge();
            halfedge->startVertex = vertex;
            halfedge->leftFace = halfedgeFaces[faceIndex];
            
            if (nullptr == halfedge->leftFace->halfedge) {
                halfedge->leftFace->halfedge = halfedge;
            }
            if (nullptr == vertex->firstHalfedge) {
                vertex->firstHalfedge = halfedge;
            }
            
            halfedges[i] = halfedge;
            
            auto insertResult = halfedgeMap.insert({makeHalfedgeKey(first, second), halfedge});
            if (!insertResult.second) {
                std::cerr << "Found repeated halfedge:" << first << "," << second << std::endl;
            }
        }
        linkFaceHalfedges(halfedges);
    }
    for (auto &it: halfedgeMap) {
        auto halfedgeIt = halfedgeMap.find(swapHalfedgeKey(it.first));
        if (halfedgeIt == halfedgeMap.end())
            continue;
        it.second->oppositeHalfedge = halfedgeIt->second;
        halfedgeIt->second->oppositeHalfedge = it.second;
    }
    
    for (Vertex *vertex = m_firstVertex; nullptr != vertex; vertex = vertex->nextVertex) {
        vertex->_isBoundary = false;
        vertex->_valence = vertexValence(vertex, &vertex->_isBoundary);
        if (vertex->_isBoundary) {
            if (vertex->_valence == vertex->initialFaces + 1)
                continue;
        } else {
            if (vertex->_valence == vertex->initialFaces)
                continue;
        }
        vertex->featured = true;
    }
}

void IsotropicHalfedgeMesh::linkFaceHalfedges(std::vector<IsotropicHalfedgeMesh::Halfedge *> &halfedges)
{
    for (size_t i = 0; i < halfedges.size(); ++i) {
        size_t j = (i + 1) % halfedges.size();
        halfedges[i]->nextHalfedge = halfedges[j];
        halfedges[j]->previousHalfedge = halfedges[i];
    }
}

void IsotropicHalfedgeMesh::updateFaceHalfedgesLeftFace(std::vector<IsotropicHalfedgeMesh::Halfedge *> &halfedges,
    IsotropicHalfedgeMesh::Face *leftFace)
{
    for (auto &it: halfedges)
        it->leftFace = leftFace;
}

void IsotropicHalfedgeMesh::linkHalfedgePair(IsotropicHalfedgeMesh::Halfedge *first, IsotropicHalfedgeMesh::Halfedge *second)
{
    if (nullptr != first)
        first->oppositeHalfedge = second;
    if (nullptr != second)
        second->oppositeHalfedge = first;
}

double IsotropicHalfedgeMesh::averageEdgeLength()
{
    double totalLength = 0.0;
    size_t halfedgeCount = 0;
    for (Face *face = m_firstFace; nullptr != face; face = face->nextFace) {
        const auto &startHalfedge = face->halfedge;
        Halfedge *halfedge = startHalfedge;
        do {
            Halfedge *nextHalfedge = halfedge->nextHalfedge;
            totalLength += (halfedge->startVertex->position - nextHalfedge->startVertex->position).length();
            ++halfedgeCount;
            halfedge = nextHalfedge;
        } while (halfedge != startHalfedge);
    }
    if (0 == halfedgeCount)
        return 0.0;
    return totalLength / halfedgeCount;
}

IsotropicHalfedgeMesh::Face *IsotropicHalfedgeMesh::newFace()
{
    Face *face = new Face;
    
    face->_allocLink = m_faceAllocLink;
    m_faceAllocLink = face;
    
    face->debugIndex = ++m_debugFaceIndex;
    if (nullptr != m_lastFace) {
        m_lastFace->nextFace = face;
        face->previousFace = m_lastFace;
    } else {
        m_firstFace = face;
    }
    m_lastFace = face;
    return face;
}

IsotropicHalfedgeMesh::Vertex *IsotropicHalfedgeMesh::newVertex()
{
    Vertex *vertex = new Vertex;
    
    vertex->_allocLink = m_vertexAllocLink;
    m_vertexAllocLink = vertex;
    
    vertex->debugIndex = ++m_debugVertexIndex;
    if (nullptr != m_lastVertex) {
        m_lastVertex->nextVertex = vertex;
        vertex->previousVertex = m_lastVertex;
    } else {
        m_firstVertex = vertex;
    }
    m_lastVertex = vertex;
    return vertex;
}

IsotropicHalfedgeMesh::Halfedge *IsotropicHalfedgeMesh::newHalfedge()
{
    Halfedge *halfedge = new Halfedge;
    
    halfedge->_allocLink = m_halfedgeAllocLink;
    m_halfedgeAllocLink = halfedge;
    
    halfedge->debugIndex = ++m_debugHalfedgeIndex;
    return halfedge;
}

IsotropicHalfedgeMesh::Face *IsotropicHalfedgeMesh::moveToNextFace(IsotropicHalfedgeMesh::Face *face)
{
    if (nullptr == face) {
        face = m_firstFace;
        if (nullptr == face)
            return nullptr;
        if (!face->removed)
            return face;
    }
    do {
        face = face->nextFace;
    } while (nullptr != face && face->removed);
    return face;
}

IsotropicHalfedgeMesh::Vertex *IsotropicHalfedgeMesh::moveToNextVertex(IsotropicHalfedgeMesh::Vertex *vertex)
{
    if (nullptr == vertex) {
        vertex = m_firstVertex;
        if (nullptr == vertex)
            return nullptr;
        if (!vertex->removed)
            return vertex;
    }
    do {
        vertex = vertex->nextVertex;
    } while (nullptr != vertex && vertex->removed);
    return vertex;
}

void IsotropicHalfedgeMesh::breakFace(IsotropicHalfedgeMesh::Face *leftOldFace,
    IsotropicHalfedgeMesh::Halfedge *halfedge,
    IsotropicHalfedgeMesh::Vertex *breakPointVertex,
    std::vector<IsotropicHalfedgeMesh::Halfedge *> &leftNewFaceHalfedges,
    std::vector<IsotropicHalfedgeMesh::Halfedge *> &leftOldFaceHalfedges)
{
    std::vector<Halfedge *> leftFaceHalfedges = {
        halfedge->previousHalfedge,
        halfedge,
        halfedge->nextHalfedge
    };
    
    Face *leftNewFace = newFace();
    leftNewFace->halfedge = leftFaceHalfedges[2];
    leftFaceHalfedges[2]->leftFace = leftNewFace;
    
    leftNewFaceHalfedges.push_back(newHalfedge());
    leftNewFaceHalfedges.push_back(newHalfedge());
    leftNewFaceHalfedges.push_back(leftFaceHalfedges[2]);
    linkFaceHalfedges(leftNewFaceHalfedges);
    updateFaceHalfedgesLeftFace(leftNewFaceHalfedges, leftNewFace);
    leftNewFaceHalfedges[0]->startVertex = leftFaceHalfedges[0]->startVertex;
    leftNewFaceHalfedges[1]->startVertex = breakPointVertex;
    
    leftOldFaceHalfedges.push_back(leftFaceHalfedges[0]);
    leftOldFaceHalfedges.push_back(halfedge);
    leftOldFaceHalfedges.push_back(newHalfedge());
    linkFaceHalfedges(leftOldFaceHalfedges);
    updateFaceHalfedgesLeftFace(leftOldFaceHalfedges, leftOldFace);
    leftOldFaceHalfedges[2]->startVertex = breakPointVertex;
    
    breakPointVertex->firstHalfedge = leftNewFaceHalfedges[1];
    
    linkHalfedgePair(leftNewFaceHalfedges[0], leftOldFaceHalfedges[2]);
    
    leftOldFace->halfedge = leftOldFaceHalfedges[0];
}

void IsotropicHalfedgeMesh::breakEdge(IsotropicHalfedgeMesh::Halfedge *halfedge)
{
    Face *leftOldFace = halfedge->leftFace;
    Halfedge *oppositeHalfedge = halfedge->oppositeHalfedge;
    Face *rightOldFace = nullptr;
    
    if (nullptr != oppositeHalfedge)
        rightOldFace = oppositeHalfedge->leftFace;
    
    Vertex *breakPointVertex = newVertex();

    // Use PN Triangle edge midpoint if both endpoints have smooth normals.  A
    // feature edge is split at its plain midpoint instead: the averaged
    // normals of a crease would lift the PN midpoint off it, and
    // projectVertices puts the new chain vertex onto the input's chain.
    if (!halfedge->feature &&
            !halfedge->startVertex->_smoothNormal.isZero() &&
            !halfedge->nextHalfedge->startVertex->_smoothNormal.isZero()) {
        const Vector3 &p1 = halfedge->startVertex->position;
        const Vector3 &p2 = halfedge->nextHalfedge->startVertex->position;
        const Vector3 &n1 = halfedge->startVertex->_smoothNormal;
        const Vector3 &n2 = halfedge->nextHalfedge->startVertex->_smoothNormal;
        double d12 = Vector3::dotProduct(p2 - p1, n1);
        double d21 = Vector3::dotProduct(p1 - p2, n2);
        Vector3 p210 = (2.0 * p1 + p2 - d12 * n1) / 3.0;
        Vector3 p120 = (2.0 * p2 + p1 - d21 * n2) / 3.0;
        breakPointVertex->position = (p1 + p2 + 3.0 * (p210 + p120)) / 8.0;
        breakPointVertex->_smoothNormal = (n1 + n2).normalized();
    } else {
        breakPointVertex->position = (halfedge->startVertex->position +
            halfedge->nextHalfedge->startVertex->position) * 0.5;
    }

    breakPointVertex->featured = halfedge->startVertex->featured &&
        halfedge->nextHalfedge->startVertex->featured;

    // Propagate target edge length to the new vertex
    if (halfedge->startVertex->targetEdgeLength > 0 && 
            halfedge->nextHalfedge->startVertex->targetEdgeLength > 0) {
        breakPointVertex->targetEdgeLength = (halfedge->startVertex->targetEdgeLength +
            halfedge->nextHalfedge->startVertex->targetEdgeLength) * 0.5;
    } else {
        breakPointVertex->targetEdgeLength = halfedge->startVertex->targetEdgeLength +
            halfedge->nextHalfedge->startVertex->targetEdgeLength;
    }

    std::vector<Halfedge *> leftNewFaceHalfedges;
    std::vector<Halfedge *> leftOldFaceHalfedges;
    breakFace(leftOldFace, halfedge, breakPointVertex,
        leftNewFaceHalfedges, leftOldFaceHalfedges);
    
    if (nullptr != rightOldFace) {
        std::vector<Halfedge *> rightNewFaceHalfedges;
        std::vector<Halfedge *> rightOldFaceHalfedges;
        breakFace(rightOldFace, oppositeHalfedge, breakPointVertex,
            rightNewFaceHalfedges, rightOldFaceHalfedges);
        linkHalfedgePair(leftOldFaceHalfedges[1], rightNewFaceHalfedges[1]);
        linkHalfedgePair(leftNewFaceHalfedges[1], rightOldFaceHalfedges[1]);
        rightNewFaceHalfedges[1]->feature = halfedge->feature;
        rightNewFaceHalfedges[1]->featureFlags = halfedge->featureFlags;
        rightNewFaceHalfedges[1]->featureChain = halfedge->featureChain;
    }
    // Both halves of a feature edge stay feature edges; `halfedge` and its
    // opposite keep theirs, the new halfedges along the edge copy it.
    leftNewFaceHalfedges[1]->feature = halfedge->feature;
    leftNewFaceHalfedges[1]->featureFlags = halfedge->featureFlags;
    leftNewFaceHalfedges[1]->featureChain = halfedge->featureChain;
}

void IsotropicHalfedgeMesh::collectVerticesAroundVertex(Vertex *vertex,
    std::set<Vertex *> *vertices)
{
    iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
        vertices->insert(halfedge->nextHalfedge->startVertex);
        return true;
    });
}

size_t IsotropicHalfedgeMesh::vertexValence(Vertex *vertex, bool *isBoundary)
{
    const auto &startHalfedge = vertex->firstHalfedge;
    if (nullptr == startHalfedge)
        return 0;
    
    size_t valence = 0;
    
    Halfedge *loopHalfedge = startHalfedge;
    do {
        ++valence;
        if (nullptr == loopHalfedge->oppositeHalfedge) {
            if (nullptr != isBoundary)
                *isBoundary = true;
            loopHalfedge = startHalfedge;
            do {
                ++valence;
                loopHalfedge = loopHalfedge->previousHalfedge->oppositeHalfedge;
                if (nullptr == loopHalfedge)
                    break;
            } while (loopHalfedge != startHalfedge);
            break;
        }
        loopHalfedge = loopHalfedge->oppositeHalfedge->nextHalfedge;
    } while (loopHalfedge != startHalfedge);
    
    return valence;
}

bool IsotropicHalfedgeMesh::testLengthSquaredAroundVertex(Vertex *vertex, 
    const Vector3 &target, 
    double maxEdgeLengthSquared)
{
    bool testSucceed = false;
    iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
        if ((halfedge->nextHalfedge->startVertex->position - 
                target).lengthSquared() > maxEdgeLengthSquared) {
            testSucceed = true;
            return false;            
        }
        return true;
    });
    return testSucceed;
}

void IsotropicHalfedgeMesh::iterateVertexHalfedges(Vertex *vertex, std::function<bool(Halfedge *halfedge)> handler)
{
    const auto &startHalfedge = vertex->firstHalfedge;
    if (nullptr == startHalfedge)
        return;

    Halfedge *loopHalfedge = startHalfedge;
    do {
        if (!handler(loopHalfedge))
            return;
        if (nullptr == loopHalfedge->oppositeHalfedge) {
            loopHalfedge = startHalfedge;
            for (;;) {
                loopHalfedge = loopHalfedge->previousHalfedge->oppositeHalfedge;
                if (nullptr == loopHalfedge)
                    break;
                if (!handler(loopHalfedge))
                    return;
                if (loopHalfedge == startHalfedge)
                    break;
            }
            break;
        }
        loopHalfedge = loopHalfedge->oppositeHalfedge->nextHalfedge;
    } while (loopHalfedge != startHalfedge);
}

void IsotropicHalfedgeMesh::pointerVertexToNewVertex(Vertex *vertex, Vertex *replacement)
{
    iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
        halfedge->startVertex = replacement;
        return true;
    });
}

bool IsotropicHalfedgeMesh::testCollapseWouldFoldOrDegenerate(Vertex *vertex,
    Vertex *otherVertex,
    const Vector3 &collapseTo,
    Face *removedFaceOne,
    Face *removedFaceTwo)
{
    bool rejected = false;
    iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
        Face *face = halfedge->leftFace;
        if (face == removedFaceOne || face == removedFaceTwo)
            return true;

        Vertex *cornerVertices[3] = {
            halfedge->previousHalfedge->startVertex,
            halfedge->startVertex,
            halfedge->nextHalfedge->startVertex
        };
        Vector3 oldPositions[3];
        Vector3 newPositions[3];
        for (size_t i = 0; i < 3; ++i) {
            oldPositions[i] = cornerVertices[i]->position;
            newPositions[i] = (cornerVertices[i] == vertex || cornerVertices[i] == otherVertex) ?
                collapseTo : cornerVertices[i]->position;
        }

        if (isTriangleDegenerate(newPositions[0], newPositions[1], newPositions[2])) {
            rejected = true;
            return false;
        }

        const Vector3 oldNormal = Vector3::normal(oldPositions[0], oldPositions[1], oldPositions[2]);
        const Vector3 newNormal = Vector3::normal(newPositions[0], newPositions[1], newPositions[2]);
        if (oldNormal.isZero())
            return true;
        if (Vector3::dotProduct(oldNormal, newNormal) <= 0) {
            rejected = true;
            return false;
        }
        return true;
    });
    return rejected;
}

bool IsotropicHalfedgeMesh::testMoveWouldDegenerate(Vertex *vertex, const Vector3 &target)
{
    bool degenerated = false;
    iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
        if (isTriangleDegenerate(halfedge->previousHalfedge->startVertex->position,
                target,
                halfedge->nextHalfedge->startVertex->position)) {
            degenerated = true;
            return false;
        }
        return true;
    });
    return degenerated;
}

bool IsotropicHalfedgeMesh::testMoveWouldFold(Vertex *vertex, const Vector3 &target)
{
    bool folded = false;
    iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
        const Vector3 &previous = halfedge->previousHalfedge->startVertex->position;
        const Vector3 &next = halfedge->nextHalfedge->startVertex->position;
        const Vector3 oldNormal = Vector3::normal(previous, vertex->position, next);
        if (oldNormal.isZero())
            return true;
        if (Vector3::dotProduct(oldNormal, Vector3::normal(previous, target, next)) <= 0) {
            folded = true;
            return false;
        }
        return true;
    });
    return folded;
}

size_t IsotropicHalfedgeMesh::featureDegree(Vertex *vertex, Vertex **first, Vertex **second)
{
    size_t degree = 0;
    iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
        if (halfedge->feature) {
            Vertex *other = halfedge->nextHalfedge->startVertex;
            if (0 == degree && nullptr != first)
                *first = other;
            else if (1 == degree && nullptr != second)
                *second = other;
            ++degree;
        }
        return true;
    });
    return degree;
}

bool IsotropicHalfedgeMesh::isChainVertex(Vertex *vertex)
{
    return !vertex->featured && !vertex->featureCorner && 2 == featureDegree(vertex);
}

void IsotropicHalfedgeMesh::relaxVertex(Vertex *vertex)
{
    if (vertex->_isBoundary || vertex->_valence <= 0)
        return;

    // A chain vertex slides along its chain towards the middle of its two
    // chain neighbors, which evens out the chain's edges without leaving
    // the crease; a corner stays (TODO 2.5).
    Vertex *chainNeighbors[2] = { nullptr, nullptr };
    const size_t degree = featureDegree(vertex, &chainNeighbors[0], &chainNeighbors[1]);
    if (degree > 0) {
        if (2 != degree || vertex->featureCorner)
            return;
        Vector3 tangent = chainNeighbors[1]->position - chainNeighbors[0]->position;
        if (tangent.isZero())
            return;
        tangent.normalize();
        const Vector3 middle = (chainNeighbors[0]->position + chainNeighbors[1]->position) * 0.5;
        const Vector3 target = vertex->position + tangent * Vector3::dotProduct(middle - vertex->position, tangent);
        if (target.containsNan() || target.containsInf())
            return;
        if (testMoveWouldDegenerate(vertex, target) || testMoveWouldFold(vertex, target))
            return;
        vertex->position = target;
        return;
    }

    Vector3 position;
    size_t count = 0;
    iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
        position += halfedge->nextHalfedge->startVertex->position;
        ++count;
        return true;
    });

    if (0 == count)
        return;

    position /= count;

    Vector3 projectedPosition = Vector3::projectPointOnLine(vertex->position, position, position + vertex->_normal);
    if (projectedPosition.containsNan() || projectedPosition.containsInf()) {
        // Averaging the one ring of a vertex whose triangles already lie on a
        // line squashes them completely, so leave such a vertex alone.
        if (!testMoveWouldDegenerate(vertex, position))
            vertex->position = position;
        return;
    }

    if (testMoveWouldDegenerate(vertex, projectedPosition))
        return;

    vertex->position = projectedPosition;
}

bool IsotropicHalfedgeMesh::isVertexPairConnected(Vertex *first, Vertex *second)
{
    bool connected = false;
    iterateVertexHalfedges(first, [&](Halfedge *halfedge) {
        // Both other corners of every incident face are visited, so boundary
        // vertices report their last neighbor too.
        if (halfedge->nextHalfedge->startVertex == second ||
                halfedge->previousHalfedge->startVertex == second) {
            connected = true;
            return false;
        }
        return true;
    });
    return connected;
}

bool IsotropicHalfedgeMesh::isFaceOfVertex(Vertex *vertex, Vertex *second, Vertex *third)
{
    bool found = false;
    iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
        Vertex *next = halfedge->nextHalfedge->startVertex;
        Vertex *previous = halfedge->previousHalfedge->startVertex;
        if ((next == second && previous == third) || (next == third && previous == second)) {
            found = true;
            return false;
        }
        return true;
    });
    return found;
}

bool IsotropicHalfedgeMesh::flipEdge(Halfedge *halfedge)
{
    if (nullptr == halfedge->oppositeHalfedge)
        return false;

    // The other diagonal would cut across the crease (TODO 2.5).
    if (halfedge->feature)
        return false;
        
    Vertex *topVertex = halfedge->previousHalfedge->startVertex;
    Vertex *bottomVertex = halfedge->oppositeHalfedge->previousHalfedge->startVertex;
    
    Vertex *leftVertex = halfedge->startVertex;
    Vertex *rightVertex = halfedge->oppositeHalfedge->startVertex;
    
    bool isLeftBoundary = false;
    int leftValence = (int)vertexValence(leftVertex, &isLeftBoundary);
    if (leftValence <= 3)
        return false;
    
    bool isRightBoundary = false;
    int rightValence = (int)vertexValence(rightVertex, &isRightBoundary);
    if (rightValence <= 3)
        return false;
    
    bool isTopBoundary = false;
    int topValence = (int)vertexValence(topVertex, &isTopBoundary);
    
    bool isBottomBoundary = false;
    int bottomValence = (int)vertexValence(bottomVertex, &isBottomBoundary);
    
    auto deviation = [](int valence, bool isBoundary) {
        return std::pow(valence - (isBoundary ? 4 : 6), 2);
    };
    
    int oldDeviation = deviation(topValence, isTopBoundary) +
        deviation(bottomValence, isBottomBoundary) +
        deviation(leftValence, isLeftBoundary) +
        deviation(rightValence, isRightBoundary);
    int newDeviation = deviation(topValence + 1, isTopBoundary) +
        deviation(bottomValence + 1, isBottomBoundary) +
        deviation(leftValence - 1, isLeftBoundary) +
        deviation(rightValence - 1, isRightBoundary);
    
    if (newDeviation >= oldDeviation)
        return false;
    
    // The flipped edge connects the two apexes. When they are already
    // connected, the flip would create a second edge between the same pair of
    // vertices, which makes the mesh non-manifold.
    if (isVertexPairConnected(topVertex, bottomVertex))
        return false;

    // Reject flips which fold the two triangles over each other or collapse
    // one of them onto a line, both of which the valence criterion above is
    // blind to.
    const Vector3 &topPosition = topVertex->position;
    const Vector3 &bottomPosition = bottomVertex->position;
    const Vector3 &leftPosition = leftVertex->position;
    const Vector3 &rightPosition = rightVertex->position;

    if (isTriangleDegenerate(topPosition, leftPosition, bottomPosition) ||
            isTriangleDegenerate(bottomPosition, rightPosition, topPosition))
        return false;

    const Vector3 oldTopNormal = Vector3::normal(topPosition, leftPosition, rightPosition);
    const Vector3 oldBottomNormal = Vector3::normal(bottomPosition, rightPosition, leftPosition);
    const Vector3 newTopNormal = Vector3::normal(topPosition, leftPosition, bottomPosition);
    const Vector3 newBottomNormal = Vector3::normal(bottomPosition, rightPosition, topPosition);

    if (Vector3::dotProduct(newTopNormal, oldTopNormal) <= 0 ||
            Vector3::dotProduct(newTopNormal, oldBottomNormal) <= 0 ||
            Vector3::dotProduct(newBottomNormal, oldTopNormal) <= 0 ||
            Vector3::dotProduct(newBottomNormal, oldBottomNormal) <= 0)
        return false;

    relinkFlippedEdge(halfedge);
    return true;
}

bool IsotropicHalfedgeMesh::flipEdgeUnderFlatFace(Halfedge *halfedge)
{
    // The face of `halfedge` is flat, so only the face across, kept as the
    // reference, says which way the two new faces must face; the valence
    // rule does not apply, since the flat face has to go either way.
    Halfedge *opposite = halfedge->oppositeHalfedge;
    if (nullptr == opposite || halfedge->feature)
        return false;
    Vertex *topVertex = halfedge->previousHalfedge->startVertex;
    Vertex *bottomVertex = opposite->previousHalfedge->startVertex;
    Vertex *leftVertex = halfedge->startVertex;
    Vertex *rightVertex = opposite->startVertex;
    if (topVertex == bottomVertex || isVertexPairConnected(topVertex, bottomVertex))
        return false;
    // Each new face keeps one edge of the face across; a feature edge there
    // would give it two.
    if (opposite->nextHalfedge->feature || opposite->previousHalfedge->feature)
        return false;
    const Vector3 &topPosition = topVertex->position;
    const Vector3 &bottomPosition = bottomVertex->position;
    const Vector3 &leftPosition = leftVertex->position;
    const Vector3 &rightPosition = rightVertex->position;
    if (isTriangleDegenerate(bottomPosition, rightPosition, leftPosition) ||
            isTriangleDegenerate(topPosition, leftPosition, bottomPosition) ||
            isTriangleDegenerate(bottomPosition, rightPosition, topPosition))
        return false;
    const Vector3 reference = Vector3::normal(bottomPosition, rightPosition, leftPosition);
    if (Vector3::dotProduct(Vector3::normal(topPosition, leftPosition, bottomPosition), reference) <= 0 ||
            Vector3::dotProduct(Vector3::normal(bottomPosition, rightPosition, topPosition), reference) <= 0)
        return false;
    relinkFlippedEdge(halfedge);
    return true;
}

void IsotropicHalfedgeMesh::relinkFlippedEdge(Halfedge *halfedge)
{
    Vertex *topVertex = halfedge->previousHalfedge->startVertex;
    Vertex *bottomVertex = halfedge->oppositeHalfedge->previousHalfedge->startVertex;
    Vertex *leftVertex = halfedge->startVertex;
    Vertex *rightVertex = halfedge->oppositeHalfedge->startVertex;
    Halfedge *opposite = halfedge->oppositeHalfedge;
    
    Face *topFace = halfedge->leftFace;
    Face *bottomFace = opposite->leftFace;
    
    if (leftVertex->firstHalfedge == halfedge)
        leftVertex->firstHalfedge = opposite->nextHalfedge;
    
    if (rightVertex->firstHalfedge == opposite)
        rightVertex->firstHalfedge = halfedge->nextHalfedge;
    
    halfedge->nextHalfedge->leftFace = bottomFace;
    opposite->nextHalfedge->leftFace = topFace;
    
    halfedge->startVertex = bottomVertex;
    opposite->startVertex = topVertex;
    
    topFace->halfedge = halfedge;
    bottomFace->halfedge = opposite;

    std::vector<Halfedge *> newLeftHalfedges = {
        halfedge->previousHalfedge,
        opposite->nextHalfedge,
        halfedge
    };
    std::vector<Halfedge *> newRightHalfedges = {
        opposite->previousHalfedge,
        halfedge->nextHalfedge,
        opposite,
    };
    
    linkFaceHalfedges(newLeftHalfedges);
    linkFaceHalfedges(newRightHalfedges);
}

bool IsotropicHalfedgeMesh::collapseEdge(Halfedge *halfedge, double maxEdgeLengthSquared)
{
    // Collapsing a boundary edge would need the face on the other side, which
    // a boundary edge does not have.
    if (nullptr == halfedge->oppositeHalfedge)
        return false;

    Halfedge *opposite = halfedge->oppositeHalfedge;
    Vertex *startVertex = halfedge->startVertex;
    Vertex *endVertex = opposite->startVertex;

    // Feature edges collapse only along their chain, and a vertex on a chain
    // stays on it (TODO 2.5, pmp-library's rules): a free vertex collapses
    // onto a feature vertex, two chain vertices to the middle of their
    // chain edge, a chain vertex onto a corner; corners never move.
    const bool startOnFeature = featureDegree(startVertex) > 0;
    const bool endOnFeature = featureDegree(endVertex) > 0;
    if (startOnFeature && endOnFeature) {
        // Two feature vertices a thousandth of an edge apart are one point
        // (on the sculpt set, two chains of the input through vertices
        // 3e-8 apart); left alone, the curvature of that edge flattened the
        // island's whole sizing field.  The merged vertex joins the chains,
        // so it is a corner.
        if ((startVertex->position - endVertex->position).lengthSquared() < 1e-6 * maxEdgeLengthSquared) {
            const bool startFixed = startVertex->featured || startVertex->featureCorner;
            const bool endFixed = endVertex->featured || endVertex->featureCorner;
            if (startFixed && endFixed)
                return false;
            Halfedge *removing = startFixed ? opposite : halfedge;
            Vertex *kept = startFixed ? startVertex : endVertex;
            if (!collapseEdgeTo(removing, kept->position, maxEdgeLengthSquared, true))
                return false;
            kept->featureCorner = true;
            return true;
        }
        // A chord between two chains, or between two vertices of one chain
        // that this edge does not follow.
        if (!halfedge->feature)
            return false;
        // The collapse would merge a feature edge of a face next to it.
        if (halfedge->previousHalfedge->feature || halfedge->nextHalfedge->feature ||
                opposite->previousHalfedge->feature || opposite->nextHalfedge->feature)
            return false;
        const bool startFixed = startVertex->featured || startVertex->featureCorner;
        const bool endFixed = endVertex->featured || endVertex->featureCorner;
        if (startFixed && endFixed)
            return false;
        if (startFixed)
            return collapseEdgeTo(opposite, startVertex->position, maxEdgeLengthSquared);
        if (endFixed)
            return collapseEdgeTo(halfedge, endVertex->position, maxEdgeLengthSquared);
        return collapseEdgeTo(halfedge, (startVertex->position + endVertex->position) * 0.5,
            maxEdgeLengthSquared);
    }
    if (startOnFeature)
        return collapseEdgeTo(opposite, startVertex->position, maxEdgeLengthSquared);
    if (endOnFeature)
        return collapseEdgeTo(halfedge, endVertex->position, maxEdgeLengthSquared);

    Vector3 collapseTo;
    if (!halfedge->startVertex->_smoothNormal.isZero() &&
            !halfedge->nextHalfedge->startVertex->_smoothNormal.isZero()) {
        const Vector3 &p1 = halfedge->startVertex->position;
        const Vector3 &p2 = halfedge->nextHalfedge->startVertex->position;
        const Vector3 &n1 = halfedge->startVertex->_smoothNormal;
        const Vector3 &n2 = halfedge->nextHalfedge->startVertex->_smoothNormal;
        double d12 = Vector3::dotProduct(p2 - p1, n1);
        double d21 = Vector3::dotProduct(p1 - p2, n2);
        Vector3 p210 = (2.0 * p1 + p2 - d12 * n1) / 3.0;
        Vector3 p120 = (2.0 * p2 + p1 - d21 * n2) / 3.0;
        collapseTo = (p1 + p2 + 3.0 * (p210 + p120)) / 8.0;
    } else {
        collapseTo = (halfedge->startVertex->position +
            halfedge->nextHalfedge->startVertex->position) * 0.5;
    }
    return collapseEdgeTo(halfedge, collapseTo, maxEdgeLengthSquared);
}

bool IsotropicHalfedgeMesh::testCollapseWouldJoinFeatureEdges(Halfedge *halfedge)
{
    // The collapse merges, on each side, the edge from the apex to the start
    // vertex with the one to the end vertex, so the faces on the far sides of
    // those two edges become neighbors across one edge, a feature edge when
    // either was.  A face that gains a feature edge that way and then has
    // two would join two chains in one triangle.
    for (Halfedge *side : { halfedge, halfedge->oppositeHalfedge }) {
        Halfedge *toStart = side->previousHalfedge;
        Halfedge *toEnd = side->nextHalfedge;
        if (toStart->feature == toEnd->feature)
            continue;
        Halfedge *gaining = toStart->feature ? toEnd->oppositeHalfedge : toStart->oppositeHalfedge;
        if (nullptr == gaining)
            continue;
        if (gaining->nextHalfedge->feature || gaining->previousHalfedge->feature)
            return true;
    }
    return false;
}

bool IsotropicHalfedgeMesh::collapseEdgeTo(Halfedge *halfedge, const Vector3 &collapseTo,
    double maxEdgeLengthSquared, bool joinFeatures)
{
    Halfedge *opposite = halfedge->oppositeHalfedge;
    
    Vertex *topVertex = halfedge->previousHalfedge->startVertex;
    Vertex *bottomVertex = opposite->previousHalfedge->startVertex;
    
    if (topVertex == bottomVertex)
        return false;
    
    if (topVertex->featured)
        return false;
    
    if (bottomVertex->featured)
        return false;

    if (!joinFeatures && testCollapseWouldJoinFeatureEdges(halfedge))
        return false;
    
    if (testLengthSquaredAroundVertex(halfedge->startVertex, collapseTo, maxEdgeLengthSquared))
        return false;
    if (testLengthSquaredAroundVertex(halfedge->nextHalfedge->startVertex, collapseTo, maxEdgeLengthSquared))
        return false;
    
    std::set<Vertex *> neighborVertices;
    std::set<Vertex *> otherNeighborVertices;
    collectVerticesAroundVertex(halfedge->startVertex, &neighborVertices);
    collectVerticesAroundVertex(halfedge->nextHalfedge->startVertex, &otherNeighborVertices);
    std::vector<Vertex *> sharedNeighborVertices;
    std::set_intersection(neighborVertices.begin(), neighborVertices.end(),
        otherNeighborVertices.begin(), otherNeighborVertices.end(),
        std::back_inserter(sharedNeighborVertices));
    if (sharedNeighborVertices.size() > 2) {
        return false;
    }

    // The shared neighbors are the top and bottom vertex. If both endpoints
    // also span a face with them, the edge belongs to a tetrahedron, and
    // collapsing it leaves two triangles glued along all three edges. This is
    // the edge part of the link condition; without it a small closed island
    // shrank to such a pair and vanished from the output.
    if (isFaceOfVertex(halfedge->startVertex, topVertex, bottomVertex) &&
            isFaceOfVertex(halfedge->nextHalfedge->startVertex, topVertex, bottomVertex)) {
        return false;
    }
    
    Vertex *leftVertex = halfedge->startVertex;
    Vertex *rightVertex = opposite->startVertex;
    Face *topFace = halfedge->leftFace;
    Face *bottomFace = opposite->leftFace;

    // Moving both endpoints to the collapse point may turn a surviving
    // neighbor triangle inside out or squash it onto a line, which leaves a
    // fold behind that no later stage can undo.
    if (testCollapseWouldFoldOrDegenerate(leftVertex, rightVertex, collapseTo, topFace, bottomFace))
        return false;
    if (testCollapseWouldFoldOrDegenerate(rightVertex, leftVertex, collapseTo, topFace, bottomFace))
        return false;

    pointerVertexToNewVertex(leftVertex, rightVertex);
    
    if (topFace == rightVertex->firstHalfedge->leftFace || 
            bottomFace == rightVertex->firstHalfedge->leftFace) {
        rightVertex->firstHalfedge = opposite->previousHalfedge->oppositeHalfedge;
    }
    if (topFace == topVertex->firstHalfedge->leftFace || 
            bottomFace == topVertex->firstHalfedge->leftFace) {
        topVertex->firstHalfedge = halfedge->nextHalfedge->oppositeHalfedge;
    }
    if (bottomFace == bottomVertex->firstHalfedge->leftFace || 
            bottomFace == bottomVertex->firstHalfedge->leftFace) {
        bottomVertex->firstHalfedge = opposite->nextHalfedge->oppositeHalfedge;
    }
    
    // Each merged pair is one feature edge when either of its edges was.
    for (Halfedge *side : { halfedge, opposite }) {
        Halfedge *first = side->previousHalfedge->oppositeHalfedge;
        Halfedge *second = side->nextHalfedge->oppositeHalfedge;
        const bool feature = side->previousHalfedge->feature || side->nextHalfedge->feature;
        const unsigned char featureFlags = side->previousHalfedge->featureFlags | side->nextHalfedge->featureFlags;
        const size_t featureChain = side->previousHalfedge->feature ? side->previousHalfedge->featureChain
                                                                     : side->nextHalfedge->featureChain;
        linkHalfedgePair(first, second);
        for (Halfedge *merged : { first, second }) {
            if (nullptr != merged) {
                merged->feature = feature;
                merged->featureFlags = featureFlags;
                merged->featureChain = featureChain;
            }
        }
    }
    
    rightVertex->position = collapseTo;
    
    leftVertex->removed = true;
    topFace->removed = true;
    bottomFace->removed = true;

    return true;
}

void IsotropicHalfedgeMesh::updateVertexValences()
{
    for (Vertex *vertex = m_firstVertex; nullptr != vertex; vertex = vertex->nextVertex) {
        if (vertex->removed)
            continue;
        vertex->_isBoundary = false;
        vertex->_valence = vertexValence(vertex, &vertex->_isBoundary);
    }
}

void IsotropicHalfedgeMesh::updateTriangleNormals()
{
    for (Face *face = m_firstFace; nullptr != face; face = face->nextFace) {
        if (face->removed)
            continue;
        auto &startHalfedge = face->halfedge;
        face->_normal = Vector3::normal(startHalfedge->previousHalfedge->startVertex->position,
            startHalfedge->startVertex->position,
            startHalfedge->nextHalfedge->startVertex->position);
    }
}

void IsotropicHalfedgeMesh::updateVertexNormals()
{
    for (Vertex *vertex = m_firstVertex; nullptr != vertex; vertex = vertex->nextVertex) {
        if (vertex->removed)
            continue;
        vertex->_normal = Vector3();
    }
    
    for (Face *face = m_firstFace; nullptr != face; face = face->nextFace) {
        if (face->removed)
            continue;
        auto &startHalfedge = face->halfedge;
        Vector3 faceNormal = face->_normal *
            Vector3::area(startHalfedge->previousHalfedge->startVertex->position,
                startHalfedge->startVertex->position,
                startHalfedge->nextHalfedge->startVertex->position);
        startHalfedge->previousHalfedge->startVertex->_normal += faceNormal;
        startHalfedge->startVertex->_normal += faceNormal;
        startHalfedge->nextHalfedge->startVertex->_normal += faceNormal;
    }
    
    for (Vertex *vertex = m_firstVertex; nullptr != vertex; vertex = vertex->nextVertex) {
        if (vertex->removed)
            continue;
        vertex->_normal.normalize();
    }
}

void IsotropicHalfedgeMesh::featureBoundaries()
{
    for (Face *face = m_firstFace; nullptr != face; face = face->nextFace) {
        if (face->removed)
            continue;
        auto &startHalfedge = face->halfedge;
        if (nullptr == startHalfedge->previousHalfedge->oppositeHalfedge) {
            startHalfedge->previousHalfedge->startVertex->featured = true;
        }
        if (nullptr == startHalfedge->oppositeHalfedge) {
            startHalfedge->startVertex->featured = true;
        }
        if (nullptr == startHalfedge->nextHalfedge->oppositeHalfedge) {
            startHalfedge->nextHalfedge->startVertex->featured = true;
        }
    }
}

void IsotropicHalfedgeMesh::markFeatureEdges(const std::vector<std::pair<size_t, size_t>> &edges,
    const std::vector<unsigned char> &flags)
{
    if (edges.empty())
        return;
    std::vector<Vertex *> vertices;
    for (Vertex *vertex = m_firstVertex; nullptr != vertex; vertex = vertex->nextVertex)
        vertices.push_back(vertex);
    std::unordered_map<Vertex *, size_t> indices;
    indices.reserve(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
        indices[vertices[i]] = i;
    std::unordered_map<uint64_t, unsigned char> featureFlags;
    featureFlags.reserve(edges.size());
    for (size_t i = 0; i < edges.size(); ++i) {
        const size_t first = std::min(edges[i].first, edges[i].second);
        const size_t second = std::max(edges[i].first, edges[i].second);
        featureFlags[makeHalfedgeKey(first, second)] = i < flags.size() ? flags[i] : 0;
    }
    for (Face *face = m_firstFace; nullptr != face; face = face->nextFace) {
        if (face->removed)
            continue;
        Halfedge *halfedge = face->halfedge;
        do {
            Halfedge *opposite = halfedge->oppositeHalfedge;
            if (nullptr != opposite) {
                size_t first = indices[halfedge->startVertex];
                size_t second = indices[opposite->startVertex];
                if (first > second)
                    std::swap(first, second);
                auto found = featureFlags.find(makeHalfedgeKey(first, second));
                if (found != featureFlags.end()) {
                    halfedge->feature = opposite->feature = true;
                    halfedge->featureFlags = opposite->featureFlags = found->second;
                }
            }
            halfedge = halfedge->nextHalfedge;
        } while (halfedge != face->halfedge);
    }
}

std::vector<IsotropicHalfedgeMesh::Vertex *> IsotropicHalfedgeMesh::splitFacesOnTwoFeatureEdges()
{
    std::vector<Vertex *> added;
    Face *lastFace = m_lastFace;
    for (Face *face = m_firstFace; nullptr != face; face = face->nextFace) {
        if (!face->removed) {
            size_t featureEdges = 0;
            Halfedge *plain = nullptr;
            Halfedge *halfedge = face->halfedge;
            do {
                if (halfedge->feature)
                    ++featureEdges;
                else
                    plain = halfedge;
                halfedge = halfedge->nextHalfedge;
            } while (halfedge != face->halfedge);
            if (2 == featureEdges) {
                // When the face is flat, its apex lies on the third edge
                // and a split would put the new vertex on top of it (on the
                // sculpt set, a straight chain with a flat face on one side:
                // an edge of 1e-8 that flattened the island's sizing field).
                // The flat face is flipped away, or left alone.
                const Vector3 &apex = plain->previousHalfedge->startVertex->position;
                const Vector3 &left = plain->startVertex->position;
                const Vector3 &right = plain->nextHalfedge->startVertex->position;
                const double baseSquared = (right - left).lengthSquared();
                const bool flat = 2.0 * Vector3::area(apex, left, right) < 0.25 * baseSquared;
                if (!flat) {
                    breakEdge(plain);
                    added.push_back(m_lastVertex);
                } else {
                    flipEdgeUnderFlatFace(plain);
                }
            }
        }
        // The faces a split adds have one feature edge at most.
        if (face == lastFace)
            break;
    }
    return added;
}

void IsotropicHalfedgeMesh::featureCorners()
{
    // A corner where the chain turns by more than this, the design's 45
    // degrees (research/crease-preservation-2026-10-26.md, 5.1): sliding
    // past it would cut the corner off.
    const double maxChainTurnCosine = std::cos(45.0 * M_PI / 180.0);
    for (Vertex *vertex = m_firstVertex; nullptr != vertex; vertex = vertex->nextVertex) {
        if (vertex->removed)
            continue;
        Vertex *first = nullptr;
        Vertex *second = nullptr;
        const size_t degree = featureDegree(vertex, &first, &second);
        if (0 == degree)
            continue;
        if (2 != degree) {
            vertex->featureCorner = true;
            continue;
        }
        Vector3 in = vertex->position - first->position;
        Vector3 out = second->position - vertex->position;
        if (in.isZero() || out.isZero()) {
            vertex->featureCorner = true;
            continue;
        }
        if (Vector3::dotProduct(in.normalized(), out.normalized()) < maxChainTurnCosine)
            vertex->featureCorner = true;
    }

    // Number the chains: walk from each unnumbered feature halfedge through
    // chain vertices in both directions.
    size_t chain = 0;
    const auto nextOnChain = [&](Halfedge *arrived) -> Halfedge * {
        Vertex *vertex = arrived->nextHalfedge->startVertex;
        if (vertex->featureCorner)
            return nullptr;
        Halfedge *next = nullptr;
        iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
            if (halfedge->feature && halfedge->oppositeHalfedge != arrived) {
                next = halfedge;
                return false;
            }
            return true;
        });
        return next;
    };
    for (Face *face = m_firstFace; nullptr != face; face = face->nextFace) {
        if (face->removed)
            continue;
        Halfedge *halfedge = face->halfedge;
        do {
            if (halfedge->feature && 0 == halfedge->featureChain) {
                ++chain;
                Halfedge *backwards = nullptr == halfedge->oppositeHalfedge ? nullptr : nextOnChain(halfedge->oppositeHalfedge);
                for (Halfedge *start : { halfedge, backwards }) {
                    for (Halfedge *walk = start; nullptr != walk && 0 == walk->featureChain; walk = nextOnChain(walk)) {
                        walk->featureChain = chain;
                        if (nullptr != walk->oppositeHalfedge)
                            walk->oppositeHalfedge->featureChain = chain;
                    }
                }
            }
            halfedge = halfedge->nextHalfedge;
        } while (halfedge != face->halfedge);
    }
}

std::vector<IsotropicHalfedgeMesh::FeatureSegment> IsotropicHalfedgeMesh::featureSegments()
{
    std::vector<FeatureSegment> segments;
    for (Face *face = m_firstFace; nullptr != face; face = face->nextFace) {
        if (face->removed)
            continue;
        Halfedge *halfedge = face->halfedge;
        do {
            if (halfedge->feature && (nullptr == halfedge->oppositeHalfedge
                    || halfedge->startVertex->debugIndex < halfedge->nextHalfedge->startVertex->debugIndex)) {
                FeatureSegment segment;
                segment.first = halfedge->startVertex->position;
                segment.second = halfedge->nextHalfedge->startVertex->position;
                segment.chain = halfedge->featureChain;
                segments.push_back(segment);
            }
            halfedge = halfedge->nextHalfedge;
        } while (halfedge != face->halfedge);
    }
    return segments;
}

size_t IsotropicHalfedgeMesh::vertexFeatureChain(Vertex *vertex)
{
    size_t chain = 0;
    iterateVertexHalfedges(vertex, [&](Halfedge *halfedge) {
        if (halfedge->feature) {
            chain = halfedge->featureChain;
            return false;
        }
        return true;
    });
    return chain;
}

bool IsotropicHalfedgeMesh::moveVertexIfValid(Vertex *vertex, const Vector3 &target)
{
    if (target.containsNan() || target.containsInf())
        return false;
    if (testMoveWouldDegenerate(vertex, target) || testMoveWouldFold(vertex, target))
        return false;
    vertex->position = target;
    return true;
}
