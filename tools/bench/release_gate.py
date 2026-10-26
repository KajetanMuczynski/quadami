#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Release check on ab.py runs: docs/TARGETS.md gates as pass/fail, against the previous release.

    release_gate.py CANDIDATE [--baseline BASELINE] [--hard] [--set corpus --set closed] [--summary FILE]

CANDIDATE and BASELINE are run folders printed by `ab.py run`, run on the same platform with
metrics. Each run and replicate is scored by gates.py (`gates()`, the one definition of the gates);
this script only decides:

- §3.1 structural gates (H1-H6, H8; H2 for edges only until TODO 2.18, H7 is not checked here).
  With --hard (the 1.0 bar) or without a baseline: no run may fail one. With a baseline (0.x
  previews ship under the ratchet, TARGETS §6 decision 3): over the runs both folders have, no gate
  may fail on more runs than in the baseline, and a candidate run the baseline lacks may fail none.
- §3.2 ratchet, only with a baseline: per validity gate (V1-V6) no fewer clean runs, and per defect
  no more defects per 1k faces, than the baseline. Aggregates over a set need no replicate vote
  (CLAUDE.md, noise rule), so a single extra defect counts as worse.

Prints Markdown (and appends it to --summary, e.g. $GITHUB_STEP_SUMMARY); exits 1 when the check
fails, 2 when a folder has no runs of a set.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ab  # noqa: E402
import gates  # noqa: E402

STRUCTURAL = ["H1 completes", "H2 no non-manifold edge", "H3 consistently oriented",
              "H4 islands kept (none dropped, split or merged)", "H5 no hole added or closed",
              "H6 count within ±10 %", "H8 no 6+-gon"]
VALIDITY = ["V1 all-quad", "V2 no folded quad",
            "V3 no face flipped vs input (off input folds)", "V4 no degenerate face", "V5 no interior valence 2 or ≥ 7",
            "V6 topology same as input per island"]
DEFECTS = ["non-quads", "folded quads", "flipped off input folds", "degenerate", "valence 2 / ≥ 7"]


def score(folder, setname):
    """{(mesh, res, target, replicate): (gates passed, defect counts)} of one set; a run that did not
    complete, or completed without metrics, fails every gate."""
    _, meshes = ab.load_set(setname)
    fams = {m["id"]: gates.family(m, setname) for m in meshes}
    out = {}
    for key, by_rep in ab.load_runs(folder, setname).items():
        for u, rec in by_rep.items():
            run_dir = os.path.join(folder, setname, key[0], f"{key[1]}-t{key[2]}", f"u{u}")
            res = gates.gates(rec, run_dir, fams.get(key[0], setname))
            g, d = res if res is not None else ({"H1 completes": False}, {})
            if not g["H1 completes"]:
                g = {name: False for name in STRUCTURAL + VALIDITY}
            out[key + (u,)] = (g, d)
    return out


def failing(runs, keys, gate):
    return sum(not runs[k][0][gate] for k in keys)


def per_1k(runs, keys, defect):
    faces = sum(runs[k][1].get("faces", 0) for k in keys)
    return 1000.0 * sum(runs[k][1].get(defect, 0) for k in keys) / faces if faces else 0.0


def check_set(cand, base, hard):
    """(Markdown lines, failed?) for one set."""
    lines, bad = [], False
    broken = {k: [g.split()[0] for g in STRUCTURAL if not cand[k][0][g]] for k in cand}
    broken = {k: v for k, v in broken.items() if v}
    lines.append("%d runs, %d fail a structural gate." % (len(cand), len(broken)))
    if base is None or hard:
        bad = bool(broken)
    else:
        common = sorted(set(cand) & set(base))
        bad = any(k not in base for k in broken)
        lines += ["", "Over the %d runs both have (baseline / candidate):" % len(common), "",
                  "| gate | runs failing | |", "|---|---|---|"]
        for g in STRUCTURAL:
            b, c = failing(base, common, g), failing(cand, common, g)
            lines.append("| %s | %d / %d | %s |" % (g, b, c, "**worse**" if c > b else "ok"))
            bad |= c > b
        lines += ["", "| validity | runs clean | |", "|---|---|---|"]
        for g in VALIDITY:
            b, c = len(common) - failing(base, common, g), len(common) - failing(cand, common, g)
            lines.append("| %s | %d / %d | %s |" % (g, b, c, "**worse**" if c < b else "ok"))
            bad |= c < b
        lines += ["", "| defect | per 1k faces | |", "|---|---|---|"]
        for d in DEFECTS:
            b, c = per_1k(base, common, d), per_1k(cand, common, d)
            lines.append("| %s | %.3f / %.3f | %s |" % (d, b, c, "**worse**" if c > b + 1e-12 else "ok"))
            bad |= c > b + 1e-12
        if len(common) < len(cand):
            lines += ["", "%d candidate runs have no baseline run." % (len(cand) - len(common))]
    if broken:
        lines += ["", "<details><summary>Runs failing a structural gate</summary>", "",
                  "| mesh | resolution | target | replicate | fails |", "|---|---|---|---|---|"]
        lines += ["| %s | %s | %d | u%d | %s |" % (k + (", ".join(v),)) for k, v in sorted(broken.items())]
        lines += ["", "</details>"]
    nmv = sum(d.get("non-manifold vertices", 0) for _, d in cand.values())
    lines += ["", "Non-manifold vertices (H2, counted, not gated until TODO 2.18): %d" % nmv]
    return lines, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("candidate")
    ap.add_argument("--baseline")
    ap.add_argument("--set", action="append", dest="sets")
    ap.add_argument("--hard", action="store_true", help="structural gates on every run (the 1.0 bar)")
    ap.add_argument("--summary")
    a = ap.parse_args()
    lines, bad = ["## Release check", "", "Candidate: `%s`" % a.candidate], False
    if a.baseline:
        lines.append("Baseline: `%s`" % a.baseline)
    lines.append("Structural gates: %s" % ("hard (1.0)" if a.hard or not a.baseline else "no worse than the baseline (0.x)"))
    for setname in a.sets or ["corpus", "closed"]:
        cand = score(a.candidate, setname)
        base = score(a.baseline, setname) if a.baseline else None
        if not cand or base == {}:
            print("no %s runs in %s" % (setname, a.baseline if cand else a.candidate), file=sys.stderr)
            sys.exit(2)
        set_lines, set_bad = check_set(cand, base, a.hard)
        lines += ["", "### Set %s: %s" % (setname, "FAIL" if set_bad else "pass"), ""] + set_lines
        bad |= set_bad
    lines += ["", "**Result: %s**" % ("FAIL" if bad else "pass")]
    text = "\n".join(lines) + "\n"
    print(text)
    if a.summary:
        with open(a.summary, "a", encoding="utf-8") as f:
            f.write(text)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
