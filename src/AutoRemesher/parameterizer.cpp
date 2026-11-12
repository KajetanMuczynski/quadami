/*
 *  Copyright (c) 2026 Jeremy HU <jeremy-at-dust3d dot org>. All rights reserved.
 *  Modifications Copyright (C) 2026 Quadami contributors, GPL-3.0-or-later (see LICENSE).
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */
#include <AutoRemesher/FrameField>
#include <AutoRemesher/MapCheck>
#include <AutoRemesher/Parameterizer>
#include <AutoRemesher/QuadParameterizer>
#include <AutoRemesher/SingularitySimplifier>
#include <AutoRemesher/SurfaceMesh>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <tbb/blocked_range.h>
#include <tbb/combinable.h>
#include <tbb/parallel_for.h>

namespace AutoRemesher {

namespace {

    inline double quadraticForm(const double* T, const Vector3& t)
    {
        return T[0] * t.x() * t.x() + T[2] * t.y() * t.y() + T[5] * t.z() * t.z()
            + 2.0 * (T[1] * t.x() * t.y() + T[3] * t.x() * t.z() + T[4] * t.y() * t.z());
    }

    void computeFaceAnisotropyField(const SurfaceMesh& mesh,
        const std::vector<Vector3>& field, double anisotropy, double maxAspectRatio,
        std::vector<double>* scalingU, std::vector<double>* scalingV)
    {
        scalingU->assign(mesh.faceCount(), 1.0);
        scalingV->assign(mesh.faceCount(), 1.0);
        if (anisotropy <= 0.0 || maxAspectRatio <= 1.0 || field.size() != mesh.faceCount())
            return;
        std::vector<double> tensor(mesh.vertexCount() * 6, 0.0);
        std::vector<std::vector<size_t>> neighbors(mesh.vertexCount());
        for (size_t c = 0; c < mesh.cornerCount(); ++c) {
            const size_t mate = mesh.oppositeCorner(c);
            if (mate == SurfaceMesh::npos || mate < c)
                continue;
            const Vector3 edge = mesh.edgeVector(c);
            const double length = edge.length();
            if (length <= 0.0)
                continue;
            const Vector3 d = edge / length;
            const double weight = length * mesh.normalAngle(c);
            const double contribution[6] = { weight * d.x() * d.x(), weight * d.x() * d.y(), weight * d.y() * d.y(), weight * d.x() * d.z(), weight * d.y() * d.z(), weight * d.z() * d.z() };
            const size_t a = mesh.cornerVertex(c), b = mesh.cornerVertex(mesh.nextCorner(c));
            for (size_t i = 0; i < 6; ++i) {
                tensor[6 * a + i] += contribution[i];
                tensor[6 * b + i] += contribution[i];
            }
        }
        for (size_t c = 0; c < mesh.cornerCount(); ++c) {
            const size_t a = mesh.cornerVertex(c), b = mesh.cornerVertex(mesh.nextCorner(c));
            neighbors[a].push_back(b);
            neighbors[b].push_back(a);
        }
        std::vector<double> smoothed(tensor.size());
        for (size_t pass = 0; pass < 12; ++pass) {
            for (size_t v = 0; v < mesh.vertexCount(); ++v) {
                if (neighbors[v].empty()) {
                    for (size_t i = 0; i < 6; ++i)
                        smoothed[6 * v + i] = tensor[6 * v + i];
                    continue;
                }
                for (size_t i = 0; i < 6; ++i) {
                    double average = 0;
                    for (size_t n : neighbors[v])
                        average += tensor[6 * n + i];
                    smoothed[6 * v + i] = .5 * tensor[6 * v + i] + .5 * average / neighbors[v].size();
                }
            }
            tensor.swap(smoothed);
        }
        std::vector<double> alongU(mesh.faceCount()), alongV(mesh.faceCount());
        double total = 0;
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            double T[6] = {};
            for (size_t l = 0; l < 3; ++l) {
                const size_t v = mesh.cornerVertex(3 * f + l);
                for (size_t i = 0; i < 6; ++i)
                    T[i] += tensor[6 * v + i];
            }
            const Vector3 n = mesh.faceNormal(f), b = field[f].normalized(), bt = Vector3::crossProduct(n, b);
            alongU[f] = std::fabs(quadraticForm(T, bt));
            alongV[f] = std::fabs(quadraticForm(T, b));
            total += alongU[f] + alongV[f];
        }
        if (total <= 0)
            return;
        const double floor = .001 * total / (2.0 * mesh.faceCount()), maxRho = std::sqrt(maxAspectRatio);
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            double rho = std::pow((alongV[f] + floor) / (alongU[f] + floor), .25);
            rho = std::pow(rho, anisotropy);
            rho = std::max(1.0 / maxRho, std::min(maxRho, rho));
            (*scalingU)[f] = rho;
            (*scalingV)[f] = 1.0 / rho;
        }
    }

}

std::vector<double> Parameterizer::computeFaceScalingField(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    const std::vector<double>& faceAreas,
    const std::vector<Vector3>& vertexNormals,
    const std::vector<std::vector<size_t>>& faceAroundVertexMap) const
{
    std::vector<double> faceScaling(triangles.size(), 1.0);
    if (m_adaptivity <= 0.0 || vertices.empty())
        return faceScaling;

    std::vector<double> vertexCurvature(vertices.size(), 0.0);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, vertices.size()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t v = range.begin(); v != range.end(); ++v) {
                const auto& facesAroundVertex = faceAroundVertexMap[v];
                if (facesAroundVertex.empty())
                    continue;
                const auto& normalV = vertexNormals[v];
                double maxCurvature = 0.0;
                for (const auto& faceIndex : facesAroundVertex) {
                    for (const auto& u : triangles[faceIndex]) {
                        if (u == v)
                            continue;
                        double distance = (vertices[u] - vertices[v]).length();
                        if (distance <= 0.0)
                            continue;
                        double cosAngle = Vector3::dotProduct(normalV, vertexNormals[u]);
                        if (cosAngle > 1.0)
                            cosAngle = 1.0;
                        else if (cosAngle < -1.0)
                            cosAngle = -1.0;
                        double curvature = std::acos(cosAngle) / distance;
                        if (curvature > maxCurvature)
                            maxCurvature = curvature;
                    }
                }
                vertexCurvature[v] = maxCurvature;
            }
        });

    double sumCurvature = 0.0;
    for (const auto& it : vertexCurvature)
        sumCurvature += it;
    double averageCurvature = sumCurvature / vertexCurvature.size();
    if (averageCurvature <= 0.0)
        return faceScaling;

    const double minRatio = 0.3;
    const double maxRatio = 3.0;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, triangles.size()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i) {
                const auto& triangle = triangles[i];
                double faceCurvature = 0.0;
                for (const auto& v : triangle)
                    faceCurvature += vertexCurvature[v];
                faceCurvature /= triangle.size();
                double normalized = faceCurvature / averageCurvature;
                if (normalized < 1e-3)
                    normalized = 1e-3;
                double multiplier = std::pow(normalized, -m_adaptivity);
                if (multiplier < minRatio)
                    multiplier = minRatio;
                else if (multiplier > maxRatio)
                    multiplier = maxRatio;
                faceScaling[i] = multiplier;
            }
        });

    // The field only says where quads are smaller or larger than average;
    // the base quad edge alone sets how many there are. A face of area A with
    // edge factor m holds A / m^2 base quads, so rescale m until
    // sum(A / m^2) = sum(A). Without this the count depended on
    // averageCurvature, a per-vertex mean on a mesh whose vertices the
    // adaptive remesh concentrates in curved regions: on the baseline corpus
    // it was 1.2-12 times the area mean, and the field asked for 0.27-0.85 of
    // the base count.
    double area = 0.0;
    double baseQuads = 0.0;
    for (size_t i = 0; i < triangles.size(); ++i) {
        area += faceAreas[i];
        baseQuads += faceAreas[i] / (faceScaling[i] * faceScaling[i]);
    }
    if (area > 0.0 && baseQuads > 0.0) {
        const double normalization = std::sqrt(baseQuads / area);
        for (auto& it : faceScaling)
            it *= normalization;
    }

    return faceScaling;
}

bool Parameterizer::parameterize()
{
    m_diagnostics.clear();
#if AUTO_REMESHER_DEV
    {
        FILE* fp = fopen("debug-input-for-parameterization.obj", "wb");
        for (size_t i = 0; i < m_vertices->size(); ++i) {
            const auto& vertex = (*m_vertices)[i];
            fprintf(fp, "v %f %f %f\n", vertex[0], vertex[1], vertex[2]);
        }
        for (size_t i = 0; i < m_triangles->size(); ++i) {
            const auto& indices = (*m_triangles)[i];
            fprintf(fp, "f %zu %zu %zu\n", indices[0] + 1, indices[1] + 1, indices[2] + 1);
        }
        fclose(fp);
    }
#endif

    // The fractions below are the measured share of parameterization each step
    // costs; "Solving quad cover" dominates, so the bar has to keep moving
    // through it rather than sitting still until it finishes.
    const auto report = [this](float fraction, const char* name) {
        if (m_progressHandler)
            m_progressHandler(fraction, name);
    };

    report(0.0f, "Computing vertex normals");
    std::vector<Vector3> vertexNormals(m_vertices->size());
    std::vector<double> faceAreas(m_triangles->size());
    {
        std::vector<Vector3> faceNormals(m_triangles->size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_triangles->size()),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i != range.end(); ++i) {
                    const auto& it = (*m_triangles)[i];
                    faceNormals[i] = Vector3::normal(
                        (*m_vertices)[it[0]], (*m_vertices)[it[1]], (*m_vertices)[it[2]]);
                    faceAreas[i] = Vector3::area(
                        (*m_vertices)[it[0]], (*m_vertices)[it[1]], (*m_vertices)[it[2]]);
                }
            });
        for (size_t i = 0; i < m_triangles->size(); ++i) {
            const auto& it = (*m_triangles)[i];
            vertexNormals[it[0]] += faceNormals[i];
            vertexNormals[it[1]] += faceNormals[i];
            vertexNormals[it[2]] += faceNormals[i];
        }
        tbb::parallel_for(tbb::blocked_range<size_t>(0, vertexNormals.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i != range.end(); ++i)
                    vertexNormals[i].normalize();
            });
    }

    report(0.01f, "Computing scaling field");
    std::vector<std::vector<size_t>> faceAroundVertexMap(m_vertices->size());
    for (size_t i = 0; i < m_triangles->size(); ++i) {
        const auto& it = (*m_triangles)[i];
        faceAroundVertexMap[it[0]].push_back(i);
        faceAroundVertexMap[it[1]].push_back(i);
        faceAroundVertexMap[it[2]].push_back(i);
    }

    std::vector<double> faceScalingField = computeFaceScalingField(*m_vertices,
        *m_triangles, faceAreas, vertexNormals, faceAroundVertexMap);

    report(0.02f, "Building surface topology");
    // The parameterization pipeline uses the triangle/corner mesh;
    // no attribute-backed interchange mesh is constructed.
    SurfaceMesh topology(*m_vertices, *m_triangles);
    if (topology.faceCount() != m_triangles->size()) {
        std::cerr << "Topology rejected a non-triangle face" << std::endl;
        return false;
    }

    // The remesh kept each conical tip as one of its vertices.
    std::vector<size_t> tipVertices;
    if (nullptr != m_conicalTips) {
        const double tolerance = 1e-6 * topology.averageEdgeLength();
        for (const Vector3& tip : *m_conicalTips)
            for (size_t v = 0; v < m_vertices->size(); ++v)
                if (((*m_vertices)[v] - tip).length() <= tolerance) {
                    tipVertices.push_back(v);
                    break;
                }
    }

    // One set of sharp edges for the field, the simplifier and the cover.
    FeatureEdgeSet classifiedEdges;
    const FeatureEdgeSet* featureEdges = m_featureEdges;
    if (nullptr == featureEdges) {
        classifiedEdges = classifySharpEdges(topology, m_sharpEdgeDegrees);
        featureEdges = &classifiedEdges;
    }

    report(0.03f, "Solving frame field");
    // Topology, field, and quad cover form the complete active path.
    std::vector<Vector3> field;
    if (nullptr != m_triangleFieldVectors) {
        field = *m_triangleFieldVectors;
    } else if (!FrameField::create(topology, *featureEdges,
                   &field, &m_diagnostics, &tipVertices)) {
        std::cerr << "Frame field solve failed" << std::endl;
        return false;
    }
    if (field.size() != topology.faceCount()) {
        std::cerr << "Frame field has the wrong face count" << std::endl;
        return false;
    }

    SingularitySimplifier simplifier(topology, &field);
    if (m_singularitySimplification) {
        report(0.17f, "Simplifying singularities");
        simplifier.setFeatureEdges(featureEdges);
        simplifier.setMaximumPairDistance(m_maximumSingularityPairDistance);
        simplifier.simplify();
        {
            DiagnosticRecord record("singularity_simplifier");
            record.set("singularities_before", (double)simplifier.singularityCountBefore());
            record.set("singularities_after", (double)simplifier.singularityCountAfter());
            record.set("cancelled_pairs", (double)simplifier.cancelledPairCount());
            m_diagnostics.push_back(std::move(record));
        }
    }

    std::vector<double> faceScalingU(m_triangles->size(), 1.0);
    std::vector<double> faceScalingV(m_triangles->size(), 1.0);
    if (m_anisotropy > 0.0) {
        report(0.26f, "Computing anisotropy field");
        computeFaceAnisotropyField(topology, field,
            m_anisotropy, m_maxAspectRatio, &faceScalingU, &faceScalingV);
    }
    // The cover solve is the longest single step here, so it reports its own
    // sub-steps from 0.28 onwards rather than going quiet until it finishes.
    ProgressHandler coverProgress;
    if (m_progressHandler) {
        coverProgress = [this](float fraction, const char* name) {
            m_progressHandler(0.28f + (0.99f - 0.28f) * fraction, name);
        };
    }
    // Base quad edge, where the face scaling is 1: the island's share of the
    // requested quads spread over its area (the scaling field keeps
    // sum(area / m^2) = area), or the mean triangle edge without a target.
    double baseEdgeLength = topology.averageEdgeLength();
    if (m_targetQuadCount > 0.0) {
        double area = 0.0;
        for (const double faceArea : faceAreas)
            area += faceArea;
        if (area > 0.0)
            baseEdgeLength = std::sqrt(area / m_targetQuadCount);
    }
    QuadParameterizer::Result cover;
    const bool coverSolved = QuadParameterizer::parameterize(*m_vertices, *m_triangles,
        &field, m_scaling * baseEdgeLength, *featureEdges, &cover,
        &faceScalingField, &faceScalingU, &faceScalingV,
        coverProgress ? &coverProgress : nullptr, &topology);
    for (auto& record : cover.diagnostics)
        m_diagnostics.push_back(std::move(record));
    if (!coverSolved) {
        std::cerr << "Quad cover solve failed" << std::endl;
        return false;
    }
    {
        // The map extraction gets, checked against the validity conditions of
        // the papers; only recorded.
        DiagnosticRecord record("map_check");
        appendMapCheck(&record, checkIntegerGridMap(*m_vertices, *m_triangles, cover.triangleUvs, cover.cornerRotations, cover.singularVertices));
        m_diagnostics.push_back(std::move(record));
    }
    report(0.99f, "Collecting singularities");
    m_originalTriangleUvs = cover.triangleUvs;
    m_triangleUvs = std::make_unique<std::vector<std::vector<Vector2>>>(cover.triangleUvs);
    m_singularVertexPositions.clear();
    m_singularVertexIndices.clear();
    for (const size_t v : cover.singularVertices) {
        if (v >= m_vertices->size())
            continue;
        m_singularVertexPositions.push_back((*m_vertices)[v]);
        m_singularVertexIndices.push_back(v);
    }
    if (simplifier.cancelledPairCount() > 0) {
        std::cerr << "Simplified cross field singularities: "
                  << simplifier.singularityCountBefore() << " -> "
                  << simplifier.singularityCountAfter() << " ("
                  << simplifier.cancelledPairCount() << " pair(s) cancelled)"
                  << std::endl;
    }
    report(1.0f, "");
    return true;
}

}
