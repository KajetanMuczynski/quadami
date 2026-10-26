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

"""Turn the remesher's JSON report into rows for the panel. No bpy in here."""

import textwrap

# Characters per line of a warning: panel labels don't wrap, and the sidebar
# is about this wide at its default size.
WARNING_WIDTH = 36

STATUS_TEXT = {
    "ok": "Done",
    "cancelled": "Cancelled",
    "failed": "Failed",
    "invalid_input": "Invalid input",
}


def offers_fold_repair(report):
    """Whether the run found folded input and did not repair it."""
    return (report.get("status") == "ok"
            and int(report.get("diagnostics", {}).get("input_fold_clusters", 0)) > 0
            and not report.get("parameters", {}).get("repair_folds"))


def spike_text(diagnostics):
    """How many spike tips were lowered and the largest drop in quad edges."""
    spikes = int(diagnostics.get("input_fold_spikes", 0))
    if not spikes:
        return "0"
    return "%d, tip lowered by up to %.1f quads" % (spikes, float(diagnostics.get("input_fold_spike_drop_quads", 0.0)))


def summary_rows(report):
    """(label, value, is_problem) rows for a report dict from --report-json."""
    rows = []
    output = report.get("output", {})
    target = report.get("parameters", {}).get("target_quads", 0)
    if report.get("status") == "ok":
        quads = output.get("quads", 0)
        ratio = " (%d%%)" % round(100.0 * quads / target) if target else ""
        rows.append(("Quads", "%d of %d target%s" % (quads, target, ratio), False))

        def count(label, value):
            rows.append((label, str(value), value > 0))

        count("Non-quads", output.get("non_quads", 0))
        # Only parts that were closed in the input can get a hole; an open
        # mesh's own borders are not counted.
        islands = report.get("islands", {})
        if islands.get("closed_in_input", 0):
            holes = islands.get("holes", 0)
            rows.append(("Holes", "%d (%d open edges)" % (holes, islands.get("hole_edges", 0))
                         if holes else "0", holes > 0))
        # Handles (tunnels) against the input, per island; reports from cores
        # before interface 1.7 don't have them.
        if "handles_lost" in islands:
            lost, added = islands.get("handles_lost", 0), islands.get("handles_added", 0)
            parts = (["%d lost" % lost] if lost else []) + (["%d added" % added] if added else [])
            rows.append(("Handles", ", ".join(parts) if parts else "as input", bool(parts)))
        count("Non-manifold edges", output.get("non_manifold_edges", 0))
        count("Non-manifold vertices", output.get("non_manifold_vertices", 0))
        count("Winding flips", output.get("orientation_flips", 0))
        count("Folded faces", output.get("folded_faces", 0))
        # The core's input cleanup: open seams it welded, duplicate triangles
        # it removed, pinched vertices it split, islands with almost no area
        # for their length (zero-width slivers) it left out, triangles on a zero-length edge it dropped and
        # triangles it turned to match their neighbours.  Not a problem, only
        # reported.
        diagnostics = report.get("diagnostics", {})
        fixes = []
        welded = int(diagnostics.get("input_welded_edges", 0))
        if welded:
            fixes.append("%d seam edges welded" % welded)
        duplicates = int(diagnostics.get("input_duplicate_triangles", 0))
        if duplicates:
            fixes.append("%d duplicate faces removed" % duplicates)
        split = int(diagnostics.get("input_split_vertices", 0))
        if split:
            fixes.append("%d pinched vertices split" % split)
        slivers = int(diagnostics.get("input_sliver_islands", 0))
        if slivers:
            fixes.append("%d zero-width sliver islands left out" % slivers)
        dropped = int(diagnostics.get("input_dropped_triangles", 0))
        if dropped:
            fixes.append("%d degenerate triangles removed" % dropped)
        flipped = int(diagnostics.get("input_flipped_triangles", 0))
        if flipped:
            fixes.append("%d triangles turned" % flipped)
        if fixes:
            rows.append(("Input cleanup", ", ".join(fixes), False))
        # Folded input: spots where a brush folded the surface over itself.
        # Counted in faces: a repair can split one spot into several smaller
        # ones, so spots before minus spots after says little.
        spots = int(diagnostics.get("input_fold_clusters", 0))
        if spots and report.get("parameters", {}).get("repair_folds"):
            faces = int(diagnostics.get("input_folded_triangles", 0))
            faces_left = int(diagnostics.get("input_folded_triangles_after", 0))
            left = int(diagnostics.get("input_fold_clusters_after", 0))
            rows.append(("Folds repaired", "%d of %d faces" % (max(faces - faces_left, 0), faces), False))
            if faces_left:
                rows.append(("Folds left", "%d faces in %d spots" % (faces_left, left), True))
            if report.get("parameters", {}).get("flatten_spikes"):
                rows.append(("Spikes flattened", spike_text(diagnostics), False))
    islands = report.get("islands", {})
    empty = islands.get("without_output", 0)
    rows.append(("Empty islands", "%d of %d" % (empty, islands.get("input", 0)), empty > 0))
    # Parts of the input farther than three quad widths from the result: a
    # limb, vine or spike that came out missing.  Older workers don't send it.
    lost = report.get("lost_parts")
    if report.get("status") == "ok" and lost is not None:
        far = int(lost.get("far_input_vertices", 0))
        rows.append(("Missing parts", "up to %d quad widths" % int(lost.get("farthest_input_quad_widths", 0))
                     if far else "none", far > 0))
    seconds = report.get("time", {}).get("total_seconds")
    if seconds is not None:
        rows.append(("Time", "%.1f s" % seconds, False))
    return rows


def wrap(text, width=WARNING_WIDTH):
    """A panel text wrapped to the panel's width."""
    return textwrap.wrap(text, width)


def warning_lines(report, width=WARNING_WIDTH):
    """(text, first, is_problem) lines for the report's warnings, wrapped to
    the panel's width; first marks the line that starts a warning or a hint."""
    lines = []

    def add(text, problem):
        for i, line in enumerate(wrap(text, width)):
            lines.append((line, i == 0, problem))

    for warning in report.get("warnings", []):
        add(warning, True)
    if report.get("status") == "ok" and report.get("islands", {}).get("holes", 0):
        add("Another Target Quads value may close them. To see the holes: Tab into "
            "Edit Mode, press 1 for vertex select, then Select > Select All by Trait "
            "> Non Manifold.", False)
    if report.get("status") == "ok" and report.get("diagnostics", {}).get("thin_parts_warned"):
        add("To thicken a part: Tab into Edit Mode, select it, press Alt+S "
            "(Shrink/Fatten) and move the mouse outwards.", False)
    if offers_fold_repair(report):
        diagnostics = report.get("diagnostics", {})
        add("Folded input: %d faces in %d spots, where the surface folds over "
            "itself. The quads there may come out messy or with holes."
            % (int(diagnostics.get("input_folded_triangles", 0)),
               int(diagnostics.get("input_fold_clusters", 0))), True)
    elif report.get("status") == "ok" and report.get("parameters", {}).get("repair_folds"):
        diagnostics = report.get("diagnostics", {})
        faces_left = int(diagnostics.get("input_folded_triangles_after", 0))
        if faces_left:
            add("%d folded faces in %d spots could not be repaired safely and were left "
                "as they were." % (faces_left, int(diagnostics.get("input_fold_clusters_after", 0))), False)
        spikes = int(diagnostics.get("input_fold_spikes", 0))
        if spikes:
            add("Flattening spikes changed the shape: %d spike tips were lowered to their base, "
                "the farthest by %.1f quad edges." % (spikes, float(diagnostics.get("input_fold_spike_drop_quads", 0.0))), False)
    return lines


def fold_offer_lines(width=WARNING_WIDTH):
    """The hint above the Repair Folds and Re-run button, wrapped."""
    return textwrap.wrap("Repair Folds and Re-run smooths out only those spots, in the copy "
                         "the remesher works on, and remeshes again. Your original object "
                         "is not changed.", width)
