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

// Nested dissection ordering of the solver's large surface systems.
//
// The order must be a permutation that does not depend on thread timing, must
// fill the factor less than minimum degree on a surface (its only purpose),
// must survive degenerate positions and many small parts, and must leave the
// solution of ConstrainedLeastSquares unchanged up to roundoff.

#include <AutoRemesher/ConstrainedLeastSquares>
#include <AutoRemesher/NestedDissection>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

using AutoRemesher::Vector3;

int failures = 0;

void check(bool condition, const char* what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Triangles of a grid of cells x cells, on a cylinder of the given turns
// (0 = flat), plus `islands` separate single-cell islands far away. Faces
// that share an edge are adjacent. Three unknowns per face.
struct Surface {
    std::vector<Vector3> centroids;
    std::vector<std::pair<int, int>> adjacentFaces;
};

Surface makeSurface(int width, int height, int islands)
{
    Surface surface;
    const double pi = 3.14159265358979323846;
    auto face = [&](int i, int j, int t) { return 2 * (j * width + i) + t; };
    surface.centroids.resize(2 * width * height);
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            for (int t = 0; t < 2; ++t) {
                // A half cylinder, so the surface is curved but not closed.
                const double u = (i + (t ? 0.66 : 0.33)) / width * pi;
                surface.centroids[face(i, j, t)] = Vector3(std::cos(u), std::sin(u), (j + 0.5) / width * pi);
            }
            surface.adjacentFaces.push_back(std::make_pair(face(i, j, 0), face(i, j, 1)));
            if (i + 1 < width)
                surface.adjacentFaces.push_back(std::make_pair(face(i, j, 1), face(i + 1, j, 0)));
            if (j + 1 < height)
                surface.adjacentFaces.push_back(std::make_pair(face(i, j, 1), face(i, j + 1, 0)));
        }
    }
    for (int k = 0; k < islands; ++k) {
        const int first = (int)surface.centroids.size();
        surface.centroids.push_back(Vector3(10.0 + k, 0.0, 0.0));
        surface.centroids.push_back(Vector3(10.0 + k, 0.1, 0.0));
        surface.adjacentFaces.push_back(std::make_pair(first, first + 1));
    }
    return surface;
}

// A symmetric positive definite matrix with three unknowns per face, all
// coupled within a face and with the faces next to it.
Eigen::SparseMatrix<double> makeMatrix(const Surface& surface)
{
    const int faces = (int)surface.centroids.size();
    std::vector<Eigen::Triplet<double>> entries;
    std::vector<double> degree(faces, 0.0);
    for (const auto& pair : surface.adjacentFaces) {
        degree[pair.first] += 1.0;
        degree[pair.second] += 1.0;
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) {
                const double value = a == b ? -1.0 : -0.1;
                entries.emplace_back(3 * pair.first + a, 3 * pair.second + b, value);
                entries.emplace_back(3 * pair.second + b, 3 * pair.first + a, value);
            }
    }
    for (int f = 0; f < faces; ++f)
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                entries.emplace_back(3 * f + a, 3 * f + b, a == b ? 2.0 * degree[f] + 1.0 : 0.05);
    Eigen::SparseMatrix<double> matrix(3 * faces, 3 * faces);
    matrix.setFromTriplets(entries.begin(), entries.end());
    matrix.makeCompressed();
    return matrix;
}

std::vector<int> groupsOf(int columns)
{
    std::vector<int> groups(columns);
    for (int c = 0; c < columns; ++c)
        groups[c] = c / 3;
    return groups;
}

bool isPermutation(const std::vector<int>& order, int size)
{
    if ((int)order.size() != size)
        return false;
    std::vector<char> seen(size, 0);
    for (int column : order) {
        if (column < 0 || column >= size || seen[column])
            return false;
        seen[column] = 1;
    }
    return true;
}

// Non-zeros of L when the matrix is factorised in `order`, and the relative
// residual of a solve; -1 when the factorisation fails.
long factorNonZeros(const Eigen::SparseMatrix<double>& matrix, const std::vector<int>& order, double* residual)
{
    const int n = (int)matrix.cols();
    Eigen::PermutationMatrix<Eigen::Dynamic, Eigen::Dynamic, int> inverse(n);
    std::copy(order.begin(), order.end(), inverse.indices().data());
    const Eigen::PermutationMatrix<Eigen::Dynamic, Eigen::Dynamic, int> permutation = inverse.inverse();
    Eigen::SparseMatrix<double> permuted(n, n);
    permuted.selfadjointView<Eigen::Upper>() = matrix.selfadjointView<Eigen::Lower>().twistedBy(permutation);
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>, Eigen::Upper, Eigen::NaturalOrdering<int>> solver(permuted);
    if (solver.info() != Eigen::Success)
        return -1;
    const Eigen::VectorXd b = Eigen::VectorXd::LinSpaced(n, -1.0, 1.0);
    const Eigen::VectorXd x = inverse * solver.solve(permutation * b);
    *residual = (matrix * x - b).norm() / b.norm();
    return (long)solver.matrixL().nestedExpression().nonZeros();
}

long minimumDegreeNonZeros(const Eigen::SparseMatrix<double>& matrix)
{
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver(matrix);
    return solver.info() == Eigen::Success ? (long)solver.matrixL().nestedExpression().nonZeros() : -1;
}

std::vector<int> order(const Eigen::SparseMatrix<double>& matrix, const std::vector<Vector3>& positions)
{
    return AutoRemesher::nestedDissectionOrder((int)matrix.cols(), matrix.outerIndexPtr(), matrix.innerIndexPtr(),
        groupsOf((int)matrix.cols()), positions);
}

void testFillAndDeterminism()
{
    // 2 x 150 x 150 faces, 135k unknowns, with 50 two-face islands.
    const Surface surface = makeSurface(150, 150, 50);
    const Eigen::SparseMatrix<double> matrix = makeMatrix(surface);
    const std::vector<int> first = order(matrix, surface.centroids);
    check(isPermutation(first, (int)matrix.cols()), "surface: order is a permutation");
    if (!isPermutation(first, (int)matrix.cols()))
        return;
    check(order(matrix, surface.centroids) == first, "surface: same order on a second run");
    double residual = 1.0;
    const long dissected = factorNonZeros(matrix, first, &residual);
    const long minimumDegree = minimumDegreeNonZeros(matrix);
    std::printf("surface: nnz(L) nested dissection %ld, minimum degree %ld (%.3f), residual %.1e\n", dissected,
        minimumDegree, (double)dissected / minimumDegree, residual);
    check(dissected > 0 && dissected < minimumDegree, "surface: less fill than minimum degree");
    check(residual < 1e-10, "surface: the factor solves the system");
}

void testDegeneratePositions()
{
    const Surface surface = makeSurface(60, 60, 0);
    const Eigen::SparseMatrix<double> matrix = makeMatrix(surface);
    // All faces at one point, then all on one line: only the graph keys
    // are left to cut along.
    for (int line = 0; line < 2; ++line) {
        std::vector<Vector3> positions(surface.centroids.size());
        for (size_t f = 0; f < positions.size(); ++f)
            positions[f] = Vector3(line ? (double)(f % 7) : 0.0, 0.0, 0.0);
        const std::vector<int> result = order(matrix, positions);
        check(isPermutation(result, (int)matrix.cols()), "degenerate positions: order is a permutation");
        double residual = 1.0;
        if (isPermutation(result, (int)matrix.cols()))
            check(factorNonZeros(matrix, result, &residual) > 0 && residual < 1e-10,
                "degenerate positions: the factor solves the system");
    }
}

void testInvalidInput()
{
    const Surface surface = makeSurface(4, 4, 0);
    const Eigen::SparseMatrix<double> matrix = makeMatrix(surface);
    std::vector<int> groups = groupsOf((int)matrix.cols());
    groups[5] = (int)surface.centroids.size();
    check(AutoRemesher::nestedDissectionOrder((int)matrix.cols(), matrix.outerIndexPtr(), matrix.innerIndexPtr(),
              groups, surface.centroids)
              .empty(),
        "invalid group: no order");
}

// The solver orders a large system with locations by nested dissection and
// gets the same solution as without them.
void testSolver()
{
    // 2 x 1000 x 90 faces = 540k unknowns, above the solver's threshold.
    const Surface surface = makeSurface(1000, 90, 0);
    const size_t faces = surface.centroids.size();
    std::vector<double> solutions[2];
    for (int located = 0; located < 2; ++located) {
        AutoRemesher::ConstrainedLeastSquares system(3 * faces);
        if (located)
            system.setVariableLocations(surface.centroids, 3);
        for (const auto& pair : surface.adjacentFaces)
            for (size_t k = 0; k < 3; ++k)
                system.addEnergy({ { 3 * (size_t)pair.first + k, 1.0 }, { 3 * (size_t)pair.second + k, -1.0 } },
                    0.01 * (double)k, 1.0);
        for (size_t f = 0; f < faces; ++f) {
            system.addEnergy({ { 3 * f, 1.0 }, { 3 * f + 1, 0.5 } }, std::sin(0.001 * (double)f), 0.01);
            for (size_t k = 0; k < 3; ++k)
                system.addEnergy({ { 3 * f + k, 1.0 } }, 0.0, 0.01);
        }
        system.addConstraint({ { 0, 1.0 }, { 4, -1.0 } });
        check(system.solve(&solutions[located]), "solver: solves");
        check(system.lastSolveStats().nestedDissection == (1 == located),
            "solver: nested dissection exactly when locations are given");
    }
    double difference = 0.0, size = 0.0;
    if (solutions[0].size() == solutions[1].size())
        for (size_t i = 0; i < solutions[0].size(); ++i) {
            difference = std::max(difference, std::fabs(solutions[0][i] - solutions[1][i]));
            size = std::max(size, std::fabs(solutions[0][i]));
        }
    else
        difference = 1e30;
    std::printf("solver: largest difference %.1e (largest value %.1e)\n", difference, size);
    check(difference <= 1e-8 * (1.0 + size), "solver: same solution with and without locations");
}

}

int main()
{
    testFillAndDeterminism();
    testDegeneratePositions();
    testInvalidInput();
    testSolver();
    if (failures)
        std::printf("%d failure(s)\n", failures);
    else
        std::printf("all nested dissection tests passed\n");
    return failures ? 1 : 0;
}
