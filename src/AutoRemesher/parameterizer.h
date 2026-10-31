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
#ifndef AUTO_REMESHER_PARAMETERIZER_H
#define AUTO_REMESHER_PARAMETERIZER_H
#include <AutoRemesher/Diagnostics>
#include <AutoRemesher/FeatureEdges>
#include <AutoRemesher/Progress>
#include <AutoRemesher/Vector2>
#include <AutoRemesher/Vector3>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace AutoRemesher {

class Parameterizer {
public:
    Parameterizer(const std::vector<Vector3>* vertices,
        const std::vector<std::vector<size_t>>* triangles,
        const std::vector<Vector3>* triangleFieldVectors)
        : m_vertices(vertices)
        , m_triangles(triangles)
        , m_triangleFieldVectors(triangleFieldVectors)
    {
    }

    std::unique_ptr<std::vector<std::vector<Vector2>>> takeTriangleUvs()
    {
        return std::move(m_triangleUvs);
    }

    const std::vector<std::vector<Vector2>>& originalTriangleUvs() const
    {
        return m_originalTriangleUvs;
    }

    const std::vector<Vector3>& singularVertexPositions() const
    {
        return m_singularVertexPositions;
    }

    const std::vector<size_t>& singularVertexIndices() const
    {
        return m_singularVertexIndices;
    }

    // Multiplies the quad edge length; the quad count goes with 1 / scaling^2.
    void setScaling(double scaling)
    {
        m_scaling = scaling;
    }

    // Quads requested for this mesh (at scaling 1). The quads are sized for
    // this count whatever the adaptivity; without it (<= 0) they are about as
    // large as the mean triangle edge of the input.
    void setTargetQuadCount(double count)
    {
        m_targetQuadCount = count;
    }

    void setGradientAdaptivity(double adaptivity)
    {
        m_adaptivity = adaptivity;
    }

    // Without setFeatureEdges(), the sharp edges are classified on the
    // triangles given, at this angle.
    void setSharpEdgeDegrees(double degrees)
    {
        m_sharpEdgeDegrees = degrees;
    }

    // The sharp edges of the triangles given (the input's, carried through
    // the remesh): the field locks to them, the singularity simplifier
    // keeps cones from crossing them and the cover puts isolines on them.
    // Must outlive parameterize().
    void setFeatureEdges(const FeatureEdgeSet* featureEdges)
    {
        m_featureEdges = featureEdges;
    }

    void setAnisotropy(double anisotropy)
    {
        m_anisotropy = anisotropy;
    }

    void setSingularitySimplification(bool simplify)
    {
        m_singularitySimplification = simplify;
    }

    void setMaximumSingularityPairDistance(size_t faceHops)
    {
        m_maximumSingularityPairDistance = faceHops;
    }

    void setProgressHandler(ProgressHandler progressHandler)
    {
        m_progressHandler = std::move(progressHandler);
    }

    bool parameterize();

    // The quad edge length the cover asks of each triangle, in input units:
    // scaling times the base edge times the scaling field (with anisotropy
    // the geometric mean of the two directions).  The same sizing as
    // parameterize() uses, without the field and the cover (the plan,
    // RemeshPlan).
    std::vector<double> plannedFaceEdgeLengths() const;

    // Records of the field, simplification and cover solves of the last
    // parameterize(), also after a failure.  Their island is left unset.
    std::vector<DiagnosticRecord> takeDiagnostics()
    {
        return std::move(m_diagnostics);
    }

private:
    const std::vector<Vector3>* m_vertices = nullptr;
    const std::vector<std::vector<size_t>>* m_triangles = nullptr;
    const std::vector<Vector3>* m_triangleFieldVectors = nullptr;
    std::unique_ptr<std::vector<std::vector<Vector2>>> m_triangleUvs;
    std::vector<Vector3> m_singularVertexPositions;
    std::vector<size_t> m_singularVertexIndices;
    std::vector<std::vector<Vector2>> m_originalTriangleUvs;
    double m_scaling = 1.0;
    double m_targetQuadCount = 0.0;
    double m_adaptivity = 0.5;
    double m_sharpEdgeDegrees = 90.0;
    const FeatureEdgeSet* m_featureEdges = nullptr;
    double m_anisotropy = 1.0;
    double m_maxAspectRatio = 2.3;
    bool m_singularitySimplification = true;
    size_t m_maximumSingularityPairDistance = 6;
    ProgressHandler m_progressHandler;
    std::vector<DiagnosticRecord> m_diagnostics;

    // Face areas, vertex normals and the scaling field of the triangles.
    std::vector<double> computeSizing(std::vector<double>* faceAreas,
        std::vector<Vector3>* vertexNormals) const;
    // Where the face scaling is 1: the island's share of the requested quads
    // spread over its area, or `fallback` without a target.
    double baseEdgeLength(const std::vector<double>& faceAreas, double fallback) const;
    std::vector<double> computeFaceScalingField(const std::vector<Vector3>& vertices,
        const std::vector<std::vector<size_t>>& triangles,
        const std::vector<double>& faceAreas,
        const std::vector<Vector3>& vertexNormals,
        const std::vector<std::vector<size_t>>& faceAroundVertexMap) const;
};

}

#endif
