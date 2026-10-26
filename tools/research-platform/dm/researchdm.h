// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Quadami contributors
// Research only (platform divergence study): the nine libm functions the core calls, computed in
// double-double from +, -, *, / and sqrt alone, so every platform gets the same bits. Force-included;
// experiment.py rewrites std::sin(...) etc. in the core and libQEx/OpenMesh to ar_dm::sin(...).
#pragma once
#ifdef __cplusplus
#include <complex>
#include <type_traits>

namespace ar_dm {

double sin(double x);
double cos(double x);
double acos(double x);
double exp(double x);
double log(double x);
double log10(double x);
double atan2(double y, double x);
double pow(double x, double y);
double hypot(double x, double y);

#define AR_DM_UNARY(f)                                                                                 \
    inline float f(float x) { return static_cast<float>(f(static_cast<double>(x))); }                \
    inline long double f(long double x) { return f(static_cast<double>(x)); }                        \
    template <class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>           \
    inline double f(T x) { return f(static_cast<double>(x)); }
AR_DM_UNARY(sin)
AR_DM_UNARY(cos)
AR_DM_UNARY(acos)
AR_DM_UNARY(exp)
AR_DM_UNARY(log)
AR_DM_UNARY(log10)
#undef AR_DM_UNARY

#define AR_DM_BINARY(f)                                                                                \
    inline float f(float a, float b) { return static_cast<float>(f(static_cast<double>(a), static_cast<double>(b))); } \
    template <class A, class B,                                                                        \
        typename std::enable_if<std::is_arithmetic<A>::value && std::is_arithmetic<B>::value, int>::type = 0> \
    inline double f(A a, B b) { return f(static_cast<double>(a), static_cast<double>(b)); }
AR_DM_BINARY(atan2)
AR_DM_BINARY(pow)
AR_DM_BINARY(hypot)
#undef AR_DM_BINARY

// libQEx's transition functions: the rotation from the argument of a quotient, i^r by pow.
// Same formulas as libstdc++'s generic complex log and pow.
template <class T>
inline std::complex<T> log(const std::complex<T>& z)
{
    return std::complex<T>(static_cast<T>(log(hypot(z.real(), z.imag()))), static_cast<T>(atan2(z.imag(), z.real())));
}
template <class T, class U, typename std::enable_if<std::is_arithmetic<U>::value, int>::type = 0>
inline std::complex<T> pow(const std::complex<T>& z, U n)
{
    if (z.imag() == T(0) && z.real() > T(0))
        return std::complex<T>(static_cast<T>(pow(z.real(), n)), T(0));
    const std::complex<T> t = ar_dm::log(z);  // qualified: ADL would also find std::log
    const double rho = exp(static_cast<double>(n) * t.real()), theta = static_cast<double>(n) * t.imag();
    return std::complex<T>(static_cast<T>(rho * cos(theta)), static_cast<T>(rho * sin(theta)));
}

}  // namespace ar_dm
#endif
