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
#include <AutoRemesher/SingularitySimplifier>

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <cstdio>
#include <iostream>
#include <memory>
#include <set>
#include <utility>

namespace AutoRemesher {
namespace {
    Vector3 unit(const Vector3& value, const Vector3& fallback)
    {
        return value.length() < 1e-12 ? fallback.normalized() : value.normalized();
    }
    struct Frame {
        Vector3 u;
        Vector3 v;
    };

    Frame frameForFace(const SurfaceMesh& mesh, size_t face)
    {
        const Vector3 normal = unit(mesh.faceNormal(face), Vector3(0, 0, 1));
        Vector3 u = mesh.edgeVector(3 * face);
        u = u - Vector3::dotProduct(u, normal) * normal;
        u = unit(u, std::fabs(normal.x()) < 0.9 ? Vector3(1, 0, 0) : Vector3(0, 1, 0));
        u = u - Vector3::dotProduct(u, normal) * normal;
        u = unit(u, Vector3(1, 0, 0));
        return { u, Vector3::crossProduct(normal, u) };
    }

    // PROBE (keep parts): does cancelling a pair of cones leave curvature
    // that the cones were there to take up?
    //
    // A cross field with cones of charge c_v (quarter turns) asks for a flat
    // metric with cone angle 2 pi - c_v pi/2 at v.  Scaling the surface by
    // exp(u) gets there when L u = b, b_v = K_v - c_v pi/2 (K: angle defect,
    // L: cotangent Laplacian, u = 0 on the boundary), and u' L u = b' L^+ b
    // measures how unevenly the map must scale (Ben-Chen, Gotsman and Bunin
    // 2008).  Removing the pair (p, c), (q, -c) adds c pi/2 (e_p - e_q) to b:
    //   dE = pi c (u_p - u_q) + (pi c / 2)^2 R_pq,
    // R_pq = (e_p - e_q)' L^+ (e_p - e_q).  A pair that is field noise on a
    // flat patch has u_p - u_q = -c pi/2 R_pq, so dE = -(pi c / 2)^2 R_pq;
    // a pair that takes up the surface's own curvature has u = 0 around it,
    // so dE = +(pi c / 2)^2 R_pq.  noiseShare() maps these to 1 and 0.
    class DistortionGuard {
    public:
        DistortionGuard(const SurfaceMesh& mesh)
            : m_mesh(mesh)
        {
            const size_t vertices = mesh.vertexCount();
            m_row.assign(vertices, -1);
            std::vector<char> boundary(vertices, 0);
            std::vector<char> used(vertices, 0);
            for (size_t c = 0; c < mesh.cornerCount(); ++c) {
                used[mesh.cornerVertex(c)] = 1;
                if (mesh.oppositeCorner(c) == SurfaceMesh::npos)
                    boundary[mesh.cornerVertex(c)] = boundary[mesh.cornerVertex(mesh.nextCorner(c))] = 1;
            }
            bool anyBoundary = false;
            for (size_t v = 0; v < vertices; ++v)
                anyBoundary = anyBoundary || (used[v] && boundary[v]);
            int rows = 0;
            bool pinned = anyBoundary;
            for (size_t v = 0; v < vertices; ++v) {
                if (!used[v] || boundary[v])
                    continue;
                if (!pinned) {
                    // A closed surface: u is fixed up to a constant.
                    pinned = true;
                    continue;
                }
                m_row[v] = rows++;
            }
            m_defect = Eigen::VectorXd::Constant(rows, 2.0 * M_PI);
            std::vector<Eigen::Triplet<double>> entries;
            entries.reserve(4 * mesh.cornerCount());
            for (size_t c = 0; c < mesh.cornerCount(); ++c) {
                const Vector3 a = mesh.edgeVector(c), b = -mesh.edgeVector(mesh.previousCorner(c));
                const double cross = Vector3::crossProduct(a, b).length(), dot = Vector3::dotProduct(a, b);
                const int v = m_row[mesh.cornerVertex(c)];
                if (v >= 0)
                    m_defect[v] -= std::atan2(cross, dot);
                // Half the cotangent, on the edge across the corner; kept
                // positive so that L stays positive definite.
                const double weight = 0.5 * std::max(1e-6, cross > 1e-300 ? dot / cross : 1e-6);
                const int i = m_row[mesh.cornerVertex(mesh.nextCorner(c))], j = m_row[mesh.cornerVertex(mesh.previousCorner(c))];
                if (i >= 0)
                    entries.emplace_back(i, i, weight);
                if (j >= 0)
                    entries.emplace_back(j, j, weight);
                if (i >= 0 && j >= 0) {
                    entries.emplace_back(i, j, -weight);
                    entries.emplace_back(j, i, -weight);
                }
            }
            if (0 == rows)
                return;
            Eigen::SparseMatrix<double> laplacian(rows, rows);
            laplacian.setFromTriplets(entries.begin(), entries.end());
            m_solver.compute(laplacian);
            m_ok = Eigen::Success == m_solver.info();
        }
        bool ok() const { return m_ok; }
        void update(const std::vector<int>& charges)
        {
            Eigen::VectorXd b = m_defect;
            for (size_t v = 0; v < charges.size(); ++v)
                if (m_row[v] >= 0)
                    b[m_row[v]] -= 0.5 * M_PI * charges[v];
            m_u = m_solver.solve(b);
        }
        // 1: the pair is noise (removing it relaxes the map by all the pair
        // costs); 0: it takes up the surface's curvature exactly; below 0:
        // the surface asks for more than the pair gives.
        double noiseShare(size_t p, size_t q, int chargeP) const
        {
            Eigen::VectorXd delta = Eigen::VectorXd::Zero(m_defect.size());
            if (m_row[p] >= 0)
                delta[m_row[p]] += 1.0;
            if (m_row[q] >= 0)
                delta[m_row[q]] -= 1.0;
            const Eigen::VectorXd w = m_solver.solve(delta);
            const auto at = [&](const Eigen::VectorXd& x, size_t v) { return m_row[v] >= 0 ? x[m_row[v]] : 0.0; };
            const double resistance = at(w, p) - at(w, q);
            if (!(resistance > 0.0))
                return 1.0;
            const double c = (double)chargeP;
            return -(at(m_u, p) - at(m_u, q)) / (0.5 * M_PI * c * resistance);
        }

    private:
        const SurfaceMesh& m_mesh;
        std::vector<int> m_row;
        Eigen::VectorXd m_defect, m_u;
        Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> m_solver;
        bool m_ok = false;
    };

    double angleInFrame(const Vector3& vector, const Frame& frame)
    {
        return std::atan2(Vector3::dotProduct(vector, frame.v), Vector3::dotProduct(vector, frame.u));
    }

    // Membership over mesh-sized indices with O(1) clear(): an index is a
    // member while its stamp equals the current epoch.
    class StampSet {
    public:
        explicit StampSet(size_t size)
            : m_stamp(size, 0)
        {
        }
        void clear()
        {
            if (++m_epoch == 0) {
                std::fill(m_stamp.begin(), m_stamp.end(), 0u);
                m_epoch = 1;
            }
        }
        bool contains(size_t index) const { return m_stamp[index] == m_epoch; }
        bool insert(size_t index)
        {
            if (contains(index))
                return false;
            m_stamp[index] = m_epoch;
            return true;
        }

    private:
        std::vector<uint32_t> m_stamp;
        uint32_t m_epoch = 1;
    };

    // Mesh-sized map with O(1) clear(); absent keys read as `T()`.
    template <typename T>
    class StampMap {
    public:
        explicit StampMap(size_t size)
            : m_keys(size)
            , m_values(size)
        {
        }
        void clear() { m_keys.clear(); }
        bool contains(size_t index) const { return m_keys.contains(index); }
        T get(size_t index) const { return m_keys.contains(index) ? m_values[index] : T(); }
        void set(size_t index, T value)
        {
            m_keys.insert(index);
            m_values[index] = value;
        }

    private:
        StampSet m_keys;
        std::vector<T> m_values;
    };

    // Faces within `radius` face-to-face steps of the seeds, in BFS order.
    void growBall(const SurfaceMesh& mesh, const std::vector<size_t>& seeds, size_t radius,
        StampMap<size_t>* distance, std::vector<size_t>* faces)
    {
        distance->clear();
        faces->clear();
        for (size_t f : seeds)
            if (!distance->contains(f)) {
                distance->set(f, 0);
                faces->push_back(f);
            }
        for (size_t head = 0; head < faces->size(); ++head) {
            const size_t f = (*faces)[head];
            const size_t d = distance->get(f);
            if (d >= radius)
                continue;
            for (size_t c = 3 * f; c < 3 * f + 3; ++c) {
                const size_t other = mesh.oppositeCorner(c);
                if (other == SurfaceMesh::npos)
                    continue;
                const size_t g = mesh.cornerFace(other);
                if (!distance->contains(g)) {
                    distance->set(g, d + 1);
                    faces->push_back(g);
                }
            }
        }
    }

    // One edge term of a free face's Gauss-Seidel update, gathered once per pair.
    struct Coupling {
        size_t face;
        double weight;
        double connection;
        int jump;
    };
}

struct SingularitySimplifier::Workspace {
    explicit Workspace(const SurfaceMesh& mesh)
        : firstDistance(mesh.faceCount())
        , secondDistance(mesh.faceCount())
        , inRegion(mesh.faceCount())
        , isFree(mesh.faceCount())
        , affected(mesh.vertexCount())
        , previous(mesh.vertexCount())
        , jump(mesh.cornerCount())
    {
    }
    StampMap<size_t> firstDistance, secondDistance;
    StampSet inRegion, isFree;
    StampSet affected;
    StampMap<size_t> previous;
    StampMap<int> jump;
    std::vector<size_t> firstBall, secondBall, region, freeFaces, affectedVertices, queue;
    std::vector<double> saved;
    std::vector<Coupling> couplings;
};

SingularitySimplifier::SingularitySimplifier(const SurfaceMesh& mesh,
    std::vector<Vector3>* faceField)
    : m_mesh(mesh)
    , m_field(faceField)
{
}

std::vector<int> SingularitySimplifier::vertexCharges() const
{
    std::vector<int> result(m_mesh.vertexCount(), 0);
    if (nullptr == m_field || m_field->size() != m_mesh.faceCount())
        return result;
    // Inside simplify() the field angles and connection are cached; only
    // pay for the per-face frames when they are missing.
    if (m_angles.size() == m_mesh.faceCount()) {
        for (size_t v = 0; v < m_mesh.vertexCount(); ++v)
            result[v] = vertexCharge(v);
        return result;
    }
    std::vector<Frame> frames;
    std::vector<double> fieldAngles;
    frames.reserve(m_mesh.faceCount());
    fieldAngles.reserve(m_mesh.faceCount());
    for (size_t f = 0; f < m_mesh.faceCount(); ++f) {
        frames.push_back(frameForFace(m_mesh, f));
        fieldAngles.push_back(angleInFrame((*m_field)[f], frames.back()));
    }
    const auto twist = [&](size_t c) {
        const size_t f = m_mesh.cornerFace(c), g = m_mesh.cornerFace(m_mesh.oppositeCorner(c));
        const Vector3 edge = m_mesh.edgeVector(c);
        return FrameField::quarterResidual(fieldAngles[g] - fieldAngles[f] - (angleInFrame(edge, frames[g]) - angleInFrame(edge, frames[f])));
    };
    for (size_t v = 0; v < m_mesh.vertexCount(); ++v) {
        int quarters = 4;
        if (FrameField::coneQuarters(m_mesh, v, twist, &quarters))
            result[v] = 4 - quarters;
    }
    return result;
}

int SingularitySimplifier::vertexCharge(size_t vertex) const
{
    const auto twist = [this](size_t c) {
        return FrameField::quarterResidual(m_angles[m_mesh.cornerFace(m_mesh.oppositeCorner(c))] - m_angles[m_mesh.cornerFace(c)] - m_connection[c]);
    };
    int quarters = 4;
    if (!FrameField::coneQuarters(m_mesh, vertex, twist, &quarters))
        return 0;
    return 4 - quarters;
}

size_t SingularitySimplifier::singularityCount() const
{
    size_t result = 0;
    for (int charge : vertexCharges())
        if (charge != 0)
            ++result;
    return result;
}

void SingularitySimplifier::buildFramesAndConnection()
{
    const size_t faces = m_mesh.faceCount(), corners = m_mesh.cornerCount();
    m_frameU.resize(faces);
    m_frameV.resize(faces);
    m_angles.resize(faces);
    for (size_t f = 0; f < faces; ++f) {
        const Frame frame = frameForFace(m_mesh, f);
        m_frameU[f] = frame.u;
        m_frameV[f] = frame.v;
        m_angles[f] = std::atan2(Vector3::dotProduct((*m_field)[f], frame.v),
            Vector3::dotProduct((*m_field)[f], frame.u));
    }
    m_connection.assign(corners, 0.0);
    m_sharpCorner.assign(corners, false);
    const std::vector<char> sharpCorners = nullptr != m_sharpEdges
        ? featureCornerFlags(m_mesh, *m_sharpEdges)
        : std::vector<char>(corners, 0);
    const std::vector<char> hardCorners = nullptr != m_sharpEdges
        ? featureCornerFlags(m_mesh, *m_sharpEdges, FeatureEdgeSet::Hard)
        : std::vector<char>(corners, 0);
    for (size_t c = 0; c < corners; ++c) {
        const size_t other = m_mesh.oppositeCorner(c);
        if (other == SurfaceMesh::npos)
            continue;
        const size_t f = m_mesh.cornerFace(c), g = m_mesh.cornerFace(other);
        const Vector3 edge = m_mesh.edgeVector(c);
        m_connection[c] = std::atan2(Vector3::dotProduct(edge, m_frameV[g]), Vector3::dotProduct(edge, m_frameU[g])) - std::atan2(Vector3::dotProduct(edge, m_frameV[f]), Vector3::dotProduct(edge, m_frameU[f]));
        // Concave sharp edges only (normalAngle > 0), as before the set:
        // freezing the convex ones too keeps cones from reaching the convex
        // corners they belong to (inferred), and measured worse (TODO 2.6:
        // cad set: SJ p1 0.61 -> 0.59, UV flips x1.3 summed).  Hard
        // (user-marked) edges both ways: they are often on a smooth surface,
        // where cones would cross them (TODO 5.4).
        m_sharpCorner[c] = 0 != hardCorners[c] || (0 != sharpCorners[c] && m_mesh.normalAngle(c) > 0.0);
    }
}

std::vector<size_t> SingularitySimplifier::facetsAroundVertex(size_t vertex) const
{
    const std::vector<size_t>& incident = m_mesh.cornersAroundVertex(vertex);
    if (incident.empty())
        return {};
    std::vector<size_t> result;
    size_t c = incident.front(), start = c;
    do {
        if (result.size() > m_mesh.cornerCount())
            return {};
        result.push_back(m_mesh.cornerFace(c));
        const size_t other = m_mesh.oppositeCorner(c);
        if (other == SurfaceMesh::npos)
            return {};
        c = m_mesh.nextCorner(other);
        if (m_mesh.cornerVertex(c) != vertex)
            return {};
    } while (c != start);
    return result;
}

size_t SingularitySimplifier::cornerAlongEdge(size_t from, size_t to) const
{
    for (const size_t c : m_mesh.cornersAroundVertex(from)) {
        if (m_mesh.cornerVertex(m_mesh.nextCorner(c)) == to && m_mesh.oppositeCorner(c) != SurfaceMesh::npos)
            return c;
    }
    return SurfaceMesh::npos;
}

std::vector<size_t> SingularitySimplifier::pathBetween(size_t first, size_t second, Workspace& work) const
{
    work.isFree.clear();
    for (const size_t f : work.freeFaces)
        work.isFree.insert(f);
    const auto usable = [&](size_t vertex) {
        const std::vector<size_t> fan = facetsAroundVertex(vertex);
        if (fan.empty())
            return false;
        for (const size_t f : fan)
            if (!work.inRegion.contains(f))
                return false;
        return true;
    };
    const auto crossable = [&](size_t corner) {
        const size_t other = m_mesh.oppositeCorner(corner);
        return other != SurfaceMesh::npos && work.isFree.contains(m_mesh.cornerFace(corner)) && work.isFree.contains(m_mesh.cornerFace(other));
    };
    // Breadth-first over vertices; `queue` keeps every visited vertex and
    // `head` is the front of the live queue.
    work.previous.clear();
    work.queue.clear();
    work.previous.set(first, first);
    work.queue.push_back(first);
    for (size_t head = 0; head < work.queue.size(); ++head) {
        const size_t v = work.queue[head];
        if (v == second) {
            std::vector<size_t> path;
            for (size_t at = second; at != first; at = work.previous.get(at))
                path.push_back(at);
            path.push_back(first);
            std::reverse(path.begin(), path.end());
            return path;
        }
        for (const size_t c : m_mesh.cornersAroundVertex(v)) {
            const size_t next = m_mesh.cornerVertex(m_mesh.nextCorner(c));
            if (work.previous.contains(next) || !crossable(c))
                continue;
            if (next != second && !usable(next))
                continue;
            work.previous.set(next, v);
            work.queue.push_back(next);
        }
    }
    return {};
}

// Everything here is bounded by the region (faces within hops + margin of
// both cones), not by the mesh: scratch comes from `work`, and charges are
// evaluated only at the vertices the region can change.
bool SingularitySimplifier::cancelPair(size_t first, size_t second, size_t hops, Workspace& work)
{
    const size_t radius = hops + m_regionMargin;
    const std::vector<size_t> firstFan = facetsAroundVertex(first), secondFan = facetsAroundVertex(second);
    if (firstFan.empty() || secondFan.empty()) {
        return false;
    }
    growBall(m_mesh, firstFan, radius, &work.firstDistance, &work.firstBall);
    growBall(m_mesh, secondFan, radius, &work.secondDistance, &work.secondBall);
    std::vector<size_t>& region = work.region;
    region.clear();
    for (size_t f : work.secondBall)
        if (work.firstDistance.contains(f))
            region.push_back(f);
    // Ascending face order: the smoothing below is Gauss-Seidel, so the
    // order is part of the result.
    std::sort(region.begin(), region.end());
    work.inRegion.clear();
    for (size_t f : region)
        work.inRegion.insert(f);
    if (region.empty()) {
        return false;
    }
    std::vector<size_t>& freeFaces = work.freeFaces;
    freeFaces.clear();
    for (size_t f : region) {
        bool frozen = false;
        for (size_t c = 3 * f; c < 3 * f + 3; ++c) {
            const size_t other = m_mesh.oppositeCorner(c);
            if (m_sharpCorner[c] || other == SurfaceMesh::npos || !work.inRegion.contains(m_mesh.cornerFace(other))) {
                frozen = true;
                break;
            }
        }
        if (!frozen)
            freeFaces.push_back(f);
    }
    if (freeFaces.empty()) {
        return false;
    }
    for (size_t v : { first, second })
        for (size_t f : facetsAroundVertex(v))
            if (!work.inRegion.contains(f)) {
                return false;
            }
    const std::vector<size_t> path = pathBetween(first, second, work);
    if (path.size() < 2) {
        return false;
    }
    // Only field angles of free region faces change, so only charges of
    // region vertices can.
    work.affected.clear();
    work.affectedVertices.clear();
    for (size_t f : region)
        for (size_t c = 3 * f; c < 3 * f + 3; ++c)
            if (work.affected.insert(m_mesh.cornerVertex(c)))
                work.affectedVertices.push_back(m_mesh.cornerVertex(c));
    const auto countAffected = [&]() {
        size_t count = 0;
        for (size_t v : work.affectedVertices)
            if (vertexCharge(v) != 0)
                ++count;
        return count;
    };
    // Charge 4 asks for no angle at all (FrameField::coneQuarters() 0),
    // which no map can follow: never move a cone onto a vertex that way.
    const auto leavesNoAngle = [&]() {
        for (size_t v : work.affectedVertices)
            if (vertexCharge(v) >= 4)
                return true;
        return false;
    };
    const size_t before = countAffected();
    std::vector<double>& saved = work.saved;
    saved.clear();
    for (size_t f : freeFaces)
        saved.push_back(m_angles[f]);

    StampMap<int>& jump = work.jump;
    jump.clear();
    for (const size_t f : region)
        for (size_t c = 3 * f; c < 3 * f + 3; ++c) {
            const size_t other = m_mesh.oppositeCorner(c);
            if (other == SurfaceMesh::npos)
                continue;
            jump.set(c, static_cast<int>(std::lround((m_angles[m_mesh.cornerFace(other)] - m_angles[f] - m_connection[c]) / (M_PI / 2.0))));
        }
    const int carried = vertexCharge(first);
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const size_t corner = cornerAlongEdge(path[i], path[i + 1]);
        if (corner == SurfaceMesh::npos) {
            return false;
        }
        const size_t other = m_mesh.oppositeCorner(corner);
        jump.set(corner, jump.get(corner) - carried);
        jump.set(other, jump.get(other) + carried);
    }

    // Free faces have all three neighbours (see the frozen test above), so
    // each contributes exactly three couplings, in corner order.
    std::vector<Coupling>& couplings = work.couplings;
    couplings.clear();
    for (size_t f : freeFaces)
        for (size_t c = 3 * f; c < 3 * f + 3; ++c)
            couplings.push_back({ m_mesh.cornerFace(m_mesh.oppositeCorner(c)),
                m_mesh.edgeVector(c).length(), m_connection[c], jump.get(c) });
    for (size_t iteration = 0; iteration < std::max<size_t>(256, 40 * radius * radius); ++iteration) {
        double maxChange = 0;
        const Coupling* coupling = couplings.data();
        for (size_t f : freeFaces) {
            double sum = 0, sw = 0;
            for (const Coupling* end = coupling + 3; coupling != end; ++coupling) {
                sum += coupling->weight * (m_angles[coupling->face] - coupling->connection - coupling->jump * (M_PI / 2.0));
                sw += coupling->weight;
            }
            if (sw > 0) {
                const double value = sum / sw;
                maxChange = std::max(maxChange, std::fabs(value - m_angles[f]));
                m_angles[f] = value;
            }
        }
        if (maxChange < 1e-10)
            break;
    }
    const size_t after = countAffected();
    if (vertexCharge(first) != 0 || vertexCharge(second) != 0 || after + 2 > before || leavesNoAngle()) {
        for (size_t i = 0; i < freeFaces.size(); ++i)
            m_angles[freeFaces[i]] = saved[i];
        return false;
    }
    for (size_t f : freeFaces)
        (*m_field)[f] = std::cos(m_angles[f]) * m_frameU[f] + std::sin(m_angles[f]) * m_frameV[f];
    return true;
}

void SingularitySimplifier::simplify()
{
    if (nullptr == m_field || m_field->empty())
        return;
    buildFramesAndConnection();
    m_cancelledPairCount = 0;
    const auto initial = vertexCharges();
    m_singularityCountBefore = 0;
    for (int i : initial)
        if (i)
            m_singularityCountBefore++;
    m_singularityCountAfter = m_singularityCountBefore;
    Workspace work(m_mesh);
    // PROBE (keep parts): AR_PROBE_CANCEL=<minimum noise share> guards the
    // cancellations (see DistortionGuard); AR_PROBE_CANCEL_FREE=<T edges>
    // lets pairs no farther apart than that cancel unguarded;
    // AR_PROBE_CANCEL_LOG=<file> lists every candidate.
    std::unique_ptr<DistortionGuard> guard;
    double minimumNoiseShare = 0.5, freeDistance = 0.0;
    const double averageEdge = m_mesh.averageEdgeLength();
    FILE* guardLog = nullptr;
    if (const char* probe = std::getenv("AR_PROBE_CANCEL")) {
        minimumNoiseShare = std::atof(probe);
        guard.reset(new DistortionGuard(m_mesh));
        if (!guard->ok())
            guard.reset();
        if (const char* free = std::getenv("AR_PROBE_CANCEL_FREE"))
            freeDistance = std::atof(free) * averageEdge;
        if (const char* log = std::getenv("AR_PROBE_CANCEL_LOG"))
            guardLog = std::fopen(log, "a");
    }
    // AR_PROBE_BAN=x,y,z,r: no cancellation with a cone inside that ball.
    Vector3 banCentre;
    double banRadius = 0.0;
    if (const char* probe = std::getenv("AR_PROBE_BAN")) {
        double x = 0, y = 0, z = 0, r = 0;
        if (4 == std::sscanf(probe, "%lf,%lf,%lf,%lf", &x, &y, &z, &r)) {
            banCentre = Vector3(x, y, z);
            banRadius = r;
        }
    }
    // Squared, so the test below is a comparison.
    const double protectedRadiusSquared = m_protectedRadius * m_protectedRadius;
    const auto isProtected = [&](size_t v) {
        if (nullptr == m_protectedPoints)
            return false;
        for (const Vector3& point : *m_protectedPoints)
            if ((m_mesh.position(v) - point).lengthSquared() < protectedRadiusSquared)
                return true;
        return false;
    };
    m_guardedPairCount = 0;
    for (size_t round = 0; round < m_maximumRounds; ++round) {
        const auto charges = vertexCharges();
        std::vector<size_t> singular;
        for (size_t v = 0; v < charges.size(); ++v)
            if (charges[v])
                singular.push_back(v);
        if (singular.size() < 2)
            break;
        std::vector<size_t> owner(m_mesh.faceCount(), SurfaceMesh::npos), distance(m_mesh.faceCount(), 0);
        std::deque<size_t> queue;
        std::vector<std::pair<size_t, size_t>> encounters;
        for (size_t i = 0; i < singular.size(); ++i)
            for (size_t f : facetsAroundVertex(singular[i])) {
                if (owner[f] == SurfaceMesh::npos) {
                    owner[f] = i;
                    queue.push_back(f);
                } else
                    encounters.push_back({ owner[f], i });
            }
        struct Candidate {
            size_t a, b, hops;
        };
        std::vector<Candidate> candidates;
        std::set<std::pair<size_t, size_t>> seen;
        auto consider = [&](size_t a, size_t b, size_t d) {if(a==b||d>m_maximumPairDistance||charges[singular[a]]+charges[singular[b]]!=0)return;auto key=std::minmax(a,b);if(seen.insert(key).second)candidates.push_back({key.first,key.second,d}); };
        for (const auto& e : encounters)
            consider(e.first, e.second, 0);
        while (!queue.empty()) {
            const size_t f = queue.front();
            queue.pop_front();
            for (size_t c = 3 * f; c < 3 * f + 3; ++c) {
                const size_t other = m_mesh.oppositeCorner(c);
                if (other == SurfaceMesh::npos)
                    continue;
                const size_t g = m_mesh.cornerFace(other);
                if (owner[g] == SurfaceMesh::npos) {
                    if (distance[f] + 1 > m_maximumPairDistance)
                        continue;
                    owner[g] = owner[f];
                    distance[g] = distance[f] + 1;
                    queue.push_back(g);
                } else
                    consider(owner[f], owner[g], distance[f] + distance[g]);
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) { return x.hops < y.hops; });
        std::vector<char> used(singular.size(), 0);
        size_t cancelled = 0;
        if (guard)
            guard->update(charges);
        for (const Candidate& c : candidates) {
            if (used[c.a] || used[c.b])
                continue;
            const size_t p = singular[c.a], q = singular[c.b];
            if (banRadius > 0.0 && ((m_mesh.position(p) - banCentre).length() < banRadius || (m_mesh.position(q) - banCentre).length() < banRadius)) {
                ++m_guardedPairCount;
                continue;
            }
            if (isProtected(p) || isProtected(q)) {
                ++m_guardedPairCount;
                continue;
            }
            if (guard) {
                const double distance = (m_mesh.position(p) - m_mesh.position(q)).length();
                const double share = guard->noiseShare(p, q, charges[p]);
                const bool allowed = distance <= freeDistance || share >= minimumNoiseShare;
                if (nullptr != guardLog)
                    std::fprintf(guardLog, "%zu %zu %zu %d %.6g %.6g %d\n", round, p, q, charges[p], distance / averageEdge, share, allowed ? 1 : 0);
                if (!allowed) {
                    ++m_guardedPairCount;
                    continue;
                }
            }
            if (cancelPair(p, q, c.hops, work)) {
                used[c.a] = used[c.b] = 1;
                ++cancelled;
                if (guard)
                    guard->update(vertexCharges());
            }
        }
        if (!cancelled)
            break;
        m_cancelledPairCount += cancelled;
    }
    m_singularityCountAfter = singularityCount();
    if (nullptr != guardLog)
        std::fclose(guardLog);
}
}
