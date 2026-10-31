/*
 *  Copyright (c) 2020 Jeremy HU <jeremy-at-dust3d dot org>. All rights reserved. 
 *  Modifications Copyright (C) 2026 Quadami contributors, GPL-3.0-or-later (see LICENSE).
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
#ifndef AUTO_REMESHER_QUAD_EXTRACTOR_H
#define AUTO_REMESHER_QUAD_EXTRACTOR_H
#include "featureedges.h"
#include "qexextraction.h"
#include <AutoRemesher/Progress>
#include <AutoRemesher/Vector2>
#include <AutoRemesher/Vector3>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace AutoRemesher {

class QuadExtractor {
public:
    QuadExtractor(const std::vector<Vector3>* vertices,
        const std::vector<std::vector<size_t>>* triangles,
        const std::vector<std::vector<Vector2>>* triangleUvs)
        : m_vertices(vertices)
        , m_triangles(triangles)
        , m_triangleUvs(triangleUvs)
    {
    }

    const std::vector<Vector3>& remeshedVertices()
    {
        return m_remeshedVertices;
    }

    const std::vector<std::vector<size_t>>& remeshedQuads()
    {
        return m_remeshedPolygons;
    }

    // The raw connections produced by extractConnections(), before graph cleanup.
    const std::vector<std::pair<Vector3, Vector3>>& extractedConnections() const
    {
        return m_extractedConnections;
    }

    void setOriginalTriangleUvs(const std::vector<std::vector<Vector2>>* originalTriangleUvs)
    {
        m_originalTriangleUvs = originalTriangleUvs;
    }

    void setSingularVertices(const std::vector<size_t>* singularVertices)
    {
        m_singularVertices = singularVertices;
    }

    // T's feature edges: the smoothing keeps the quad vertices that lie on a
    // hard one where they are, so an edge loop along a guide stays on it
    // (TODO 5.7). The set must outlive the extractor.
    void setFeatureEdges(const FeatureEdgeSet* featureEdges)
    {
        m_featureEdges = featureEdges;
    }
    // Output vertices on a hard edge of T (setFeatureEdges): within this
    // share of T's mean feature edge length of it. A vertex the extractor
    // puts on an isoline that runs along T's edge is on it up to rounding.
    static constexpr double kPinnedToleranceOfEdge = 1e-6;

    void setProgressHandler(ProgressHandler progressHandler)
    {
        m_progressHandler = std::move(progressHandler);
    }

    // libQEx first (default), or only our own graph tracing.
    void setUseQex(bool useQex)
    {
        m_useQex = useQex;
    }

    // The island's target; sizes libQEx's memory cap (0: no cap).
    void setTargetQuadCount(size_t targetQuadCount)
    {
        m_targetQuadCount = targetQuadCount;
    }

    // Smooth the extracted quads only along the surface (each Laplace step
    // without its part along the vertex normal) instead of in 3D, so ridges and
    // creases are not rounded off before the projection onto T.  Off by default.
    void setTangentialSmoothing(bool tangential)
    {
        m_tangentialSmoothing = tangential;
    }

    // Whether libQEx extracted this island, and if not, why.
    const QexOutcome& qexOutcome() const
    {
        return m_qexOutcome;
    }

    // Per connection of extractedConnections(): 0 untouched, 1 on a triangle whose uv
    // was repaired, 2 added by holdSingularLines(). Drives the [Param] preview color.
    const std::vector<uint8_t>& extractedConnectionMoved() const
    {
        return m_extractedConnectionMoved;
    }

    bool extract();

    // The open boundary of a triangle mesh: the edges only one triangle uses,
    // each with the boundary loop it belongs to.  It keeps its own copy of the
    // boundary vertices.
    struct Boundary {
        std::vector<Vector3> vertices;
        std::vector<std::pair<size_t, size_t>> edges;
        std::vector<size_t> edgeLoops;
        std::vector<std::vector<size_t>> loopVertices;
    };

    static Boundary findBoundary(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles);

    // The boundary of the input surface the triangle mesh was resampled from.
    // fixHoles() leaves an output loop open when it follows one of its loops
    // and fills every other hole.  The triangle mesh's own boundary is not
    // used: decimation and resampling can open small holes in a closed input.
    void setInputBoundary(const Boundary* inputBoundary)
    {
        m_inputBoundary = inputBoundary;
    }

    // The hole-filling step of extract() on its own: fills the holes of a
    // polygon mesh as extract() does, leaving open the loops that follow
    // inputBoundary (nullptr fills every hole up to 65 edges).  A hole the
    // quads can't close gets a vertex at its center, appended to vertices.
    static void fillHoles(std::vector<Vector3>* vertices,
        std::vector<std::vector<size_t>>* polygons,
        const Boundary* inputBoundary = nullptr);

    // The whole repair step of extract() on its own: fills the holes, then
    // drops the faces at non-manifold vertices and every piece but the
    // largest, and fills the holes that leaves.
    static void repairSurface(std::vector<Vector3>* vertices,
        std::vector<std::vector<size_t>>* polygons,
        const Boundary* inputBoundary = nullptr);

private:
    // The isoline a connection was cut from: which uv coordinate is held constant,
    // which integer value it is held at, and which triangle produced the segment.
    struct ConnectionInfo {
        size_t triangleIndex = 0;
        int coordIndex = 0;
        int integer = 0;
    };

    const std::vector<Vector3>* m_vertices = nullptr;
    const std::vector<std::vector<size_t>>* m_triangles = nullptr;
    const std::vector<std::vector<Vector2>>* m_triangleUvs = nullptr;
    std::vector<Vector3> m_remeshedVertices;
    std::vector<std::vector<size_t>> m_remeshedPolygons;
    std::vector<std::pair<Vector3, Vector3>> m_extractedConnections;
    std::vector<uint8_t> m_extractedConnectionMoved;
    const std::vector<std::vector<Vector2>>* m_originalTriangleUvs = nullptr;
    const std::vector<size_t>* m_singularVertices = nullptr;
    const FeatureEdgeSet* m_featureEdges = nullptr;
    const Boundary* m_inputBoundary = nullptr;
    ProgressHandler m_progressHandler;
    bool m_useQex = true;
    bool m_tangentialSmoothing = false;
    size_t m_targetQuadCount = 0;
    QexOutcome m_qexOutcome;
    std::map<std::pair<size_t, size_t>, ConnectionInfo> m_connectionInfos;
    std::set<std::pair<size_t, size_t>> m_addedConnections;
    std::set<std::pair<size_t, size_t>> m_halfEdges;

    bool extractWithQexFrontEnd();
    void traceMesh();
    // Hole filling, repair and the cleanup passes, after either extractor.
    void finishMesh();
    void extractConnections(std::vector<Vector3>* crossPoints,
        std::vector<size_t>* sourceTriangles,
        std::set<std::pair<size_t, size_t>>* connections);
    void holdSingularLines(std::vector<Vector3>* crossPoints,
        std::vector<size_t>* sourceTriangles,
        std::set<std::pair<size_t, size_t>>* connections);
    void extractEdges(const std::set<std::pair<size_t, size_t>>& connections,
        std::unordered_map<size_t, std::unordered_set<size_t>>* edgeConnectMap);
    bool collapseShortEdges(std::vector<Vector3>* crossPoints,
        std::unordered_map<size_t, std::unordered_set<size_t>>* edgeConnectMap);
    bool collapseTriangles(std::vector<Vector3>* crossPoints,
        std::unordered_map<size_t, std::unordered_set<size_t>>* edgeConnectMap);
    bool removeSingleEndpoints(std::vector<Vector3>* crossPoints,
        std::unordered_map<size_t, std::unordered_set<size_t>>* edgeConnectMap);
    void collapseEdge(std::vector<Vector3>* crossPoints,
        std::unordered_map<size_t, std::unordered_set<size_t>>* edgeConnectMap,
        const std::pair<size_t, size_t>& edge);
    void extractMesh(std::vector<Vector3>& points,
        const std::vector<size_t>& pointSourceTriangles,
        std::unordered_map<size_t, std::unordered_set<size_t>>& edgeConnectMap,
        std::vector<std::vector<size_t>>* quads);
    void simplifyGraph(std::unordered_map<size_t, std::unordered_set<size_t>>& graph);
    void searchBoundaries(const std::set<std::pair<size_t, size_t>>& halfEdges,
        std::vector<std::vector<size_t>>* loops);
    // Closes the hole with quads (and a last triangle on an odd loop) and
    // returns true when it closed it.  Each quad it adds takes two vertices
    // off hole, so on false hole is what is still open.
    bool fixHoleWithQuads(std::vector<size_t>& hole, bool checkScore = true);
    // Closes the hole with a fan of quads (and a triangle on an odd loop)
    // around a new vertex at its center.  Always succeeds, so a hole the
    // quads above can't close is not left open.
    void fixHoleAroundCenter(const std::vector<size_t>& hole);
    bool testPointInTriangle(const std::vector<Vector3>& points,
        const std::vector<size_t>& triangle,
        const std::vector<size_t>& testPoints);
    bool removeIsolatedFaces();
    // Vertices whose faces form more than one fan: two pieces of surface
    // touching at a point.
    static std::unordered_set<size_t> findPinchedVertices(const std::vector<std::vector<size_t>>& polygons);
    std::vector<size_t> pinnedVertices() const;
    void smoothAndProject(size_t iterations,
        const std::unordered_set<size_t>* movableVertices = nullptr);
    void smoothAroundVertices(const std::unordered_set<size_t>& seedVertices,
        size_t rings, size_t iterations);
    void splitSixEdgeFaces();
    void switchHighValenceEdges();
    void convertTriangleAndFiveEdgeFans();
    void collapseThreeValenceDiagonals();
    void mergeDoubleSharedEdgeQuads();
    void mergeThreeAndFiveValenceTriangles();
    void collapseThreeValenceCorners();
    void splitHighValenceTriangleFans();
    void collapseThreeValenceEdgePairs();
    void cleanupTriangles();
    void splitSevenEdgeFaces();
    // Merges one edge per pass over the whole polygon set, so it reports from
    // inside its round loop instead of running silently to completion.
    void mergeSharedFiveEdgeFaces(const ProgressHandler* progressHandler = nullptr);
    bool removeNonManifoldFaces();
    // Drops the faces at non-manifold vertices and every piece but the
    // largest, then fills the holes that leaves.
    void repairTopology();
    void rebuildHalfEdges();
    void fixHoles();
};

}

#endif
