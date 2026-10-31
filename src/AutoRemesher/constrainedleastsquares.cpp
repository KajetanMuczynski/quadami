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

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseLU>
#include <Eigen/SparseQR>
#if AUTO_REMESHER_USE_ACCELERATE
#include <Eigen/AccelerateSupport>
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace AutoRemesher {
namespace {
    const size_t noIndex = std::numeric_limits<size_t>::max();

    // The regularizer nudges a rank-deficient normal matrix back to something
    // factorizable, so it only has meaning relative to that matrix.  Callers
    // weight their energies in whatever units they measure -- the quad cover
    // weights each corner equation by the area of one quad, for instance -- so
    // an absolute ridge is negligible on one model and dominant on another that
    // is a hundred times smaller in world units.  It is applied as a fraction of
    // the mean diagonal instead, which keeps a solve's outcome independent of
    // the units the input mesh happens to be modelled in.
    const double relativeRidge = 1e-10;

    // A normal matrix whose product would take more than this many multiply-adds
    // (the sum of the squared row lengths, which also bounds its non-zeros) is
    // not formed and the solve fails, so that Eigen's int indices cannot
    // overflow. The bound is loose for memory: rows of one dense system share
    // most of their columns, and its normal matrix held 1/60 of the bound
    // (ext_watch at 3000 quads), so it is no per-unknown fill limit; the
    // elimination's fill budget in MixedIntegerLeastSquares is.
    const size_t maximumNormalNonZeros = (size_t)std::numeric_limits<int>::max() / 2;

    typedef std::chrono::steady_clock Clock;

    double secondsSince(const Clock::time_point& start)
    {
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    size_t countNonFinite(const Eigen::VectorXd& values)
    {
        size_t count = 0;
        for (Eigen::Index i = 0; i < values.size(); ++i)
            if (!std::isfinite(values[i]))
                ++count;
        return count;
    }

    double ridgeFor(const Eigen::SparseMatrix<double>& normalMatrix)
    {
        const Eigen::Index size = normalMatrix.rows();
        if (0 == size)
            return relativeRidge;
        const double mean = normalMatrix.diagonal().sum() / (double)size;
        // An all-zero diagonal leaves nothing to be relative to; any positive
        // ridge does the same job there.
        return mean > 0.0 ? relativeRidge * mean : relativeRidge;
    }
}

struct ConstrainedLeastSquares::Cache {
    Eigen::SparseMatrix<double> scaledMatrix;
    Eigen::SparseMatrix<double> normalMatrix;
    Eigen::VectorXd rowShift;
    Eigen::VectorXd rowWeightRoot;
    // Held free unknowns (fixVariables): flag and value per column, and the
    // normal matrix with their rows and columns reduced to the diagonal.
    // Same pattern as normalMatrix, explicit zeros included, so the symbolic
    // analysis carries over.
    std::vector<char> fixedColumn;
    Eigen::VectorXd fixedValue;
    std::vector<size_t> fixedColumns;
    Eigen::SparseMatrix<double> fixedMatrix;
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
#if AUTO_REMESHER_USE_ACCELERATE
    Eigen::AccelerateLLT<Eigen::SparseMatrix<double>, Eigen::Lower> acceleratedSolver;
    bool useAccelerated = true;
#endif
    bool patternAnalyzed = false;
    bool factorized = false;
    size_t factorNonZeros = 0;
    size_t nonFiniteValues = 0;

    // The matrix the solver factorises: normalMatrix unless unknowns are held.
    const Eigen::SparseMatrix<double>& systemMatrix()
    {
        if (fixedColumns.empty())
            return normalMatrix;
        fixedMatrix = normalMatrix;
        for (Eigen::Index column = 0; column < fixedMatrix.outerSize(); ++column)
            for (Eigen::SparseMatrix<double>::InnerIterator it(fixedMatrix, column); it; ++it)
                if (fixedColumn[(size_t)it.row()] || fixedColumn[(size_t)column])
                    it.valueRef() = it.row() == column ? it.value() : 0.0;
        return fixedMatrix;
    }

    // Moves the held unknowns to the right-hand side: their equations become
    // diagonal * value = diagonal * value.
    void applyFixedColumns(Eigen::VectorXd* rightHandSide) const
    {
        for (size_t column : fixedColumns) {
            const double value = fixedValue[(Eigen::Index)column];
            for (Eigen::SparseMatrix<double>::InnerIterator it(normalMatrix, (Eigen::Index)column); it; ++it) {
                if (it.row() == (Eigen::Index)column)
                    (*rightHandSide)[it.row()] = it.value() * value;
                else if (!fixedColumn[(size_t)it.row()])
                    (*rightHandSide)[it.row()] -= it.value() * value;
            }
        }
    }

    bool factorize(const Eigen::SparseMatrix<double>& matrix)
    {
#if AUTO_REMESHER_USE_ACCELERATE
        if (useAccelerated) {
            if (!patternAnalyzed)
                acceleratedSolver.analyzePattern(matrix);
            acceleratedSolver.factorize(matrix);
            if (acceleratedSolver.info() == Eigen::Success) {
                patternAnalyzed = true;
                factorNonZeros = 0;
                return true;
            }
            useAccelerated = false;
            patternAnalyzed = false;
        }
#endif
        if (!patternAnalyzed)
            solver.analyzePattern(matrix);
        solver.factorize(matrix);
        if (solver.info() != Eigen::Success)
            return false;
        patternAnalyzed = true;
        factorNonZeros = (size_t)solver.matrixL().nestedExpression().nonZeros();
        return true;
    }

    Eigen::VectorXd solve(const Eigen::VectorXd& rightHandSide, bool* succeeded)
    {
#if AUTO_REMESHER_USE_ACCELERATE
        if (useAccelerated) {
            const Eigen::VectorXd solution = acceleratedSolver.solve(rightHandSide);
            nonFiniteValues = countNonFinite(solution);
            *succeeded = acceleratedSolver.info() == Eigen::Success && 0 == nonFiniteValues;
            return solution;
        }
#endif
        const Eigen::VectorXd solution = solver.solve(rightHandSide);
        nonFiniteValues = countNonFinite(solution);
        *succeeded = solver.info() == Eigen::Success && 0 == nonFiniteValues;
        return solution;
    }
};

ConstrainedLeastSquares::ConstrainedLeastSquares(size_t variableCount)
    : m_variableCount(variableCount)
    , m_cache(new Cache)
{
}

ConstrainedLeastSquares::~ConstrainedLeastSquares() = default;

size_t ConstrainedLeastSquares::addEnergy(const std::vector<std::pair<size_t, double>>& coefficients,
    double rightHandSide, double weight)
{
    if (coefficients.empty() || weight <= 0.0)
        return noIndex;
    m_energyCoefficients.insert(m_energyCoefficients.end(), coefficients.begin(), coefficients.end());
    m_energyStart.push_back(m_energyCoefficients.size());
    m_energyRightHandSides.push_back(rightHandSide);
    m_energyWeights.push_back(weight);
    m_matrixDirty = true;
    return energyCount() - 1;
}

void ConstrainedLeastSquares::setEnergyRightHandSide(size_t energyIndex, double rightHandSide)
{
    if (energyIndex >= energyCount())
        return;
    m_energyRightHandSides[energyIndex] = rightHandSide;
}

void ConstrainedLeastSquares::addConstraint(const std::vector<std::pair<size_t, double>>& coefficients,
    double rightHandSide)
{
    if (coefficients.empty())
        return;
    m_constraintEquations.push_back({ coefficients, rightHandSide, 1.0 });
    m_matrixDirty = true;
}

void ConstrainedLeastSquares::clearConstraints()
{
    if (m_constraintEquations.empty())
        return;
    m_constraintEquations.clear();
    m_matrixDirty = true;
}

void ConstrainedLeastSquares::fixVariables(const std::vector<std::pair<size_t, double>>& values)
{
    if (values.empty() && m_fixedVariables.empty())
        return;
    m_fixedVariables = values;
    m_fixedDirty = true;
}

ConstrainedLeastSquares::Substitution ConstrainedLeastSquares::resolve(size_t variable)
{
    double scale = 1.0, offset = 0.0;
    size_t cursor = variable;
    while (!m_substitutions[cursor].fixed && m_substitutions[cursor].root != cursor) {
        const Substitution& link = m_substitutions[cursor];
        offset += scale * link.offset;
        scale *= link.scale;
        cursor = link.root;
    }
    if (m_substitutions[cursor].fixed)
        m_substitutions[variable] = { variable, 1.0, offset + scale * m_substitutions[cursor].offset, true };
    else
        m_substitutions[variable] = { cursor, scale, offset, false };
    return m_substitutions[variable];
}

bool ConstrainedLeastSquares::buildSubstitutions()
{
    m_substitutions.resize(m_variableCount);
    for (size_t i = 0; i < m_variableCount; ++i)
        m_substitutions[i] = { i, 1.0, 0.0, false };

    std::vector<std::pair<size_t, double>> rootCoefficients;
    for (const LinearEquation& constraint : m_constraintEquations) {
        double rightHandSide = constraint.rightHandSide;
        rootCoefficients.clear();
        for (const auto& coefficient : constraint.coefficients) {
            if (coefficient.first >= m_variableCount)
                return false;
            const Substitution substitution = resolve(coefficient.first);
            if (substitution.fixed) {
                rightHandSide -= coefficient.second * substitution.offset;
                continue;
            }
            rightHandSide -= coefficient.second * substitution.offset;
            const double scaled = coefficient.second * substitution.scale;
            bool merged = false;
            for (auto& existing : rootCoefficients) {
                if (existing.first == substitution.root) {
                    existing.second += scaled;
                    merged = true;
                    break;
                }
            }
            if (!merged)
                rootCoefficients.push_back({ substitution.root, scaled });
        }
        rootCoefficients.erase(std::remove_if(rootCoefficients.begin(), rootCoefficients.end(),
                                   [](const std::pair<size_t, double>& c) { return std::fabs(c.second) < 1e-12; }),
            rootCoefficients.end());
        if (rootCoefficients.empty()) {
            if (std::fabs(rightHandSide) > 1e-9 * (1.0 + std::fabs(constraint.rightHandSide)))
                return false;
            continue;
        }
        if (1 == rootCoefficients.size()) {
            const size_t root = rootCoefficients[0].first;
            m_substitutions[root] = { root, 1.0, rightHandSide / rootCoefficients[0].second, true };
        } else if (2 == rootCoefficients.size()) {
            const size_t a = rootCoefficients[0].first, b = rootCoefficients[1].first;
            m_substitutions[a] = { b, -rootCoefficients[1].second / rootCoefficients[0].second,
                rightHandSide / rootCoefficients[0].second, false };
        } else {
            return false;
        }
    }

    m_freeIndexOfRoot.assign(m_variableCount, noIndex);
    m_freeCount = 0;
    for (size_t i = 0; i < m_variableCount; ++i) {
        const Substitution substitution = resolve(i);
        if (substitution.fixed)
            continue;
        if (noIndex == m_freeIndexOfRoot[substitution.root])
            m_freeIndexOfRoot[substitution.root] = m_freeCount++;
    }
    return true;
}

bool ConstrainedLeastSquares::buildReducedSystem()
{
    const Eigen::Index rowCount = static_cast<Eigen::Index>(energyCount());
    const Eigen::Index columnCount = static_cast<Eigen::Index>(m_freeCount);
    Cache& cache = *m_cache;

    // The rows are complete once the system is built: drop the slack their
    // growth left before the matrices are allocated next to them.
    m_energyCoefficients.shrink_to_fit();
    m_energyStart.shrink_to_fit();
    m_energyRightHandSides.shrink_to_fit();
    m_energyWeights.shrink_to_fit();

    std::vector<Eigen::Triplet<double>> entries;
    size_t normalBound = 0;
    for (size_t row = 0; row < energyCount(); ++row) {
        const size_t length = m_energyStart[row + 1] - m_energyStart[row];
        normalBound += length * length;
    }
    m_lastSolveStats.normalNonZerosBound = normalBound;
    if (normalBound > maximumNormalNonZeros)
        return false;
    entries.reserve(m_energyCoefficients.size());

    cache.rowShift = Eigen::VectorXd::Zero(rowCount);
    cache.rowWeightRoot = Eigen::VectorXd::Zero(rowCount);
    for (Eigen::Index row = 0; row < rowCount; ++row) {
        const double weightRoot = std::sqrt(m_energyWeights[static_cast<size_t>(row)]);
        cache.rowWeightRoot[row] = weightRoot;
        double shift = 0.0;
        for (size_t k = m_energyStart[static_cast<size_t>(row)]; k < m_energyStart[static_cast<size_t>(row) + 1]; ++k) {
            const auto& coefficient = m_energyCoefficients[k];
            if (coefficient.first >= m_variableCount)
                return false;
            const Substitution& substitution = m_substitutions[coefficient.first];
            if (substitution.fixed) {
                shift += coefficient.second * substitution.offset;
                continue;
            }
            shift += coefficient.second * substitution.offset;
            const size_t column = m_freeIndexOfRoot[substitution.root];
            if (noIndex == column)
                continue;
            entries.emplace_back(row, static_cast<Eigen::Index>(column),
                weightRoot * coefficient.second * substitution.scale);
        }
        cache.rowShift[row] = shift;
    }

    cache.scaledMatrix.resize(rowCount, columnCount);
    cache.scaledMatrix.setFromTriplets(entries.begin(), entries.end());

    cache.normalMatrix = (cache.scaledMatrix.transpose() * cache.scaledMatrix).pruned();
    Eigen::SparseMatrix<double> regularizer(columnCount, columnCount);
    regularizer.setIdentity();
    cache.normalMatrix += ridgeFor(cache.normalMatrix) * regularizer;
    cache.normalMatrix.makeCompressed();
    return true;
}

bool ConstrainedLeastSquares::solveReduced(std::vector<double>* solution)
{
    Cache& cache = *m_cache;
    if (0 == m_freeCount) {
        solution->assign(m_variableCount, 0.0);
        for (size_t i = 0; i < m_variableCount; ++i)
            (*solution)[i] = m_substitutions[i].offset;
        return true;
    }

    if (m_matrixDirty) {
        const Clock::time_point buildStart = Clock::now();
        if (!buildReducedSystem())
            return false;
        m_lastSolveStats.buildSeconds = secondsSince(buildStart);
        m_matrixDirty = false;
        m_fixedDirty = true;
    }
    if (m_fixedDirty) {
        if (!mapFixedVariables() || !factorizeReducedSystem())
            return false;
        m_fixedDirty = false;
    }
    if (!cache.factorized)
        return false;
    m_lastSolveStats.fixedVariables = cache.fixedColumns.size();

    Eigen::VectorXd scaledRightHandSide(static_cast<Eigen::Index>(energyCount()));
    for (Eigen::Index row = 0; row < scaledRightHandSide.size(); ++row) {
        scaledRightHandSide[row] = cache.rowWeightRoot[row]
            * (m_energyRightHandSides[static_cast<size_t>(row)] - cache.rowShift[row]);
    }
    bool solveSucceeded = false;
    const Clock::time_point solveStart = Clock::now();
    Eigen::VectorXd normalRightHandSide = cache.scaledMatrix.transpose() * scaledRightHandSide;
    cache.applyFixedColumns(&normalRightHandSide);
    Eigen::VectorXd reducedSolution = cache.solve(normalRightHandSide, &solveSucceeded);
    m_lastSolveStats.solveSeconds = secondsSince(solveStart);
    m_lastSolveStats.nonFiniteValues = cache.nonFiniteValues;
    if (!solveSucceeded)
        return false;
    for (size_t column : cache.fixedColumns)
        reducedSolution[(Eigen::Index)column] = cache.fixedValue[(Eigen::Index)column];

    solution->resize(m_variableCount);
    for (size_t i = 0; i < m_variableCount; ++i) {
        const Substitution& substitution = m_substitutions[i];
        if (substitution.fixed) {
            (*solution)[i] = substitution.offset;
            continue;
        }
        const size_t column = m_freeIndexOfRoot[substitution.root];
        (*solution)[i] = noIndex == column
            ? substitution.offset
            : substitution.offset + substitution.scale * reducedSolution[static_cast<Eigen::Index>(column)];
    }
    return true;
}

bool ConstrainedLeastSquares::mapFixedVariables()
{
    Cache& cache = *m_cache;
    cache.fixedColumn.assign(m_freeCount, 0);
    cache.fixedValue = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(m_freeCount));
    cache.fixedColumns.clear();
    for (const auto& fixed : m_fixedVariables) {
        if (fixed.first >= m_variableCount)
            return false;
        const Substitution& substitution = m_substitutions[fixed.first];
        if (substitution.fixed)
            return false;
        const size_t column = m_freeIndexOfRoot[substitution.root];
        if (noIndex == column)
            return false;
        const double value = (fixed.second - substitution.offset) / substitution.scale;
        if (cache.fixedColumn[column]) {
            const double held = cache.fixedValue[static_cast<Eigen::Index>(column)];
            if (std::fabs(held - value) > 1e-9 * (1.0 + std::fabs(value)))
                return false;
            continue;
        }
        cache.fixedColumn[column] = 1;
        cache.fixedValue[static_cast<Eigen::Index>(column)] = value;
        cache.fixedColumns.push_back(column);
    }
    return true;
}

bool ConstrainedLeastSquares::factorizeReducedSystem()
{
    Cache& cache = *m_cache;
    cache.factorized = false;
    m_lastSolveStats.refactorized = true;
    m_lastSolveStats.normalNonZeros = (size_t)cache.normalMatrix.nonZeros();
    m_lastSolveStats.ridge = ridgeFor(cache.normalMatrix);
    const Clock::time_point factorizeStart = Clock::now();
    ++m_lastSolveStats.factorizations;
    const bool factorized = cache.factorize(cache.systemMatrix());
    m_lastSolveStats.factorizeSeconds = secondsSince(factorizeStart);
    m_lastSolveStats.factorNonZeros = cache.factorNonZeros;
    cache.factorized = factorized;
    return factorized;
}

bool ConstrainedLeastSquares::solve(std::vector<double>* solution)
{
    if (0 == m_variableCount)
        return false;

    const Clock::time_point start = Clock::now();
    SolveStats& stats = m_lastSolveStats;
    const size_t factorizations = stats.factorizations;
    const size_t solves = stats.solves;
    stats = SolveStats();
    stats.factorizations = factorizations;
    stats.solves = solves + 1;
    stats.variables = m_variableCount;
    stats.energies = energyCount();
    stats.constraints = m_constraintEquations.size();
    stats.fixedVariables = m_fixedVariables.size();
    if (m_matrixDirty) {
        m_cache->patternAnalyzed = false;
        m_cache->factorized = false;
        if (!buildSubstitutions()) {
            stats.lagrangeFallback = true;
            stats.refactorized = true;
            ++stats.factorizations;
            stats.succeeded = solveWithLagrangeMultipliers(solution);
            stats.totalSeconds = secondsSince(start);
            return stats.succeeded;
        }
    }
    stats.freeVariables = m_freeCount;
    stats.succeeded = solveReduced(solution);
    stats.totalSeconds = secondsSince(start);
    return stats.succeeded;
}

bool ConstrainedLeastSquares::solveWithLagrangeMultipliers(std::vector<double>* solution)
{
    const Eigen::Index variableCount = static_cast<Eigen::Index>(m_variableCount);
    // Held variables are plain constraints here: this path factorises from
    // scratch anyway.
    std::vector<LinearEquation> constraints = m_constraintEquations;
    for (const auto& fixed : m_fixedVariables)
        constraints.push_back({ { { fixed.first, 1.0 } }, fixed.second, 1.0 });
    const Eigen::Index originalConstraintCount = static_cast<Eigen::Index>(constraints.size());

    Eigen::SparseMatrix<double> constraintMatrix(originalConstraintCount, variableCount);
    std::vector<Eigen::Triplet<double>> constraintEntries;
    for (Eigen::Index constraintIndex = 0; constraintIndex < originalConstraintCount; ++constraintIndex) {
        for (const auto& coefficient : constraints[static_cast<size_t>(constraintIndex)].coefficients) {
            if (coefficient.first >= m_variableCount)
                return false;
            constraintEntries.emplace_back(constraintIndex, static_cast<Eigen::Index>(coefficient.first), coefficient.second);
        }
    }
    constraintMatrix.setFromTriplets(constraintEntries.begin(), constraintEntries.end());
    Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>> constraintQR;
    constraintQR.compute(constraintMatrix.transpose());
    if (constraintQR.info() != Eigen::Success)
        return false;
    const Eigen::Index independentConstraintCount = constraintQR.rank();
    std::vector<Eigen::Index> selectedConstraints;
    selectedConstraints.reserve(static_cast<size_t>(independentConstraintCount));
    const auto permutation = constraintQR.colsPermutation().indices();
    for (Eigen::Index i = 0; i < independentConstraintCount; ++i)
        selectedConstraints.push_back(permutation[i]);

    Eigen::SparseMatrix<double> normal(variableCount + independentConstraintCount, variableCount + independentConstraintCount);
    std::vector<Eigen::Triplet<double>> entries;
    Eigen::VectorXd rightHandSide = Eigen::VectorXd::Zero(variableCount + independentConstraintCount);
    double diagonalSum = 0.0;
    for (size_t row = 0; row < energyCount(); ++row) {
        const auto begin = m_energyCoefficients.begin() + m_energyStart[row];
        const auto end = m_energyCoefficients.begin() + m_energyStart[row + 1];
        const double weight = m_energyWeights[row];
        for (auto coefficient = begin; coefficient != end; ++coefficient) {
            if (coefficient->first >= m_variableCount)
                return false;
            rightHandSide[static_cast<Eigen::Index>(coefficient->first)] += weight * coefficient->second * m_energyRightHandSides[row];
            for (auto otherCoefficient = begin; otherCoefficient != end; ++otherCoefficient) {
                if (otherCoefficient->first >= m_variableCount)
                    return false;
                const double entry = weight * coefficient->second * otherCoefficient->second;
                if (coefficient->first == otherCoefficient->first)
                    diagonalSum += entry;
                entries.emplace_back(static_cast<Eigen::Index>(coefficient->first), static_cast<Eigen::Index>(otherCoefficient->first),
                    entry);
            }
        }
    }
    for (Eigen::Index constraintIndex = 0; constraintIndex < independentConstraintCount; ++constraintIndex) {
        const LinearEquation& equation = constraints[static_cast<size_t>(selectedConstraints[constraintIndex])];
        rightHandSide[variableCount + constraintIndex] = equation.rightHandSide;
        for (const auto& coefficient : equation.coefficients) {
            entries.emplace_back(static_cast<Eigen::Index>(coefficient.first), variableCount + constraintIndex, coefficient.second);
            entries.emplace_back(variableCount + constraintIndex, static_cast<Eigen::Index>(coefficient.first), coefficient.second);
        }
    }
    const double meanDiagonal = variableCount > 0 ? diagonalSum / (double)variableCount : 0.0;
    const double ridge = meanDiagonal > 0.0 ? relativeRidge * meanDiagonal : relativeRidge;
    for (Eigen::Index i = 0; i < variableCount; ++i)
        entries.emplace_back(i, i, ridge);
    normal.setFromTriplets(entries.begin(), entries.end());
    m_lastSolveStats.freeVariables = (size_t)normal.rows();
    m_lastSolveStats.normalNonZeros = (size_t)normal.nonZeros();
    m_lastSolveStats.ridge = ridge;
    Eigen::SparseLU<Eigen::SparseMatrix<double>> solver;
    const Clock::time_point factorizeStart = Clock::now();
    solver.compute(normal);
    m_lastSolveStats.factorizeSeconds = secondsSince(factorizeStart);
    if (solver.info() != Eigen::Success)
        return false;
    m_lastSolveStats.factorNonZeros = (size_t)(solver.nnzL() + solver.nnzU());
    const Clock::time_point solveStart = Clock::now();
    const Eigen::VectorXd solutionVector = solver.solve(rightHandSide);
    m_lastSolveStats.solveSeconds = secondsSince(solveStart);
    m_lastSolveStats.nonFiniteValues = countNonFinite(solutionVector);
    if (solver.info() != Eigen::Success || 0 != m_lastSolveStats.nonFiniteValues)
        return false;
    solution->resize(m_variableCount);
    for (size_t i = 0; i < m_variableCount; ++i)
        (*solution)[i] = solutionVector[static_cast<Eigen::Index>(i)];
    return true;
}

void appendSolveStats(DiagnosticRecord* record, const std::string& prefix,
    const ConstrainedLeastSquares::SolveStats& stats)
{
    const std::string p = prefix + "_";
    record->set(p + "ok", stats.succeeded ? 1.0 : 0.0);
    record->set(p + "variables", (double)stats.variables);
    record->set(p + "energies", (double)stats.energies);
    record->set(p + "constraints", (double)stats.constraints);
    record->set(p + "fixed", (double)stats.fixedVariables);
    record->set(p + "n", (double)stats.freeVariables);
    record->set(p + "normal_nnz", (double)stats.normalNonZeros);
    record->set(p + "normal_nnz_bound", (double)stats.normalNonZerosBound);
    record->set(p + "factor_nnz", (double)stats.factorNonZeros);
    record->set(p + "ridge", stats.ridge);
    record->set(p + "refactorized", stats.refactorized ? 1.0 : 0.0);
    record->set(p + "kkt_fallback", stats.lagrangeFallback ? 1.0 : 0.0);
    record->set(p + "non_finite", (double)stats.nonFiniteValues);
    record->set(p + "build_ms", 1e3 * stats.buildSeconds);
    record->set(p + "factorize_ms", 1e3 * stats.factorizeSeconds);
    record->set(p + "solve_ms", 1e3 * stats.solveSeconds);
    record->set(p + "total_ms", 1e3 * stats.totalSeconds);
    record->set(p + "factorizations", (double)stats.factorizations);
    record->set(p + "solves", (double)stats.solves);
}

}
