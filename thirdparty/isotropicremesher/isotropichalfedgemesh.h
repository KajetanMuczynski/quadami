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
#ifndef ISOTROPIC_HALFEDGE_MESH_H
#define ISOTROPIC_HALFEDGE_MESH_H
#include <cstdint>
#include <set>
#include <functional>
#include <utility>
#include <vector>
#include "vector3.h"
#include <cstdint>

class IsotropicHalfedgeMesh
{
public:
    struct Halfedge;
      
    struct Vertex
    {
        Vector3 position;
        Halfedge *firstHalfedge = nullptr;
        Vertex *previousVertex = nullptr;
        Vertex *nextVertex = nullptr;
        int initialFaces = 0;
        int _valence = -1;
        bool _isBoundary = false;
        Vector3 _normal;
        Vector3 _smoothNormal; // Smooth normal from input (propagated through splits)
        bool removed = false;
        size_t debugIndex = 0;
        size_t outputIndex = 0;
        // Frozen: a boundary vertex, or one whose faces don't close a fan.
        bool featured = false;
        // An end of a feature edge that the remesh never moves or removes:
        // feature degree other than 2, or a turn of more than 45 degrees
        // between its two feature edges (featureCorners).  The other
        // vertices on feature edges are chain vertices: they slide along the
        // chain and are projected onto it.
        bool featureCorner = false;
        double targetEdgeLength = 0.0; // 0 = use global default in split/collapse
        Vertex *_allocLink = nullptr;
    };

    struct Face
    {
        Halfedge *halfedge = nullptr;
        Face *previousFace = nullptr;
        Face *nextFace = nullptr;
        Vector3 _normal;
        bool removed = false;
        size_t debugIndex = 0;
        Face *_allocLink = nullptr;
    };

    struct Halfedge
    {
        Vertex *startVertex = nullptr;
        Face *leftFace = nullptr;
        Halfedge *nextHalfedge = nullptr;
        Halfedge *previousHalfedge = nullptr;
        Halfedge *oppositeHalfedge = nullptr;
        // A feature edge (both halfedges carry it), from the caller's set:
        // split halves inherit it, it is never flipped, and a collapse
        // removes one only along its chain.
        bool feature = false;
        unsigned char featureFlags = 0;
        // The input chain the feature edge lies on, from 1 (featureCorners);
        // 0 for no feature edge.
        size_t featureChain = 0;
        size_t debugIndex = 0;
        Halfedge *_allocLink = nullptr;
    };
    
    IsotropicHalfedgeMesh(const std::vector<Vector3> &vertices,
        const std::vector<std::vector<size_t>> &faces);
    ~IsotropicHalfedgeMesh();
    
    double averageEdgeLength();
    void breakEdge(Halfedge *halfedge);
    bool collapseEdge(Halfedge *halfedge, double maxEdgeLengthSquared);
    bool flipEdge(Halfedge *halfedge);
    // Flips the edge under a flat face, judged by the face across only.
    bool flipEdgeUnderFlatFace(Halfedge *halfedge);
    void relaxVertex(Vertex *vertex);
    // Number of feature edges at the vertex; the other ends of the first two
    // go to `first` and `second` when given.
    size_t featureDegree(Vertex *vertex, Vertex **first = nullptr, Vertex **second = nullptr);
    // A chain vertex: on exactly two feature edges, not frozen, no corner.
    bool isChainVertex(Vertex *vertex);
    size_t vertexValence(Vertex *vertex, bool *isBoundary=nullptr);
    Face *moveToNextFace(Face *face);
    Vertex *moveToNextVertex(Vertex *vertex);
    void updateVertexValences();
    void updateVertexNormals();
    void updateTriangleNormals();
    // Marks the caller's feature edges, given as pairs of input vertex
    // indices with a flags byte each; call before anything adds a vertex.
    void markFeatureEdges(const std::vector<std::pair<size_t, size_t>> &edges,
        const std::vector<unsigned char> &flags);
    // Splits the third edge of every face with two feature edges, so that
    // no face lies on two chains (or twice on one); returns the new vertices.
    std::vector<Vertex *> splitFacesOnTwoFeatureEdges();
    // Marks the input's feature corners and numbers its chains (the paths
    // of feature edges between corners); call right after markFeatureEdges.
    void featureCorners();
    struct FeatureSegment
    {
        Vector3 first;
        Vector3 second;
        size_t chain = 0;
    };
    // Every feature edge once, with its chain.
    std::vector<FeatureSegment> featureSegments();
    // The chain of the vertex's first feature edge; 0 for none.
    size_t vertexFeatureChain(Vertex *vertex);
    // Moves the vertex unless that degenerates or folds one of its faces.
    bool moveVertexIfValid(Vertex *vertex, const Vector3 &target);
    void featureBoundaries();
private:
    void relinkFlippedEdge(Halfedge *halfedge);
    Vertex *m_firstVertex = nullptr;
    Vertex *m_lastVertex = nullptr;
    Face *m_firstFace = nullptr;
    Face *m_lastFace = nullptr;
    size_t m_debugVertexIndex = 0;
    size_t m_debugFaceIndex = 0;
    size_t m_debugHalfedgeIndex = 0;
    Vertex *m_vertexAllocLink = nullptr;
    Face *m_faceAllocLink = nullptr;
    Halfedge *m_halfedgeAllocLink = nullptr;
    
    static inline uint64_t makeHalfedgeKey(size_t first, size_t second)
    {
        return (first << 32) | second;
    }
    
    static inline uint64_t swapHalfedgeKey(uint64_t key)
    {
        return makeHalfedgeKey(key & 0xffffffff, key >> 32);
    }
    
    Face *firstFace()
    {
        return m_firstFace;
    }
    
    Vertex *firstVertex()
    {
        return m_firstVertex;
    }
    
    Face *newFace();
    Vertex *newVertex();
    Halfedge *newHalfedge();
    void linkFaceHalfedges(std::vector<Halfedge *> &halfedges);
    void updateFaceHalfedgesLeftFace(std::vector<Halfedge *> &halfedges,
        Face *leftFace);
    void linkHalfedgePair(Halfedge *first, Halfedge *second);
    void breakFace(Face *leftOldFace,
        Halfedge *halfedge,
        Vertex *breakPointVertex,
        std::vector<Halfedge *> &leftNewFaceHalfedges,
        std::vector<Halfedge *> &leftOldFaceHalfedges);
    void pointerVertexToNewVertex(Vertex *vertex, Vertex *replacement);
    bool testLengthSquaredAroundVertex(Vertex *vertex, 
        const Vector3 &target, 
        double maxEdgeLengthSquared);
    bool isVertexPairConnected(Vertex *first, Vertex *second);
    bool isFaceOfVertex(Vertex *vertex, Vertex *second, Vertex *third);
    bool testMoveWouldDegenerate(Vertex *vertex, const Vector3 &target);
    bool testMoveWouldFold(Vertex *vertex, const Vector3 &target);
    bool collapseEdgeTo(Halfedge *halfedge, const Vector3 &collapseTo, double maxEdgeLengthSquared,
        bool joinFeatures = false);
    bool testCollapseWouldJoinFeatureEdges(Halfedge *halfedge);
    bool testCollapseWouldFoldOrDegenerate(Vertex *vertex,
        Vertex *otherVertex,
        const Vector3 &collapseTo,
        Face *removedFaceOne,
        Face *removedFaceTwo);
    void collectVerticesAroundVertex(Vertex *vertex,
        std::set<Vertex *> *vertices);
    void iterateVertexHalfedges(Vertex *vertex, std::function<bool(Halfedge *halfedge)> handler);
};

#endif
