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

"""Checks the Blender tests (blender_smoke.py, blender_operator.py) share on
a finished remesh: no bpy here, so they run on any Blender."""

import os
import re

# The band ci/output_gate.py holds the remesher to.
MIN_RATIO, MAX_RATIO = 0.6, 1.5


def check_quad_count(output, target):
    """The result's quads are near Target Quads and almost all faces are quads."""
    quads, faces = output["quads"], output["faces"]
    assert MIN_RATIO * target <= quads <= MAX_RATIO * target, (quads, target)
    assert faces - quads <= 0.02 * faces, (faces, quads)


def check_build(build):
    """The add-on's build line names the commit CI built, as the CI build step
    sets it: "pr<N> <head>+master" on a PR (PR_NUMBER, PR_HEAD), else
    "[<tag> ]<commit> <date>" with SOURCE_COMMIT, nothing uncommitted (no
    -dirty) and not "dev". Skipped without SOURCE_COMMIT (a local run)."""
    commit = os.environ.get("SOURCE_COMMIT", "")
    if not commit:
        print("blender_checks: SOURCE_COMMIT not set, build line not checked against a commit")
        return
    head = os.environ.get("PR_HEAD", "")
    if head:
        expected = "pr%s %s+master" % (os.environ.get("PR_NUMBER", ""), head[:7])
        assert build == expected, (build, expected)
        return
    match = re.fullmatch(r"(?:\S+ )?([0-9a-f]{7,40}) (\d{4}-\d{2}-\d{2})", build)
    assert match, "build id %r is not '[<tag> ]<commit> <date>' (a -dirty or dev build?)" % build
    assert commit.startswith(match.group(1)), (build, commit)


def check_copy_report(text, result_report):
    """The Copy Report text shows the result box's Folded faces row as the
    report has it: "N (T twisted)" when T > 0, else "N"; "! " in front when
    N > 0. The core must report twisted quads (interface 1.11)."""
    output = result_report["output"]
    assert "twisted_quads" in output and "folded_or_twisted_faces" in output, sorted(output)
    folded, twisted = output["folded_or_twisted_faces"], output["twisted_quads"]
    lines = [line for line in text.splitlines() if "Folded faces:" in line]
    assert len(lines) == 1, lines
    match = re.fullmatch(r"  (! )?Folded faces: (\d+)(?: \((\d+) twisted\))?", lines[0])
    assert match, lines[0]
    assert bool(match.group(1)) == (folded > 0), lines[0]
    assert int(match.group(2)) == folded and int(match.group(3) or 0) == twisted, (lines[0], folded, twisted)
    assert (match.group(3) is not None) == (twisted > 0), lines[0]
    print("blender_checks: %s" % lines[0].strip())
    return folded, twisted
