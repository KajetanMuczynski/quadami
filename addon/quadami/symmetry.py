# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Mirror symmetry settings and the asymmetry check (TODO G12). No bpy in here.

The core cuts the mesh at each plane, remeshes the kept part and mirrors it
back (--symmetry, research/symmetry-2026-11-25.md).  The result copies the
kept side, so before a run the add-on measures how far the two sides differ:
every sampled point reflected across the plane, its distance to the surface,
relative to the mesh's diagonal.  It never averages the sides; it warns and
names the side that is kept.  A plane through the object's origin can miss
the mesh's own symmetry (an off-centre origin); the check also finds the
offset along the axis where the sides fit best.
"""
import math

import numpy as np

AXES = "xyz"
# Max reflection distance over the diagonal: below NOTE the sides count as
# the same; up to WARN a note; above, a warning.  The head sculpt measured
# 0.059, Spot 0.016, koala 0.0006 (research/symmetry-2026-11-25.md).
NOTE = 0.005
WARN = 0.03
# A fitted plane is offered when it halves the mean distance and sits this
# far from the origin (share of the diagonal).
FIT_GAIN = 0.5
FIT_OFFSET = 0.001
SAMPLES = 20000
FIT_SAMPLES = 2000


def planes_text(axes, negative):
    """The --symmetry value: axes and negative are sets of 0, 1, 2."""
    return ",".join(("-" if a in negative else "") + AXES[a] for a in sorted(axes))


def center_text(center):
    return ",".join(repr(float(c)) for c in center)


def _distances(points, axis, center, nearest):
    reflected = points.copy()
    reflected[:, axis] = 2.0 * center - reflected[:, axis]
    return np.array([nearest(p) for p in reflected])


def _fit(points, axis, start, span, nearest):
    """Golden-section search of the mean reflection distance over
    [start - span, start + span]."""
    ratio = (math.sqrt(5.0) - 1.0) / 2.0
    lo, hi = start - span, start + span

    def cost(c):
        return float(_distances(points, axis, c, nearest).mean())

    a, b = hi - ratio * (hi - lo), lo + ratio * (hi - lo)
    fa, fb = cost(a), cost(b)
    for _ in range(24):
        if fa < fb:
            hi, b, fb = b, a, fa
            a = hi - ratio * (hi - lo)
            fa = cost(a)
        else:
            lo, a, fa = a, b, fb
            b = lo + ratio * (hi - lo)
            fb = cost(b)
    return (a, fa) if fa < fb else (b, fb)


def check(positions, axes, center, nearest, seed=0):
    """Per axis {axis, max, mean (shares of the diagonal), fit, fit_mean}:
    positions (n, 3) in the space of nearest(point) -> distance to the surface."""
    positions = np.asarray(positions, dtype=np.float64)
    if len(positions) == 0:
        return []
    diag = float(np.linalg.norm(positions.max(0) - positions.min(0))) or 1.0
    rng = np.random.default_rng(seed)
    pick = rng.choice(len(positions), min(SAMPLES, len(positions)), replace=False)
    points = positions[pick]
    few = points[:FIT_SAMPLES]
    out = []
    for axis in sorted(axes):
        d = _distances(points, axis, center[axis], nearest) / diag
        row = {"axis": axis, "max": float(d.max()), "mean": float(d.mean()), "fit": None, "fit_mean": None}
        if row["max"] > NOTE:
            mid = 0.5 * float(positions[:, axis].min() + positions[:, axis].max())
            best, best_mean = _fit(few, axis, mid, 0.1 * diag, nearest)
            here = float(_distances(few, axis, center[axis], nearest).mean())
            if best_mean < FIT_GAIN * here and abs(best - center[axis]) > FIT_OFFSET * diag:
                row["fit"], row["fit_mean"] = best, best_mean / diag
        out.append(row)
    return out


def check_lines(rows, negative):
    """Panel lines for check()'s rows: (text, is_warning)."""
    lines = []
    for row in rows:
        name = AXES[row["axis"]].upper()
        side = ("-" if row["axis"] in negative else "+") + name
        percent = 100.0 * row["max"]
        if row["max"] > WARN:
            lines.append(("The two sides across %s differ by up to %.1f %% of the model's size; the result copies the %s "
                          "side. Check the axis and the object's origin, or turn Symmetry %s off"
                          % (name, percent, side, name), True))
        elif row["max"] > NOTE:
            lines.append(("The sides across %s differ by up to %.1f %% of the model's size; the result copies the %s side"
                          % (name, percent, side), False))
        else:
            lines.append(("Symmetric across %s" % name, False))
        if row["fit"] is not None:
            lines.append(("The mesh is most symmetric about %s = %.4g, not about the origin: turn on Fit Plane, "
                          "or move the origin there" % (name, row["fit"]), True))
    return lines


def fitted_center(rows, center):
    """center with each fitted plane's offset in place."""
    out = list(center)
    for row in rows:
        if row["fit"] is not None:
            out[row["axis"]] = row["fit"]
    return out


def object_hint(mirror_axes, mesh_axes, axes):
    """A line when the object asks for symmetry the settings don't have: the
    axes of an enabled Mirror modifier or of the mesh's own symmetry
    (Sculpt and Edit Mode X/Y/Z)."""
    missing_mirror = set(mirror_axes) - set(axes)
    if missing_mirror:
        return ("The Mirror modifier (%s) is applied before remeshing, so the result is only roughly symmetric: "
                "turn on Symmetry %s" % ("".join(AXES[a].upper() for a in sorted(mirror_axes)),
                                         " ".join(AXES[a].upper() for a in sorted(missing_mirror))))
    missing_mesh = set(mesh_axes) - set(axes)
    if missing_mesh:
        return ("The mesh's own symmetry is on for %s: turn on Symmetry %s for a symmetric result"
                % ("".join(AXES[a].upper() for a in sorted(mesh_axes)),
                   " ".join(AXES[a].upper() for a in sorted(missing_mesh))))
    return None
