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
#ifndef AUTO_REMESHER_NESTED_DISSECTION_H
#define AUTO_REMESHER_NESTED_DISSECTION_H

#include <AutoRemesher/Vector3>
#include <vector>

namespace AutoRemesher {

// Fill-reducing elimination order for a sparse symmetric system whose
// unknowns live on a surface: each unknown belongs to a group (a mesh face,
// say) with a position. Nested dissection on the graph of groups: split it
// into connected parts, cut each part in two with a small separator chosen
// from several geometric and graph-distance cuts (each turned into a minimum
// vertex cover and refined), order both halves recursively and the separator
// last. Small parts and separators are ordered with minimum degree.
//
// On the solver's large curl systems this fills the Cholesky factor 10-30 %
// less than minimum degree alone, but not on every surface system and not
// on degenerate positions (all on a line, say), so a caller compares both
// with estimateFactor below. Non-finite positions give no order.
//
// columnStart/rows: compressed columns of the matrix pattern, both triangles
// (the diagonal may be present). groupOfColumn: the group of each column,
// an index into groupPositions. Returns order, where order[k] is the column
// eliminated k-th (Eigen's inverse permutation), or an empty vector when the
// input is inconsistent. The result depends only on the input, not on the
// number of threads.
std::vector<int> nestedDissectionOrder(int columns, const int* columnStart, const int* rows,
    const std::vector<int>& groupOfColumn, const std::vector<Vector3>& groupPositions);

// What factorising a matrix in `order` (as above) would cost, from its
// pattern alone: the non-zeros of L below the diagonal and the
// multiply-adds of the factorisation (the sum of each column's count
// squared). Takes about as long as walking L once.
struct FactorEstimate {
    double nonZeros = 0.0;
    double work = 0.0;
};
FactorEstimate estimateFactor(int columns, const int* columnStart, const int* rows, const std::vector<int>& order);

}

#endif
