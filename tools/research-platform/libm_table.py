#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Research only (platform divergence study, 2026-10-26); removed from the branch before its PR.

The math functions the core calls, evaluated by the CLI itself (autoremesher-cli --research-libm,
from research-hash.patch), so the C library it links is measured: glibc with its CPU dispatch on
Linux, the static UCRT on Windows (/MT), Apple's libm on macOS. Arguments are seeded and identical
on every platform; the reference is mpmath at 160 bits, rounded to double. Per argument set:

    LIBM  <platform> <tag> <set> n=.. wrong=.. max_ulp=.. results=<hash of all result bits>
    LIBMX <platform> <tag> <set> <index>:<ulps>,...     (only the arguments not correctly rounded)

Two platforms return the same bits on an argument exactly when both list it with the same offset
or neither lists it.
"""
import hashlib
import os
import struct
import subprocess

import mpmath
import numpy as np

mpmath.mp.prec = 160
N = 20000

# name, function id in researchLibm(), argument map from two uniforms in [0, 1), exact function
SETS = [
    ("sin", 0, lambda u, v: (4 * np.pi * (2 * u - 1), 0 * v), lambda a, b: mpmath.sin(a)),
    ("cos", 1, lambda u, v: (4 * np.pi * (2 * u - 1), 0 * v), lambda a, b: mpmath.cos(a)),
    ("atan2", 2, lambda u, v: (2 * u - 1, 2 * v - 1), lambda a, b: mpmath.atan2(a, b)),
    ("acos", 3, lambda u, v: (2 * u - 1, 0 * v), lambda a, b: mpmath.acos(a)),
    ("acos_near1", 3, lambda u, v: (1 - u * 2.0 ** -10, 0 * v), lambda a, b: mpmath.acos(a)),
    ("exp", 4, lambda u, v: (8 * u - 4, 0 * v), lambda a, b: mpmath.exp(a)),
    ("log", 5, lambda u, v: (1e-3 + 10 * u, 0 * v), lambda a, b: mpmath.log(a)),
    ("log10", 6, lambda u, v: (1e-3 + 10 * u, 0 * v), lambda a, b: mpmath.log10(a)),
    ("pow", 7, lambda u, v: (1e-3 + 4 * u, 4 * v - 2), lambda a, b: mpmath.power(a, b)),
    ("pow_m0.5", 7, lambda u, v: (1e-3 + 30 * u, -0.5 + 0 * v), lambda a, b: mpmath.power(a, b)),  # sizing
    ("pow_0.25", 7, lambda u, v: (1e-3 + 30 * u, 0.25 + 0 * v), lambda a, b: mpmath.power(a, b)),  # anisotropy
    ("hypot", 8, lambda u, v: (2 * u - 1, 2 * v - 1), lambda a, b: mpmath.hypot(a, b)),
]
# The OBJ reader (tiny_obj_loader.h tryParseDouble) takes pow(10, -k) for the 9th and later
# fractional digits and pow(5, e) for an exponent.
PARSE = [(10.0, -k) for k in range(1, 31)] + [(5.0, e) for e in range(-40, 41)]


def arguments():
    """[(set name, function id, a array, b array)], the same on every platform."""
    out = []
    for i, (name, fid, args_of, _) in enumerate(SETS):
        rng = np.random.default_rng(1000 + i)
        u, v = rng.random(N), rng.random(N)
        a, b = args_of(u, v)
        out.append((name, fid, np.asarray(a, dtype="<f8"), np.asarray(b, dtype="<f8") + np.zeros(N)))
    out.append(("pow_parse", 7, np.array([x for x, _ in PARSE], "<f8"), np.array([float(y) for _, y in PARSE], "<f8")))
    return out


def exact_of(name):
    if name == "pow_parse":
        return lambda a, b: mpmath.power(a, b)
    return next(s[3] for s in SETS if s[0] == name)


def write_input(path, args):
    with open(path, "wb") as fp:
        for _, fid, a, b in args:
            for x, y in zip(a, b):
                fp.write(struct.pack("<Idd", fid, float(x), float(y)))


def reference(args, cache):
    if os.path.exists(cache):
        return np.load(cache)
    ref = []
    for name, _, a, b in args:
        f = exact_of(name)
        ref.extend(float(f(mpmath.mpf(float(x)), mpmath.mpf(float(y)))) for x, y in zip(a, b))
    ref = np.array(ref, "<f8")
    np.save(cache, ref)
    return ref


def evaluate(cli, work, tag, env, platform):
    """Runs the CLI's table, prints LIBM/LIBMX lines; returns them."""
    args = arguments()
    inp = os.path.join(work, "libm-args.bin")
    if not os.path.exists(inp):
        write_input(inp, args)
    ref = reference(args, os.path.join(work, "libm-ref.npy"))
    out = os.path.join(work, f"libm-{tag}.bin")
    e = dict(os.environ)
    e.update(env)
    rc = subprocess.call([cli, "--research-libm", inp, out], env=e)
    if rc != 0 or not os.path.exists(out):
        return [f"LIBM {platform} {tag} failed rc={rc}"]
    got = np.fromfile(out, "<f8")
    if got.size != ref.size:
        return [f"LIBM {platform} {tag} failed size={got.size} expected={ref.size}"]
    gi, ri = got.view("<i8"), ref.view("<i8")
    lines, start = [], 0
    for name, _, a, _ in args:
        n = a.size
        d = gi[start:start + n] - ri[start:start + n]
        wrong = np.nonzero(d)[0]
        h = hashlib.sha256(got[start:start + n].tobytes()).hexdigest()[:16]
        lines.append(f"LIBM {platform} {tag} {name} n={n} wrong={wrong.size} "
                     f"max_ulp={int(np.abs(d).max()) if wrong.size else 0} results={h}")
        lines.append(f"LIBMX {platform} {tag} {name} " + ",".join(f"{j}:{int(d[j])}" for j in wrong))
        start += n
    return lines
