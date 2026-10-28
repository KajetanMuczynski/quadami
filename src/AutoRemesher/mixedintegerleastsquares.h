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
#ifndef AUTO_REMESHER_MIXED_INTEGER_LEAST_SQUARES_H
#define AUTO_REMESHER_MIXED_INTEGER_LEAST_SQUARES_H

#include <AutoRemesher/ConstrainedLeastSquares>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace AutoRemesher {

class MixedIntegerLeastSquares {
public:
    explicit MixedIntegerLeastSquares(size_t variableCount);
    ~MixedIntegerLeastSquares();
    void setVariablePeriod(size_t variable, int period);
    void addConstraint(const std::vector<std::pair<size_t, double>>& coefficients);
    void addConstraint(size_t firstVariable, double firstCoefficient);
    void addConstraint(size_t firstVariable, double firstCoefficient,
        size_t secondVariable, double secondCoefficient);
    void addConstraint(size_t firstVariable, double firstCoefficient,
        size_t secondVariable, double secondCoefficient,
        size_t thirdVariable, double thirdCoefficient);
    void finalizeConstraints();

    void beginConstraint();
    void addConstraintCoefficient(size_t variable, double coefficient);
    bool endConstraint();
    void endPass(size_t pass);

    void clearEnergy();
    void addEnergy(const std::vector<std::pair<size_t, double>>& coefficients,
        double rhs, double weight = 1.0);
    void addEnergy(size_t firstVariable, double firstCoefficient,
        size_t secondVariable, double secondCoefficient, double rhs, double weight = 1.0);

    // The first call solves the continuous relaxation.  Each later call fixes
    // the next batch of integers to their nearest lattice values and solves
    // again; converged() once every integer is fixed, after about
    // expectedRoundingPasses() calls.
    bool solveIteration();
    static size_t expectedRoundingPasses();
    // Multiplies the right-hand sides and the current solution by factor.
    // Valid only while no integer is fixed (after the first, continuous
    // solveIteration): every constraint is homogeneous, so the continuous
    // solution is linear in the right-hand sides. Returns false otherwise.
    bool scaleSolution(double factor);
    bool converged() const;
    // Frees the factorised system once rounding is over; the solution and
    // everything that reads it stay, but solveIteration() returns false after.
    void releaseSystem();
    double value(size_t originalVariable) const;
    size_t kernelSize() const;
    size_t integerKernelVariableCount() const;
    size_t originalIntegerVariableCount() const;

    // The kernel the solution lives in, for moving the free part of a
    // rounded solution (QuadParameterizer's untangling): original variable
    // i is the sum of a * kernelValue(k) over the (k, a) of kernelTerms(i),
    // and the integers rounding fixed are held.
    void kernelTerms(size_t originalVariable, std::vector<std::pair<size_t, double>>* terms) const;
    bool kernelVariableFixed(size_t kernelVariable) const;
    double kernelValue(size_t kernelVariable) const;
    // Returns false, changing nothing, for a fixed or unknown variable.
    bool setKernelValue(size_t kernelVariable, double value);

    // Diagnostics only; none of these change the solve.

    // What the last solveIteration() rounded and how its solve went.  A
    // rounding shift is |x - p round(x / p)| in the variable's own units,
    // taken over the integers this iteration fixed.
    struct IterationStats {
        size_t iteration = 0;
        size_t kernelSize = 0;
        size_t integerVariables = 0;
        size_t fixedBefore = 0;
        size_t newlyFixed = 0;
        double maximumRoundingShift = 0.0;
        double meanRoundingShift = 0.0;
        ConstrainedLeastSquares::SolveStats solve;
    };
    const IterationStats& lastIterationStats() const
    {
        return m_lastIterationStats;
    }

    // Constraint rows that still had non-zero coefficients after elimination
    // (pass 3).  The kernel does not satisfy them; they used to be dropped
    // without a trace.
    size_t droppedConstraintCount() const
    {
        return m_droppedConstraintCount;
    }

    // Non-zeros of the elimination (the matrix from the kernel to the
    // variables left after the two-term constraints), and whether they grew
    // past 64 per kernel variable. Then the elimination stops, no kernel is
    // built and solveIteration() fails. The cover of chain-mail lace (about
    // 1,600 holes in an island of 49 quads, ext_nemetona at 3000 quads) filled
    // in to 326 per kernel variable, and its first solve passed 10 GB before
    // it was stopped; other islands stay near 1.
    size_t eliminationNonZeros() const
    {
        return m_eliminationNonZeros;
    }
    bool eliminationOverBudget() const
    {
        return m_eliminationOverBudget;
    }

    // max |x / p - round(x / p)| over the variables given a period before
    // elimination, evaluated on the current solution.  Integers fixed in the
    // kernel can still leave an original variable fractional when the
    // elimination folded it into a combination of kernel variables.
    double integralityResidual() const;

    // The least-squares energy sum w (a . x - rhs)^2 of the current solution;
    // its growth from the continuous solution is what rounding cost.
    double energy() const;

    // max |a . x| over all constraints as added (every constraint row is
    // homogeneous), and how many exceed `tolerance`.
    double constraintResidual(double tolerance, size_t* violatedCount) const;

private:
    struct Coeff {
        size_t index;
        double a;
    };
    struct Row {
        std::vector<Coeff> coefficients;
        double rhs;
        double weight;
    };
    struct SparseMatrix {
        std::vector<std::vector<Coeff>> rows;
    };

    static void normalize(std::vector<Coeff>* row);
    static void add(std::vector<Coeff>* row, size_t index, double a);
    void axpy(std::vector<Coeff>* row, const std::vector<Coeff>& source, double factor);
    std::vector<Coeff> multiply(const std::vector<Coeff>& row,
        const SparseMatrix& matrix) const;
    std::vector<Coeff> line(size_t originalVariable) const;
    const std::vector<Coeff>& kernelLine(size_t originalVariable) const;
    void buildKernel();
    std::vector<Coeff> throughM0(const std::vector<Coeff>& row) const;
    std::vector<Coeff> throughM1(const std::vector<Coeff>& row) const;
    bool processConstraint(const std::vector<Coeff>& row);
    void recordConstraint();
    size_t recordedConstraintCount() const;
    const std::vector<Coeff>& recordedConstraint(size_t index) const;
    void finalizeM0();
    void finalizeM1();
    void finalizeM2();
    void applyRemoveColumn(const std::vector<Coeff>& row, size_t remove);
    void fixNextBatch(IterationStats* stats);

    size_t m_size;
    size_t m_pass;
    size_t m_kernelSize;
    std::vector<size_t> m_m0;
    std::vector<Coeff> m_m1;
    SparseMatrix m_m2, m_m2t;
    std::vector<int> m_period;
    std::vector<int> m_initialPeriod;
    std::vector<Coeff> m_pendingConstraint;
    // The constraints as added, kept for constraintResidual(): constraint i
    // is m_constraintData[m_constraintStart[i], m_constraintStart[i + 1]).
    std::vector<Coeff> m_constraintData;
    std::vector<size_t> m_constraintStart;
    std::vector<Coeff> m_constraintScratch;
    mutable std::vector<Coeff> m_rowScratch;
    std::vector<Row> m_energy;
    std::vector<Row> m_reducedEnergy;
    std::vector<double> m_values;
    std::vector<bool> m_fixed;
    std::vector<double> m_fixedValue;

    SparseMatrix m_kernel;
    bool m_kernelBuilt = false;

    mutable std::vector<double> m_scatter;
    mutable std::vector<size_t> m_touched;
    mutable std::vector<Coeff> m_merged;

    std::unique_ptr<ConstrainedLeastSquares> m_system;
    bool m_systemReleased = false;
    std::vector<size_t> m_energyIndices;
    IterationStats m_lastIterationStats;
    size_t m_iterationCount = 0;
    // Integers fixed per rounding pass besides the nearly integral ones.
    size_t m_roundingBatch = 0;
    size_t m_droppedConstraintCount = 0;
    size_t m_eliminationNonZeros = 0;
    bool m_eliminationOverBudget = false;
};
}
#endif
