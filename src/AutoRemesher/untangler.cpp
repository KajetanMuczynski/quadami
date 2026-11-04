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
#include <AutoRemesher/Untangler>

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace AutoRemesher {
namespace {
    // chi(D, eps) and its first two derivatives by D. For D < 0 the
    // difference D + sqrt(eps^2 + D^2) cancels, so it is written as
    // eps^2 / (sqrt(eps^2 + D^2) - D) there.
    struct Chi {
        double value, first, second;
    };

    Chi chi(double d, double eps)
    {
        const double root = std::sqrt(eps * eps + d * d);
        Chi c;
        c.value = d >= 0.0 ? .5 * (d + root) : .5 * eps * eps / (root - d);
        c.first = c.value / root;
        c.second = .5 * eps * eps / (root * root * root);
        return c;
    }

    double determinant(const double* j) { return j[0] * j[3] - j[1] * j[2]; }

    double triangleEnergy(const double* j, double lambda, double alignment, double eps)
    {
        const double d = determinant(j);
        const double frobenius = j[0] * j[0] + j[1] * j[1] + j[2] * j[2] + j[3] * j[3];
        const double offTarget = (j[0] - 1.0) * (j[0] - 1.0) + j[1] * j[1] + j[2] * j[2] + (j[3] - 1.0) * (j[3] - 1.0);
        return (frobenius + lambda * (d * d + 1.0)) / chi(d, eps).value + alignment * offTarget;
    }

    // Projects the Hessian of triangleEnergy onto the positive semidefinite
    // cone in closed form. The Hessian is a I + b C + terms in J and cof(J),
    // where C is the constant Hessian of det J. C is +1 on the similarity
    // matrices (p1, p2 below) and -1 on the anti-similarity ones (m1, m2), and
    // J and cof(J) span the same plane as J's two parts in those spaces. So in
    // the basis of those two parts and their in-space normals the Hessian is a
    // 2x2 block plus two diagonal entries, and only the block needs an
    // eigenvalue (Smith et al. 2019, "Analytic eigensystems for isotropic
    // distortion energies", for the same idea).
    void projectToPositiveSemidefinite(const double* j, Eigen::Matrix4d* hessian)
    {
        const double s = 0.70710678118654752440;
        // J = x p1 + y p2 + u m1 + v m2, with p1 = (1, 0, 0, 1) s,
        // p2 = (0, 1, -1, 0) s, m1 = (1, 0, 0, -1) s, m2 = (0, 1, 1, 0) s.
        double x = (j[0] + j[3]) * s, y = (j[1] - j[2]) * s;
        double u = (j[0] - j[3]) * s, v = (j[1] + j[2]) * s;
        const double r = std::hypot(x, y), q = std::hypot(u, v);
        if (r > 0.0) {
            x /= r;
            y /= r;
        } else {
            x = 1.0;
            y = 0.0;
        }
        if (q > 0.0) {
            u /= q;
            v /= q;
        } else {
            u = 1.0;
            v = 0.0;
        }
        Eigen::Matrix4d basis;
        basis.col(0) << s * x, s * y, -s * y, s * x;
        basis.col(1) << s * u, s * v, s * v, -s * u;
        basis.col(2) << -s * y, s * x, -s * x, -s * y;
        basis.col(3) << -s * v, s * u, s * u, s * v;
        const Eigen::Matrix4d rotated = basis.transpose() * (*hessian) * basis;
        const double p = rotated(0, 0), w = .5 * (rotated(0, 1) + rotated(1, 0)), t = rotated(1, 1);
        const double mean = .5 * (p + t), half = std::hypot(.5 * (p - t), w);
        const double larger = mean + half, smaller = mean - half;
        Eigen::Matrix4d clamped = Eigen::Matrix4d::Zero();
        if (smaller >= 0.0) {
            clamped(0, 0) = p;
            clamped(0, 1) = clamped(1, 0) = w;
            clamped(1, 1) = t;
        } else if (larger > 0.0) {
            // Keep the eigenvector of the larger eigenvalue only.
            Eigen::Vector2d e = p >= t ? Eigen::Vector2d(p - smaller, w) : Eigen::Vector2d(w, t - smaller);
            // normalize() would overflow to a zero vector past about 1e154.
            const double length2 = e.squaredNorm();
            if (std::isfinite(length2))
                e /= std::sqrt(length2);
            else
                e.stableNormalize();
            clamped.topLeftCorner<2, 2>() = larger * e * e.transpose();
        }
        clamped(2, 2) = std::max(rotated(2, 2), 0.0);
        clamped(3, 3) = std::max(rotated(3, 3), 0.0);
        *hessian = basis * clamped * basis.transpose();
    }

    // Gradient and Hessian of triangleEnergy by J (row-major), with the
    // Hessian projected onto the positive semidefinite cone.
    void triangleDerivatives(const double* j, double lambda, double alignment, double eps,
        Eigen::Vector4d* gradient, Eigen::Matrix4d* hessian)
    {
        const double d = determinant(j);
        const Eigen::Vector4d a(j[0], j[1], j[2], j[3]);
        const Eigen::Vector4d dd(j[3], -j[2], -j[1], j[0]);
        Eigen::Matrix4d ddd = Eigen::Matrix4d::Zero();
        ddd(0, 3) = ddd(3, 0) = 1.0;
        ddd(1, 2) = ddd(2, 1) = -1.0;
        const double numerator = a.squaredNorm() + lambda * (d * d + 1.0);
        const Eigen::Vector4d dn = 2.0 * a + 2.0 * lambda * d * dd;
        const Eigen::Matrix4d ddn = 2.0 * Eigen::Matrix4d::Identity() + 2.0 * lambda * (dd * dd.transpose() + d * ddd);
        const Chi c = chi(d, eps);
        const double inverse = 1.0 / c.value;
        const double ratio = c.first * inverse * inverse;
        *gradient = dn * inverse - numerator * ratio * dd;
        *hessian = ddn * inverse
            - ratio * (dn * dd.transpose() + dd * dn.transpose())
            + numerator * (2.0 * c.first * c.first * inverse * inverse * inverse - c.second * inverse * inverse) * dd * dd.transpose()
            - numerator * ratio * ddd;
        // A Hessian that already has a Cholesky factor is positive definite
        // and needs no projection.
        if (Eigen::Success != Eigen::LLT<Eigen::Matrix4d>(*hessian).info())
            projectToPositiveSemidefinite(j, hessian);
        if (alignment > 0.0) {
            *gradient += 2.0 * alignment * (a - Eigen::Vector4d(1.0, 0.0, 0.0, 1.0));
            *hessian += 2.0 * alignment * Eigen::Matrix4d::Identity();
        }
    }

    double secondsSince(const std::chrono::steady_clock::time_point& start)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
}

Untangler::Untangler(size_t variableCount)
    : Untangler(variableCount, Options())
{
}

Untangler::Untangler(size_t variableCount, const Options& options)
    : m_variableCount(variableCount)
    , m_options(options)
{
}

void Untangler::addTriangle(const std::array<Vector2, 3>& target, double weight,
    const std::array<Coordinate, 6>& corners)
{
    const Vector2 e1 = target[1] - target[0], e2 = target[2] - target[0];
    const double area = e1.x() * e2.y() - e1.y() * e2.x();
    if (!(area > 0.0) || !(weight > 0.0))
        return;
    // J = (corner1 - corner0, corner2 - corner0) S^-1 with S = (e1, e2), so
    // coordinate i of corner k enters row i of J with the row of S^-1 that
    // belongs to corner k (corner 0: minus the sum of the other two).
    const double inverse[2][2] = { { e2.y() / area, -e2.x() / area }, { -e1.y() / area, e1.x() / area } };
    const double cornerRow[3][2] = { { -inverse[0][0] - inverse[1][0], -inverse[0][1] - inverse[1][1] },
        { inverse[0][0], inverse[0][1] }, { inverse[1][0], inverse[1][1] } };
    Triangle triangle;
    triangle.firstVariable = m_variables.size();
    triangle.weight = weight;
    std::fill(triangle.constant, triangle.constant + 4, 0.0);
    for (const Coordinate& coordinate : corners)
        for (const auto& term : coordinate.terms)
            if (term.first < m_variableCount && term.second != 0.0)
                m_variables.push_back(term.first);
    std::sort(m_variables.begin() + triangle.firstVariable, m_variables.end());
    m_variables.erase(std::unique(m_variables.begin() + triangle.firstVariable, m_variables.end()), m_variables.end());
    triangle.variableCount = m_variables.size() - triangle.firstVariable;
    m_gradients.resize(4 * m_variables.size(), 0.0);
    for (size_t corner = 0; corner < 3; ++corner)
        for (size_t row = 0; row < 2; ++row) {
            const Coordinate& coordinate = corners[2 * corner + row];
            for (size_t column = 0; column < 2; ++column) {
                const double factor = cornerRow[corner][column];
                triangle.constant[2 * row + column] += factor * coordinate.constant;
                for (const auto& term : coordinate.terms) {
                    if (term.first >= m_variableCount || term.second == 0.0)
                        continue;
                    const size_t local = std::lower_bound(m_variables.begin() + triangle.firstVariable,
                                             m_variables.end(), term.first)
                        - m_variables.begin();
                    m_gradients[4 * local + 2 * row + column] += factor * term.second;
                }
            }
        }
    m_triangles.push_back(triangle);
}

void Untangler::jacobian(const Triangle& triangle, const std::vector<double>& x, double* j) const
{
    std::copy(triangle.constant, triangle.constant + 4, j);
    for (size_t k = triangle.firstVariable; k < triangle.firstVariable + triangle.variableCount; ++k) {
        const double value = x[m_variables[k]];
        const double* gradient = &m_gradients[4 * k];
        for (size_t entry = 0; entry < 4; ++entry)
            j[entry] += gradient[entry] * value;
    }
}

double Untangler::energy(const std::vector<double>& x, double eps, double* minimumDeterminant,
    size_t* inverted) const
{
    std::vector<double> energies(m_triangles.size()), determinants(m_triangles.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_triangles.size()), [&](const tbb::blocked_range<size_t>& range) {
        for (size_t t = range.begin(); t != range.end(); ++t) {
            double j[4];
            jacobian(m_triangles[t], x, j);
            determinants[t] = determinant(j);
            energies[t] = m_triangles[t].weight * triangleEnergy(j, m_options.lambda, m_options.alignment, eps);
        }
    });
    // Summed in order, so the result does not depend on the thread count.
    double total = 0.0, minimum = std::numeric_limits<double>::infinity();
    size_t count = 0;
    for (size_t t = 0; t < m_triangles.size(); ++t) {
        total += energies[t];
        minimum = std::min(minimum, determinants[t]);
        if (!(determinants[t] > 0.0))
            ++count;
    }
    if (nullptr != minimumDeterminant)
        *minimumDeterminant = minimum;
    if (nullptr != inverted)
        *inverted = count;
    return total;
}

bool Untangler::untangle(std::vector<double>* x, Stats* stats)
{
    const auto start = std::chrono::steady_clock::now();
    Stats local;
    Stats& s = nullptr != stats ? *stats : local;
    s = Stats();
    s.triangles = m_triangles.size();
    s.variables = m_variableCount;
    if (x->size() != m_variableCount || m_triangles.empty())
        return false;
    double minimumDeterminant = 0.0;
    energy(*x, 1.0, &minimumDeterminant, &s.invertedBefore);
    s.minimumDeterminantBefore = s.minimumDeterminantAfter = minimumDeterminant;
    s.invertedAfter = s.invertedBefore;
    if (0 == s.invertedBefore || 0 == m_variableCount) {
        s.converged = 0 == s.invertedBefore;
        s.seconds = secondsSince(start);
        return 0 == s.invertedBefore;
    }

    // The pattern of the Hessian is the union of the triangles' cliques of
    // variables: its own analysis, since a determinant couples u and v of a
    // corner, which the least-squares cover never does. Built and analysed
    // once; each Newton step only refactorises. Lower triangle only.
    typedef Eigen::SparseMatrix<double> Matrix;
    Matrix hessian(m_variableCount, m_variableCount);
    {
        std::vector<Eigen::Triplet<double>> pattern;
        for (const Triangle& triangle : m_triangles)
            for (size_t a = 0; a < triangle.variableCount; ++a)
                for (size_t b = 0; b <= a; ++b)
                    pattern.emplace_back((int)m_variables[triangle.firstVariable + a],
                        (int)m_variables[triangle.firstVariable + b], 0.0);
        for (size_t i = 0; i < m_variableCount; ++i)
            pattern.emplace_back((int)i, (int)i, 0.0);
        hessian.setFromTriplets(pattern.begin(), pattern.end());
        hessian.makeCompressed();
    }
    // Where each triangle's lower-triangle entries land in the values of the
    // compressed matrix; variables of a triangle are sorted, so local a >= b
    // means global row >= column.
    std::vector<size_t> slotOffset(m_triangles.size() + 1, 0);
    for (size_t t = 0; t < m_triangles.size(); ++t) {
        const size_t m = m_triangles[t].variableCount;
        slotOffset[t + 1] = slotOffset[t] + m * (m + 1) / 2;
    }
    const auto slotOf = [&](int row, int column) {
        const int* begin = hessian.innerIndexPtr() + hessian.outerIndexPtr()[column];
        const int* end = hessian.innerIndexPtr() + hessian.outerIndexPtr()[column + 1];
        return (size_t)(std::lower_bound(begin, end, row) - hessian.innerIndexPtr());
    };
    std::vector<size_t> slots(slotOffset.back());
    for (size_t t = 0; t < m_triangles.size(); ++t) {
        const Triangle& triangle = m_triangles[t];
        size_t k = slotOffset[t];
        for (size_t a = 0; a < triangle.variableCount; ++a)
            for (size_t b = 0; b <= a; ++b)
                slots[k++] = slotOf((int)m_variables[triangle.firstVariable + a], (int)m_variables[triangle.firstVariable + b]);
    }
    std::vector<size_t> diagonal(m_variableCount);
    for (size_t i = 0; i < m_variableCount; ++i)
        diagonal[i] = slotOf((int)i, (int)i);

    Eigen::SimplicialLDLT<Matrix, Eigen::Lower> solver;
    solver.analyzePattern(hessian);
    std::vector<double> localValues(slotOffset.back()), localGradients(m_variables.size());

    const double lambda = m_options.lambda;
    double eps = 1.0;
    double previousEnergy = std::numeric_limits<double>::infinity();
    // Where no inversion-free map exists eps stalls and the inverted
    // triangles move around, so the map with the fewest is kept, and the
    // loop ends once that has not improved for a while.
    const size_t patience = 10;
    std::vector<double> best = *x;
    size_t bestInverted = s.invertedBefore, sinceBest = 0;
    double bestDeterminant = s.minimumDeterminantBefore;
    std::vector<double> trial(m_variableCount);
    Eigen::VectorXd gradient(m_variableCount), step(m_variableCount);
    for (size_t iteration = 0; iteration < m_options.maximumIterations; ++iteration) {
        ++s.iterations;
        const double startEnergy = energy(*x, eps, nullptr, nullptr);
        double current = startEnergy;
        for (size_t newton = 0; newton < m_options.newtonStepsPerIteration; ++newton) {
            tbb::parallel_for(tbb::blocked_range<size_t>(0, m_triangles.size()), [&](const tbb::blocked_range<size_t>& range) {
                for (size_t t = range.begin(); t != range.end(); ++t) {
                    const Triangle& triangle = m_triangles[t];
                    double j[4];
                    jacobian(triangle, *x, j);
                    Eigen::Vector4d g;
                    Eigen::Matrix4d h;
                    triangleDerivatives(j, lambda, m_options.alignment, eps, &g, &h);
                    const Eigen::Map<const Eigen::Matrix<double, 4, Eigen::Dynamic>> dj(
                        &m_gradients[4 * triangle.firstVariable], 4, (Eigen::Index)triangle.variableCount);
                    const Eigen::VectorXd lg = triangle.weight * dj.transpose() * g;
                    const Eigen::MatrixXd lh = triangle.weight * dj.transpose() * h * dj;
                    size_t k = slotOffset[t];
                    for (size_t a = 0; a < triangle.variableCount; ++a) {
                        localGradients[triangle.firstVariable + a] = lg[(Eigen::Index)a];
                        for (size_t b = 0; b <= a; ++b)
                            localValues[k++] = lh((Eigen::Index)a, (Eigen::Index)b);
                    }
                }
            });
            // Scattered in order, so the sums do not depend on the threads.
            std::fill(hessian.valuePtr(), hessian.valuePtr() + hessian.nonZeros(), 0.0);
            gradient.setZero();
            for (size_t t = 0; t < m_triangles.size(); ++t) {
                const Triangle& triangle = m_triangles[t];
                for (size_t a = 0; a < triangle.variableCount; ++a)
                    gradient[(Eigen::Index)m_variables[triangle.firstVariable + a]] += localGradients[triangle.firstVariable + a];
                for (size_t k = slotOffset[t]; k < slotOffset[t + 1]; ++k)
                    hessian.valuePtr()[slots[k]] += localValues[k];
            }
            // The projected Hessian can be singular (a variable only in
            // triangles whose energy is flat along it), so add a small
            // multiple of the largest diagonal, more if LDL^T still fails.
            double largest = 0.0;
            for (size_t i = 0; i < m_variableCount; ++i)
                largest = std::max(largest, hessian.valuePtr()[diagonal[i]]);
            if (!(largest > 0.0) || !std::isfinite(largest))
                break;
            bool factorized = false;
            double ridge = 1e-10 * largest, added = 0.0;
            for (int attempt = 0; attempt < 6 && !factorized; ++attempt, ridge *= 100.0) {
                for (size_t i = 0; i < m_variableCount; ++i)
                    hessian.valuePtr()[diagonal[i]] += ridge - added;
                added = ridge;
                solver.factorize(hessian);
                ++s.factorizations;
                factorized = solver.info() == Eigen::Success && (solver.vectorD().array() > 0.0).all();
            }
            if (!factorized)
                break;
            step = solver.solve(-gradient);
            double slope = gradient.dot(step);
            if (!(slope < 0.0)) {
                step = -gradient;
                slope = -gradient.squaredNorm();
            }
            ++s.newtonSteps;
            // Backtracking with Armijo's condition; F is finite everywhere for
            // eps > 0, so any step can be tried.
            const double before = current;
            double alpha = 1.0;
            bool accepted = false;
            for (int halving = 0; halving < 40; ++halving, alpha *= .5) {
                for (size_t i = 0; i < m_variableCount; ++i)
                    trial[i] = (*x)[i] + alpha * step[(Eigen::Index)i];
                const double value = energy(trial, eps, nullptr, nullptr);
                if (value <= current + 1e-4 * alpha * slope) {
                    x->swap(trial);
                    accepted = value < current;
                    current = value;
                    break;
                }
            }
            if (!accepted || current > (1.0 - 1e-6) * before)
                break;
        }
        // The paper's update: sigma is how much the minimisation lowered the
        // energy, at least a tenth, and eps is chosen so that chi at the
        // smallest determinant shrinks by 1 - sigma.
        size_t inverted = 0;
        energy(*x, eps, &minimumDeterminant, &inverted);
        const double sigma = std::max(1.0 - current / startEnergy, 0.1);
        const double mu = (1.0 - sigma) * chi(minimumDeterminant, eps).value;
        eps = minimumDeterminant < mu ? 2.0 * std::sqrt(mu * (mu - minimumDeterminant)) : 1e-10;
        if (inverted <= bestInverted) {
            if (inverted < bestInverted)
                sinceBest = 0;
            bestInverted = inverted;
            bestDeterminant = minimumDeterminant;
            best = *x;
        }
        if (0 == inverted && current > (1.0 - 1e-3) * previousEnergy) {
            s.converged = true;
            break;
        }
        if (++sinceBest > patience && bestInverted > 0)
            break;
        previousEnergy = current;
    }
    x->swap(best);
    s.invertedAfter = bestInverted;
    s.minimumDeterminantAfter = bestDeterminant;
    s.seconds = secondsSince(start);
    return 0 == s.invertedAfter;
}

}
