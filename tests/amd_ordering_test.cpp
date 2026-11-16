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

// Eigen's AMD ordering (thirdparty/eigen, OrderingMethods/Amd.h) sums a node's
// element and neighbour indices into its supervariable hash.  In a
// StorageIndex (int) that sum passed 2^31 on the head sculpt's rounding system
// at 400,000 target quads (1.7M unknowns, three rows of 2,500-4,100 entries),
// wrapped negative and indexed the hash buckets out of bounds: 25 minutes in
// the ordering, then a segfault.  The patched header sums in 64 bits.
//
// The matrix here is a chain of 1.2M unknowns plus three hubs coupled to
// 2,500 unknowns each at the high end of the index range, so a hub's hash
// sums to about 3e9.  Unpatched, this ordering does not finish in minutes;
// patched it takes a fraction of a second, and the factor it gives must solve
// the system.

#include <Eigen/OrderingMethods>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    const int n = 1200000;
    const int hubs = 3;
    const int spokes = 2500;
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve((size_t)n * 3 + (size_t)hubs * spokes * 2);
    for (int i = 0; i < n; ++i) {
        triplets.emplace_back(i, i, 4.0);
        if (i + 1 < n) {
            triplets.emplace_back(i, i + 1, -1.0);
            triplets.emplace_back(i + 1, i, -1.0);
        }
    }
    for (int h = 0; h < hubs; ++h) {
        for (int s = 0; s < spokes; ++s) {
            const int j = n - 1 - s * hubs - h;
            triplets.emplace_back(h, j, -0.1);
            triplets.emplace_back(j, h, -0.1);
        }
    }
    Eigen::SparseMatrix<double> matrix(n, n);
    matrix.setFromTriplets(triplets.begin(), triplets.end());

    Eigen::AMDOrdering<int> ordering;
    Eigen::PermutationMatrix<Eigen::Dynamic, Eigen::Dynamic, int> permutation;
    ordering(matrix, permutation);
    if (permutation.size() != n) {
        std::fprintf(stderr, "FAIL: permutation has %ld entries, expected %d\n", (long)permutation.size(), n);
        return 1;
    }
    std::vector<char> seen((size_t)n, 0);
    for (int i = 0; i < n; ++i) {
        const int v = permutation.indices()[i];
        if (v < 0 || v >= n || seen[(size_t)v]) {
            std::fprintf(stderr, "FAIL: entry %d of the ordering is %d, not a permutation\n", i, v);
            return 1;
        }
        seen[(size_t)v] = 1;
    }

    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver(matrix);
    if (solver.info() != Eigen::Success) {
        std::fprintf(stderr, "FAIL: factorisation failed\n");
        return 1;
    }
    const Eigen::VectorXd rightHandSide = Eigen::VectorXd::Ones(n);
    const Eigen::VectorXd solution = solver.solve(rightHandSide);
    const double residual = (matrix * solution - rightHandSide).norm() / rightHandSide.norm();
    if (!(residual < 1e-10)) {
        std::fprintf(stderr, "FAIL: relative residual %g\n", residual);
        return 1;
    }
    std::printf("PASS: AMD ordering of %d unknowns with hash sums above 2^31, residual %g\n", n, residual);
    return 0;
}
