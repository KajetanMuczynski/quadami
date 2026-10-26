// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Quadami contributors
// Research only (platform divergence study). Double-double (about 104 bits) versions of the nine
// libm functions the core calls, rounded to double at the end. Not correctly rounded in every case,
// but built only from IEEE +, -, *, /, sqrt, floor, frexp, ldexp, so the bits are the same on every
// platform as long as nothing is contracted into a fused multiply-add (pragmas below, and
// -ffp-contract=off from CMake). Special cases follow C99 Annex F.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#include "researchdm.h"
#include <cmath>
#include <limits>

namespace {

struct dd {
    double hi, lo;
};

inline dd quickTwoSum(double a, double b)
{
    double s = a + b;
    return {s, b - (s - a)};
}
inline dd twoSum(double a, double b)
{
    double s = a + b;
    double bb = s - a;
    return {s, (a - (s - bb)) + (b - bb)};
}
inline void split(double a, double& hi, double& lo)
{
    double t = 134217729.0 * a;  // 2^27 + 1
    hi = t - (t - a);
    lo = a - hi;
}
inline dd twoProd(double a, double b)
{
    double p = a * b, ah, al, bh, bl;
    split(a, ah, al);
    split(b, bh, bl);
    return {p, ((ah * bh - p) + ah * bl + al * bh) + al * bl};
}
inline dd neg(dd a) { return {-a.hi, -a.lo}; }
inline dd add(dd a, dd b)
{
    dd s = twoSum(a.hi, b.hi), t = twoSum(a.lo, b.lo);
    s.lo += t.hi;
    s = quickTwoSum(s.hi, s.lo);
    s.lo += t.lo;
    return quickTwoSum(s.hi, s.lo);
}
inline dd add(dd a, double b)
{
    dd s = twoSum(a.hi, b);
    s.lo += a.lo;
    return quickTwoSum(s.hi, s.lo);
}
inline dd sub(dd a, dd b) { return add(a, neg(b)); }
inline dd mul(dd a, dd b)
{
    dd p = twoProd(a.hi, b.hi);
    p.lo += a.hi * b.lo + a.lo * b.hi;
    return quickTwoSum(p.hi, p.lo);
}
inline dd mul(dd a, double b)
{
    dd p = twoProd(a.hi, b);
    p.lo += a.lo * b;
    return quickTwoSum(p.hi, p.lo);
}
inline dd div(dd a, double b)
{
    double q1 = a.hi / b;
    dd p = twoProd(q1, b);
    dd s = twoSum(a.hi, -p.hi);
    s.lo -= p.lo;
    s.lo += a.lo;
    return quickTwoSum(q1, (s.hi + s.lo) / b);
}
inline dd div(dd a, dd b)
{
    double q1 = a.hi / b.hi;
    dd r = sub(a, mul(b, q1));
    double q2 = r.hi / b.hi;
    r = sub(r, mul(b, q2));
    double q3 = r.hi / b.hi;
    return add(quickTwoSum(q1, q2), q3);
}
inline dd sqrtDd(dd a)
{
    if (a.hi <= 0)
        return {0, 0};
    double x = 1.0 / std::sqrt(a.hi), ax = a.hi * x;
    return twoSum(ax, sub(a, twoProd(ax, ax)).hi * (x * 0.5));
}

const dd kPi = {3.141592653589793, 1.2246467991473532e-16};
const dd kPiO2 = {1.5707963267948966, 6.123233995736766e-17};
const dd kPiO4 = {0.7853981633974483, 3.061616997868383e-17};
const double kPiO2Parts[3] = {1.5707963267948966, 6.123233995736766e-17, -1.4973849048591698e-33};
const double kLn2Parts[3] = {0.6931471805599453, 2.3190468138462996e-17, 5.707708438416212e-34};
const dd kLn2 = {kLn2Parts[0], kLn2Parts[1]};
const dd kInvLn10 = {0.4342944819032518, 1.098319650216765e-17};
const double kInvPiO2 = 0.6366197723675814;
const double kInvLn2 = 1.4426950408889634;
const double kNan = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

// sin and cos of |r| <= pi/4 (a little more after reduction), Taylor to r^31.
void sinCosReduced(dd r, dd& s, dd& c)
{
    dd r2 = mul(r, r);
    s = {1, 0};
    c = {1, 0};
    for (int n = 15; n >= 1; --n) {
        s = sub(dd{1, 0}, div(mul(r2, s), double(2 * n) * double(2 * n + 1)));
        c = sub(dd{1, 0}, div(mul(r2, c), double(2 * n - 1) * double(2 * n)));
    }
    s = mul(r, s);
}

void sinCos(double x, double& sOut, double& cOut)
{
    if (std::fabs(x) > 1e9)  // the core's angles never are; deterministic, not accurate
        x = std::fmod(x, 2 * kPi.hi);
    double k = std::floor(x * kInvPiO2 + 0.5);
    dd r = add(dd{x, 0}, neg(twoProd(k, kPiO2Parts[0])));
    r = add(r, neg(twoProd(k, kPiO2Parts[1])));
    r = add(r, -(k * kPiO2Parts[2]));
    dd s, c;
    sinCosReduced(r, s, c);
    long long q = static_cast<long long>(std::fmod(k, 4.0));
    q = (q + 4) % 4;
    switch (q) {
    case 0: sOut = s.hi; cOut = c.hi; break;
    case 1: sOut = c.hi; cOut = -s.hi; break;
    case 2: sOut = -s.hi; cOut = -c.hi; break;
    default: sOut = -c.hi; cOut = s.hi; break;
    }
}

// log of a positive finite x, double-double.
dd logDd(double x)
{
    int e;
    double m = std::frexp(x, &e);  // [0.5, 1)
    if (m < 0.7071067811865476) {  // sqrt(1/2)
        m *= 2;
        --e;
    }
    dd s = div(dd{m - 1, 0}, twoSum(m, 1.0));  // m - 1 is exact (Sterbenz)
    dd s2 = mul(s, s);
    dd sum = div(dd{1, 0}, 41.0);
    for (int n = 19; n >= 0; --n)
        sum = add(div(dd{1, 0}, double(2 * n + 1)), mul(s2, sum));
    dd lm = mul(mul(s, sum), 2.0);
    double de = e;
    dd le = add(twoProd(de, kLn2Parts[0]), twoProd(de, kLn2Parts[1]));
    le = add(le, de * kLn2Parts[2]);
    return add(le, lm);
}

// exp of a double-double t with |t.hi| < 746.
double expDd(dd t)
{
    double k = std::floor(t.hi * kInvLn2 + 0.5);
    dd r = add(t, neg(twoProd(k, kLn2Parts[0])));
    r = add(r, neg(twoProd(k, kLn2Parts[1])));
    r = add(r, -(k * kLn2Parts[2]));
    r = {std::ldexp(r.hi, -9), std::ldexp(r.lo, -9)};
    // expm1(r) by Taylor, then 9 squarings of 1 + p as p <- 2p + p^2.
    dd q = {1, 0};
    for (int n = 10; n >= 2; --n)
        q = add(div(mul(r, q), double(n)), 1.0);
    dd p = mul(r, q);
    for (int i = 0; i < 9; ++i)
        p = add(mul(p, 2.0), mul(p, p));
    dd m = add(p, 1.0);
    return std::ldexp(m.hi, static_cast<int>(k));
}

// atan of 0 <= t <= 1 (double-double).
dd atanDd(dd t)
{
    static const struct Table {
        dd inv[25];
        Table()
        {
            for (int n = 0; n < 25; ++n)
                inv[n] = div(dd{1, 0}, double(2 * n + 1));
        }
    } table;
    dd offset = {0, 0}, u = t;
    if (t.hi > 0.41421356237309503) {  // tan(pi/8)
        u = div(add(t, -1.0), add(t, 1.0));
        offset = kPiO4;
    }
    dd w = div(u, add(sqrtDd(add(mul(u, u), 1.0)), 1.0));  // atan(u) = 2 atan(w)
    dd w2 = mul(w, w);
    dd sum = table.inv[24];
    for (int n = 23; n >= 0; --n)
        sum = sub(table.inv[n], mul(w2, sum));
    return add(offset, mul(mul(w, sum), 2.0));
}

// atan2 of finite y >= 0, x, not both zero, given as double-doubles.
dd atan2Dd(dd y, dd x)
{
    dd ax = x.hi < 0 ? neg(x) : x;
    dd a = (y.hi < ax.hi || (y.hi == ax.hi && y.lo <= ax.lo)) ? atanDd(div(y, ax)) : sub(kPiO2, atanDd(div(ax, y)));
    return x.hi < 0 ? sub(kPi, a) : a;
}

bool isOddInteger(double y)
{
    if (std::floor(y) != y || std::fabs(y) >= 9007199254740992.0)
        return false;
    return std::fmod(y, 2.0) != 0;
}

}  // namespace

namespace ar_dm {

double sin(double x)
{
    if (x == 0)
        return x;
    if (!std::isfinite(x))
        return kNan;
    double s, c;
    sinCos(x, s, c);
    return s;
}

double cos(double x)
{
    if (x == 0)
        return 1;
    if (!std::isfinite(x))
        return kNan;
    double s, c;
    sinCos(x, s, c);
    return c;
}

double atan2(double y, double x)
{
    if (std::isnan(x) || std::isnan(y))
        return kNan;
    double sign = std::signbit(y) ? -1.0 : 1.0;
    if (y == 0)
        return std::signbit(x) ? sign * kPi.hi : y;
    if (x == 0)
        return sign * kPiO2.hi;
    if (std::isinf(y)) {
        if (std::isinf(x))
            return sign * (x > 0 ? kPiO4.hi : 3 * kPiO4.hi);
        return sign * kPiO2.hi;
    }
    if (std::isinf(x))
        return x > 0 ? sign * 0.0 : sign * kPi.hi;
    double ay = std::fabs(y), ax = std::fabs(x);
    int ey = std::ilogb(ay), ex = std::ilogb(ax);
    if (ey < ex - 60)  // atan2 = y/|x| or pi to the last bit
        return x > 0 ? sign * (ay / ax) : sign * kPi.hi;
    if (ex < ey - 60)
        return sign * kPiO2.hi;
    int e = ey > ex ? ey : ex;
    ay = std::ldexp(ay, -e);
    ax = std::ldexp(ax, -e);
    dd a = atan2Dd(dd{ay, 0}, dd{x < 0 ? -ax : ax, 0});
    return sign * a.hi;
}

double acos(double x)
{
    if (std::isnan(x) || x > 1 || x < -1)
        return kNan;
    if (x == 1)
        return 0;
    dd y = sqrtDd(mul(twoSum(1.0, -x), twoSum(1.0, x)));
    return atan2Dd(y, dd{x, 0}).hi;
}

double exp(double x)
{
    if (std::isnan(x))
        return x;
    if (x > 709.8)
        return kInf;
    if (x < -745.2)
        return 0;
    return expDd(dd{x, 0});
}

double log(double x)
{
    if (std::isnan(x) || x < 0)
        return kNan;
    if (x == 0)
        return -kInf;
    if (std::isinf(x))
        return x;
    return logDd(x).hi;
}

double log10(double x)
{
    if (std::isnan(x) || x < 0)
        return kNan;
    if (x == 0)
        return -kInf;
    if (std::isinf(x))
        return x;
    return mul(logDd(x), kInvLn10).hi;
}

double pow(double x, double y)
{
    if (y == 0 || x == 1)
        return 1;
    if (std::isnan(x) || std::isnan(y))
        return kNan;
    bool odd = isOddInteger(y);
    if (x == 0) {
        if (y < 0)
            return odd ? std::copysign(kInf, x) : kInf;
        return odd ? x : 0.0;
    }
    if (std::isinf(y)) {
        double ax = std::fabs(x);
        if (ax == 1)
            return 1;
        return (ax < 1) == (y < 0) ? kInf : 0.0;
    }
    if (std::isinf(x)) {
        if (x < 0) {
            if (y < 0)
                return odd ? -0.0 : 0.0;
            return odd ? -kInf : kInf;
        }
        return y < 0 ? 0.0 : kInf;
    }
    double sign = 1;
    if (x < 0) {
        if (std::floor(y) != y)
            return kNan;
        sign = odd ? -1 : 1;
        x = -x;
    }
    dd l = logDd(x);
    double approx = l.hi * y;
    if (approx > 709.8)
        return sign * kInf;
    if (approx < -745.2)
        return sign * 0.0;
    return sign * expDd(mul(l, y));
}

double hypot(double x, double y)
{
    if (std::isinf(x) || std::isinf(y))
        return kInf;
    if (std::isnan(x) || std::isnan(y))
        return kNan;
    double a = std::fabs(x), b = std::fabs(y);
    if (a < b) {
        double t = a;
        a = b;
        b = t;
    }
    if (b == 0)
        return a;
    int e = std::ilogb(a);
    if (std::ilogb(b) < e - 60)
        return a;
    a = std::ldexp(a, -e);
    b = std::ldexp(b, -e);
    dd s = sqrtDd(add(twoProd(a, a), twoProd(b, b)));
    return std::ldexp(s.hi, e);
}

}  // namespace ar_dm
