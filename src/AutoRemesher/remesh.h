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
#ifndef AUTO_REMESHER_REMESH_H
#define AUTO_REMESHER_REMESH_H
#include <AutoRemesher/Diagnostics>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// The stable interface between the remeshing core and its hosts (the Qt app,
// the Blender add-on's worker process, benchmarks and tests).  Everything in
// this header is versioned: adding an optional field is a minor version,
// anything that breaks a caller is a major version.  The engine class in
// <AutoRemesher/AutoRemesher> and the stage classes are not part of it.

namespace AutoRemesher {

const int InterfaceVersionMajor = 1;
const int InterfaceVersionMinor = 7;

// Since 1.3.  Peak memory grows linearly with the target, by about this much
// per quad at Quality resolution (21 KB measured on spot at 25k-100k quads;
// Fast needs about 9 KB).  Hosts use it to warn before a large run.
constexpr double ApproximatePeakBytesPerQuad = 22e3;
// Since 1.3.  Targets above this are rejected as InvalidInput: they would
// need about 44 GB and hours of run time (100k quads took 2.4 minutes, and
// time grows faster than the count).  One is usually a typo, an extra zero.
constexpr uint32_t MaximumTargetQuadCount = 2000000;

// How fine the triangle mesh is that the cross field and the
// parametrization are solved on.  The quad count is the same either way.
// Quality gives cleaner quads and takes about 1.5 times as long as Fast.
enum class MeshResolution {
    Fast,
    Quality
};

// Which code turns the integer-grid map into faces.  Qex: libQEx (Ebke et al.
// 2013) with the fork's guards; an island it can't take falls back to Trace,
// our own graph tracing (TODO 6.1b).  Trace alone is for comparisons.
enum class Extractor {
    Qex,
    Trace
};

// Defaults are those of the command line.
struct RemeshParameters {
    uint32_t targetQuadCount = 50000;
    // Curvature-adaptive density, 0 = uniform quads.  The quad edge goes as
    // curvature^-adaptivity, so the default 0.5 is the chord-error law.  1 is
    // twice as steep; on the corpus it has about twice the UV flips and 1.4
    // times the non-quads at the same mean distance to the input (REFERENCE
    // M7, TODO 4.1).
    double adaptivity = 0.5;
    // Curvature-adaptive elongation, 0 = square quads.
    double anisotropy = 1.0;
    // Multiplies the quad edge length; <= 0 leaves the core default.
    double edgeScaling = 1.0;
    // Dihedral angle above which an edge counts as sharp.
    double sharpEdgeDegrees = 90.0;
    // > 0 projects onto a PN-triangle smoothed input instead of the facets.
    double smoothNormalDegrees = 0.0;
    // Since 1.2.
    MeshResolution resolution = MeshResolution::Quality;
    // Since 1.5.  Untangle folded-over input triangles (brush leftovers)
    // before remeshing; moves input vertices only, along the input surface.
    // Off: the folds are only counted (diagnostics input_fold_*).
    bool repairFolds = false;
    // Since 1.6.
    Extractor extractor = Extractor::Qex;
    // Since 1.7.  With repairFolds: where a fold cannot be untangled, also
    // lower the tips of needle spikes near it (a shape change, so off by
    // default; diagnostics input_fold_spikes and input_fold_spike_drop_quads).
    bool flattenSpikes = false;
};

struct RemeshRequest {
    // x0 y0 z0 x1 y1 z1 ..., in any units; the result is in the same space.
    std::vector<double> positions;
    // a0 b0 c0 a1 b1 c1 ..., indices into positions.  Triangles only.
    std::vector<uint32_t> triangles;
    RemeshParameters parameters;
    // Planned optional inputs, each empty by default so that leaving it out
    // keeps today's behaviour: vertexDensity, featureEdges, faceGroups,
    // guideCurves, symmetry (see INTERFACE.md in the project docs).
};

// Lets another thread stop a running remesh().  The core checks it at every
// progress point; remesh() then returns RemeshStatus::Cancelled without a mesh.
class CancelToken {
public:
    void requestCancel() noexcept
    {
        m_requested.store(true, std::memory_order_relaxed);
    }

    bool cancelRequested() const noexcept
    {
        return m_requested.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> m_requested { false };
};

// `fraction` is how far the whole run has got, 0..1 and never decreasing;
// `step` names the step that is starting and is only valid during the call.
// Called from the calling thread or from worker threads, but never from two
// at once.  It should return quickly and must not call back into the core.
typedef std::function<void(float fraction, const char* step)> RemeshProgressHandler;

struct RemeshCallbacks {
    RemeshProgressHandler progress;
    const CancelToken* cancel = nullptr;
};

enum class RemeshStatus {
    Ok, // mesh present
    InvalidInput, // rejected before any work; message says why
    Cancelled, // stopped through the CancelToken; no mesh
    Failed // ran to the end but produced no faces
};

struct RemeshMesh {
    // x0 y0 z0 x1 y1 z1 ...
    std::vector<double> positions;
    // Corner count of each face, mostly 4.
    std::vector<uint32_t> faceSizes;
    // The corners of all faces, one face after the other.
    std::vector<uint32_t> faceIndices;
};

// Measured on the result mesh.  Field definitions are in INTERFACE.md §3.1.
struct QualityReport {
    size_t inputVertexCount = 0;
    size_t inputTriangleCount = 0;
    size_t vertexCount = 0;
    size_t faceCount = 0;
    size_t quadCount = 0;
    size_t nonQuadCount = 0;
    size_t nonManifoldEdgeCount = 0;
    size_t boundaryEdgeCount = 0;
    size_t orientationFlipCount = 0;
    size_t foldedFaceCount = 0;
    size_t degenerateFaceCount = 0;
    size_t componentCount = 0;
    // Since 1.4.  Connected pieces of the boundary-edge graph (one per hole
    // or open border; two loops touching at a vertex count once), and
    // vertices whose faces do not form one fan joined by shared edges.
    size_t boundaryLoopCount = 0;
    size_t nonManifoldVertexCount = 0;
    size_t islandCount = 0;
    size_t islandsWithoutOutput = 0;
    // Since 1.4.  Input islands that are closed (no boundary or non-manifold
    // edge); those of them whose output has a boundary edge, a non-manifold
    // edge or a non-manifold vertex; and the boundary loops and edges in the
    // output of those islands, i.e. holes the input did not have.  Measured
    // by measureMesh() on each island's output.
    size_t closedInputIslandCount = 0;
    size_t openedClosedIslandCount = 0;
    size_t closedIslandHoleCount = 0;
    size_t closedIslandHoleEdgeCount = 0;
    // Since 1.7.  Handles (tunnels) the output has fewer or more than the
    // input, summed over the islands whose input has no non-manifold edge,
    // and the islands where the count changed.  Per island the count is
    // 2C - chi - B over its pieces, so a hole cut or filled does not count; a
    // handle lost and another one added on the same island cancel out.
    size_t handlesLost = 0;
    size_t handlesAdded = 0;
    size_t handlesChangedIslandCount = 0;
    // Wall time of the whole call.
    double seconds = 0.0;
    // Per-step times summed over the islands, in pipeline order.
    std::vector<std::pair<std::string, double>> stepSeconds;
    std::vector<std::string> warnings;
    // Numeric counters added by later versions (solver, rounding, cleanup);
    // readers must accept keys they don't know.  Since 1.1: whole-run totals
    // and maxima of `RemeshResult::stageDiagnostics` (summarizeDiagnostics()).
    std::vector<std::pair<std::string, double>> diagnostics;
};

struct RemeshResult {
    RemeshStatus status = RemeshStatus::Failed;
    std::string message;
    RemeshParameters parameters;
    RemeshMesh mesh;
    QualityReport report;
    // Since 1.1: the per-stage records of the run, also after a failed run
    // (empty when the core never started).  diagnosticsToJsonLines() writes
    // them as JSON lines, schema "autoremesher.stage-diagnostics".
    std::vector<DiagnosticRecord> stageDiagnostics;
};

RemeshResult remesh(const RemeshRequest& request,
    const RemeshCallbacks& callbacks = RemeshCallbacks());

// Status, message, parameters and report as one JSON object (everything but
// the mesh arrays), schema "autoremesher.remesh-result".
std::string toJson(const RemeshResult& result);

const char* toString(RemeshStatus status);

// "fast" and "quality", as in the JSON report and on the command line.
const char* toString(MeshResolution resolution);
bool parseMeshResolution(const std::string& text, MeshResolution* resolution);

// "qex" and "trace".
const char* toString(Extractor extractor);
bool parseExtractor(const std::string& text, Extractor* extractor);

}

#endif
