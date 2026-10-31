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
#include <AutoRemesher/ConstrainedLeastSquares>
#include <AutoRemesher/FrameField>
#include <AutoRemesher/MixedIntegerLeastSquares>
#include <AutoRemesher/Predicates>
#include <AutoRemesher/QuadParameterizer>
#include <AutoRemesher/SurfaceMesh>
#include <AutoRemesher/Untangler>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>
#include <limits>
#include <memory>
#include <queue>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace AutoRemesher {
namespace {
    Vector3 unit(const Vector3& v, const Vector3& fallback)
    {
        return v.length() < 1e-12 ? fallback.normalized() : v.normalized();
    }

    int edgeQuarterTurn(const SurfaceMesh& mesh, size_t corner,
        const std::vector<Vector3>& field, const std::vector<Vector3>& normals)
    {
        const size_t opposite = mesh.oppositeCorner(corner);
        if (opposite == SurfaceMesh::npos)
            return 0;
        const size_t f = mesh.cornerFace(corner), g = mesh.cornerFace(opposite);
        if (f > g) {
            const int r = edgeQuarterTurn(mesh, opposite, field, normals);
            return (4 - r) % 4;
        }
        size_t v0 = mesh.cornerVertex(corner), v1 = mesh.cornerVertex(mesh.nextCorner(corner));
        if (v1 < v0)
            std::swap(v0, v1);
        const Vector3 e = unit(mesh.position(v1) - mesh.position(v0), Vector3(1, 0, 0));
        const Vector3 y0 = unit(Vector3::crossProduct(normals[f], e), Vector3(0, 1, 0));
        const Vector3 yg = unit(Vector3::crossProduct(normals[g], e), Vector3(0, 1, 0));
        const double a0 = std::atan2(Vector3::dotProduct(field[f], y0), Vector3::dotProduct(field[f], e));
        int best = 0;
        double bestError = 1e100;
        Vector3 candidate = field[g];
        for (int r = 0; r < 4; ++r) {
            const double ag = std::atan2(Vector3::dotProduct(candidate, yg), Vector3::dotProduct(candidate, e));
            double d = std::fabs(a0 - ag);
            while (d > M_PI)
                d = std::fabs(d - 2.0 * M_PI);
            if (d < bestError) {
                bestError = d;
                best = r;
            }
            candidate = Vector3::crossProduct(normals[g], candidate);
        }
        return best;
    }

    enum EdgeConstraint { ConstraintNone = 0,
        ConstraintU = 1,
        ConstraintV = 2 };
    int edgeConstraint(const SurfaceMesh& mesh, size_t c, const std::vector<Vector3>& field,
        const std::vector<Vector3>& normals, const std::vector<char>& hardCorners)
    {
        if (mesh.oppositeCorner(c) != SurfaceMesh::npos && !hardCorners[c])
            return ConstraintNone;
        const size_t f = mesh.cornerFace(c);
        const Vector3 edge = unit(mesh.edgeVector(c), Vector3(1, 0, 0));
        const Vector3 b = unit(field[f], edge);
        const Vector3 br = unit(Vector3::crossProduct(normals[f], b), Vector3(0, 1, 0));
        const bool alongB = std::acos(std::max(-1.0, std::min(1.0, std::fabs(Vector3::dotProduct(edge, b))))) < 10.0 * M_PI / 180.0;
        const bool alongBr = std::acos(std::max(-1.0, std::min(1.0, std::fabs(Vector3::dotProduct(edge, br))))) < 10.0 * M_PI / 180.0;
        if (alongB == alongBr)
            return ConstraintNone;
        return alongB ? ConstraintV : ConstraintU;
    }

    struct CoverContext {
        const SurfaceMesh& mesh;
        const std::vector<Vector3>& field;
        const std::vector<Vector3>& normals;
        const std::vector<int>& rotation;
        const std::vector<char>& seam;
        const std::vector<signed char>& cornerConstraints;
        const std::vector<double>& scalingU;
        const std::vector<double>& scalingV;
        const std::vector<double>* faceScaling;
        double scale;
    };

    void initializeFieldAndNormals(const SurfaceMesh& mesh, const std::vector<Vector3>* guidance,
        std::vector<Vector3>* normals, std::vector<Vector3>* field)
    {
        const bool hasGuidance = guidance && guidance->size() == mesh.faceCount();
        normals->assign(mesh.faceCount(), Vector3());
        field->resize(mesh.faceCount());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, mesh.faceCount()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t faceIndex = range.begin(); faceIndex != range.end(); ++faceIndex) {
                (*normals)[faceIndex] = unit(mesh.faceNormal(faceIndex), Vector3(0, 0, 1));
                Vector3 tangentAxis(1, 0, 0);
                if (std::fabs(Vector3::dotProduct(tangentAxis, (*normals)[faceIndex])) > .8)
                    tangentAxis = Vector3(0, 1, 0);
                Vector3 fieldDirection = hasGuidance ? (*guidance)[faceIndex] : tangentAxis;
                if (!hasGuidance)
                    fieldDirection = fieldDirection - (*normals)[faceIndex] * Vector3::dotProduct(fieldDirection, (*normals)[faceIndex]);
                (*field)[faceIndex] = unit(fieldDirection, mesh.edgeVector(3 * faceIndex));
            }
        });
    }

    void smoothCrossField(const SurfaceMesh& mesh, const std::vector<Vector3>& normals,
        const std::vector<char>& hardCorners, std::vector<Vector3>* field)
    {
        std::vector<double> alpha(2 * mesh.faceCount(), 0.0);
        std::vector<char> locked(mesh.faceCount(), 0);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, mesh.faceCount()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t faceIndex = range.begin(); faceIndex != range.end(); ++faceIndex) {
                alpha[2 * faceIndex] = 1.0;
                for (size_t localCorner = 0; localCorner < 3; ++localCorner) {
                    const size_t cornerIndex = 3 * faceIndex + localCorner, oppositeCornerIndex = mesh.oppositeCorner(cornerIndex);
                    if (oppositeCornerIndex != SurfaceMesh::npos && !hardCorners[cornerIndex])
                        continue;
                    const Vector3 edge = unit(mesh.edgeVector(cornerIndex), Vector3(1, 0, 0));
                    const Vector3 fieldDirection = (*field)[faceIndex];
                    const Vector3 perpendicular = unit(Vector3::crossProduct(normals[faceIndex], fieldDirection), Vector3(0, 1, 0));
                    const double fieldAngle = std::atan2(Vector3::dotProduct(edge, perpendicular), Vector3::dotProduct(edge, fieldDirection));
                    alpha[2 * faceIndex] = std::cos(4.0 * fieldAngle);
                    alpha[2 * faceIndex + 1] = std::sin(4.0 * fieldAngle);
                    locked[faceIndex] = 1;
                }
            }
        });
        for (int iteration = 0; iteration < 40; ++iteration) {
            std::vector<double> next = alpha;
            tbb::parallel_for(tbb::blocked_range<size_t>(0, mesh.faceCount()), [&](const tbb::blocked_range<size_t>& range) {
                for (size_t f = range.begin(); f != range.end(); ++f)
                    if (!locked[f]) {
                        double x = alpha[2 * f], y = alpha[2 * f + 1];
                        const Vector3 bf = (*field)[f];
                        const Vector3 btf = unit(Vector3::crossProduct(normals[f], bf), Vector3(0, 1, 0));
                        for (size_t l = 0; l < 3; ++l) {
                            const size_t oc = mesh.oppositeCorner(3 * f + l);
                            if (oc == SurfaceMesh::npos)
                                continue;
                            const size_t g = mesh.cornerFace(oc);
                            Vector3 bg = (*field)[g];
                            bg = unit(bg - normals[f] * Vector3::dotProduct(bg, normals[f]), bf);
                            const double d = std::atan2(Vector3::dotProduct(bg, btf), Vector3::dotProduct(bg, bf));
                            const double cs = std::cos(4.0 * d), sn = std::sin(4.0 * d);
                            x += cs * alpha[2 * g] - sn * alpha[2 * g + 1];
                            y += sn * alpha[2 * g] + cs * alpha[2 * g + 1];
                        }
                        const double length = std::hypot(x, y);
                        if (length > 1e-12) {
                            next[2 * f] = x / length;
                            next[2 * f + 1] = y / length;
                        }
                    }
            });
            alpha.swap(next);
        }
        tbb::parallel_for(tbb::blocked_range<size_t>(0, mesh.faceCount()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t faceIndex = range.begin(); faceIndex != range.end(); ++faceIndex) {
                const double fieldAngle = .25 * std::atan2(alpha[2 * faceIndex + 1], alpha[2 * faceIndex]);
                const Vector3 fieldDirection = (*field)[faceIndex];
                const Vector3 perpendicular = unit(Vector3::crossProduct(normals[faceIndex], fieldDirection), Vector3(0, 1, 0));
                (*field)[faceIndex] = unit(fieldDirection * std::cos(fieldAngle) + perpendicular * std::sin(fieldAngle), fieldDirection);
            }
        });
    }

    void brushFieldAlongSpanningTree(const SurfaceMesh& mesh, const std::vector<Vector3>& normals,
        std::vector<Vector3>* field)
    {
        std::vector<char> seen(mesh.faceCount(), 0);
        if (mesh.faceCount() != 0) {
            std::queue<size_t> q;
            q.push(0);
            seen[0] = 1;
            while (!q.empty()) {
                size_t f = q.front();
                q.pop();
                for (size_t l = 0; l < 3; ++l) {
                    size_t c = 3 * f + l, oc = mesh.oppositeCorner(c);
                    if (oc == SurfaceMesh::npos)
                        continue;
                    size_t g = mesh.cornerFace(oc);
                    if (!seen[g]) {
                        int turns = edgeQuarterTurn(mesh, c, *field, normals);
                        Vector3 brushed = (*field)[g];
                        for (int k = 0; k < turns; ++k)
                            brushed = Vector3::crossProduct(normals[g], brushed);
                        (*field)[g] = unit(brushed, mesh.edgeVector(3 * g));
                        seen[g] = 1;
                        q.push(g);
                    }
                }
            }
        }
    }

    std::vector<int> computeCornerRotations(const SurfaceMesh& mesh,
        const std::vector<Vector3>& field, const std::vector<Vector3>& normals)
    {
        const size_t corners = mesh.cornerCount();
        std::vector<int> rotation(corners, 0);
        for (size_t c = 0; c < corners; ++c) {
            const size_t oc = mesh.oppositeCorner(c);
            if (oc == SurfaceMesh::npos)
                continue;
            rotation[c] = edgeQuarterTurn(mesh, c, field, normals);
        }
        return rotation;
    }

    // Smallest cone angle, in quarter turns, the cover is given: valence 2.
    // Valence 0 and 1 fold the whole map; raising valence 2 as well moved
    // cones at many hard-surface vertices and cost validity (corpus A/B).
    const int minimumConeQuarters = 2;

    // Rounding each edge's field turn to its nearest quarter turns can give
    // a vertex a cone angle below minimumConeQuarters: the needle tip of a
    // thin blade, where the field asks for a valence-0 or -1 cone. The cover
    // cannot follow either and folds over the whole island instead of only
    // near the tip (Kajetan's sword, 2026-10-21).
    // Round edges at such a vertex to their next quarter turn instead, each
    // adding pi / 2 to its angle and taking it from the neighbour across the
    // edge (a cone moves off the tip), until the vertex reaches the minimum.
    // Prefers edges that are not hard and whose turn is closest to rounding
    // that way; never takes a neighbour below the minimum. Marks the
    // re-rounded corners, which no longer follow the field, in `rerounded`.
    // Returns the number of re-rounded edges.
    size_t raiseSmallConeAngles(const SurfaceMesh& mesh, const std::vector<Vector3>& field,
        const std::vector<Vector3>& normals, const std::vector<signed char>& cornerConstraints,
        std::vector<int>* rotation, std::vector<char>* rerounded)
    {
        const double quarter = .5 * M_PI;
        const auto edgeAngle = [&](size_t face, const Vector3& edge) {
            const Vector3 u = field[face], v = Vector3::crossProduct(normals[face], u);
            return std::atan2(Vector3::dotProduct(edge, v), Vector3::dotProduct(edge, u));
        };
        // The field's turn across the edge net of the rotation's quarter
        // turns: within pi / 4 of 0 where the rotation is the nearest one.
        const auto residual = [&](size_t c) {
            const Vector3 edge = mesh.edgeVector(c);
            double turn = edgeAngle(mesh.cornerFace(c), edge) - edgeAngle(mesh.cornerFace(mesh.oppositeCorner(c)), edge)
                + quarter * (*rotation)[c];
            return std::remainder(turn, 2.0 * M_PI);
        };
        rerounded->assign(mesh.cornerCount(), 0);
        size_t count = 0;
        for (size_t vertex = 0; vertex < mesh.vertexCount(); ++vertex) {
            int quarters = minimumConeQuarters;
            if (!FrameField::coneQuarters(mesh, vertex, residual, &quarters))
                continue;
            while (quarters < minimumConeQuarters) {
                size_t best = SurfaceMesh::npos;
                bool bestHard = true;
                double bestCost = 0.0;
                for (const size_t c : mesh.cornersAroundVertex(vertex)) {
                    const size_t opposite = mesh.oppositeCorner(c);
                    if ((*rerounded)[c])
                        continue;
                    int neighbourQuarters = minimumConeQuarters + 1;
                    if (FrameField::coneQuarters(mesh, mesh.cornerVertex(opposite), residual, &neighbourQuarters)
                        && neighbourQuarters <= minimumConeQuarters)
                        continue;
                    const bool hard = ConstraintNone != cornerConstraints[c] || ConstraintNone != cornerConstraints[opposite];
                    const double cost = std::fabs(residual(c) + quarter);
                    if (best == SurfaceMesh::npos || (bestHard && !hard) || (bestHard == hard && cost < bestCost)) {
                        best = c;
                        bestHard = hard;
                        bestCost = cost;
                    }
                }
                if (best == SurfaceMesh::npos)
                    break;
                const size_t opposite = mesh.oppositeCorner(best);
                (*rotation)[best] = ((*rotation)[best] + 1) % 4;
                (*rotation)[opposite] = (4 - (*rotation)[best]) % 4;
                (*rerounded)[best] = (*rerounded)[opposite] = 1;
                ++count;
                ++quarters;
            }
        }
        return count;
    }

    std::vector<signed char> computeCornerConstraints(const SurfaceMesh& mesh,
        const std::vector<Vector3>& field, const std::vector<Vector3>& normals, const std::vector<char>& hardCorners)
    {
        const size_t corners = mesh.cornerCount();
        std::vector<signed char> cornerConstraints(corners, ConstraintNone);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, corners), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t c = range.begin(); c != range.end(); ++c)
                cornerConstraints[c] = static_cast<signed char>(edgeConstraint(mesh, c, field, normals, hardCorners));
        });
        return cornerConstraints;
    }

    // What became of T's hard edges (`hardCorners`, the FeatureEdgeSet): an isoline
    // constraint, none because the field is more than 10 deg off the edge, or none because
    // the edge was re-rounded. Lengths relative to T's mean edge (`features` diagnostics,
    // research/crease-preservation-2026-10-26.md).
    DiagnosticRecord creaseConstraintRecord(const SurfaceMesh& mesh, const std::vector<char>& hardCorners,
        const std::vector<signed char>& cornerConstraints, const std::vector<char>& rerounded)
    {
        double hard = 0.0, constrained = 0.0, misaligned = 0.0, droppedRerounded = 0.0;
        size_t hardEdges = 0;
        for (size_t c = 0; c < mesh.cornerCount(); ++c) {
            const size_t opposite = mesh.oppositeCorner(c);
            if (opposite == SurfaceMesh::npos || opposite < c || !hardCorners[c])
                continue;
            const double length = mesh.edgeVector(c).length();
            ++hardEdges;
            hard += length;
            if (cornerConstraints[c] == ConstraintNone && cornerConstraints[opposite] == ConstraintNone)
                misaligned += length;
            else if (rerounded[c] || rerounded[opposite])
                droppedRerounded += length;
            else
                constrained += length;
        }
        const double edge = mesh.averageEdgeLength() > 0.0 ? mesh.averageEdgeLength() : 1.0;
        DiagnosticRecord record("crease_constraints");
        record.set("hard_edges", (double)hardEdges);
        record.set("hard_rel_edge", hard / edge);
        record.set("constrained_rel_edge", constrained / edge);
        record.set("misaligned_rel_edge", misaligned / edge);
        record.set("rerounded_rel_edge", droppedRerounded / edge);
        return record;
    }

    void applyDirectionalSwaps(const SurfaceMesh& mesh, const std::vector<Vector3>& fieldBeforeBrush,
        const std::vector<Vector3>& field, const std::vector<Vector3>& normals,
        std::vector<double>* scalingU, std::vector<double>* scalingV)
    {
        size_t directionalSwaps = 0;
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            const Vector3 before = unit(fieldBeforeBrush[f], field[f]);
            const Vector3 after = unit(field[f], before);
            const Vector3 perpendicular = unit(Vector3::crossProduct(normals[f], after), before);
            if (std::fabs(Vector3::dotProduct(before, after)) < std::fabs(Vector3::dotProduct(before, perpendicular)))
                std::swap((*scalingU)[f], (*scalingV)[f]), ++directionalSwaps;
        }
    }

    const double quarterTurn[4][2][2] = {
        { { 1, 0 }, { 0, 1 } },
        { { 0, 1 }, { -1, 0 } },
        { { -1, 0 }, { 0, -1 } },
        { { 0, -1 }, { 1, 0 } }
    };

    void applyCurlCorrection(const SurfaceMesh& mesh, const std::vector<Vector3>& normals,
        const std::vector<int>& rotation, const std::vector<signed char>& cornerConstraints,
        const std::vector<double>* faceScaling, double scale, double regularization,
        std::vector<double>* scalingU, std::vector<double>* scalingV,
        std::vector<Vector3>* field, DiagnosticRecord* diagnostics)
    {
        const size_t faceCount = mesh.faceCount();
        const bool hasFaceScaling = faceScaling && faceScaling->size() == faceCount;
        std::vector<double> su(faceCount), sv(faceCount);
        std::vector<Vector3> perpendicular(faceCount);
        for (size_t f = 0; f < faceCount; ++f) {
            const double faceScale = hasFaceScaling ? std::max(1e-12, (*faceScaling)[f]) : 1.0;
            su[f] = scale * faceScale * std::max(1e-12, (*scalingU)[f]);
            sv[f] = scale * faceScale * std::max(1e-12, (*scalingV)[f]);
            perpendicular[f] = unit(Vector3::crossProduct(normals[f], (*field)[f]),
                mesh.edgeVector(3 * f));
        }

        std::vector<char> anchored(faceCount, 0);
        for (size_t c = 0; c < mesh.cornerCount(); ++c)
            if (cornerConstraints[c] != ConstraintNone)
                anchored[mesh.cornerFace(c)] = 1;

        const auto request = [&](size_t corner, size_t face, double* constant,
                                 double* turnGradient, double* alongGradient, double* acrossGradient) {
            const Vector3 e = mesh.edgeVector(corner);
            const double along = Vector3::dotProduct((*field)[face], e);
            const double across = Vector3::dotProduct(perpendicular[face], e);
            constant[0] = along / su[face];
            constant[1] = across / sv[face];
            turnGradient[0] = across / su[face];
            turnGradient[1] = -along / sv[face];
            alongGradient[0] = -along / su[face];
            alongGradient[1] = 0.0;
            acrossGradient[0] = 0.0;
            acrossGradient[1] = -across / sv[face];
        };

        ConstrainedLeastSquares system(3 * faceCount);
        size_t edgeCount = 0;
        for (size_t c = 0; c < mesh.cornerCount(); ++c) {
            const size_t oc = mesh.oppositeCorner(c);
            if (oc == SurfaceMesh::npos || oc < c)
                continue;
            const size_t f = mesh.cornerFace(c), g = mesh.cornerFace(oc);
            const int r = (rotation[c] % 4 + 4) % 4;
            double here[2], hereTurn[2], hereAlong[2], hereAcross[2];
            double across[2], acrossTurn[2], acrossAlong[2], acrossAcross[2];
            request(c, f, here, hereTurn, hereAlong, hereAcross);
            request(oc, g, across, acrossTurn, acrossAlong, acrossAcross);
            const auto turned = [&](const double* value, size_t k) {
                return quarterTurn[r][k][0] * value[0] + quarterTurn[r][k][1] * value[1];
            };
            for (size_t k = 0; k < 2; ++k) {
                const double residual = here[k] + turned(across, k);
                system.addEnergy({ { 3 * f, hereTurn[k] },
                                     { 3 * f + 1, hereAlong[k] },
                                     { 3 * f + 2, hereAcross[k] },
                                     { 3 * g, turned(acrossTurn, k) },
                                     { 3 * g + 1, turned(acrossAlong, k) },
                                     { 3 * g + 2, turned(acrossAcross, k) } },
                    -residual, 1.0);
            }
            ++edgeCount;
        }
        if (0 == edgeCount)
            return;
        for (size_t f = 0; f < faceCount; ++f) {
            if (anchored[f])
                system.addConstraint({ { 3 * f, 1.0 } }, 0.0);
            else
                system.addEnergy({ { 3 * f, 1.0 } }, 0.0, regularization);
            system.addEnergy({ { 3 * f + 1, 1.0 } }, 0.0, regularization);
            system.addEnergy({ { 3 * f + 2, 1.0 } }, 0.0, regularization);
        }

        std::vector<double> correction;
        const bool solved = system.solve(&correction);
        appendSolveStats(diagnostics, "solve", system.lastSolveStats());
        if (!solved || correction.size() != 3 * faceCount)
            return;

        const double limit = .3, scaleLimit = std::log(1.5);
        size_t clampedAngles = 0, clampedScales = 0;
        for (size_t f = 0; f < faceCount; ++f) {
            if (std::fabs(correction[3 * f]) > limit)
                ++clampedAngles;
            if (std::fabs(correction[3 * f + 1]) > scaleLimit || std::fabs(correction[3 * f + 2]) > scaleLimit)
                ++clampedScales;
        }
        diagnostics->set("clamped_angles", (double)clampedAngles);
        diagnostics->set("clamped_scales", (double)clampedScales);
        for (size_t f = 0; f < faceCount; ++f) {
            const double angle = std::max(-limit, std::min(limit, correction[3 * f]));
            (*field)[f] = unit(std::cos(angle) * (*field)[f] + std::sin(angle) * perpendicular[f],
                (*field)[f]);
            (*scalingU)[f] *= std::exp(std::max(-scaleLimit, std::min(scaleLimit, correction[3 * f + 1])));
            (*scalingV)[f] *= std::exp(std::max(-scaleLimit, std::min(scaleLimit, correction[3 * f + 2])));
        }
        double areaBefore = 0.0, areaAfter = 0.0;
        for (size_t f = 0; f < faceCount; ++f) {
            const double faceScale = hasFaceScaling ? std::max(1e-12, (*faceScaling)[f]) : 1.0;
            const double area = Vector3::crossProduct(mesh.edgeVector(3 * f),
                -mesh.edgeVector(3 * f + 2))
                                    .length();
            areaBefore += area / (su[f] * sv[f]);
            su[f] = scale * faceScale * std::max(1e-12, (*scalingU)[f]);
            sv[f] = scale * faceScale * std::max(1e-12, (*scalingV)[f]);
            areaAfter += area / (su[f] * sv[f]);
        }
        if (areaBefore > 0.0 && areaAfter > 0.0) {
            const double factor = std::sqrt(areaAfter / areaBefore);
            for (size_t f = 0; f < faceCount; ++f) {
                (*scalingU)[f] *= factor;
                (*scalingV)[f] *= factor;
            }
        }
    }

    std::vector<char> computeSeam(const SurfaceMesh& mesh, const std::vector<int>& rotation)
    {
        const size_t corners = mesh.cornerCount();
        std::vector<char> insideBall(corners, 0), ballSeen(mesh.faceCount(), 0);
        if (mesh.faceCount() != 0) {
            std::queue<size_t> q;
            q.push(0);
            ballSeen[0] = 1;
            while (!q.empty()) {
                const size_t f = q.front();
                q.pop();
                for (size_t l = 0; l < 3; ++l) {
                    const size_t c = 3 * f + l, oc = mesh.oppositeCorner(c);
                    if (oc == SurfaceMesh::npos || rotation[c] != 0)
                        continue;
                    const size_t g = mesh.cornerFace(oc);
                    if (!ballSeen[g]) {
                        ballSeen[g] = 1;
                        q.push(g);
                        insideBall[c] = insideBall[oc] = 1;
                    }
                }
            }
        }
        std::vector<char> seam(corners, 0);
        for (size_t c = 0; c < corners; ++c)
            seam[c] = (mesh.oppositeCorner(c) == SurfaceMesh::npos || !insideBall[c]);
        std::vector<size_t> borderDegree(mesh.vertexCount(), 0);
        for (size_t c = 0; c < corners; ++c)
            if (seam[c])
                ++borderDegree[mesh.cornerVertex(c)];
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t c = 0; c < corners; ++c) {
                const size_t oc = mesh.oppositeCorner(c);
                if (oc == SurfaceMesh::npos || !seam[c] || rotation[c] != 0)
                    continue;
                const size_t v0 = mesh.cornerVertex(c);
                if (borderDegree[v0] != 1)
                    continue;
                const size_t v1 = mesh.cornerVertex(mesh.nextCorner(c));
                seam[c] = seam[oc] = 0;
                insideBall[c] = insideBall[oc] = 1;
                if (borderDegree[v0] > 0)
                    --borderDegree[v0];
                if (borderDegree[v1] > 0)
                    --borderDegree[v1];
                changed = true;
            }
        }
        return seam;
    }

    // Face f is mapped into the plane of its cross field direction u and the
    // quarter turn v of it about the normal, one quad edge being su long
    // along u and sv along v.
    struct FaceFrame {
        Vector3 u, v;
        double su = 1.0, sv = 1.0;
        Vector2 map(const Vector3& e) const
        {
            return Vector2(Vector3::dotProduct(u, e) / su, Vector3::dotProduct(v, e) / sv);
        }
    };

    std::vector<FaceFrame> computeFaceFrames(const CoverContext& ctx)
    {
        const SurfaceMesh& mesh = ctx.mesh;
        const bool hasFaceScaling = ctx.faceScaling && ctx.faceScaling->size() == mesh.faceCount();
        std::vector<FaceFrame> frames(mesh.faceCount());
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            FaceFrame& frame = frames[f];
            frame.u = ctx.field[f];
            frame.v = unit(Vector3::crossProduct(ctx.normals[f], frame.u), mesh.edgeVector(3 * f));
            const double faceScale = hasFaceScaling ? std::max(1e-12, (*ctx.faceScaling)[f]) : 1.0;
            frame.su = ctx.scale * faceScale * std::max(1e-12, ctx.scalingU[f]);
            frame.sv = ctx.scale * faceScale * std::max(1e-12, ctx.scalingV[f]);
        }
        return frames;
    }

    // The angle that turns `from` onto `to`, counter-clockwise positive.
    double signedAngle(const Vector2& from, const Vector2& to)
    {
        return std::atan2(from.x() * to.y() - from.y() * to.x(), from.x() * to.x() + from.y() * to.y());
    }

    // `v` turned by r quarter turns counter-clockwise.
    Vector2 turnedQuarters(const Vector2& v, int r)
    {
        switch ((r % 4 + 4) % 4) {
        case 1:
            return Vector2(-v.y(), v.x());
        case 2:
            return Vector2(-v.x(), -v.y());
        case 3:
            return Vector2(v.y(), -v.x());
        default:
            return v;
        }
    }

    // How the map winds around interior vertices compared with what the cross
    // field asks for. Walking around a vertex, the direction of an edge out of
    // it turns by minus the corner angle inside each face and by rotation[c]
    // quarter turns across each edge (the cover maps face f to Q g + t, with
    // Q the quarter turn by -rotation[c]), and returns to itself. So the
    // map's angle sum is 2 pi w + R pi / 2 for an integer w and the fan's
    // rotation sum R. The target triangles (each face in its frame) give the
    // field's own sum in the same way, where the frames also turn a little
    // across edges besides the quarter turns. A vertex whose two sums differ,
    // necessarily by a multiple of 2 pi, is wound wrongly: a cone at -pi / 2
    // instead of 3 pi / 2 is one turn short, a regular vertex at 0 is folded
    // over. Boundary vertices are skipped.
    struct Winding {
        size_t interiorVertices = 0;
        size_t cones = 0;
        size_t miswoundCones = 0;
        // Cones whose map angle is smaller than the field's (one turn or more).
        size_t shortCones = 0;
        size_t miswoundRegular = 0;
        // Cones the field asks to have an angle other than 3 pi / 2 or 5 pi / 2.
        size_t unusualCones = 0;
        size_t conesWithFlips = 0;
        // Vertices with an edge of zero length in the map: their winding is
        // not defined, so they are not classified.
        size_t degenerateFans = 0;
        size_t flipped = 0;
        size_t flippedNearCones = 0;
        size_t flippedNearMiswound = 0;
        size_t flippedNearUnusual = 0;
        // The largest turn of a mapped edge across a seam besides its quarter
        // turns: zero up to rounding while the transitions hold.
        double transitionError = 0.0;
    };

    Winding measureWinding(const SurfaceMesh& mesh, const std::vector<int>& rotation,
        const std::vector<FaceFrame>& frames, const std::vector<double>& uvs)
    {
        Winding winding;
        const auto uvOf = [&](size_t c) { return Vector2(uvs[2 * c], uvs[2 * c + 1]); };
        std::vector<char> flipped(mesh.faceCount(), 0), nearCone(mesh.faceCount(), 0), nearMiswound(mesh.faceCount(), 0),
            nearUnusual(mesh.faceCount(), 0);
        for (size_t f = 0; f < mesh.faceCount(); ++f)
            if (Predicates::orient2d(uvOf(3 * f), uvOf(3 * f + 1), uvOf(3 * f + 2)) < 0) {
                flipped[f] = 1;
                ++winding.flipped;
            }
        const double quarter = .5 * M_PI;
        for (size_t vertex = 0; vertex < mesh.vertexCount(); ++vertex) {
            const auto& incident = mesh.cornersAroundVertex(vertex);
            if (incident.empty())
                continue;
            const size_t start = incident.front();
            size_t c = start, steps = 0;
            int rotationSum = 0;
            double mapSum = 0.0, fieldSum = 0.0;
            bool closed = true, degenerate = false, starFlipped = false;
            do {
                const size_t oc = mesh.oppositeCorner(c);
                if (oc == SurfaceMesh::npos) {
                    closed = false;
                    break;
                }
                const size_t f = mesh.cornerFace(c), g = mesh.cornerFace(oc), other = mesh.nextCorner(oc);
                const size_t n = mesh.nextCorner(c), p = mesh.previousCorner(c);
                const int r = (rotation[c] % 4 + 4) % 4;
                const Vector2 out = uvOf(n) - uvOf(c), in = uvOf(p) - uvOf(c);
                if (out.lengthSquared() == 0.0 || in.lengthSquared() == 0.0)
                    degenerate = true;
                starFlipped = starFlipped || flipped[f];
                // Angle sums net of the extra turn across the edge, so that
                // each is exactly 2 pi w + R pi / 2.
                const double mapTurn = signedAngle(turnedQuarters(out, r), uvOf(oc) - uvOf(other));
                winding.transitionError = std::max(winding.transitionError, std::fabs(mapTurn));
                mapSum += signedAngle(out, in) - mapTurn;
                const Vector3 e = mesh.edgeVector(c);
                const Vector2 fieldOut = frames[f].map(e), fieldIn = frames[f].map(-mesh.edgeVector(p));
                fieldSum += signedAngle(fieldOut, fieldIn) - signedAngle(turnedQuarters(fieldOut, r), frames[g].map(e));
                rotationSum += r;
                c = other;
            } while (c != start && ++steps <= incident.size());
            if (!closed || c != start)
                continue;
            ++winding.interiorVertices;
            const bool cone = rotationSum % 4 != 0;
            const double base = quarter * rotationSum;
            const long mapTurns = std::lround((mapSum - base) / (2.0 * M_PI));
            const long fieldTurns = std::lround((fieldSum - base) / (2.0 * M_PI));
            const double fieldAngle = base + 2.0 * M_PI * fieldTurns;
            if (cone) {
                ++winding.cones;
                if (std::fabs(fieldAngle - 3.0 * quarter) > 1.0 && std::fabs(fieldAngle - 5.0 * quarter) > 1.0) {
                    ++winding.unusualCones;
                    for (size_t corner : incident)
                        nearUnusual[mesh.cornerFace(corner)] = 1;
                }
                if (starFlipped)
                    ++winding.conesWithFlips;
            }
            if (degenerate) {
                ++winding.degenerateFans;
                continue;
            }
            const bool miswound = mapTurns != fieldTurns;
            if (miswound) {
                if (cone) {
                    ++winding.miswoundCones;
                    if (mapTurns < fieldTurns)
                        ++winding.shortCones;
                } else {
                    ++winding.miswoundRegular;
                }
            }
            if (cone || miswound)
                for (size_t corner : incident) {
                    if (cone)
                        nearCone[mesh.cornerFace(corner)] = 1;
                    if (miswound)
                        nearMiswound[mesh.cornerFace(corner)] = 1;
                }
        }
        for (size_t f = 0; f < mesh.faceCount(); ++f)
            if (flipped[f]) {
                winding.flippedNearCones += nearCone[f];
                winding.flippedNearMiswound += nearMiswound[f];
                winding.flippedNearUnusual += nearUnusual[f];
            }
        return winding;
    }

    void appendWinding(DiagnosticRecord* record, const std::string& prefix, const Winding& winding)
    {
        record->set(prefix + "_interior_vertices", (double)winding.interiorVertices);
        record->set(prefix + "_cones", (double)winding.cones);
        record->set(prefix + "_miswound_cones", (double)winding.miswoundCones);
        record->set(prefix + "_short_cones", (double)winding.shortCones);
        record->set(prefix + "_miswound_regular", (double)winding.miswoundRegular);
        record->set(prefix + "_unusual_cones", (double)winding.unusualCones);
        record->set(prefix + "_cones_with_flips", (double)winding.conesWithFlips);
        record->set(prefix + "_degenerate_fans", (double)winding.degenerateFans);
        record->set(prefix + "_flipped_near_cones", (double)winding.flippedNearCones);
        record->set(prefix + "_flipped_near_miswound", (double)winding.flippedNearMiswound);
        record->set(prefix + "_flipped_near_unusual", (double)winding.flippedNearUnusual);
        record->set(prefix + "_transition_error", winding.transitionError);
    }

    void addRotationConstraints(MixedIntegerLeastSquares& s, size_t ax, size_t bx, int r, double sign)
    {
        r = (r % 4 + 4) % 4;
        if (r == 0) {
            s.addConstraint(ax, 1, bx, sign);
            s.addConstraint(ax + 1, 1, bx + 1, sign);
        } else if (r == 1) {
            s.addConstraint(ax, 1, bx + 1, sign);
            s.addConstraint(ax + 1, 1, bx, -sign);
        } else if (r == 2) {
            s.addConstraint(ax, 1, bx, -sign);
            s.addConstraint(ax + 1, 1, bx + 1, -sign);
        } else {
            s.addConstraint(ax, 1, bx + 1, -sign);
            s.addConstraint(ax + 1, 1, bx, sign);
        }
    }

    // Net uv area of the cover in grid cells (one cell is one quad); faces
    // mapped with the opposite orientation count negative.
    double coverCellArea(const SurfaceMesh& mesh, const MixedIntegerLeastSquares& s)
    {
        double area = 0.0;
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            const size_t a = 3 * f, b = 3 * f + 1, c = 3 * f + 2;
            const double ax = s.value(2 * a), ay = s.value(2 * a + 1);
            area += .5 * ((s.value(2 * b) - ax) * (s.value(2 * c + 1) - ay) - (s.value(2 * c) - ax) * (s.value(2 * b + 1) - ay));
        }
        return std::fabs(area);
    }

    // Rescales the continuous cover so it spans as many cells as the face
    // sizes ask for. Least squares projects the sized frame onto integrable
    // maps and loses the part of it that has curl, so the map came out with
    // 40-97 % of the requested cells on the baseline corpus, the least on
    // hard-surface parts. The continuous solution is linear in 1 / size, so
    // the rescale is exact there; rounding afterwards moves the count by a
    // few percent.
    // Returns expected / actual cells, the factor the cover's area was (or,
    // beyond the limit, would have been) scaled by; 0 if either is empty.
    // How far it is from 1 measures how much of the requested sizing was not
    // integrable (REFERENCE M3).
    double matchCellCount(const SurfaceMesh& mesh, double expectedCells, MixedIntegerLeastSquares* s)
    {
        const double cells = coverCellArea(mesh, *s);
        if (!(expectedCells > 0.0) || !(cells > 0.0))
            return 0.0;
        const double ratio = expectedCells / cells;
        // Beyond this the map is broken rather than shrunk, and growing it
        // would only multiply the extraction work on a bad map.
        const double maximumRatio = 4.0;
        if (ratio > maximumRatio || ratio < 1.0 / maximumRatio)
            return ratio;
        s->scaleSolution(std::sqrt(ratio));
        return ratio;
    }

    // How closely the map follows the requested quad sizes. Per face,
    // a = log of the edge the map gives it (sqrt of 3D area over uv area) and
    // r = log of the requested edge, both area weighted and centred. Records
    // the RMS of a - r and the slope of a against r: 1 when every requested
    // size change is realised, 0 for uniform quads. Flipped faces count with
    // their absolute uv area; degenerate ones are skipped. The spread of
    // the requested sizes and the measured area, which do not depend on the
    // map much, are recorded only for the continuous map.
    void appendSizeFidelity(DiagnosticRecord* record, const std::string& prefix, const SurfaceMesh& mesh,
        const MixedIntegerLeastSquares& s, const std::vector<double>* faceScaling, double scale)
    {
        const bool hasFaceScaling = faceScaling && faceScaling->size() == mesh.faceCount();
        std::vector<double> realised(mesh.faceCount(), 0.0), requested(mesh.faceCount(), 0.0), weight(mesh.faceCount(), 0.0);
        double totalWeight = 0.0, meanRealised = 0.0, meanRequested = 0.0;
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            const size_t a = 3 * f, b = 3 * f + 1, c = 3 * f + 2;
            const double ax = s.value(2 * a), ay = s.value(2 * a + 1);
            const double uvArea = .5 * std::fabs((s.value(2 * b) - ax) * (s.value(2 * c + 1) - ay) - (s.value(2 * c) - ax) * (s.value(2 * b + 1) - ay));
            const double area = .5 * Vector3::crossProduct(mesh.edgeVector(a), -mesh.edgeVector(c)).length();
            if (!(uvArea > 0.0) || !(area > 0.0))
                continue;
            weight[f] = area;
            realised[f] = .5 * std::log(area / uvArea);
            requested[f] = std::log(scale * (hasFaceScaling ? std::max(1e-12, (*faceScaling)[f]) : 1.0));
            totalWeight += area;
            meanRealised += area * realised[f];
            meanRequested += area * requested[f];
        }
        if (!(totalWeight > 0.0))
            return;
        meanRealised /= totalWeight;
        meanRequested /= totalWeight;
        double error = 0.0, covariance = 0.0, variance = 0.0;
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            const double da = realised[f] - meanRealised, dr = requested[f] - meanRequested;
            error += weight[f] * (da - dr) * (da - dr);
            covariance += weight[f] * da * dr;
            variance += weight[f] * dr * dr;
        }
        record->set(prefix + "_size_error_rms", std::sqrt(error / totalWeight));
        record->set(prefix + "_size_slope", variance > 0.0 ? covariance / variance : 0.0);
        if (prefix != "continuous")
            return;
        record->set("requested_size_spread", std::sqrt(variance / totalWeight));
        record->set("size_measured_area", totalWeight);
    }

    // The corner uvs of the current solution, laid out as measureUvOrientation
    // expects.
    std::vector<double> cornerUvs(const MixedIntegerLeastSquares& s, size_t uvVariables)
    {
        std::vector<double> uvs(uvVariables);
        for (size_t i = 0; i < uvVariables; ++i)
            uvs[i] = s.value(i);
        return uvs;
    }

    // How far around an inverted face untangling moves the map, in vertex
    // rings, and how strongly it holds the map to the frames' rotation
    // (Untangler::Options::alignment). On 20 corpus runs (2k and 10k quads,
    // Quality) 1, 2 and 4 rings without alignment left 0.24, 0.16 and 0.08
    // times the final flips but did not lower folded quads and SJ <= 0; with
    // alignment 10, 2 rings gave flips x0.29, non-quads x0.64, folded quads
    // x0.80 and SJ <= 0 x0.79 (geometric means) for about 4 % of the run.
    const size_t untangleRings = 2;
    const double untangleAlignment = 10.0;

    // Untangles the rounded cover where it is inverted. The faces within
    // `rings` vertex rings of an inverted or degenerate face give the
    // continuous kernel variables that move; every integer stays fixed, and
    // so do the variables of faces farther out. Every face that depends on a
    // moving variable enters Untangler's energy, with itself in its frame as
    // the target. The result is kept only if fewer faces end up inverted or
    // degenerate.
    //
    // Faces whose three corners share a coordinate whatever the free
    // variables are (all three on one hard-feature isoline, say) are
    // degenerate by the constraints alone; they are counted, and left out.
    void untangleCover(const SurfaceMesh& mesh, const std::vector<FaceFrame>& frames, size_t rings,
        MixedIntegerLeastSquares* s, DiagnosticRecord* result)
    {
        const size_t faceCount = mesh.faceCount();
        const size_t coordinates = 2 * mesh.cornerCount();
        const auto badFace = [&](size_t f) {
            const auto uv = [&](size_t c) { return Vector2(s->value(2 * c), s->value(2 * c + 1)); };
            return Predicates::orient2d(uv(3 * f), uv(3 * f + 1), uv(3 * f + 2)) <= 0;
        };
        std::vector<char> bad(faceCount, 0);
        size_t badCount = 0;
        for (size_t f = 0; f < faceCount; ++f)
            if (badFace(f)) {
                bad[f] = 1;
                ++badCount;
            }
        if (0 == badCount)
            return;

        // Every uv coordinate in kernel terms.
        std::vector<size_t> termOffset(coordinates + 1, 0);
        std::vector<std::pair<size_t, double>> terms, line;
        for (size_t i = 0; i < coordinates; ++i) {
            s->kernelTerms(i, &line);
            terms.insert(terms.end(), line.begin(), line.end());
            termOffset[i + 1] = terms.size();
        }
        // Whether coordinates i and j are the same affine function of the
        // free kernel variables.
        const auto sameCoordinate = [&](size_t i, size_t j) {
            std::vector<std::pair<size_t, double>> difference;
            double constant = 0.0, magnitude = 0.0;
            const auto collect = [&](size_t coordinate, double sign) {
                for (size_t k = termOffset[coordinate]; k < termOffset[coordinate + 1]; ++k) {
                    const size_t variable = terms[k].first;
                    const double a = sign * terms[k].second;
                    magnitude = std::max(magnitude, std::fabs(a));
                    if (s->kernelVariableFixed(variable))
                        constant += a * s->kernelValue(variable);
                    else
                        difference.push_back({ variable, a });
                }
            };
            collect(i, 1.0);
            collect(j, -1.0);
            if (std::fabs(constant) > 1e-9)
                return false;
            std::sort(difference.begin(), difference.end());
            for (size_t k = 0; k < difference.size();) {
                double sum = 0.0;
                size_t next = k;
                for (; next < difference.size() && difference[next].first == difference[k].first; ++next)
                    sum += difference[next].second;
                if (std::fabs(sum) > 1e-12 * magnitude)
                    return false;
                k = next;
            }
            return true;
        };
        std::vector<char> pinned(faceCount, 0), region(faceCount, 0);
        size_t pinnedCount = 0, seeds = 0;
        for (size_t f = 0; f < faceCount; ++f) {
            if (!bad[f])
                continue;
            for (size_t row = 0; row < 2 && !pinned[f]; ++row)
                pinned[f] = sameCoordinate(6 * f + row, 6 * f + 2 + row) && sameCoordinate(6 * f + row, 6 * f + 4 + row);
            pinnedCount += pinned[f];
            if (!pinned[f]) {
                region[f] = 1;
                ++seeds;
            }
        }
        result->set("untangle_seed_faces", (double)seeds);
        result->set("untangle_pinned_faces", (double)pinnedCount);
        if (0 == seeds)
            return;
        std::vector<char> marked(mesh.vertexCount(), 0);
        for (size_t ring = 0; ring <= rings; ++ring) {
            for (size_t f = 0; f < faceCount; ++f)
                if (region[f])
                    for (size_t l = 0; l < 3; ++l)
                        marked[mesh.cornerVertex(3 * f + l)] = 1;
            if (ring == rings)
                break;
            for (size_t f = 0; f < faceCount; ++f)
                for (size_t l = 0; l < 3 && !region[f]; ++l)
                    if (marked[mesh.cornerVertex(3 * f + l)])
                        region[f] = 1;
        }

        // The kernel variables that move: the free ones of the region.
        const size_t none = std::numeric_limits<size_t>::max();
        std::vector<size_t> moving(s->kernelSize(), none);
        size_t variableCount = 0, regionFaces = 0;
        for (size_t f = 0; f < faceCount; ++f) {
            if (!region[f] || pinned[f])
                continue;
            ++regionFaces;
            for (size_t i = 6 * f; i < 6 * f + 6; ++i)
                for (size_t k = termOffset[i]; k < termOffset[i + 1]; ++k) {
                    const size_t variable = terms[k].first;
                    if (moving[variable] == none && !s->kernelVariableFixed(variable))
                        moving[variable] = variableCount++;
                }
        }
        std::vector<size_t> faces;
        for (size_t f = 0; f < faceCount; ++f) {
            bool depends = false;
            for (size_t k = termOffset[6 * f]; k < termOffset[6 * f + 6] && !depends; ++k)
                depends = moving[terms[k].first] != none;
            if (depends && !pinned[f])
                faces.push_back(f);
        }
        result->set("untangle_region_faces", (double)regionFaces);
        result->set("untangle_faces", (double)faces.size());
        result->set("untangle_variables", (double)variableCount);
        if (0 == variableCount)
            return;

        // Faces linked by a moving variable form a component, and each
        // component is untangled on its own, with its own eps: a spot where
        // no inversion-free map exists keeps eps up, and would hold back the
        // others if they shared it. Components are numbered by their first
        // face, so the result does not depend on the threads.
        std::vector<size_t> parent(variableCount);
        for (size_t i = 0; i < variableCount; ++i)
            parent[i] = i;
        const auto root = [&](size_t i) {
            while (parent[i] != i)
                i = parent[i] = parent[parent[i]];
            return i;
        };
        const auto faceVariable = [&](size_t f) {
            for (size_t k = termOffset[6 * f]; k < termOffset[6 * f + 6]; ++k)
                if (moving[terms[k].first] != none)
                    return moving[terms[k].first];
            return none;
        };
        for (size_t f : faces) {
            const size_t first = root(faceVariable(f));
            for (size_t k = termOffset[6 * f]; k < termOffset[6 * f + 6]; ++k)
                if (moving[terms[k].first] != none) {
                    const size_t other = root(moving[terms[k].first]);
                    if (other != first)
                        parent[other] = first;
                }
        }
        std::vector<size_t> componentOf(variableCount, none);
        std::vector<std::vector<size_t>> componentFaces;
        for (size_t f : faces) {
            const size_t r = root(faceVariable(f));
            if (componentOf[r] == none) {
                componentOf[r] = componentFaces.size();
                componentFaces.emplace_back();
            }
            componentFaces[componentOf[r]].push_back(f);
        }
        // Variables of each component, numbered from 0 within it.
        std::vector<std::vector<size_t>> componentVariables(componentFaces.size());
        std::vector<size_t> local(s->kernelSize(), none);
        for (size_t variable = 0; variable < moving.size(); ++variable)
            if (moving[variable] != none) {
                std::vector<size_t>& list = componentVariables[componentOf[root(moving[variable])]];
                local[variable] = list.size();
                list.push_back(variable);
            }

        std::vector<std::vector<double>> solutions(componentFaces.size());
        std::vector<Untangler::Stats> stats(componentFaces.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, componentFaces.size()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t component = range.begin(); component != range.end(); ++component) {
                const std::vector<size_t>& variables = componentVariables[component];
                Untangler::Options options;
                options.alignment = untangleAlignment;
                Untangler untangler(variables.size(), options);
                for (size_t f : componentFaces[component]) {
                    const FaceFrame& frame = frames[f];
                    const std::array<Vector2, 3> target = { Vector2(0.0, 0.0), frame.map(mesh.edgeVector(3 * f)),
                        -1.0 * frame.map(mesh.edgeVector(3 * f + 2)) };
                    const double area = .5 * (target[1].x() * target[2].y() - target[1].y() * target[2].x());
                    std::array<Untangler::Coordinate, 6> corners;
                    for (size_t i = 0; i < 6; ++i)
                        for (size_t k = termOffset[6 * f + i]; k < termOffset[6 * f + i + 1]; ++k) {
                            const size_t variable = terms[k].first;
                            if (moving[variable] != none)
                                corners[i].terms.push_back({ local[variable], terms[k].second });
                            else
                                corners[i].constant += terms[k].second * s->kernelValue(variable);
                        }
                    untangler.addTriangle(target, area, corners);
                }
                std::vector<double>& x = solutions[component];
                x.resize(variables.size());
                for (size_t i = 0; i < variables.size(); ++i)
                    x[i] = s->kernelValue(variables[i]);
                untangler.untangle(&x, &stats[component]);
            }
        });

        // Keeps a component's result only if fewer of its faces are bad.
        size_t badBefore = 0, badAfter = 0, accepted = 0, solved = 0;
        size_t iterations = 0, newtonSteps = 0, factorizations = 0, largest = 0;
        double minimumDeterminant = std::numeric_limits<double>::infinity();
        for (size_t component = 0; component < componentFaces.size(); ++component) {
            const std::vector<size_t>& variables = componentVariables[component];
            std::vector<double> previous(variables.size());
            size_t before = 0, after = 0;
            for (size_t f : componentFaces[component])
                before += badFace(f);
            for (size_t i = 0; i < variables.size(); ++i) {
                previous[i] = s->kernelValue(variables[i]);
                s->setKernelValue(variables[i], solutions[component][i]);
            }
            for (size_t f : componentFaces[component])
                after += badFace(f);
            if (after < before) {
                ++accepted;
            } else {
                for (size_t i = 0; i < variables.size(); ++i)
                    s->setKernelValue(variables[i], previous[i]);
                after = before;
            }
            badBefore += before;
            badAfter += after;
            solved += 0 == after;
            const Untangler::Stats& st = stats[component];
            iterations = std::max(iterations, st.iterations);
            newtonSteps += st.newtonSteps;
            factorizations += st.factorizations;
            largest = std::max(largest, variables.size());
            minimumDeterminant = std::min(minimumDeterminant, st.minimumDeterminantAfter);
        }
        {
            std::vector<char> nearPinned(mesh.vertexCount(), 0);
            for (size_t f = 0; f < faceCount; ++f)
                if (pinned[f])
                    for (size_t l = 0; l < 3; ++l)
                        nearPinned[mesh.cornerVertex(3 * f + l)] = 1;
            size_t leftNearPinned = 0;
            for (size_t f = 0; f < faceCount; ++f) {
                if (pinned[f] || !badFace(f))
                    continue;
                bool near = false;
                for (size_t l = 0; l < 3; ++l)
                    near = near || nearPinned[mesh.cornerVertex(3 * f + l)];
                leftNearPinned += near;
            }
            result->set("untangle_left_near_pinned", (double)leftNearPinned);
        }
        result->set("untangle_components", (double)componentFaces.size());
        result->set("untangle_components_accepted", (double)accepted);
        result->set("untangle_components_solved", (double)solved);
        result->set("untangle_largest_component", (double)largest);
        result->set("untangle_inverted_before", (double)badBefore);
        result->set("untangle_inverted_after", (double)badAfter);
        result->set("untangle_min_det_after", minimumDeterminant);
        result->set("untangle_iterations_max", (double)iterations);
        result->set("untangle_newton_steps", (double)newtonSteps);
        result->set("untangle_factorizations", (double)factorizations);
    }

    using HolonomyRow = std::vector<std::pair<size_t, double>>;

    // Coefficients of component `axis` of the translation holonomy along a
    // closed dual corner path (the composed map from the last face's chart
    // into the first one's is uv -> A uv + sum A_k t_k).
    HolonomyRow holonomyRow(const std::vector<int>& rotation, const std::vector<size_t>& path,
        size_t uvVariables, int axis, bool* identity)
    {
        static const int M[4][2][2] = { { { 1, 0 }, { 0, 1 } }, { { 0, 1 }, { -1, 0 } }, { { -1, 0 }, { 0, -1 } }, { { 0, -1 }, { 1, 0 } } };
        int A[2][2] = { { 1, 0 }, { 0, 1 } };
        HolonomyRow row;
        for (const size_t pc : path) {
            const size_t t = uvVariables + 2 * pc;
            if (A[axis][0])
                row.push_back({ t, (double)A[axis][0] });
            if (A[axis][1])
                row.push_back({ t + 1, (double)A[axis][1] });
            const int r = (rotation[pc] % 4 + 4) % 4;
            int B[2][2];
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j)
                    B[i][j] = A[i][0] * M[r][0][j] + A[i][1] * M[r][1][j];
            std::copy(&B[0][0], &B[0][0] + 4, &A[0][0]);
        }
        *identity = A[0][0] == 1 && A[1][1] == 1 && A[0][1] == 0 && A[1][0] == 0;
        return row;
    }

    // Research prototype (closed edge loops, AUTOREMESHER_AUTO_RINGS=<ratio>):
    // finds rings of the continuous cover, closed dual paths whose chart comes
    // back unrotated and shifted mostly along the ring, by tracing isolines
    // from seed faces until they pass close to their start and closing the
    // gap with a short dual path. Returns one holonomy row per distinct ring
    // whose cross shift is at most `ratio` times its length; constraining
    // the row to zero closes the ring's edge loops.
    std::vector<HolonomyRow> detectRings(const SurfaceMesh& mesh, const std::vector<int>& rotation,
        const std::vector<double>& values, size_t uvVariables, const std::vector<FaceFrame>& frames,
        double ratio, double beta, DiagnosticRecord* record)
    {
        static const double M[4][2][2] = { { { 1, 0 }, { 0, 1 } }, { { 0, 1 }, { -1, 0 } }, { { -1, 0 }, { 0, -1 } }, { { 0, -1 }, { 1, 0 } } };
        const size_t faces = mesh.faceCount();
        const auto uvAt = [&](size_t c) { return std::array<double, 2> { values[2 * c], values[2 * c + 1] }; };
        const auto point = [&](size_t v) { return mesh.position(v); };
        const auto centre = [&](size_t f) { return (point(mesh.cornerVertex(3 * f)) + point(mesh.cornerVertex(3 * f + 1)) + point(mesh.cornerVertex(3 * f + 2))) / 3.0; };
        const size_t seedCount = std::min<size_t>(faces, 400);
        const size_t stride = std::max<size_t>(1, faces / std::max<size_t>(1, seedCount));
        const size_t maxSteps = (size_t)(8.0 * std::sqrt((double)faces)) + 50;
        std::vector<HolonomyRow> rows;
        std::vector<std::array<double, 2>> found;
        std::vector<double> support, lengths;
        size_t traces = 0, closures = 0, rotated = 0, rejected = 0, duplicates = 0, unreached = 0;
        std::vector<int> bfsMark(faces, -1);
        std::vector<size_t> bfsParent(faces, SurfaceMesh::npos);
        int bfsStamp = 0;
        for (size_t f0 = 0; f0 < faces; f0 += stride) {
            const double quad = std::max(frames[f0].su, frames[f0].sv);
            for (int k = 0; k < 2; ++k) {
                ++traces;
                double w0 = 0.0;
                for (int l = 0; l < 3; ++l)
                    w0 += uvAt(3 * f0 + l)[k] / 3.0;
                double A[2][2] = { { 1, 0 }, { 0, 1 } }, b[2] = { 0, 0 };
                const auto level = [&](size_t c) {
                    const auto uv = uvAt(c);
                    return A[k][0] * uv[0] + A[k][1] * uv[1] + b[k] - w0;
                };
                size_t c = SurfaceMesh::npos;
                for (int l = 0; l < 3 && c == SurfaceMesh::npos; ++l) {
                    const size_t cc = 3 * f0 + l;
                    if ((level(cc) > 0) != (level(mesh.nextCorner(cc)) > 0))
                        c = cc;
                }
                if (c == SurfaceMesh::npos)
                    continue;
                const Vector3 start = centre(f0);
                Vector3 previous = start;
                double travelled = 0.0, nearest = std::numeric_limits<double>::max();
                std::vector<size_t> path;
                bool done = false;
                for (size_t step = 0; step < maxSteps && !done; ++step) {
                    path.push_back(c);
                    const double la = level(c), lb = level(mesh.nextCorner(c));
                    const double s01 = la / (la - lb);
                    const Vector3 at = point(mesh.cornerVertex(c)) * (1.0 - s01) + point(mesh.cornerVertex(mesh.nextCorner(c))) * s01;
                    travelled += (at - previous).length();
                    previous = at;
                    const size_t oc = mesh.oppositeCorner(c);
                    if (oc == SurfaceMesh::npos)
                        break;
                    {
                        const int r = (rotation[c] % 4 + 4) % 4;
                        const double tu = values[uvVariables + 2 * c], tv = values[uvVariables + 2 * c + 1];
                        const double nb0 = A[0][0] * tu + A[0][1] * tv + b[0], nb1 = A[1][0] * tu + A[1][1] * tv + b[1];
                        b[0] = nb0;
                        b[1] = nb1;
                        double B[2][2];
                        for (int i = 0; i < 2; ++i)
                            for (int j = 0; j < 2; ++j)
                                B[i][j] = A[i][0] * M[r][0][j] + A[i][1] * M[r][1][j];
                        std::copy(&B[0][0], &B[0][0] + 4, &A[0][0]);
                    }
                    const size_t h = oc / 3;
                    const double distance = (at - start).length();
                    // Closest pass near the start after at least 4 quads:
                    // close the ring with a short dual path back to f0.
                    const double window = ratio * travelled + 2.0 * quad;
                    if (travelled <= 4.0 * quad || distance >= window)
                        nearest = std::numeric_limits<double>::max();
                    else if (distance <= nearest)
                        nearest = distance;
                    else {
                        ++closures;
                        ++bfsStamp;
                        const double radius = nearest + 3.0 * quad;
                        std::vector<size_t> queue { h };
                        bfsMark[h] = bfsStamp;
                        bfsParent[h] = SurfaceMesh::npos;
                        bool reached = h == f0;
                        for (size_t qi = 0; qi < queue.size() && !reached; ++qi) {
                            const size_t g = queue[qi];
                            for (int l = 0; l < 3 && !reached; ++l) {
                                const size_t oc2 = mesh.oppositeCorner(3 * g + l);
                                if (oc2 == SurfaceMesh::npos)
                                    continue;
                                const size_t n2 = oc2 / 3;
                                if (bfsMark[n2] == bfsStamp || (centre(n2) - start).length() > radius)
                                    continue;
                                bfsMark[n2] = bfsStamp;
                                bfsParent[n2] = 3 * g + l;
                                queue.push_back(n2);
                                if (n2 == f0)
                                    reached = true;
                            }
                        }
                        if (!reached) {
                            ++unreached;
                            if (std::getenv("AUTOREMESHER_RING_VERBOSE") && unreached < 4)
                                std::fprintf(stderr, "unreached: travelled %g nearest %g distance %g quad %g steps %zu\n", travelled, nearest, distance, quad, path.size());
                        }
                        if (reached) {
                            std::vector<size_t> back;
                            for (size_t g = f0; g != h;) {
                                const size_t pc = bfsParent[g];
                                back.push_back(pc);
                                g = pc / 3;
                            }
                            std::reverse(back.begin(), back.end());
                            std::vector<size_t> cycle = path;
                            cycle.insert(cycle.end(), back.begin(), back.end());
                            bool identity = false;
                            HolonomyRow rowU = holonomyRow(rotation, cycle, uvVariables, 0, &identity);
                            HolonomyRow rowV = holonomyRow(rotation, cycle, uvVariables, 1, &identity);
                            if (!identity) {
                                ++rotated;
                            } else {
                                double hol[2] = { 0, 0 };
                                for (const auto& e : rowU)
                                    hol[0] += e.second * values[e.first];
                                for (const auto& e : rowV)
                                    hol[1] += e.second * values[e.first];
                                const int cross = std::fabs(hol[0]) < std::fabs(hol[1]) ? 0 : 1;
                                const double along = std::fabs(hol[1 - cross]), shift = std::fabs(hol[cross]);
                                if (along < 6.0 || shift > ratio * along) {
                                    ++rejected;
                                } else {
                                    bool duplicate = false;
                                    for (size_t i = 0; i < found.size(); ++i)
                                        if (std::fabs(found[i][0] - along) < 1e-6 * (1.0 + along) && std::fabs(found[i][1] - shift) < 1e-6 * (1.0 + along)) {
                                            duplicate = true;
                                            support[i] += 1.0;
                                            lengths[i] = std::min(lengths[i], travelled);
                                        }
                                    if (duplicate) {
                                        ++duplicates;
                                    } else {
                                        found.push_back({ along, shift });
                                        support.push_back(1.0);
                                        lengths.push_back(travelled);
                                        rows.push_back(cross == 0 ? rowU : rowV);
                                    }
                                }
                            }
                        }
                        done = true;
                        break;
                    }
                    size_t next = SurfaceMesh::npos, count = 0;
                    for (int l = 0; l < 3; ++l) {
                        const size_t cc = 3 * h + l;
                        if (cc != oc && (level(cc) > 0) != (level(mesh.nextCorner(cc)) > 0)) {
                            next = cc;
                            ++count;
                        }
                    }
                    if (count != 1)
                        break;
                    c = next;
                }
            }
        }
        // Tube test: a ring around a tube is the iso-line class of a band
        // free of singularities about as long as the ring (support counts
        // the seeds whose isoline gave this ring, so support / seeds is
        // about the band's share of the area). Rings that wind between
        // singularities (meridians over caps, loops around a head) have
        // narrow bands and are dropped: constraining them overconstrains
        // the cone layout.
        double totalArea = 0.0;
        for (size_t f = 0; f < faces; ++f)
            totalArea += 0.5 * Vector3::crossProduct(mesh.edgeVector(3 * f), -mesh.edgeVector(3 * f + 2)).length();
        const double seeds = (double)((faces + stride - 1) / stride);
        std::vector<HolonomyRow> tubes;
        std::string list;
        for (size_t i = 0; i < rows.size(); ++i) {
            const double band = support[i] / seeds * totalArea;
            const bool tube = band >= beta * lengths[i] * lengths[i];
            list += std::to_string(found[i][0]).substr(0, 6) + "/" + std::to_string(found[i][1]).substr(0, 5) + "/b" + std::to_string(band / (lengths[i] * lengths[i])).substr(0, 5) + (tube ? "*" : "") + " ";
            if (tube)
                tubes.push_back(rows[i]);
        }
        rows.swap(tubes);
        record->set("ring_traces", (double)traces);
        record->set("ring_closures", (double)closures);
        record->set("ring_rotated", (double)rotated);
        record->set("ring_rejected", (double)rejected);
        record->set("ring_duplicates", (double)duplicates);
        record->set("rings", (double)rows.size());
        record->set("ring_unreached", (double)unreached);
        if (std::getenv("AUTOREMESHER_RING_VERBOSE"))
            std::fprintf(stderr, "rings: %s\n", list.c_str());
        return rows;
    }

    // Research probe (closed edge loops): AUTOREMESHER_RING_PLANES="nx ny nz d [fx fy fz fd];..."
    // cuts T with each plane (optionally keeping faces whose centroid c has
    // f.c > fd, or radius > fd when f = 0), walks the dual ring of cut faces
    // and adds the integer constraint "cross component of the ring's
    // translation holonomy = 0". The cross component is the chart axis the
    // ring crosses least in its first face.
    void addRingConstraints(const CoverContext& ctx, const std::vector<FaceFrame>& frames,
        size_t uvVariables, MixedIntegerLeastSquares* s, DiagnosticRecord* record)
    {
        const char* env = std::getenv("AUTOREMESHER_RING_PLANES");
        if (nullptr == env)
            return;
        const SurfaceMesh& mesh = ctx.mesh;
        std::string all(env);
        size_t added = 0, failed = 0;
        size_t start = 0;
        while (start < all.size()) {
            size_t end = all.find(';', start);
            if (end == std::string::npos)
                end = all.size();
            std::vector<double> a;
            {
                std::string part = all.substr(start, end - start);
                char* q = &part[0];
                char* e = nullptr;
                while (true) {
                    const double x = std::strtod(q, &e);
                    if (e == q)
                        break;
                    a.push_back(x);
                    q = e;
                }
            }
            start = end + 1;
            if (a.size() < 4)
                continue;
            const Vector3 n(a[0], a[1], a[2]);
            const double d = a[3];
            const bool filter = a.size() >= 8;
            const Vector3 fn = filter ? Vector3(a[4], a[5], a[6]) : Vector3();
            const double fd = filter ? a[7] : 0.0;
            const auto side = [&](size_t v) { return Vector3::dotProduct(mesh.position(v), n) - d > 0; };
            const auto crosses = [&](size_t c) { return side(mesh.cornerVertex(c)) != side(mesh.cornerVertex(mesh.nextCorner(c))); };
            const auto keep = [&](size_t f) {
                if (!filter)
                    return true;
                const Vector3 centre = (mesh.position(mesh.cornerVertex(3 * f)) + mesh.position(mesh.cornerVertex(3 * f + 1)) + mesh.position(mesh.cornerVertex(3 * f + 2))) / 3.0;
                if (fn.isZero())
                    return std::hypot(centre.x(), centre.y()) > fd;
                return Vector3::dotProduct(centre, fn) > fd;
            };
            size_t f0 = SurfaceMesh::npos, c0 = SurfaceMesh::npos;
            for (size_t f = 0; f < mesh.faceCount() && f0 == SurfaceMesh::npos; ++f)
                if (keep(f))
                    for (size_t l = 0; l < 3; ++l)
                        if (crosses(3 * f + l)) {
                            f0 = f;
                            c0 = 3 * f + l;
                            break;
                        }
            if (f0 == SurfaceMesh::npos) {
                ++failed;
                continue;
            }
            std::vector<size_t> path;
            size_t c = c0;
            bool ok = false;
            for (size_t step = 0; step < mesh.faceCount(); ++step) {
                path.push_back(c);
                const size_t oc = mesh.oppositeCorner(c);
                if (oc == SurfaceMesh::npos)
                    break;
                const size_t g = oc / 3;
                if (g == f0) {
                    ok = true;
                    break;
                }
                if (!keep(g))
                    break;
                size_t next = SurfaceMesh::npos, count = 0;
                for (size_t l = 0; l < 3; ++l)
                    if (3 * g + l != oc && crosses(3 * g + l)) {
                        next = 3 * g + l;
                        ++count;
                    }
                if (count != 1)
                    break;
                c = next;
            }
            if (!ok) {
                ++failed;
                continue;
            }
            // A maps the current face's chart into f0's chart; b = sum A t.
            int A[2][2] = { { 1, 0 }, { 0, 1 } };
            std::vector<std::pair<size_t, double>> rows[2];
            for (const size_t pc : path) {
                const size_t t = uvVariables + 2 * pc;
                for (int i = 0; i < 2; ++i) {
                    if (A[i][0])
                        rows[i].push_back({ t, (double)A[i][0] });
                    if (A[i][1])
                        rows[i].push_back({ t + 1, (double)A[i][1] });
                }
                const int r = (ctx.rotation[pc] % 4 + 4) % 4;
                static const int M[4][2][2] = { { { 1, 0 }, { 0, 1 } }, { { 0, 1 }, { -1, 0 } }, { { -1, 0 }, { 0, -1 } }, { { 0, -1 }, { 1, 0 } } };
                int B[2][2];
                for (int i = 0; i < 2; ++i)
                    for (int j = 0; j < 2; ++j)
                        B[i][j] = A[i][0] * M[r][0][j] + A[i][1] * M[r][1][j];
                std::copy(&B[0][0], &B[0][0] + 4, &A[0][0]);
            }
            if (!(A[0][0] == 1 && A[1][1] == 1 && A[0][1] == 0 && A[1][0] == 0)) {
                ++failed;
                continue;
            }
            // Ring direction in f0: the cut edge c0 is crossed, so the ring
            // runs along n x normal; the chart axis it runs along least is
            // the cross one.
            const Vector3 along = Vector3::crossProduct(n, ctx.normals[f0]);
            const double du = std::fabs(Vector3::dotProduct(along, frames[f0].u)) / frames[f0].su;
            const double dv = std::fabs(Vector3::dotProduct(along, frames[f0].v)) / frames[f0].sv;
            const int cross = du > dv ? 1 : 0;
            if (rows[cross].empty()) {
                ++failed;
                continue;
            }
            s->addConstraint(rows[cross]);
            ++added;
        }
        record->set("ring_constraints", (double)added);
        record->set("ring_constraints_failed", (double)failed);
    }

    // Research probe (closed edge loops): AUTOREMESHER_COVER_DUMP=<dir> writes
    // T, per-corner rotation, seam, uv and transitions of the continuous and
    // the final cover, one file per call.
    void dumpCover(const SurfaceMesh& mesh, const std::vector<int>& rotation, const std::vector<char>& seam,
        const std::vector<double>& continuous, const std::vector<double>& final_, size_t uvVariables,
        const std::vector<FaceFrame>& frames)
    {
        const char* dir = std::getenv("AUTOREMESHER_COVER_DUMP");
        if (nullptr == dir || continuous.empty())
            return;
        static int counter = 0;
        const std::string path = std::string(dir) + "/cover_" + std::to_string(counter++) + ".txt";
        FILE* f = std::fopen(path.c_str(), "w");
        if (nullptr == f)
            return;
        std::fprintf(f, "V %zu\n", mesh.vertexCount());
        for (size_t v = 0; v < mesh.vertexCount(); ++v) {
            const Vector3& p = mesh.position(v);
            std::fprintf(f, "%.17g %.17g %.17g\n", p.x(), p.y(), p.z());
        }
        std::fprintf(f, "C %zu\n", mesh.cornerCount());
        for (size_t c = 0; c < mesh.cornerCount(); ++c) {
            const size_t oc = mesh.oppositeCorner(c);
            std::fprintf(f, "%zu %lld %d %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n", mesh.cornerVertex(c),
                oc == SurfaceMesh::npos ? -1LL : (long long)oc, rotation[c], (int)seam[c],
                continuous[2 * c], continuous[2 * c + 1], continuous[uvVariables + 2 * c], continuous[uvVariables + 2 * c + 1],
                final_[2 * c], final_[2 * c + 1], final_[uvVariables + 2 * c], final_[uvVariables + 2 * c + 1]);
        }
        std::fprintf(f, "F %zu\n", frames.size());
        for (const FaceFrame& fr : frames)
            std::fprintf(f, "%.9g %.9g %.9g %.9g %.9g\n", fr.u.x(), fr.u.y(), fr.u.z(), fr.su, fr.sv);
        std::fclose(f);
    }

    // Appends "cover_system" and one "rounding_iteration" per pass to
    // `diagnostics`, and fills `result` with the residuals and map validity.
    bool solveQuadCover(const CoverContext& ctx, std::vector<double>* values,
        const ProgressHandler* progressHandler,
        std::vector<DiagnosticRecord>* diagnostics, DiagnosticRecord* result,
        const std::vector<HolonomyRow>* rings = nullptr, bool continuousOnly = false)
    {
        const SurfaceMesh& mesh = ctx.mesh;
        const std::vector<int>& rotation = ctx.rotation;
        const std::vector<char>& seam = ctx.seam;
        const std::vector<signed char>& cornerConstraints = ctx.cornerConstraints;
        const std::vector<double>* faceScaling = ctx.faceScaling;
        const double scale = ctx.scale;
        const size_t corners = mesh.cornerCount();
        const size_t uvVariables = 2 * corners;
        const size_t variables = 2 * uvVariables;
        const auto report = [progressHandler](float fraction, const char* name) {
            if (nullptr != progressHandler && *progressHandler)
                (*progressHandler)(fraction, name);
        };
        report(0.0f, "Building cover system");
        MixedIntegerLeastSquares s(variables);
        for (size_t t = 0; t < 2 * corners; ++t)
            s.setVariablePeriod(uvVariables + t, 2);
        const std::vector<FaceFrame> frames = computeFaceFrames(ctx);
        double expectedCells = 0.0;
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            const FaceFrame& frame = frames[f];
            const double faceArea = .5 * Vector3::crossProduct(mesh.edgeVector(3 * f), -mesh.edgeVector(3 * f + 2)).length();
            expectedCells += faceArea / (frame.su * frame.sv);
            for (size_t l = 0; l < 3; ++l) {
                size_t c = 3 * f + l, n = mesh.nextCorner(c);
                const Vector2 target = frame.map(mesh.edgeVector(c));
                const double weight = frame.su * frame.sv;
                s.addEnergy(2 * n, 1, 2 * c, -1, target.x(), weight);
                s.addEnergy(2 * n + 1, 1, 2 * c + 1, -1, target.y(), weight);
            }
        }
        s.addConstraint(0, 1);
        s.addConstraint(1, 1);
        size_t hardCoordinateCount = 0;
        for (size_t c = 0; c < corners; ++c) {
            size_t oc = mesh.oppositeCorner(c);
            if (oc == SurfaceMesh::npos)
                continue;
            const size_t tc = uvVariables + 2 * c;
            const size_t toc = uvVariables + 2 * oc;
            int r = rotation[c];
            if (seam[c]) {
                addRotationConstraints(s, tc, toc, r, 1.0);
            } else {
                s.addConstraint(tc, 1);
                s.addConstraint(tc + 1, 1);
            }
        }
        for (size_t c = 0; c < corners; ++c) {
            const size_t oc = mesh.oppositeCorner(c);
            if (oc == SurfaceMesh::npos)
                continue;
            const size_t other = mesh.nextCorner(oc);
            const size_t tc = uvVariables + 2 * c;
            const int r = (rotation[c] % 4 + 4) % 4;
            if (r == 0) {
                s.addConstraint(2 * c, 1, 2 * other, -1, tc, -1);
                s.addConstraint(2 * c + 1, 1, 2 * other + 1, -1, tc + 1, -1);
            } else if (r == 1) {
                s.addConstraint(2 * c, 1, 2 * other + 1, -1, tc, -1);
                s.addConstraint(2 * c + 1, 1, 2 * other, 1, tc + 1, -1);
            } else if (r == 2) {
                s.addConstraint(2 * c, 1, 2 * other, 1, tc, -1);
                s.addConstraint(2 * c + 1, 1, 2 * other + 1, 1, tc + 1, -1);
            } else {
                s.addConstraint(2 * c, 1, 2 * other + 1, 1, tc, -1);
                s.addConstraint(2 * c + 1, 1, 2 * other, -1, tc + 1, -1);
            }
        }
        for (size_t vertex = 0; vertex < mesh.vertexCount(); ++vertex) {
            const auto& incident = mesh.cornersAroundVertex(vertex);
            if (incident.empty())
                continue;
            size_t start = incident.front(), c = start;
            int accumulated = 0;
            bool closed = true;
            std::vector<std::pair<size_t, int>> wheel;
            do {
                if (mesh.oppositeCorner(c) == SurfaceMesh::npos) {
                    closed = false;
                    break;
                }
                wheel.push_back({ c, accumulated });
                accumulated = (accumulated + rotation[c]) % 4;
                c = mesh.nextCorner(mesh.oppositeCorner(c));
            } while (c != start && wheel.size() <= incident.size() + 1);
            if (!closed || c != start || accumulated != 0)
                continue;
            for (int coord = 0; coord < 2; ++coord) {
                std::vector<std::pair<size_t, double>> row;
                for (const auto& item : wheel) {
                    const size_t t = uvVariables + 2 * item.first;
                    const int r = item.second;
                    if (coord == 0) {
                        if (r == 0)
                            row.push_back({ t, 1 });
                        else if (r == 1)
                            row.push_back({ t + 1, 1 });
                        else if (r == 2)
                            row.push_back({ t, -1 });
                        else
                            row.push_back({ t + 1, -1 });
                    } else {
                        if (r == 0)
                            row.push_back({ t + 1, 1 });
                        else if (r == 1)
                            row.push_back({ t, -1 });
                        else if (r == 2)
                            row.push_back({ t + 1, -1 });
                        else
                            row.push_back({ t, 1 });
                    }
                }
                s.addConstraint(row);
            }
        }
        for (size_t c = 0; c < corners; ++c) {
            const size_t n = mesh.nextCorner(c);
            const int constraint = cornerConstraints[c];
            if (constraint == ConstraintV) {
                s.setVariablePeriod(2 * c + 1, 1);
                s.setVariablePeriod(2 * n + 1, 1);
                hardCoordinateCount += 2;
                s.addConstraint(2 * c + 1, 1, 2 * n + 1, -1);
            } else if (constraint == ConstraintU) {
                s.setVariablePeriod(2 * c, 1);
                s.setVariablePeriod(2 * n, 1);
                hardCoordinateCount += 2;
                s.addConstraint(2 * c, 1, 2 * n, -1);
            }
        }
        DiagnosticRecord ringRecord("ring_constraints");
        addRingConstraints(ctx, frames, uvVariables, &s, &ringRecord);
        if (nullptr != rings)
            for (const HolonomyRow& row : *rings)
                s.addConstraint(row);
        if (std::getenv("AUTOREMESHER_RING_PLANES"))
            diagnostics->push_back(std::move(ringRecord));
        report(0.2f, "Eliminating cover constraints");
        s.finalizeConstraints();
        {
            DiagnosticRecord record("cover_system");
            record.set("faces", (double)mesh.faceCount());
            record.set("variables", (double)variables);
            record.set("kernel_size", (double)s.kernelSize());
            record.set("original_integers", (double)s.originalIntegerVariableCount());
            record.set("kernel_integers", (double)s.integerKernelVariableCount());
            record.set("hard_coordinates", (double)hardCoordinateCount);
            record.set("dropped_constraints", (double)s.droppedConstraintCount());
            record.set("elimination_nonzeros", (double)s.eliminationNonZeros());
            record.set("elimination_over_budget", s.eliminationOverBudget() ? 1.0 : 0.0);
            diagnostics->push_back(std::move(record));
        }
        if (s.eliminationOverBudget())
            return false;
        // Rounding converges well short of the cap, so spread the fraction over
        // the passes it is expected to take and clamp instead of pacing it
        // against the cap and barely moving.
        const size_t maximumIterations = 100;
        const size_t expectedIterations = 1 + MixedIntegerLeastSquares::expectedRoundingPasses();
        size_t iterations = 0;
        std::vector<double> continuousValues;
        for (size_t iteration = 0; iteration < maximumIterations; ++iteration) {
            report(0.3f + 0.65f * std::min(1.0f, (float)iteration / expectedIterations),
                "Rounding cover to integers");
            const bool solved = s.solveIteration();
            ++iterations;
            const MixedIntegerLeastSquares::IterationStats& stats = s.lastIterationStats();
            DiagnosticRecord record("rounding_iteration");
            record.set("iteration", (double)stats.iteration);
            record.set("kernel_integers", (double)stats.integerVariables);
            record.set("fixed_before", (double)stats.fixedBefore);
            record.set("newly_fixed", (double)stats.newlyFixed);
            record.set("max_rounding_shift", stats.maximumRoundingShift);
            record.set("mean_rounding_shift", stats.meanRoundingShift);
            appendSolveStats(&record, "solve", stats.solve);
            diagnostics->push_back(std::move(record));
            result->set("iterations", (double)iterations);
            if (!solved)
                return false;
            if (0 == iteration) {
                result->set("continuous_rescale", matchCellCount(mesh, expectedCells, &s));
                if (std::getenv("AUTOREMESHER_COVER_DUMP")) {
                    continuousValues.resize(variables);
                    for (size_t i = 0; i < variables; ++i)
                        continuousValues[i] = s.value(i);
                }
                result->set("continuous_energy", s.energy());
                appendSizeFidelity(result, "continuous", mesh, s, faceScaling, scale);
                // Nothing is fixed in the first pass, so its solution is the
                // continuous relaxation: flips there are not caused by
                // rounding.
                const std::vector<double> uvs = cornerUvs(s, uvVariables);
                appendUvOrientation(result, "continuous", measureUvOrientation(uvs));
                appendWinding(result, "continuous", measureWinding(mesh, rotation, frames, uvs));
                if (continuousOnly) {
                    values->resize(variables);
                    for (size_t i = 0; i < variables; ++i)
                        (*values)[i] = s.value(i);
                    return true;
                }
            }
            if (s.converged())
                break;
        }
        // Nothing below solves again; the factorisation was the largest
        // thing alive during the untangling that follows.
        s.releaseSystem();
        report(0.96f, "Rounding cover to integers");
        size_t violatedConstraints = 0;
        const double constraintResidual = s.constraintResidual(1e-6, &violatedConstraints);
        result->set("converged", s.converged() ? 1.0 : 0.0);
        result->set("integrality_residual", s.integralityResidual());
        result->set("rounded_energy", s.energy());
        result->set("constraint_residual", constraintResidual);
        result->set("violated_constraints", (double)violatedConstraints);
        {
            const std::vector<double> uvs = cornerUvs(s, uvVariables);
            appendUvOrientation(result, "rounded", measureUvOrientation(uvs));
            appendSizeFidelity(result, "rounded", mesh, s, faceScaling, scale);
            result->set("rounded_cells", coverCellArea(mesh, s));
            appendWinding(result, "rounded", measureWinding(mesh, rotation, frames, uvs));
        }
        report(0.97f, "Untangling cover");
        {
            const auto untangleStart = std::chrono::steady_clock::now();
            untangleCover(mesh, frames, untangleRings, &s, result);
            result->set("untangle_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - untangleStart).count());
            const std::vector<double> uvs = cornerUvs(s, uvVariables);
            appendUvOrientation(result, "untangled", measureUvOrientation(uvs));
            appendSizeFidelity(result, "untangled", mesh, s, faceScaling, scale);
            result->set("untangled_cells", coverCellArea(mesh, s));
            appendWinding(result, "untangled", measureWinding(mesh, rotation, frames, uvs));
            result->set("untangled_energy", s.energy());
        }
        values->resize(variables);
        for (size_t i = 0; i < variables; ++i)
            (*values)[i] = s.value(i);
        dumpCover(mesh, rotation, seam, continuousValues, *values, uvVariables, frames);
        return s.converged();
    }

    void buildResultUv(const SurfaceMesh& mesh, const std::vector<int>& rotation,
        const std::vector<double>& allValues,
        size_t uvVariables, QuadParameterizer::Result* result, DiagnosticRecord* diagnostics)
    {
        const std::vector<double> unsnapped(allValues.begin(), allValues.begin() + uvVariables);
        std::vector<double> uv = unsnapped;
        for (double& coordinate : uv) {
            const double integer = std::round(coordinate);
            if (std::fabs(coordinate - integer) < 0.01)
                coordinate = integer;
        }
        // A snap moves a corner by up to 0.01, enough to invert a thin
        // triangle the map had right (untangling leaves some), though it
        // also rights some that were inverted by less. A vertex next to a
        // triangle the snap inverted keeps its unsnapped uv, at all its
        // corners so that the charts stay consistent, where that leaves
        // fewer of its triangles inverted; repeated until nothing changes,
        // which ends because each change lowers the count.
        {
            const auto bad = [&](size_t f) {
                const auto at = [&](size_t c) { return Vector2(uv[2 * c], uv[2 * c + 1]); };
                return Predicates::orient2d(at(3 * f), at(3 * f + 1), at(3 * f + 2)) <= 0;
            };
            const auto badUnsnapped = [&](size_t f) {
                const auto at = [&](size_t c) { return Vector2(unsnapped[2 * c], unsnapped[2 * c + 1]); };
                return Predicates::orient2d(at(3 * f), at(3 * f + 1), at(3 * f + 2)) <= 0;
            };
            const auto setVertex = [&](size_t vertex, const std::vector<double>& from, std::vector<double>* to) {
                for (size_t c : mesh.cornersAroundVertex(vertex)) {
                    (*to)[2 * c] = from[2 * c];
                    (*to)[2 * c + 1] = from[2 * c + 1];
                }
            };
            const auto badAround = [&](size_t vertex) {
                size_t count = 0;
                for (size_t c : mesh.cornersAroundVertex(vertex))
                    count += bad(mesh.cornerFace(c));
                return count;
            };
            std::vector<char> kept(mesh.vertexCount(), 0);
            const std::vector<double> snapped = uv;
            size_t keptCount = 0;
            for (bool changed = true; changed;) {
                changed = false;
                for (size_t f = 0; f < mesh.faceCount(); ++f) {
                    if (!bad(f) || badUnsnapped(f))
                        continue;
                    for (size_t l = 0; l < 3 && bad(f); ++l) {
                        const size_t vertex = mesh.cornerVertex(3 * f + l);
                        if (kept[vertex])
                            continue;
                        const size_t before = badAround(vertex);
                        setVertex(vertex, unsnapped, &uv);
                        if (badAround(vertex) < before) {
                            kept[vertex] = 1;
                            ++keptCount;
                            changed = true;
                        } else {
                            setVertex(vertex, snapped, &uv);
                        }
                    }
                }
            }
            diagnostics->set("snap_kept_vertices", (double)keptCount);
        }
        // The map quad extraction actually sees, after the snap above.
        appendUvOrientation(diagnostics, "snapped", measureUvOrientation(uv));
        result->singularVertices.clear();
        for (size_t vertex = 0; vertex < mesh.vertexCount(); ++vertex) {
            const auto& fan = mesh.cornersAroundVertex(vertex);
            int sum = 0;
            bool boundary = false;
            for (size_t c : fan) {
                sum = (sum + rotation[c]) % 4;
                if (mesh.oppositeCorner(c) == SurfaceMesh::npos)
                    boundary = true;
            }
            if (!boundary && sum != 0)
                result->singularVertices.push_back(vertex);
        }
        result->triangleUvs.assign(mesh.faceCount(), std::vector<Vector2>(3));
        for (size_t f = 0; f < mesh.faceCount(); ++f)
            for (size_t l = 0; l < 3; ++l) {
                size_t c = 3 * f + l;
                result->triangleUvs[f][l] = Vector2(uv[2 * c], uv[2 * c + 1]);
            }
    }

}

bool QuadParameterizer::parameterize(const std::vector<Vector3>& vertices,
    const std::vector<std::vector<size_t>>& triangles,
    const std::vector<Vector3>* guidance, double quadEdgeLength,
    const FeatureEdgeSet& hardEdges, Result* result,
    const std::vector<double>* faceScaling,
    const std::vector<double>* faceScalingU,
    const std::vector<double>* faceScalingV,
    const ProgressHandler* progressHandler,
    const SurfaceMesh* surfaceMesh)
{
    const auto report = [progressHandler](float fraction, const char* name) {
        if (nullptr != progressHandler && *progressHandler)
            (*progressHandler)(fraction, name);
    };

    if (vertices.empty() || triangles.empty() || !(quadEdgeLength > 0.0))
        return false;
    report(0.0f, "Initializing cover field");
    std::unique_ptr<SurfaceMesh> ownMesh;
    if (nullptr == surfaceMesh) {
        ownMesh.reset(new SurfaceMesh(vertices, triangles));
        surfaceMesh = ownMesh.get();
    }
    const SurfaceMesh& mesh = *surfaceMesh;
    if (mesh.faceCount() != triangles.size())
        return false;
    const size_t corners = mesh.cornerCount();
    const size_t uvVariables = 2 * corners;
    const std::vector<char> hardCorners = featureCornerFlags(mesh, hardEdges);
    const double scale = std::max(1e-12, quadEdgeLength);

    std::vector<Vector3> normals;
    initializeFieldAndNormals(mesh, guidance, &normals, &result->field);
    {
        // Faces where unit() had to fall back because a normal or a projected
        // guidance direction had (near) zero length.
        const bool hasGuidance = guidance && guidance->size() == mesh.faceCount();
        size_t zeroNormals = 0, zeroGuidance = 0;
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            const Vector3 normal = mesh.faceNormal(f);
            if (normal.length() < 1e-12)
                ++zeroNormals;
            if (hasGuidance) {
                const Vector3 projected = (*guidance)[f] - normals[f] * Vector3::dotProduct((*guidance)[f], normals[f]);
                if (projected.length() < 1e-12)
                    ++zeroGuidance;
            }
        }
        {
            DiagnosticRecord record("cover_input");
            record.set("faces", (double)mesh.faceCount());
            record.set("vertices", (double)mesh.vertexCount());
            record.set("scale", scale);
            record.set("zero_normal_faces", (double)zeroNormals);
            record.set("zero_guidance_faces", (double)zeroGuidance);
            result->diagnostics.push_back(std::move(record));
        }
    }
    const std::vector<Vector3> fieldBeforeBrush = result->field;

    std::vector<double> activeScalingU(mesh.faceCount(), 1.0), activeScalingV(mesh.faceCount(), 1.0);
    const bool trackDirectionalScale = faceScalingU && faceScalingV
        && faceScalingU->size() == mesh.faceCount() && faceScalingV->size() == mesh.faceCount();
    if (trackDirectionalScale) {
        activeScalingU = *faceScalingU;
        activeScalingV = *faceScalingV;
    }

    report(0.04f, "Smoothing cross field");
    if (!(guidance && guidance->size() == mesh.faceCount()))
        smoothCrossField(mesh, normals, hardCorners, &result->field);
    brushFieldAlongSpanningTree(mesh, normals, &result->field);

    report(0.14f, "Computing corner rotations");
    std::vector<int> rotation = computeCornerRotations(mesh, result->field, normals);
    std::vector<signed char> cornerConstraints = computeCornerConstraints(mesh, result->field,
        normals, hardCorners);
    std::vector<char> rerounded;
    const size_t reroundedEdges = raiseSmallConeAngles(mesh, result->field, normals, cornerConstraints,
        &rotation, &rerounded);
    result->diagnostics.push_back(creaseConstraintRecord(mesh, hardCorners, cornerConstraints, rerounded));
    // A hard edge's constraint follows the field, which a re-rounded edge
    // no longer does: on that edge the two faces could ask for different
    // isolines and collapse it.
    for (size_t c = 0; c < corners; ++c)
        if (rerounded[c])
            cornerConstraints[c] = ConstraintNone;
    result->cornerRotations = rotation;
    if (trackDirectionalScale)
        applyDirectionalSwaps(mesh, fieldBeforeBrush, result->field, normals,
            &activeScalingU, &activeScalingV);
    report(0.20f, "Correcting field curl");
    DiagnosticRecord curlDiagnostics("curl_correction");
    curlDiagnostics.set("rerounded_edges", (double)reroundedEdges);
    applyCurlCorrection(mesh, normals, rotation, cornerConstraints,
        faceScaling, scale, 1e-4, &activeScalingU, &activeScalingV, &result->field,
        &curlDiagnostics);
    result->diagnostics.push_back(std::move(curlDiagnostics));
    const std::vector<char> seam = computeSeam(mesh, rotation);

    const CoverContext ctx { mesh, result->field, normals, rotation, seam, cornerConstraints,
        activeScalingU, activeScalingV, faceScaling, scale };

    // The cover solve reports on its own 0..1, so remap it into the tail of this
    // function's range and keep the fractions monotonic end to end.
    ProgressHandler coverProgress;
    if (nullptr != progressHandler && *progressHandler) {
        coverProgress = [progressHandler](float fraction, const char* name) {
            (*progressHandler)(0.35f + 0.63f * fraction, name);
        };
    }
    std::vector<double> allValues;
    DiagnosticRecord coverResult("cover_result");
    std::vector<HolonomyRow> rings;
    if (const char* autoRings = std::getenv("AUTOREMESHER_AUTO_RINGS")) {
        std::vector<double> continuous;
        std::vector<DiagnosticRecord> scratch;
        DiagnosticRecord scratchResult("cover_probe");
        if (solveQuadCover(ctx, &continuous, nullptr, &scratch, &scratchResult, nullptr, true)) {
            DiagnosticRecord ringRecord("auto_rings");
            rings = detectRings(mesh, rotation, continuous, uvVariables, computeFaceFrames(ctx),
                std::atof(autoRings), std::getenv("AUTOREMESHER_RING_BETA") ? std::atof(std::getenv("AUTOREMESHER_RING_BETA")) : 0.5, &ringRecord);
            result->diagnostics.push_back(std::move(ringRecord));
        }
    }
    if (!solveQuadCover(ctx, &allValues, coverProgress ? &coverProgress : nullptr,
            &result->diagnostics, &coverResult, rings.empty() ? nullptr : &rings)) {
        coverResult.set("ok", 0.0);
        result->diagnostics.push_back(std::move(coverResult));
        return false;
    }

    report(0.99f, "Building cover uvs");
    buildResultUv(mesh, rotation, allValues, uvVariables, result, &coverResult);
    coverResult.set("ok", 1.0);
    result->diagnostics.push_back(std::move(coverResult));
    return true;
}
}
