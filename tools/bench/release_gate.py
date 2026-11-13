#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Release check on ab.py runs: docs/TARGETS.md gates as pass/fail, against the previous release.

    release_gate.py CANDIDATE [--baseline BASELINE] [--hard] [--set corpus --set closed] [--summary FILE]
    release_gate.py --pair CAND1 BASE1 --pair CAND2 BASE2 ...   (one pair per platform, pooled)

CANDIDATE and BASELINE are run folders printed by `ab.py run` (or the records of one, as
release-check.yml uploads them), with metrics; each pair ran on one platform, because platforms give
different output. Each run and replicate is scored by gates.py (`gates()`, the one definition of the
gates); this script only decides (TARGETS §6 decision 3, TODO 1.16).

Per run, on every pair:
- A baseline run the candidate lacks fails; a run without metrics stops the check (exit 2).
- H1: a candidate run that crashed or timed out fails, unless the baseline crashed on some replicate
  of that case too (listed then).
- With --hard (the 1.0 bar) or without a baseline: no run may fail a structural gate (H2-H6, H8; H2
  for edges only until TODO 2.18, H7 is not checked here).

Per case, with a baseline (the release decision): for each measure, the candidate-minus-baseline
count per case (mesh, resolution, target), summed over the replicates of every pair, and a one-sided
sign test over the cases that moved. Measures: the 5 defects behind V1-V5 (faces), and for each
structural gate and V6 the replicates failing it. The case is the unit, because the replicates of a
case repeat its decisions (1-ulp inputs); a change that only moves last bits moves cases both ways.
- **fail**: significant after Holm at --alpha over every measure of every set.
- **lead**: p < --alpha before Holm. Does not fail the check; re-check it with `ab.py --replicates 5`
  on the cases listed (docs/RELEASING.md).
Per-pair columns are a report; only the pooled test decides. `--decide runs` reports the tests and
decides on the per-run checks only (release-check.yml's per-platform jobs). The aggregates (defects
per 1k faces) are printed as context, not gated: they move by noise on every neutral change.

Prints Markdown (and appends it to --summary, e.g. $GITHUB_STEP_SUMMARY); exits 1 when the check
fails, 2 when a folder has no runs of a set or a run completed without metrics.
"""
import argparse
import math
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
# What the sign test counts per run: a defect's count, or 1 for a failed gate.
MEASURES = [("defect", d) for d in DEFECTS] + [("gate", g) for g in STRUCTURAL[1:] + VALIDITY[-1:]]


def score(folder, setname):
    """({(mesh, res, target, replicate): (gates passed, defect counts)}, [runs without metrics]) of one
    set; a run that did not complete fails every gate. A run that completed without metrics (metrics.py
    failed) is not scored: it is listed, and main() refuses to decide rather than call it a crash (H1)."""
    _, meshes = ab.load_set(setname)
    fams = {m["id"]: gates.family(m, setname) for m in meshes}
    out, unmeasured = {}, []
    for key, by_rep in ab.load_runs(folder, setname).items():
        for u, rec in by_rep.items():
            run_dir = os.path.join(folder, setname, key[0], f"{key[1]}-t{key[2]}", f"u{u}")
            res = gates.gates(rec, run_dir, fams.get(key[0], setname))
            if res is None:
                unmeasured.append(key + (u,))
                continue
            g, d = res
            if not g["H1 completes"]:
                g = {name: False for name in STRUCTURAL + VALIDITY}
            out[key + (u,)] = (g, d)
    return out, unmeasured


def count(run, measure):
    kind, name = measure
    g, d = run
    if kind == "gate":
        return int(not g[name])
    return d.get(name, 0) if g["H1 completes"] else 0  # a crash counts in H1, not as zero defects


def sign_p(worse, better):
    """One-sided exact sign test: P(at least `worse` of worse + better cases are worse | no change)."""
    n = worse + better
    return sum(math.comb(n, i) for i in range(worse, n + 1)) / 2.0 ** n if n else 1.0


def holm(ps, alpha):
    """Which p-values Holm's step-down rejects at family-wise level alpha."""
    order = sorted(range(len(ps)), key=lambda i: ps[i])
    rejected = [False] * len(ps)
    for j, i in enumerate(order):
        if ps[i] > alpha / (len(ps) - j):
            break
        rejected[i] = True
    return rejected


def case_moves(pairs, measure):
    """{case: candidate minus baseline count over the replicates both sides have, summed over pairs}
    and the same per pair."""
    total, per_pair = {}, []
    for cand, base in pairs:
        diff = {}
        for k in set(cand) & set(base):
            diff[k[:3]] = diff.get(k[:3], 0) + count(cand[k], measure) - count(base[k], measure)
        per_pair.append(diff)
        for c, v in diff.items():
            total[c] = total.get(c, 0) + v
    return total, per_pair


def tally(diff):
    worse = sum(v > 0 for v in diff.values())
    better = sum(v < 0 for v in diff.values())
    return worse, better, sign_p(worse, better)


def run_checks(cand, base, hard):
    """(Markdown lines, failed?) of the per-run checks of one pair and set."""
    lines, bad = [], False
    broken = {k: [g.split()[0] for g in STRUCTURAL if not cand[k][0][g]] for k in cand}
    broken = {k: v for k, v in broken.items() if v}
    lines.append("%d runs, %d fail a structural gate." % (len(cand), len(broken)))
    # A run the candidate lacks (a mesh missing from its run, a run folder cut short) would otherwise
    # drop out of every row below and pass unseen.
    lost = sorted(set(base) - set(cand)) if base is not None else []
    if lost:
        lines.append("**%d baseline runs have no candidate run** (fails the check): %s" % (
            len(lost), ", ".join("%s %s %d u%d" % k for k in lost[:20]) + (" ..." if len(lost) > 20 else "")))
        bad = True
    crashed = sorted(k for k in cand if not cand[k][0]["H1 completes"])
    if crashed:
        excused = set()
        if base is not None and not hard:
            base_crash_cases = {k[:3] for k in base if not base[k][0]["H1 completes"]}
            excused = {k for k in crashed if k[:3] in base_crash_cases}
        new = [k for k in crashed if k not in excused]
        if new:
            lines.append("**%d candidate runs crashed or timed out** (H1, fails the check): %s" % (
                len(new), ", ".join("%s %s %d u%d" % k for k in new[:20])))
            bad = True
        if excused:
            lines.append("%d candidate runs crashed in cases where the baseline crashed too (listed, not failed): %s" % (
                len(excused), ", ".join("%s %s %d u%d" % k for k in sorted(excused)[:20])))
    if base is None or hard:
        bad |= bool(broken)
    if broken:
        lines += ["", "<details><summary>Runs failing a structural gate</summary>", "",
                  "| mesh | resolution | target | replicate | fails |", "|---|---|---|---|---|"]
        lines += ["| %s | %s | %d | u%d | %s |" % (k + (", ".join(v),)) for k, v in sorted(broken.items())]
        lines += ["", "</details>"]
    nmv = sum(d.get("non-manifold vertices", 0) for _, d in cand.values())
    lines.append("Non-manifold vertices (H2, counted, not gated until TODO 2.18): %d" % nmv)
    return lines, bad


def per_1k(runs, defect):
    faces = sum(d.get("faces", 0) for _, d in runs.values())
    return 1000.0 * sum(d.get(defect, 0) for _, d in runs.values()) / faces if faces else 0.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("candidate", nargs="?")
    ap.add_argument("--baseline")
    ap.add_argument("--pair", nargs="+", action="append", default=[], metavar="FOLDER",
                    help="CANDIDATE [BASELINE] of one platform; repeat per platform to pool them")
    ap.add_argument("--set", action="append", dest="sets")
    ap.add_argument("--hard", action="store_true", help="structural gates on every run (the 1.0 bar)")
    ap.add_argument("--alpha", type=float, default=0.05)
    ap.add_argument("--decide", choices=["all", "runs"], default="all",
                    help="runs: the sign tests are reported only (a per-platform job of a pooled check)")
    ap.add_argument("--summary")
    a = ap.parse_args()
    pairs = [(a.candidate, a.baseline)] if a.candidate else []
    if a.baseline and not a.candidate:
        ap.error("--baseline needs CANDIDATE")
    for p in a.pair:
        if len(p) > 2:
            ap.error("--pair takes CANDIDATE [BASELINE]")
        pairs.append((p[0], p[1] if len(p) == 2 else None))
    if not pairs:
        ap.error("no candidate")
    if len({b is None for _, b in pairs}) > 1:
        ap.error("either every pair has a baseline or none has")
    with_base = pairs[0][1] is not None
    sets = a.sets or ["corpus", "closed"]

    lines, bad = ["## Release check", ""], False
    for i, (c, b) in enumerate(pairs, 1):
        lines.append("Pair %d: candidate `%s`%s" % (i, c, ", baseline `%s`" % b if b else ""))
    lines += ["", "Structural gates: %s. %s" % (
        "on every run (hard, 1.0)" if a.hard or not with_base else "per case, sign test against the baseline (0.x)",
        "Sign tests: %s at α = %g with Holm over %d tests%s." % (
            "pooled over %d pairs" % len(pairs) if len(pairs) > 1 else "one pair", a.alpha,
            len(MEASURES) * len(sets), "" if a.decide == "all" else "; **reported only** (--decide runs)")
        if with_base else "No baseline: no sign tests.")]

    scored = {}
    for setname in sets:
        for c, b in pairs:
            for folder in (c, b):
                if folder is None or (folder, setname) in scored:
                    continue
                runs, missing = score(folder, setname)
                if missing:
                    print("%d %s runs in %s completed without metrics (%s): `ab.py run` measures them again; "
                          "no decision without them" % (len(missing), setname, folder,
                                                        ", ".join("%s %s %d u%d" % k for k in sorted(missing)[:10])),
                          file=sys.stderr)
                    sys.exit(2)
                if not runs:
                    print("no %s runs in %s" % (setname, folder), file=sys.stderr)
                    sys.exit(2)
                scored[folder, setname] = runs

    # Per-run checks, every pair and set.
    for setname in sets:
        for i, (c, b) in enumerate(pairs, 1):
            pl, pbad = run_checks(scored[c, setname], scored[b, setname] if b else None, a.hard)
            lines += ["", "### Set %s, pair %d: runs %s" % (setname, i, "FAIL" if pbad else "pass"), ""]
            lines += pl
            bad |= pbad

    if with_base:
        rows = []
        for setname in sets:
            sp = [(scored[c, setname], scored[b, setname]) for c, b in pairs]
            for m in MEASURES:
                total, per_pair = case_moves(sp, m)
                rows.append((setname, m, tally(total), [tally(d) for d in per_pair], total))
        rejected = holm([r[2][2] for r in rows], a.alpha)
        failed = [r for r, x in zip(rows, rejected) if x]
        leads = [r for r, x in zip(rows, rejected) if not x and r[2][2] < a.alpha]
        cols = " | ".join("pair %d" % i for i in range(1, len(pairs) + 1)) if len(pairs) > 1 else ""
        lines += ["", "### Per case: candidate vs baseline", "",
                  "Cases worse / better (sum over replicates%s), one-sided sign-test p." % (
                      " and pairs" if len(pairs) > 1 else ""), "",
                  "| set | measure | pooled | %s|" % (cols + " | " if cols else ""),
                  "|---|---|---|%s" % ("---|" * (len(pairs) + 1) if cols else "---|")]
        for (setname, m, (w, bt, p), pp, _), rej in zip(rows, rejected):
            verdict = "**fail**" if rej else ("lead" if p < a.alpha else "ok")
            cells = " | ".join("%d / %d, %.3g" % t for t in pp) + " | " if cols else ""
            lines.append("| %s | %s | %d / %d, p = %.3g | %s%s |" % (setname, m[1], w, bt, p, cells, verdict))
        for title, group in (("Failed (Holm)", failed), ("Leads: re-check with `--replicates 5` before tagging", leads)):
            if group:
                lines += ["", "**%s**, cases that got worse (largest first):" % title, ""]
                for setname, m, _, _, total in group:
                    worst = sorted(((v, c) for c, v in total.items() if v > 0), reverse=True)[:8]
                    lines.append("- %s, %s: %s" % (setname, m[1], ", ".join("%s %s %d (+%d)" % (c + (v,)) for v, c in worst)))
        if a.decide == "all":
            bad |= bool(failed)
        lines += ["", "<details><summary>Aggregates, defects per 1k faces (context, not gated)</summary>", "",
                  "| set | pair | " + " | ".join(DEFECTS) + " |", "|---|---|" + "---|" * len(DEFECTS)]
        for setname in sets:
            for i, (c, b) in enumerate(pairs, 1):
                lines.append("| %s | %d | %s |" % (setname, i, " | ".join(
                    "%.4f → %.4f" % (per_1k(scored[b, setname], d), per_1k(scored[c, setname], d)) for d in DEFECTS)))
        lines += ["", "</details>"]

    lines += ["", "**Result: %s**" % ("FAIL" if bad else "pass")]
    text = "\n".join(lines) + "\n"
    print(text)
    if a.summary:
        with open(a.summary, "a", encoding="utf-8") as f:
            f.write(text)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
