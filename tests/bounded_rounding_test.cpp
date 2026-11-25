/*
 *  Copyright (C) 2026 Quadami contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

// Lower bounds on integers of the mixed-integer solve (TODO 5.7).
//
// The solver has only equalities. "At least one row of quads between a
// guide and a cone" is an inequality on a difference of unknowns; it enters
// as one more integer d with the row d = difference, and rounding fixes d at
// the nearest integer that is not below its bound, first of all where the
// relaxed value would round below it.

#include <AutoRemesher/MixedIntegerLeastSquares>
#include <cmath>
#include <cstdio>

namespace {

size_t solve(AutoRemesher::MixedIntegerLeastSquares* system)
{
    size_t passes = 0;
    bool solved = true;
    while (solved && !system->converged() && passes < 20) {
        solved = system->solveIteration();
        ++passes;
    }
    return solved && system->converged() ? passes : 0;
}

// x1 - x0 wants to be `wanted`; d = x1 - x0 is an integer, bounded below by
// 1 when `bounded`. Returns d.
double difference(double wanted, bool bounded, size_t* raised, size_t* kept)
{
    AutoRemesher::MixedIntegerLeastSquares system(3);
    system.setVariablePeriod(2, 1);
    if (bounded)
        system.setVariableLowerBound(2, 1.0);
    system.addEnergy({ { 1, 1.0 }, { 0, -1.0 } }, wanted, 1.0);
    system.addEnergy({ { 0, 1.0 } }, 0.0, 1.0);
    system.addConstraint({ { 2, 1.0 }, { 1, -1.0 }, { 0, 1.0 } });
    system.finalizeConstraints();
    if (0 == solve(&system))
        return std::nan("");
    *raised = system.raisedToBoundCount();
    *kept = system.boundedKernelVariableCount();
    return system.value(1) - system.value(0);
}

bool differences()
{
    bool ok = true;
    const struct {
        double wanted;
        bool bounded;
        double expected;
        size_t raised;
    } cases[] = {
        { 0.2, false, 0.0, 0 }, // rounds to no row
        { 0.2, true, 1.0, 1 }, // the bound is active
        { -0.7, true, 1.0, 1 }, // on the wrong side in the relaxation
        { 0.8, true, 1.0, 0 }, // rounds to the bound by itself
        { 2.6, true, 3.0, 0 }, // the bound is slack: nearest
    };
    for (const auto& c : cases) {
        size_t raised = 0, kept = 0;
        const double d = difference(c.wanted, c.bounded, &raised, &kept);
        const bool good = d == c.expected && raised == c.raised && kept == (c.bounded ? 1u : 0u);
        std::printf("wanted %5.2f %-9s d = %g, raised %zu, kept %zu%s\n", c.wanted,
            c.bounded ? "bounded" : "free", d, raised, kept, good ? "" : "  FAIL");
        ok = good && ok;
    }
    return ok;
}

// The cover's case: the difference is half the sum of two translations of
// period 2 (a cone's coordinate), so its row has only integers. The bounded
// one must stay a kernel variable, and the translations must stay even.
bool integerRow()
{
    AutoRemesher::MixedIntegerLeastSquares system(3);
    system.setVariablePeriod(0, 2);
    system.setVariablePeriod(1, 2);
    system.setVariablePeriod(2, 1);
    system.setVariableLowerBound(2, 1.0);
    system.addEnergy({ { 0, 1.0 } }, 0.3, 1.0);
    system.addEnergy({ { 1, 1.0 } }, 0.1, 2.0);
    system.addConstraint({ { 2, 1.0 }, { 0, -0.5 }, { 1, -0.5 } });
    system.finalizeConstraints();
    const size_t passes = solve(&system);
    const double t0 = system.value(0), t1 = system.value(1), d = system.value(2);
    const bool even = std::fmod(std::fabs(t0), 2.0) == 0.0 && std::fmod(std::fabs(t1), 2.0) == 0.0;
    const bool ok = passes > 0 && 1.0 == d && even && t0 + t1 == 2.0 && 1 == system.boundedKernelVariableCount()
        && system.integralityResidual() == 0.0;
    std::printf("integer row: t = (%g, %g), d = %g, integrality residual %g%s\n", t0, t1, d,
        system.integralityResidual(), ok ? "" : "  FAIL");
    return ok;
}

}

int main()
{
    bool ok = differences();
    ok = integerRow() && ok;
    return ok ? 0 : 1;
}
