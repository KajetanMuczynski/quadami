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
#ifndef AUTO_REMESHER_AUTO_REMESHER_H
#define AUTO_REMESHER_AUTO_REMESHER_H
#include <AutoRemesher/Diagnostics>
#include <AutoRemesher/Progress>
#include <AutoRemesher/Vector3>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace AutoRemesher {

class IsotropicRemesher;

enum class ModelType {
    Organic,
    HardSurface
};

typedef void (*AutoRemesherProgressHandler)(void* tag, float progress, const char* status);

// Triangles per output quad in the mesh T that the cross field and the
// parametrization are solved on, counted before stage 3 refines curved
// regions.  The quad count does not depend on it; a finer T gives cleaner
// quads and takes longer.  Fast is the resolution the core always used.
// Quality is the knee of the corpus sweep in TODO 2.15.
const double FastTrianglesPerQuad = 2.0;
const double QualityTrianglesPerQuad = 4.5;

class AutoRemesher {
public:
    AutoRemesher(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles)
        : m_vertices(vertices)
        , m_triangles(triangles)
    {
    }

    AutoRemesher(std::vector<Vector3>&& vertices,
        std::vector<std::vector<size_t>>&& triangles)
        : m_vertices(std::move(vertices))
        , m_triangles(std::move(triangles))
    {
    }

    void setTargetTriangleCount(size_t targetTriangleCount)
    {
        m_targetTriangleCount = targetTriangleCount;
    }

    void setScaling(double scaling)
    {
        m_scaling = scaling;
    }

    // See FastTrianglesPerQuad.
    void setTrianglesPerQuad(double trianglesPerQuad)
    {
        m_trianglesPerQuad = trianglesPerQuad;
    }

    void setProgressHandler(AutoRemesherProgressHandler progressHandler)
    {
        m_progressHandler = progressHandler;
    }

    void setTag(void* tag)
    {
        m_tag = tag;
    }

    void setModelType(ModelType modelType)
    {
        m_modelType = modelType;
    }

    void setGradientAdaptivity(double adaptivity)
    {
        m_adaptivity = adaptivity;
    }

    void setSharpEdgeDegrees(double degrees)
    {
        m_sharpEdgeDegrees = degrees;
    }

    void setAnisotropy(double anisotropy)
    {
        m_anisotropy = anisotropy;
    }

    // As RemeshParameters::extractor: libQEx first, or our tracing only.
    void setUseQex(bool useQex)
    {
        m_useQex = useQex;
    }

    void setSmoothNormalDegrees(double degrees)
    {
        m_smoothNormalDegrees = degrees;
    }

    // Untangle folded-over input triangles before remeshing (FoldRepair).
    void setRepairInputFolds(bool repair)
    {
        m_repairInputFolds = repair;
    }

    // With the fold repair: also lower needle tips where a fold cannot be
    // untangled (FoldRepair::Options::flattenSpikes).
    void setFlattenSpikes(bool flatten)
    {
        m_flattenSpikes = flatten;
    }

    // Asked at every progress point and at island and stage boundaries; once it
    // returns true the run stops and remesh() returns false with cancelled()
    // set.  Called from the island worker threads, so it must be thread-safe.
    void setCancelCheck(std::function<bool()> cancelCheck)
    {
        m_cancelCheck = std::move(cancelCheck);
    }

    bool cancelled() const
    {
        return m_cancelled;
    }

    size_t islandCount() const
    {
        return m_islandCount;
    }

    // Islands that went through the pipeline but contributed no faces, either
    // because a stage failed on them or because extraction found nothing.
    size_t islandsWithoutOutput() const
    {
        return m_islandsWithoutOutput;
    }

    // One entry per input island, in island order: the island's slice of
    // remeshedVertices() and remeshedQuads(), which follow each other in the
    // same order, and whether the island was closed in the input (no boundary
    // or non-manifold edge).
    struct RemeshedIsland {
        size_t vertexCount = 0;
        size_t faceCount = 0;
        bool closedInput = false;
        // Twice the handle count (twiceHandleCount()) of the island's input
        // and of its output; -1 where it is not measured: an input with a
        // non-manifold edge has no genus to keep, an island without output
        // has nothing to compare.
        long long inputTwiceHandles = -1;
        long long outputTwiceHandles = -1;
        // The quads the island was asked for: its share of the target by
        // area, raised for a small island.
        double targetQuads = 0.0;
    };

    const std::vector<RemeshedIsland>& remeshedIslands() const
    {
        return m_remeshedIslands;
    }

    const std::vector<Vector3>& remeshedVertices()
    {
        return m_remeshedVertices;
    }

    const std::vector<std::vector<size_t>>& remeshedQuads()
    {
        return m_remeshedQuads;
    }

    const std::vector<Vector3>& decimatedVertices()
    {
        return m_decimatedVertices;
    }

    const std::vector<std::vector<size_t>>& decimatedTriangles()
    {
        return m_decimatedTriangles;
    }

    bool decimated()
    {
        return m_decimated;
    }

    const std::vector<Vector3>& isotropicVertices()
    {
        return m_isotropicVertices;
    }

    const std::vector<std::vector<size_t>>& isotropicTriangles()
    {
        return m_isotropicTriangles;
    }

    const std::vector<uint8_t>& isotropicExtractedConnectionMoved()
    {
        return m_isotropicExtractedConnectionMoved;
    }

    const std::vector<std::vector<Vector2>>& isotropicOriginalTriangleUvs()
    {
        return m_isotropicOriginalTriangleUvs;
    }

    const std::vector<std::vector<Vector2>>& isotropicTriangleUvs()
    {
        return m_isotropicTriangleUvs;
    }

    const std::vector<Vector3>& isotropicSingularVertices()
    {
        return m_isotropicSingularVertices;
    }

    const std::vector<std::pair<Vector3, Vector3>>& isotropicExtractedConnections()
    {
        return m_isotropicExtractedConnections;
    }

    bool remesh();

    // `progress` is how far island `threadIndex` has got, 0..1.  `status` names
    // the step it is on, or nullptr to keep the island's current one.  Called
    // from the island worker threads.
    void updateProgress(size_t threadIndex, float progress, const char* status = nullptr);

    // Records how long a named pipeline step took, summed over the islands that
    // ran it.  `order` places the step in the phase report; it is the step's
    // position along the pipeline, so the report reads in execution order no
    // matter which island happened to reach the step first.
    void accumulateStageTime(const char* name, float order, long long microseconds);

    // A handler for one stage of one island: maps the stage's own 0..1 fraction
    // onto [begin, end] of that island's progress, and times each named step on
    // the way through for the phase report.  `stageOrder` is where the stage sits
    // along the pipeline, so the report reads in execution order.  Only the
    // island's own worker thread calls the result.
    ProgressHandler makeStageProgress(size_t islandIndex, float begin, float end, float stageOrder);

    const std::vector<std::string>& phaseReport()
    {
        return m_phaseReport;
    }

    struct StageTime {
        std::string name;
        float order = 0.0f;
        long long microseconds = 0;
    };

    // Per-step times summed over the islands, in pipeline order; filled by a
    // completed remesh().
    const std::vector<StageTime>& stepTimes() const
    {
        return m_stageTimes;
    }

    // Per-stage measurements of the last remesh(): one "run" record, one
    // "step" record per named step, one "island" record per island, then each
    // island's solver and rounding records in island order.  Write them with
    // diagnosticsToJsonLines().
    const std::vector<DiagnosticRecord>& diagnostics() const
    {
        return m_diagnostics;
    }

    // What cleanInput() changed in the input.
    struct InputCleanupStats {
        // Vertices merged into another across an edge no longer than the
        // tolerance.
        size_t mergedVertices = 0;
        // Triangles left with a repeated corner by the merge.
        size_t droppedTriangles = 0;
        // Connected parts left as they were because the merge would have
        // changed their topology.
        size_t keptParts = 0;
        // Triangles of zero area that remain: three distinct but collinear
        // corners, which only an edge flip can remove.
        size_t zeroAreaTriangles = 0;
        // Triangles turned to agree with their neighbours across manifold
        // edges, and pieces left as they were because no orientation fits
        // them (a Moebius strip).
        size_t flippedTriangles = 0;
        size_t nonOrientablePieces = 0;
        // Open seams joined by weldSeams() (inputsanitizer.h): boundary edge
        // pairs, vertices merged, and parts left unwelded.
        size_t weldedEdges = 0;
        size_t weldedVertices = 0;
        size_t weldKeptParts = 0;
        // Triangles removed because an earlier one has the same corners.
        size_t duplicateTriangles = 0;
    };

    // Edges up to this long are rounding noise of the float32 positions
    // hosts store: a few float32 ulps of the bounding box diagonal.
    static double inputCleanupTolerance(const std::vector<Vector3>& vertices);

    // Merges the ends of every edge no longer than `tolerance` and drops the
    // triangles that become degenerate, in each connected part whose
    // components, boundary, non-manifold edges and Euler characteristic this
    // keeps.  Coincident vertices that no edge joins stay apart (a surface
    // touching itself).  Vertices keep their indices; merged ones stay in
    // `vertices`, unused.  Without such an edge nothing is changed.
    static InputCleanupStats cleanInput(const std::vector<Vector3>& vertices,
        std::vector<std::vector<size_t>>& triangles,
        double tolerance);

    // Turns triangles so that each piece connected through manifold edges
    // (two triangles) has one orientation: the one of most of its area.
    // Island splitting follows opposite half-edges, so a flipped patch would
    // otherwise come out as an island of its own.  A consistent mesh is left
    // unchanged; counts go to `stats`.
    static void orientInput(const std::vector<Vector3>& vertices,
        std::vector<std::vector<size_t>>& triangles,
        InputCleanupStats* stats);

    static const double m_defaultSharpEdgeDegrees;

    // Per-island durations are accumulated in microseconds: a mesh split into
    // hundreds of islands spends well under a millisecond on most of them, and
    // truncating each one to whole milliseconds loses the bulk of the total.
    // Of a mesh's faces: Euler characteristic and the edges on one face or
    // on more than two.
    struct SurfaceTopology {
        size_t components = 0;
        size_t boundaryEdges = 0;
        size_t nonManifoldEdges = 0;
        long long eulerCharacteristic = 0;
    };

    struct DecimationStats {
        std::atomic<long long> timeUs { 0 };
        std::atomic<size_t> islandsDecimated { 0 };
        // Islands left undecimated because decimating changed their topology.
        std::atomic<size_t> islandsKeptUndecimated { 0 };
        std::atomic<size_t> islandsConsidered { 0 };
        std::atomic<size_t> trianglesBefore { 0 };
        std::atomic<size_t> trianglesAfter { 0 };
    };

private:
    std::vector<Vector3> m_vertices;
    std::vector<std::vector<size_t>> m_triangles;
    std::vector<Vector3> m_remeshedVertices;
    std::vector<std::vector<size_t>> m_remeshedQuads;
    std::vector<RemeshedIsland> m_remeshedIslands;
    std::vector<Vector3> m_decimatedVertices;
    std::vector<std::vector<size_t>> m_decimatedTriangles;
    bool m_decimated = false;
    std::vector<Vector3> m_isotropicVertices;
    std::vector<std::vector<size_t>> m_isotropicTriangles;
    std::vector<std::vector<Vector2>> m_isotropicTriangleUvs;
    std::vector<std::vector<Vector2>> m_isotropicOriginalTriangleUvs;
    std::vector<uint8_t> m_isotropicExtractedConnectionMoved;
    std::vector<Vector3> m_isotropicSingularVertices;
    std::vector<std::pair<Vector3, Vector3>> m_isotropicExtractedConnections;
    std::vector<float> m_threadProgress;
    std::vector<float> m_threadProgressWeights;
    std::vector<const char*> m_threadStatus;
    // The weighted sum of m_threadProgress, kept incrementally so that a
    // fine-grained update stays O(1) rather than a scan of every island.
    double m_progressSum = 0.0;
    int m_reportedPermille = -1;
    const char* m_reportedStatus = nullptr;
    std::vector<std::string> m_phaseReport;
    std::vector<DiagnosticRecord> m_diagnostics;
    mutable std::mutex m_progressMutex;
    std::mutex m_stageTimingMutex;
    std::vector<StageTime> m_stageTimes;
    double m_scaling = 0.0;
    size_t m_targetTriangleCount = 0;
    double m_trianglesPerQuad = QualityTrianglesPerQuad;
    double m_voxelSize = 0.0;
    // As RemeshParameters::adaptivity.
    double m_adaptivity = 0.5;
    double m_anisotropy = 1.0;
    double m_sharpEdgeDegrees = m_defaultSharpEdgeDegrees;
    double m_smoothNormalDegrees = 0.0;
    bool m_useQex = true;
    bool m_repairInputFolds = false;
    bool m_flattenSpikes = false;
    ModelType m_modelType = ModelType::Organic;
    AutoRemesherProgressHandler m_progressHandler = nullptr;
    void* m_tag = nullptr;
    std::function<bool()> m_cancelCheck;
    bool m_cancelled = false;
    size_t m_islandCount = 0;
    size_t m_islandsWithoutOutput = 0;

    bool remeshIslands();
    void throwIfCancelled() const;

    static double calculateAverageEdgeLength(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& faces);
    void initializeVoxelSize();
    // `topology` is that of vertices and triangles; decimation must keep it.
    static bool decimateIfTooDense(std::vector<Vector3>& vertices,
        std::vector<std::vector<size_t>>& triangles,
        const SurfaceTopology& topology,
        double voxelSize,
        double sharpEdgeDegrees,
        size_t islandIndex,
        DecimationStats* stats);
    static void resample(std::vector<Vector3>& vertices,
        std::vector<std::vector<size_t>>& triangles,
        const SurfaceTopology& topology,
        double voxelSize,
        double adaptivity,
        double sharpEdgeDegrees,
        double smoothNormalDegrees,
        size_t islandIndex,
        DecimationStats* decimationStats,
        std::atomic<long long>* adaptiveFieldTimeUs,
        const ProgressHandler* progressHandler,
        std::vector<Vector3>* decimatedVerticesOut,
        std::vector<std::vector<size_t>>* decimatedTrianglesOut);
    static double calculateMeshArea(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles);
};

}

#endif
