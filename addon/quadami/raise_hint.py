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

"""Which Target Quads to try when a run lost parts, and whether they came back.
No bpy in here.

Design: docs/RAISE-TO-CLEAR.md (Tier A).  Two rules shape it:
- The core's lost-part rule is relative (3 quad widths of the run's own
  target), so a higher count also counts finer parts as lost and its warning
  need never clear.  "Back" is therefore judged at the first lossy run's
  width: that run's target is the reference, passed to the next runs as
  --lost-parts-reference, and the report's lost_parts.reference counts every
  input vertex still farther than 3 of its quad widths (new losses too, so it
  can only under-report what came back).
- No count is predicted (thickness misses the needed count by up to 30 times,
  and a higher count can lose parts again), so the hint offers the next step
  of a fixed ladder, 1.5 times the reference and then 1.33 times the last
  run, up to 8 times, and only a run says what came back.

The history of runs lives on the source object, keyed by a hash of the input
and the settings that change the output: a step checked under other settings
proves nothing.
"""

import hashlib
import json
import math

import numpy as np

HISTORY_LIMIT = 8
FIRST_STEP = 1.5
NEXT_STEP = 1.33
LAST_STEP = 8.0


def round_up(count):
    """count rounded up to two significant digits (6,140 -> 6,200)."""
    count = int(math.ceil(count))
    if count < 100:
        return count
    step = 10 ** (len(str(count)) - 2)
    return -(-count // step) * step


def next_target(reference, target, maximum):
    """The next count to try after a run at target that did not bring back
    the parts lost at reference, or None past 8 times the reference or the
    add-on's maximum."""
    last = round_up(LAST_STEP * reference)
    if target >= last:
        return None
    step = min(max(round_up(FIRST_STEP * reference), round_up(NEXT_STEP * target)), last)
    return step if step <= maximum else None


def raisable(lost):
    """Whether a higher Target Quads is the lever for these lost parts: at
    least a quarter of the far vertices are in thin parts, separate pieces or
    along open edges (the core's warning rule).  Parts lost for no such cause
    may not be a Target Quads limit at all."""
    far = int(lost.get("far_input_vertices", 0))
    return far > 0 and any(4 * int(lost.get(name, 0)) >= far
                           for name in ("thin_vertices", "piece_vertices", "open_edge_vertices"))


def input_key(positions, triangles, marked, parameters):
    """A hash of the input arrays and every setting but Target Quads."""
    digest = hashlib.blake2b(digest_size=16)
    for array in (positions, triangles, marked):
        array = np.ascontiguousarray(array)
        digest.update(("%s %s;" % (array.dtype.str, array.shape)).encode())
        digest.update(array.tobytes())
    settings = {name: value for name, value in parameters.items() if name != "target_quads"}
    digest.update(json.dumps(settings, sort_keys=True).encode())
    return digest.hexdigest()


def load(text):
    """The history stored on a source object, or None."""
    try:
        history = json.loads(text) if text else None
    except ValueError:
        return None
    return history if isinstance(history, dict) else None


def reference_target(history, key, target):
    """The reference to measure a run at target against: the first lossy
    run's target, if this input and settings had one below target; else 0."""
    if history is None or history.get("key") != key:
        return 0
    reference = int(history.get("reference", 0))
    return reference if 0 < reference < target else 0


def record(history, key, report):
    """The history with the finished run of report added.  A new input or
    new settings start a new history; a lossy run at or below the reference
    (or the first lossy run) becomes the new reference."""
    if report.get("status") != "ok":
        return history
    if history is None or history.get("key") != key:
        history = {"key": key, "reference": 0, "reference_far": 0, "reference_folds": 0,
                   "dismissed": False, "runs": []}
    else:
        history = dict(history, runs=list(history.get("runs", [])))
    target = int(report.get("parameters", {}).get("target_quads", 0))
    lost = report.get("lost_parts") or {}
    folds = int(report.get("output", {}).get("folded_faces", 0))
    run = {"target": target, "quads": int(report.get("output", {}).get("quads", 0)),
           "far": int(lost.get("far_input_vertices", 0)), "folds": folds,
           "holes": int(report.get("islands", {}).get("holes", 0)),
           "seconds": float(report.get("time", {}).get("total_seconds", 0.0))}
    reference = int(history.get("reference", 0))
    measured = lost.get("reference")
    if 0 < reference < target:
        if measured and int(measured.get("target_quads", 0)) == reference:
            run["reference_far"] = int(measured.get("far_input_vertices", 0))
            run["reference_raisable"] = raisable(measured)
    elif run["far"] and raisable(lost):
        history.update(reference=target, reference_far=run["far"], reference_folds=folds)
    else:
        history.update(reference=0, reference_far=0, reference_folds=0)
    history["runs"] = (history["runs"] + [run])[-HISTORY_LIMIT:]
    return history


def hint(history, maximum):
    """What the panel says about the history's last run, as a dict for the
    result's report ("raise"), or None.  state is
    - "try": the reference run itself; next is the first step;
    - "back": nothing is farther than 3 quad widths of the reference;
    - "partly": some points are still missing; next is the next step;
    - "other": what is still missing is not in thin parts, pieces or open
      edges, so a higher count may not help;
    - "not_back": the ladder or the add-on's maximum is used up."""
    if not history or not history.get("runs") or not history.get("reference"):
        return None
    run = history["runs"][-1]
    reference = int(history["reference"])
    target = run["target"]
    result = {"reference": reference, "reference_far": int(history.get("reference_far", 0)),
              "target": target, "dismissed": bool(history.get("dismissed"))}
    if target == reference:
        step = next_target(reference, target, maximum)
        if step is None:
            return None
        result.update(state="try", next=step)
        return result
    if "reference_far" not in run:
        return None
    missing = run["reference_far"]
    result["missing"] = missing
    if missing == 0:
        result.update(state="back", more_folds=max(0, run["folds"] - int(history.get("reference_folds", 0))),
                      finer=run["far"])
        return result
    if not run.get("reference_raisable"):
        result["state"] = "other"
        return result
    step = next_target(reference, target, maximum)
    if step is None:
        result["state"] = "not_back"
    else:
        result.update(state="partly", next=step)
    return result


def _count(value):
    return format(int(value), ",")


def text(hint):
    """The hint's sentences for the panel, unwrapped; "" when dismissed."""
    if not hint or hint.get("dismissed"):
        return ""
    state = hint["state"]
    reference, far, target = _count(hint["reference"]), hint["reference_far"], _count(hint["target"])
    if state == "try":
        return ("Try %s Target Quads (1.5 times). On test models such parts came back at 1.1 to 6 times "
                "as many, sometimes never; only a run on this model shows it." % _count(hint["next"]))
    missing = hint.get("missing", 0)
    if state == "back":
        sentences = "All %s points of the original missing at %s are back at %s." % (_count(far), reference, target)
        if hint.get("more_folds"):
            sentences += " The result has %s more folded faces than at %s." % (_count(hint["more_folds"]), reference)
        if hint.get("finer"):
            sentences += " The warning above is about finer parts, thinner than a quad at %s." % target
        return sentences
    if missing < far:
        found = "%s of %s points missing at %s are back at %s; %s are still missing." % (
            _count(far - missing), _count(far), reference, target, _count(missing))
    else:
        found = "The %s points missing at %s are not back at %s (%s missing now)." % (
            _count(far), reference, target, _count(missing))
    if state == "partly":
        return found + " Try %s." % _count(hint["next"])
    if state == "other":
        return found + " They are not in thin parts, so more Target Quads may not bring them back."
    return ("Not back at up to %s (%.1f times %s): %s of %s points are still missing. These parts may be "
            "too thin for any count you would want: thicken them (Alt+S) or delete them."
            % (target, hint["target"] / hint["reference"], reference, _count(missing), _count(far)))
