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
#ifndef AUTO_REMESHER_CONSTRAINED_LEAST_SQUARES_H
#define AUTO_REMESHER_CONSTRAINED_LEAST_SQUARES_H

#include <AutoRemesher/Diagnostics>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace AutoRemesher {

class ConstrainedLeastSquares {
public:
    explicit ConstrainedLeastSquares(size_t variableCount);
    ~ConstrainedLeastSquares();

    size_t addEnergy(const std::vector<std::pair<size_t, double>>& coefficients,
        double rightHandSide, double weight = 1.0);
    void setEnergyRightHandSide(size_t energyIndex, double rightHandSide);
    void addConstraint(const std::vector<std::pair<size_t, double>>& coefficients,
        double rightHandSide = 0.0);
    void clearConstraints();

    // Holds variables at the given values until the next call, which
    // replaces the set (an empty set releases them all). Unlike a constraint
    // this keeps the factorised pattern: the held unknowns' rows and columns
    // of the normal matrix are replaced by their diagonal, so a new set
    // costs one numeric refactorisation instead of a new substitution,
    // ordering and factorisation. A held variable must not also be fixed by
    // a constraint, and variables that constraints tie together must be held
    // at consistent values; solve() fails otherwise.
    void fixVariables(const std::vector<std::pair<size_t, double>>& values);

    bool solve(std::vector<double>* solution);

    // What the last solve() did and cost, for the diagnostics report.  Only
    // recorded; nothing reads it back.
    struct SolveStats {
        size_t variables = 0;
        size_t energies = 0;
        size_t constraints = 0;
        // Variables held by fixVariables().
        size_t fixedVariables = 0;
        // Unknowns of the factorised system after constraint substitution.
        size_t freeVariables = 0;
        size_t normalNonZeros = 0;
        // Sum of the squared lengths of the energy rows, which bounds the
        // non-zeros of the normal matrix before it is formed.
        size_t normalNonZerosBound = 0;
        // Non-zeros of L; 0 when the platform solver does not expose it.
        size_t factorNonZeros = 0;
        double ridge = 0.0;
        // Whether this solve rebuilt and refactorised the system, or reused
        // the factorisation of an earlier solve with a new right-hand side.
        bool refactorized = false;
        // The constraints did not reduce to substitutions, so the dense
        // Lagrange (KKT) system was solved with SparseLU instead.
        bool lagrangeFallback = false;
        bool succeeded = false;
        size_t nonFiniteValues = 0;
        double buildSeconds = 0.0;
        double factorizeSeconds = 0.0;
        double solveSeconds = 0.0;
        double totalSeconds = 0.0;
        // Running totals over the lifetime of this object.
        size_t factorizations = 0;
        size_t solves = 0;
    };
    const SolveStats& lastSolveStats() const
    {
        return m_lastSolveStats;
    }

private:
    struct LinearEquation {
        std::vector<std::pair<size_t, double>> coefficients;
        double rightHandSide;
        double weight;
    };

    struct Substitution {
        size_t root;
        double scale;
        double offset;
        bool fixed;
    };

    Substitution resolve(size_t variable);
    bool buildSubstitutions();
    bool buildReducedSystem();
    bool mapFixedVariables();
    bool factorizeReducedSystem();
    bool solveReduced(std::vector<double>* solution);
    bool solveWithLagrangeMultipliers(std::vector<double>* solution);

    size_t energyCount() const
    {
        return m_energyWeights.size();
    }

    size_t m_variableCount;
    // Energy rows in one flat array, not a vector per row (a few hundred
    // thousand to millions of short rows): row i's coefficients are
    // m_energyCoefficients[m_energyStart[i], m_energyStart[i + 1]).
    std::vector<std::pair<size_t, double>> m_energyCoefficients;
    std::vector<size_t> m_energyStart = std::vector<size_t>(1, 0);
    std::vector<double> m_energyRightHandSides;
    std::vector<double> m_energyWeights;
    std::vector<LinearEquation> m_constraintEquations;
    std::vector<std::pair<size_t, double>> m_fixedVariables;

    bool m_matrixDirty = true;
    // The held values changed since the last factorisation.
    bool m_fixedDirty = false;

    std::vector<Substitution> m_substitutions;
    std::vector<size_t> m_freeIndexOfRoot;
    size_t m_freeCount = 0;

    struct Cache;
    std::unique_ptr<Cache> m_cache;
    SolveStats m_lastSolveStats;
};

// Appends one solve's statistics to `record` as `<prefix>_<field>`.
void appendSolveStats(DiagnosticRecord* record, const std::string& prefix,
    const ConstrainedLeastSquares::SolveStats& stats);
}
#endif
