#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A/B harness: build autoremesher-cli from a git ref, run it over a mesh set, compare two runs.

    ab.py run REF [--set corpus] [--targets 2000,10000] [--resolutions fast,quality]
                  [--only a,b | --skip a,b] [--replicates N] [--cli-arg=--adaptivity=0 ...]
                  [--cmake-arg=-DX=1 ...] [--jobs 2] [--no-metrics] [--remeasure]
                  [--timeout S] [--work DIR]
    ab.py compare A B [--set corpus] [--per-run] [--work DIR]
    ab.py build REF [--cmake-arg=... ...] [--work DIR]
    ab.py lane BASE HEAD [--sets closed,corpus,...] [--replicates 1] [--jobs 2] [--work DIR]

REF is any commit-ish of this repository (master, HEAD, a SHA); commit work in progress first. A
and B are run folders printed by `run`, or refs (then without --cli-arg). The work folder (default
$AR_WORK, else ~/ar-work) needs the meshes from fetch_meshes.sh and make_synthetic.py in
<work>/meshes; builds, perturbed inputs and runs go next to it:

    <work>/builds/<key>[+<cmake args>]/   source export of REF and its CMake Release build
    <work>/inputs/              1-ulp replicate inputs, metrics.py's input cache (inputs/metrics)
    <work>/runs/<key>[+<cmake args>][+<args>]/<set>/<mesh>/<resolution>-t<target>/u<k>/
                                out.obj, report.json, stages.jsonl, metrics.json, record.json

<key> hashes the trees of everything that is compiled into the CLI (src/, include/, thirdparty/,
CMakeLists.txt), not the commit, so a rebase or a docs-only commit reuses builds and runs. A run
whose record.json exists is not repeated; delete its folder to force it.

Replicate u0 is the input as is; uk (k >= 1) moves every coordinate to float32 and then one float32
ulp up or down (seeded by k), the precision Blender hands the add-on. Discrete outcomes (topology,
"fixed on mesh X") flip under such noise, so `compare` calls a run fixed or broken only when every
replicate agrees; see "Noise rule" in README.md.

`lane` decides which test lane a change needs (README.md "Test lanes"): none if BASE and HEAD have the
same key, light if HEAD's out.obj is byte-identical to BASE's in every run of every set (plain input and one
float32 replicate by default), full otherwise. It runs both sides without metrics; a later `run` of the same folders adds them.

Runs on Linux, macOS and Windows (release-check.yml uses all three); Windows records no peak RSS.
"""
import argparse
import concurrent.futures as cf
import hashlib
import json
import math
import os
import re
import shutil
import statistics as st
import subprocess
import sys
import tarfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
# AR_CORPUS points at another file of the same shape, e.g. private meshes that must not enter the
# repo; its "file" entries may be absolute paths.
CORPUS = os.environ.get("AR_CORPUS") or os.path.join(HERE, "corpus.json")
METRICS = os.path.join(HERE, "metrics.py")
# Everything compiled into autoremesher-cli. A change elsewhere (docs, add-on, tools, CI) keeps the key.
KEY_PATHS = ["src", "include", "thirdparty", "CMakeLists.txt"]


def git(*args):
    return subprocess.run(["git", "-C", REPO] + list(args), check=True, capture_output=True, text=True).stdout.strip()


def args_tag(args):
    return "+" + hashlib.sha1("\0".join(args).encode()).hexdigest()[:8] if args else ""


def source_key(ref, cmake_args=()):
    """Key of REF's build; CMake options (experiments behind a build switch) get their own key."""
    trees = []
    for path in KEY_PATHS:
        try:
            trees.append(path + " " + git("rev-parse", f"{ref}:{path}"))
        except subprocess.CalledProcessError:
            trees.append(path + " -")
    return hashlib.sha1("\n".join(trees).encode()).hexdigest()[:12] + args_tag(cmake_args)


def built_cli(build_dir):
    """The CLI in a build folder: single-config generators put it at the top, Visual Studio in Release/."""
    exe = "autoremesher-cli" + (".exe" if os.name == "nt" else "")
    for path in (os.path.join(build_dir, exe), os.path.join(build_dir, "Release", exe)):
        if os.path.exists(path):
            return path
    return None


def build(ref, work, jobs, cmake_args=()):
    """Exports REF, builds autoremesher-cli once per key and returns its path."""
    key = source_key(ref, cmake_args)
    root = os.path.join(work, "builds", key)
    cli = built_cli(os.path.join(root, "build"))
    if cli:
        return key, cli
    src = os.path.join(root, "src")
    os.makedirs(src, exist_ok=True)
    # tarfile, not tar: Windows has no tar that reads git's output reliably.
    archive = subprocess.Popen(["git", "-C", REPO, "archive", git("rev-parse", ref)], stdout=subprocess.PIPE)
    with tarfile.open(fileobj=archive.stdout, mode="r|") as tar:
        tar.extractall(src, filter="data")
    if archive.wait() != 0:
        sys.exit(f"git archive {ref} failed")
    log = os.path.join(root, "build.log")
    print(f"building {ref} ({key}) with {jobs} jobs, log {log}", flush=True)
    nice = ["nice", "-n", "10"] if shutil.which("nice") else []
    with open(log, "w") as fp:
        for cmd in (["cmake", "-S", src, "-B", os.path.join(root, "build"), "-DCMAKE_BUILD_TYPE=Release"] + list(cmake_args),
                    nice + ["cmake", "--build", os.path.join(root, "build"), "--config", "Release",
                            "--parallel", str(jobs), "--target", "autoremesher-cli"]):
            if subprocess.call(cmd, stdout=fp, stderr=subprocess.STDOUT) != 0:
                sys.exit(f"build of {ref} failed, see {log}")
    cli = built_cli(os.path.join(root, "build"))
    if not cli:
        sys.exit(f"build of {ref} made no autoremesher-cli, see {log}")
    return key, cli


def load_set(name):
    corpus = json.load(open(CORPUS))
    if name not in corpus["sets"]:
        sys.exit(f"unknown set {name}; corpus.json has {', '.join(corpus['sets'])}")
    return corpus["sets"][name], [m for m in corpus["meshes"] if m.get("set", "corpus") == name]


def file_hash(path):
    h = hashlib.sha256()
    with open(path, "rb") as fp:
        for block in iter(lambda: fp.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()[:16]


def perturbed(src, k, work):
    """Replicate k of an input: float32, then one float32 ulp in a seeded random direction."""
    if k == 0:
        return src
    import numpy as np

    dst = os.path.join(work, "inputs", f"{file_hash(src)}-u{k}.obj")
    if os.path.exists(dst):
        return dst
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    rng = np.random.default_rng(k)
    tmp = f"{dst}.{os.getpid()}.{threading.get_ident()}.tmp"  # parallel runs of one replicate race here
    with open(src) as fi, open(tmp, "w") as fo:
        for line in fi:
            if line.startswith("v "):
                p = line.split()
                xyz = np.array([float(x) for x in p[1:4]], np.float32)
                xyz = np.nextafter(xyz, np.where(rng.random(3) < 0.5, -np.inf, np.inf).astype(np.float32))
                fo.write("v %.9g %.9g %.9g\n" % tuple(float(x) for x in xyz))
            else:
                fo.write(line)
    try:
        os.replace(tmp, dst)
    except PermissionError:  # Windows: a parallel run wrote the same file and a CLI has it open
        if not os.path.exists(dst):
            raise
        os.remove(tmp)
    return dst


def metrics_cache(work):
    return os.path.join(work, "inputs", "metrics")


def prepare_input(src, work):
    """Fills metrics.py's input cache once per mesh, so runs started together don't each fill it."""
    subprocess.call([sys.executable, METRICS, "--input", src, "--input-cache", metrics_cache(work), "--prepare-input"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def measure(rec, d, mesh, target, src, work):
    mj = os.path.join(d, "metrics.json")
    cmd = [sys.executable, METRICS, "--input", src, "--output", os.path.join(d, "out.obj"),
           "--target-quads", str(target), "--samples", "100000", "--json", mj, "--input-cache", metrics_cache(work)]
    if mesh.get("sym"):
        axis, center = mesh["sym"]
        if center == "auto":  # bounding-box centre on that axis, as run_corpus.py does
            col = "xyz".index(axis) + 1
            xs = [float(line.split()[col]) for line in open(src) if line.startswith("v ")]
            center = (min(xs) + max(xs)) / 2
        cmd += ["--sym-axis", axis, "--sym-center", repr(float(center))]
    args = rec.get("cli_args") or []
    if "--sharp-edge" in args[:-1]:  # A1theta and bent quads at the run's own sharp-edge angle
        cmd += ["--sharp-degrees", args[args.index("--sharp-edge") + 1]]
    if os.path.exists(mj):  # a failed --remeasure must not leave the old metrics in place
        os.remove(mj)
    subprocess.call(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        rec["metrics"] = json.load(open(mj))
    except (OSError, ValueError):
        rec["metrics"] = None


LOG_CAP = 8 << 20


def copy_capped(src, dst):
    """Copies a run's output to its log, keeping the first LOG_CAP bytes: a hung run that keeps
    printing must not fill the disk (one libQEx hang filled a cloud container's twice)."""
    written = 0
    for block in iter(lambda: src.read(1 << 16), b""):
        if written < LOG_CAP:
            dst.write(block[:LOG_CAP - written])
            written += len(block)


def call_measured(cmd, log, timeout):
    """Run cmd; return (exit status, peak RSS in MB of that child). On timeout the child is killed
    and the status is "timeout". wait4 gives this child's own rusage, unlike RUSAGE_CHILDREN, which
    mixes the runs of parallel jobs."""
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    reader = threading.Thread(target=copy_capped, args=(p.stdout, log), daemon=True)
    reader.start()
    if not hasattr(os, "wait4"):  # Windows: no rusage, so no peak RSS
        try:
            rc = p.wait(timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            rc = "timeout"
        reader.join()
        return rc, None
    scale = 1 << 20 if sys.platform == "darwin" else 1 << 10  # ru_maxrss in bytes on macOS, KiB on Linux; MB out
    t0 = time.time()
    while True:
        pid, status, ru = os.wait4(p.pid, os.WNOHANG)
        if pid:
            p.returncode = os.waitstatus_to_exitcode(status)  # reaped here, so Popen must not wait again
            reader.join()
            return p.returncode, ru.ru_maxrss / scale
        if timeout and time.time() - t0 > timeout:
            p.kill()
            _, _, ru = os.wait4(p.pid, 0)
            p.returncode = -9
            reader.join()
            return "timeout", ru.ru_maxrss / scale
        time.sleep(0.05)


def run_one(cli, mesh, setname, target, res, k, out, cli_args, work, metrics, jobs, remeasure=False, timeout=None):
    d = os.path.join(out, setname, mesh["id"], f"{res}-t{target}", f"u{k}")
    record_path = os.path.join(d, "record.json")
    src = os.path.join(work, "meshes", mesh["file"])
    if os.path.exists(record_path):
        rec = json.load(open(record_path))
        # A finished run is never repeated, but its metrics can be recomputed after metrics.py changed,
        # and are added to a run made with --no-metrics (e.g. by `lane`) once a run asks for them.
        if (remeasure or (metrics and "metrics" not in rec)) and rec["rc"] == 0 \
                and os.path.exists(os.path.join(d, "out.obj")):
            measure(rec, d, mesh, target, src, work)
            with open(record_path, "w") as fp:
                json.dump(rec, fp, indent=1)
        return rec
    os.makedirs(d, exist_ok=True)
    inp = perturbed(src, k, work)
    cmd = [cli, "--input", inp, "--output", os.path.join(d, "out.obj"), "--target-quads", str(target),
           "--resolution", res, "--report-json", os.path.join(d, "report.json"),
           "--stage-diagnostics", os.path.join(d, "stages.jsonl")] + cli_args
    t0 = time.time()
    with open(os.path.join(d, "log.txt"), "wb") as log:
        rc, rss = call_measured(cmd, log, timeout)
    rec = {"mesh": mesh["id"], "set": setname, "target": target, "res": res, "replicate": k, "rc": rc,
           "wall": time.time() - t0, "peak_rss_mb": rss, "cli_args": cli_args, "jobs": jobs,
           "input_sha": file_hash(inp)}
    if os.path.exists(os.path.join(d, "out.obj")):
        rec["out_sha"] = file_hash(os.path.join(d, "out.obj"))
    try:
        rep = json.load(open(os.path.join(d, "report.json")))
        rec["output"] = rep.get("output")
        rec["diagnostics"] = rep.get("diagnostics")
        rec["time"] = rep.get("time", {}).get("total_seconds")
    except (OSError, ValueError) as ex:
        rec["error"] = str(ex)
    try:
        stages = [json.loads(line) for line in open(os.path.join(d, "stages.jsonl")) if line.strip()]
        rec["cover"] = [{k2: s.get(k2) for k2 in ("continuous_energy", "rounded_energy", "iterations")}
                        for s in stages if s.get("stage") == "cover_result"]
        rec["t_topology"] = t_topology(stages)
        rec["extractor"] = extractor_summary(stages)
    except (OSError, ValueError) as ex:
        rec["stage_error"] = str(ex)
    if metrics and rc == 0 and os.path.exists(os.path.join(d, "out.obj")):
        measure(rec, d, mesh, target, src, work)
    with open(record_path, "w") as fp:
        json.dump(rec, fp, indent=1)
    return rec


def extractor_summary(stages):
    """Islands libQEx extracted and those that fell back to our tracing (TODO 6.1b), with the reasons.

    An island whose result was unhealthy is run again; the records of dropped attempts are marked
    superseded. `fallbacks` counts the islands whose output came from our tracing, `attempt_fallbacks`
    every extraction attempt that fell back."""
    recs = [s for s in stages if s.get("stage") == "extractor"]
    final = [s for s in recs if s.get("superseded") != "yes"]
    return {"islands": len(final), "fallbacks": sum(1 for s in final if s.get("used") == "trace"),
            "attempts": len(recs), "attempt_fallbacks": sum(1 for s in recs if s.get("used") == "trace"),
            "reasons": sorted({s.get("fallback_reason") for s in recs if s.get("used") == "trace"})}


def t_topology(stages):
    """T's defects from the run record (cores from 2026-10-20 on; None before)."""
    run = next((s for s in stages if s.get("stage") == "run"), {})
    keys = ("decimated_non_manifold_edges", "isotropic_non_manifold_edges", "isotropic_opened_closed_islands")
    return {k: run[k] for k in keys} if all(k in run for k in keys) else None


def has_metrics(d):
    try:
        return "metrics" in json.load(open(os.path.join(d, "record.json")))
    except (OSError, ValueError):
        return False


def run_dir(ref, cli_args, work, cmake_args=()):
    return os.path.join(work, "runs", source_key(ref, cmake_args) + args_tag(cli_args))


def cmd_run(a):
    defaults, meshes = load_set(a.set)
    if a.only:
        meshes = [m for m in meshes if m["id"] in a.only.split(",")]
    if a.skip:
        meshes = [m for m in meshes if m["id"] not in a.skip.split(",")]
    if not meshes:
        sys.exit("no meshes selected")
    if not a.no_metrics:
        try:
            import igl  # noqa: F401
            import scipy  # noqa: F401
        except ImportError:
            sys.exit("metrics need numpy, scipy and libigl in this Python (the venv from build.sh), or pass --no-metrics")
    for m in meshes:
        if not os.path.exists(os.path.join(a.work, "meshes", m["file"])):
            sys.exit(f"missing {m['file']} in {a.work}/meshes (fetch_meshes.sh, make_synthetic.py; see README.md)")
    _, cli = build(a.ref, a.work, a.jobs, a.cmake_arg)
    out = run_dir(a.ref, a.cli_arg, a.work, a.cmake_arg)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "source.json"), "w") as fp:
        json.dump({"ref": a.ref, "commit": git("rev-parse", a.ref), "key": source_key(a.ref, a.cmake_arg),
                   "cli_args": a.cli_arg, "cmake_args": a.cmake_arg}, fp, indent=1)
    targets = [int(t) for t in a.targets.split(",")] if a.targets else defaults["targets"]
    resolutions = a.resolutions.split(",") if a.resolutions else defaults["resolutions"]
    jobs = [(m, t, r, k) for m in meshes for t in targets for r in resolutions for k in range(a.replicates + 1)]
    print(f"{len(jobs)} runs into {out}", flush=True)
    with cf.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        if not a.no_metrics:
            pending = {m["id"]: m for m, t, r, k in jobs
                       if a.remeasure or not has_metrics(os.path.join(out, a.set, m["id"], f"{r}-t{t}", f"u{k}"))}
            list(pool.map(lambda m: prepare_input(os.path.join(a.work, "meshes", m["file"]), a.work), pending.values()))
        futs = [pool.submit(run_one, cli, m, a.set, t, r, k, out, a.cli_arg, a.work, not a.no_metrics,
                            a.jobs, a.remeasure, a.timeout)
                for m, t, r, k in jobs]
        for f in futs:
            rec = f.result()
            o, d = rec.get("output") or {}, rec.get("diagnostics") or {}
            print(f"{rec['mesh']:20s} {rec['res']:7s} {rec['target']:6d} u{rec['replicate']} rc={rec['rc']} "
                  f"quads={o.get('quads')} non_quads={o.get('non_quads')} flips={d.get('uv_flipped_triangles')} "
                  f"same_topology={topology_ok(rec)} t={rec.get('time')}", flush=True)
    print(out)


# ---- compare ---------------------------------------------------------------------------------

def load_runs(path, setname):
    runs = {}
    base = os.path.join(path, setname)
    if not os.path.isdir(base):
        sys.exit(f"no {setname} runs in {path}")
    for root, _, files in os.walk(base):
        if "record.json" in files:
            r = json.load(open(os.path.join(root, "record.json")))
            if "out_sha" not in r and os.path.exists(os.path.join(root, "out.obj")):
                r["out_sha"] = file_hash(os.path.join(root, "out.obj"))
            if ("t_topology" not in r or "extractor" not in r) and os.path.exists(os.path.join(root, "stages.jsonl")):
                stages = [json.loads(line) for line in open(os.path.join(root, "stages.jsonl")) if line.strip()]
                r.setdefault("t_topology", t_topology(stages))
                r.setdefault("extractor", extractor_summary(stages))
            runs.setdefault((r["mesh"], r["res"], r["target"]), {})[r["replicate"]] = r
    return runs


def topology_ok(r):
    t = ((r.get("metrics") or {}).get("topology") or {})
    return t.get("same_as_input")


def island_ok(r):
    return ((r.get("metrics") or {}).get("validity") or {}).get("islands_same_as_input")


def value(r, k):
    d, o, m = r.get("diagnostics") or {}, r.get("output") or {}, r.get("metrics") or {}
    if k == "flips_cont": return d.get("uv_flipped_triangles_continuous")
    if k == "flips_round": return d.get("uv_flipped_triangles_rounded")
    if k == "flips": return d.get("uv_flipped_triangles")
    if k == "degenerate": return d.get("uv_degenerate_triangles")
    if k == "non_quads": return o.get("non_quads")
    if k == "sj_le_0": return (m.get("quads") or {}).get("sj_le_0")
    if k == "flipped_vs_input": return (m.get("flipped") or {}).get("faces_vs_input")
    if k == "flipped_vs_raw": return (m.get("flipped") or {}).get("faces_vs_raw_input")
    if k == "flipped_off_folds": return (m.get("flipped") or {}).get("faces_off_input_folds")
    if k == "flipped_over_folds": return (m.get("flipped") or {}).get("faces_over_input_folds")
    if k in ("folded_faces", "holes_added", "holes_closed", "handles_lost", "handles_added", "islands_dropped",
             "islands_merged", "area_uncovered", "area_wrong_side"):
        return (m.get("validity") or {}).get(k)
    if k == "nm_vertices": return (m.get("validity") or {}).get("non_manifold_vertices")
    if k == "islands_changed": return (m.get("validity") or {}).get("islands_topology_changed")
    if k == "irregular_1k": return (m.get("valence") or {}).get("irregular_per_1k_quads")
    if k == "hausdorff": return (m.get("distance") or {}).get("hausdorff")
    if k == "in_out_mean": return (m.get("distance") or {}).get("in_to_out_mean")
    if k == "in_out_p99": return (m.get("distance") or {}).get("in_to_out_p99")
    if k == "vertex_dist": return (m.get("distance") or {}).get("vertex_to_in_median")
    if k == "normal_dev": return (m.get("normal_deviation_deg") or {}).get("mean")
    if k == "sj_p1": return (m.get("quads") or {}).get("sj_p1")
    if k == "skew_p95": return (m.get("quads") or {}).get("skew_p95")
    if k == "crease_cov": return (m.get("creases") or {}).get("coverage")
    if k == "crease_cov_theta": return (m.get("creases") or {}).get("coverage_theta")
    if k == "crease_bent_1k": return (m.get("creases") or {}).get("bent_quads_per_1k")
    if k == "count_err": return abs(o["quads"] / r["target"] - 1) if o.get("quads") else None
    if k == "energy_ratio":
        e = [(c["continuous_energy"], c["rounded_energy"]) for c in r.get("cover") or []
             if c.get("continuous_energy") and c.get("rounded_energy") is not None]
        return sum(b for _, b in e) / sum(a for a, _ in e) if e else None
    if k == "time": return r.get("time")
    if k == "peak_rss_mb": return r.get("peak_rss_mb")
    if k == "t_non_manifold": return (r.get("t_topology") or {}).get("isotropic_non_manifold_edges")
    if k == "t_opened": return (r.get("t_topology") or {}).get("isotropic_opened_closed_islands")
    if k == "qex_fallbacks":
        e = r.get("extractor") or {}
        return e.get("fallbacks") if e.get("islands") else None
    raise KeyError(k)


COUNTS = ["flips_cont", "flips_round", "flips", "degenerate", "non_quads", "sj_le_0", "flipped_vs_input",
          "t_non_manifold", "t_opened", "qex_fallbacks",
          # Validity from metrics.py (2026-10-22): per input island, matched to the output's islands.
          "folded_faces", "nm_vertices", "holes_added", "holes_closed", "handles_lost", "handles_added",
          "islands_dropped", "islands_merged", "islands_changed", "flipped_vs_raw",
          # V3's split (2026-10-23): flips over folded input vs the rest
          "flipped_off_folds", "flipped_over_folds"]
MEANS = ["area_uncovered", "area_wrong_side", "irregular_1k", "in_out_p99", "hausdorff", "in_out_mean", "normal_dev", "vertex_dist", "sj_p1",
         "skew_p95", "crease_cov", "crease_cov_theta", "crease_bent_1k", "count_err", "energy_ratio", "time", "peak_rss_mb"]
# Rows where a higher value is the better one (for "runs B better / worse").
HIGHER_IS_BETTER = {"sj_p1", "crease_cov", "crease_cov_theta"}


def resolve(spec, work):
    if os.path.isdir(spec):
        return spec
    try:
        return run_dir(spec, [], work)
    except subprocess.CalledProcessError:
        sys.exit(f"{spec} is neither a run folder nor a git ref")


def fmt(x):
    if x is None or (isinstance(x, float) and math.isnan(x)):
        return "–"
    if isinstance(x, int) or float(x).is_integer() and abs(x) >= 10:
        return f"{x:.0f}"
    return f"{x:.4g}"


def cmd_compare(a):
    pa, pb = resolve(a.a, a.work), resolve(a.b, a.work)
    A, B = load_runs(pa, a.set), load_runs(pb, a.set)
    keys = sorted(set(A) & set(B))
    reps = sorted(set.intersection(*[set(A[k]) & set(B[k]) for k in keys])) if keys else []
    print(f"A = {pa}\nB = {pb}\nset {a.set}: {len(keys)} common runs, replicates {reps} "
          f"(only in A {len(set(A) - set(B))}, only in B {len(set(B) - set(A))})\n")
    if not keys:
        return
    failed = [(k, side) for k in keys for side, D in (("A", A), ("B", B)) if any(D[k][u]["rc"] != 0 for u in reps)]
    if failed:
        print("**Failed runs** (non-zero exit in some replicate; left out below): "
              + ", ".join(f"{side} {k[0]} {k[1]} {k[2]}" for k, side in failed) + "\n")
    bad = {k for k, _ in failed}
    keys = [k for k in keys if k not in bad]
    # metrics.py before 2026-10-22 measured flipped_vs_input against the raw input's normals and had no
    # validity block; a side measured that way is not comparable on those rows.
    for side, D, p in (("A", A, pa), ("B", B, pb)):
        old = sum(1 for k in keys for u in reps if D[k][u].get("metrics") and "validity" not in D[k][u]["metrics"])
        if old:
            print(f"**{side} has {old} replicate-runs measured by an older metrics.py** (flipped_vs_input against the "
                  f"raw input, no validity rows): `ab.py run <ref> --remeasure` with the same flags updates {p}.\n")

    def mean_over(D, k, metric):
        xs = [value(D[k][u], metric) for u in reps]
        xs = [x for x in xs if x is not None]
        return st.mean(xs) if xs else None

    # Aggregates: per run the mean over replicates, then sums (counts) or means over runs.
    print("| metric | A | B | B/A geo-mean | runs B better / worse |")
    print("|---|---|---|---|---|")
    for metric in COUNTS + MEANS:
        va = [mean_over(A, k, metric) for k in keys]
        vb = [mean_over(B, k, metric) for k in keys]
        pairs = [(x, y) for x, y in zip(va, vb) if x is not None and y is not None]
        if not pairs:
            continue
        agg = sum if metric in COUNTS else st.mean
        shift = 1 if metric in COUNTS else 0
        logs = [math.log((y + shift) / (x + shift)) for x, y in pairs if x + shift > 0 and y + shift > 0]
        gm = math.exp(st.mean(logs)) if logs else float("nan")
        better = sum(1 for x, y in pairs if (y > x if metric in HIGHER_IS_BETTER else y < x))
        worse = sum(1 for x, y in pairs if (y < x if metric in HIGHER_IS_BETTER else y > x))
        print(f"| {metric} | {fmt(agg(x for x, _ in pairs))} | {fmt(agg(y for _, y in pairs))} | ×{gm:.3f} | "
              f"{better} / {worse} |")
    print("\nCounts are summed over runs (geo-mean of (B+1)/(A+1)), the rest averaged. time is the CLI's own "
          "total under --jobs parallel load: use it for large effects only.\n")
    # Gates like "no run over 2× master's time or memory" look at the worst run, not the mean.
    for metric in ("time", "peak_rss_mb"):
        ratios = [(mean_over(B, k, metric) / mean_over(A, k, metric), k) for k in keys
                  if mean_over(A, k, metric) and mean_over(B, k, metric) is not None]
        if ratios:
            r, k = max(ratios)
            print(f"Worst run for {metric} (mean over replicates): B/A ×{r:.2f}, {k[0]} {k[1]} {k[2]}")
    print()
    # Each parallel run slows the others (4 jobs on 4 cores: about +20 % CLI time against 2 jobs), so
    # a time row is only fair between sides run with the same --jobs. Records before 2026-10-21 lack it.
    ja = {A[k][u].get("jobs") for k in keys for u in reps}
    jb = {B[k][u].get("jobs") for k in keys for u in reps}
    if ja != jb or len(ja) > 1:
        fj = lambda j: ", ".join(sorted(str(x) if x else "unrecorded" for x in j))  # noqa: E731
        print(f"**time is not comparable**: A ran with --jobs {fj(ja)}, B with --jobs {fj(jb)}.\n")

    # Noise: with replicates, how often B's spread separates from A's.
    if len(reps) > 1:
        same = {side: sum(1 for k in keys for u in reps[1:]
                    if D[k][u].get("out_sha") and D[k][u].get("out_sha") == D[k][0].get("out_sha"))
                for side, D in (("A", A), ("B", B))}
        if any(same.values()):
            print(f"Replicates with output identical to the plain run (no extra information): A {same['A']}, "
                  f"B {same['B']} of {len(keys) * (len(reps) - 1)}\n")
        print(f"**Replicates** ({len(reps)} per run): runs where every B replicate is below (above) every A one\n")
        print("| metric | B lower, separated | B higher, separated | median CV of A over replicates |")
        print("|---|---|---|---|")
        for metric in ("flips", "non_quads", "sj_le_0", "hausdorff"):
            lo = hi = 0
            cvs = []
            for k in keys:
                xa = [value(A[k][u], metric) for u in reps]
                xb = [value(B[k][u], metric) for u in reps]
                if None in xa or None in xb:
                    continue
                lo += max(xb) < min(xa)
                hi += min(xb) > max(xa)
                if st.mean(xa) > 0:
                    cvs.append(st.pstdev(xa) / st.mean(xa))
            print(f"| {metric} | {lo} | {hi} | {st.median(cvs):.2f} |" if cvs else f"| {metric} | {lo} | {hi} | – |")
        print()

    # Topology: a discrete outcome, so only unanimous replicates count as a claim.
    ta = {k: [topology_ok(A[k][u]) for u in reps] for k in keys}
    tb = {k: [topology_ok(B[k][u]) for u in reps] for k in keys}
    if any(None in v for v in list(ta.values()) + list(tb.values())):
        print("Topology: not measured in every run (run without --no-metrics).")
    else:
        n = len(reps)
        fixed = [k for k in keys if not any(ta[k]) and all(tb[k])]
        broken = [k for k in keys if all(ta[k]) and not any(tb[k])]
        noisy = [k for k in keys if 0 < sum(ta[k]) < n or 0 < sum(tb[k]) < n]
        avg = lambda T: sum(sum(v) for v in T.values()) / n  # noqa: E731
        print(f"Topology same as input: A {avg(ta):.1f}, B {avg(tb):.1f} of {len(keys)} runs "
              f"(mean over {n} replicate{'s' if n > 1 else ''})")
        name = lambda k: f"{k[0]} {k[1]} {k[2]}"  # noqa: E731
        if n == 1:
            print("Single replicate: the lists below are candidates, not claims. Re-run them with --replicates 5.")
        print("- fixed: " + (", ".join(map(name, fixed)) or "none"))
        print("- broken: " + (", ".join(map(name, broken)) or "none"))
        if noisy:
            print("- replicates disagree (noise, count neither way): "
                  + ", ".join(f"{name(k)} (A {sum(ta[k])}/{n}, B {sum(tb[k])}/{n})" for k in noisy))
        # Per island (validity.islands_same_as_input): also catches a handle or hole moved between
        # islands and non-manifold vertices, which the global check above cannot see.
        ia = [island_ok(A[k][u]) for k in keys for u in reps]
        ib = [island_ok(B[k][u]) for k in keys for u in reps]
        if None not in ia + ib:
            print(f"Topology same as input per island: A {sum(ia) / n:.1f}, B {sum(ib) / n:.1f} of {len(keys)} runs")

    # libQEx fallbacks (TODO 6.1b): how often B's islands went to our tracing, why, and what those
    # replicate-runs look like on both sides.
    ex = {(k, u): B[k][u].get("extractor") or {} for k in keys for u in reps}
    if any(e.get("islands") for e in ex.values()):
        fb = [ku for ku, e in ex.items() if e.get("attempt_fallbacks", e.get("fallbacks"))]
        reasons = {}
        for ku in fb:
            for r in ex[ku].get("reasons") or []:
                r = re.sub(r"\d+", "#", r or "?")
                reasons[r] = reasons.get(r, 0) + 1
        print(f"\n**libQEx fallbacks in B**: {len(fb)} of {len(ex)} replicate-runs, "
              f"{sum(e.get('fallbacks', 0) for e in ex.values())} of {sum(e.get('islands', 0) for e in ex.values())} "
              f"islands output by our tracing, "
              f"{sum(e.get('attempt_fallbacks', e.get('fallbacks', 0)) for e in ex.values())} of "
              f"{sum(e.get('attempts', e.get('islands', 0)) for e in ex.values())} extraction attempts fell back "
              f"(an unhealthy island is run again)")
        for r, c in sorted(reasons.items(), key=lambda x: -x[1]):
            print(f"- {c}× {r}")
        if fb:
            print("\n| on those replicate-runs | A | B |\n|---|---|---|")
            for metric in ("flipped_vs_input", "sj_le_0", "non_quads"):
                xs = [(value(A[k][u], metric), value(B[k][u], metric)) for k, u in fb]
                xs = [(x, y) for x, y in xs if x is not None and y is not None]
                if xs:
                    print(f"| {metric} (sum) | {fmt(sum(x for x, _ in xs))} | {fmt(sum(y for _, y in xs))} |")
            print(f"| topology same as input | {sum(bool(topology_ok(A[k][u])) for k, u in fb)} | "
                  f"{sum(bool(topology_ok(B[k][u])) for k, u in fb)} |")
            print("\nFallback replicate-runs: " + ", ".join(f"{k[0]} {k[1]} {k[2]} u{u}" for k, u in sorted(fb)))

    if a.per_run:
        cols = ["flips", "non_quads", "sj_le_0", "hausdorff", "area_uncovered"]
        print("\n| mesh | res | target | " + " | ".join(f"{c} A → B" for c in cols) + " | topology A → B |")
        print("|---|---|---|" + "---|" * (len(cols) + 1))
        for k in keys:
            cells = [f"{fmt(mean_over(A, k, c))} → {fmt(mean_over(B, k, c))}" for c in cols]
            print(f"| {k[0]} | {k[1]} | {k[2]} | " + " | ".join(cells)
                  + f" | {sum(map(bool, ta[k]))}/{len(reps)} → {sum(map(bool, tb[k]))}/{len(reps)} |")


# ---- lane ------------------------------------------------------------------------------------

def output_diff(B, H, reps=(0,)):
    """Replicates `reps` of two load_runs() results: (identical count, differing (key, replicate),
    failed (key, replicate, BASE rc, HEAD rc)). A replicate-run missing or failing on either side
    counts as failed, never as identical."""
    same, differ, failed = 0, [], []
    for k in sorted(set(B) | set(H)):
        for u in reps:
            rb, rh = B.get(k, {}).get(u), H.get(k, {}).get(u)
            if not rb or not rh or rb["rc"] != 0 or rh["rc"] != 0 or not rb.get("out_sha"):
                failed.append((k, u, rb and rb["rc"], rh and rh["rc"]))
            elif rb["out_sha"] == rh.get("out_sha"):
                same += 1
            else:
                differ.append((k, u))
    return same, differ, failed


def cmd_lane(a):
    """Which test lane HEAD needs against BASE: none, light or full (README.md "Test lanes")."""
    kb, kh = source_key(a.base), source_key(a.head)
    print(f"BASE {a.base} = {git('rev-parse', '--short', a.base)} key {kb}\n"
          f"HEAD {a.head} = {git('rev-parse', '--short', a.head)} key {kh}")
    if kb == kh:
        print("Nothing compiled into the CLI differs (src/, include/, thirdparty/, CMakeLists.txt).\nlane: none")
        return 0
    corpus = json.load(open(CORPUS))
    # A set without default targets (gallery: one target per entry, run by tools/gallery) has no plain runs.
    sets = a.sets.split(",") if a.sets else [s for s, d in corpus["sets"].items() if d.get("targets")]
    missing = [s for s in sets if any(not os.path.exists(os.path.join(a.work, "meshes", m["file"]))
                                      for m in load_set(s)[1])]
    if missing and not a.sets:
        print(f"**Not checked** (meshes missing in {a.work}/meshes): {', '.join(missing)}")
        sets = [s for s in sets if s not in missing]
    elif missing:
        sys.exit(f"missing meshes for {', '.join(missing)} (fetch_meshes.sh, make_synthetic.py, make_sculpt.py)")
    t0 = time.time()
    same, differ, failed = 0, [], []
    for setname in sets:
        for ref in (a.base, a.head):
            ra = argparse.Namespace(ref=ref, set=setname, only=None, skip=None, targets=None, resolutions=None,
                                    replicates=a.replicates, cli_arg=[], cmake_arg=[], jobs=a.jobs, no_metrics=True,
                                    remeasure=False, timeout=a.timeout, work=a.work)
            with open(os.devnull, "w") as quiet:
                stdout, sys.stdout = sys.stdout, quiet
                try:
                    cmd_run(ra)
                finally:
                    sys.stdout = stdout
        s, d, f = output_diff(load_runs(run_dir(a.base, [], a.work), setname),
                              load_runs(run_dir(a.head, [], a.work), setname), range(a.replicates + 1))
        same += s
        differ += [(setname,) + x for x in d]
        failed += [(setname,) + x for x in f]
    print(f"Sets {', '.join(sets)}, replicates u0-u{a.replicates}: {same} runs byte-identical, {len(differ)} differ, "
          f"{len(failed)} failed or missing ({time.time() - t0:.0f} s)")
    for setname, k, u in differ[:20]:
        print(f"- differs: {setname} {k[0]} {k[1]} {k[2]} u{u}")
    if len(differ) > 20:
        print(f"- ... and {len(differ) - 20} more")
    for setname, k, u, rb, rh in failed:
        print(f"- failed: {setname} {k[0]} {k[1]} {k[2]} u{u} (BASE rc {rb}, HEAD rc {rh})")
    lane = "full" if differ or failed or missing else "light"
    print(f"lane: {lane}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--work", default=os.environ.get("AR_WORK", os.path.expanduser("~/ar-work")))
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="build autoremesher-cli from REF and print its path")
    b.add_argument("ref")
    b.add_argument("--jobs", type=int, default=2)
    b.add_argument("--cmake-arg", action="append", default=[],
                   help="extra CMake configure argument, repeatable; write it as --cmake-arg=-DX=1")
    r = sub.add_parser("run", help="run REF's CLI over a mesh set")
    r.add_argument("ref")
    r.add_argument("--set", default="corpus")
    r.add_argument("--targets", help="comma list; default from the set in corpus.json")
    r.add_argument("--resolutions", help="comma list of fast,quality; default from the set")
    r.add_argument("--only")
    r.add_argument("--skip")
    r.add_argument("--replicates", type=int, default=0, help="1-ulp replicates besides the plain input")
    r.add_argument("--cli-arg", action="append", default=[],
                   help="extra CLI argument, repeatable; write it as --cli-arg=--adaptivity=0")
    r.add_argument("--cmake-arg", action="append", default=[],
                   help="extra CMake configure argument, repeatable; write it as --cmake-arg=-DX=1")
    r.add_argument("--jobs", type=int, default=2, help="parallel runs and build jobs (default 2)")
    r.add_argument("--no-metrics", action="store_true")
    r.add_argument("--remeasure", action="store_true",
                   help="recompute metrics.json of finished runs (after metrics.py changed); outputs are kept")
    r.add_argument("--timeout", type=float, help="kill a run after this many seconds; recorded as rc \"timeout\"")
    c = sub.add_parser("compare", help="compare run folder (or ref) A with B")
    c.add_argument("a")
    c.add_argument("b")
    c.add_argument("--set", default="corpus")
    c.add_argument("--per-run", action="store_true")
    ln = sub.add_parser("lane", help="which test lane HEAD needs against BASE: none, light or full")
    ln.add_argument("base", help="the commit HEAD branched from or last merged (git merge-base master HEAD)")
    ln.add_argument("head")
    ln.add_argument("--sets", help="comma list; default every set in corpus.json whose meshes are present")
    ln.add_argument("--replicates", type=int, default=1,
                    help="float32 1-ulp replicates besides the plain input (default 1: Blender hands the add-on float32)")
    ln.add_argument("--jobs", type=int, default=2)
    ln.add_argument("--timeout", type=float, default=1200)
    for p in (b, r, c, ln):
        p.add_argument("--work", default=argparse.SUPPRESS)
    a = ap.parse_args()
    a.work = os.path.abspath(a.work)
    if a.cmd == "build":
        print(build(a.ref, a.work, a.jobs, a.cmake_arg)[1])
    elif a.cmd == "run":
        cmd_run(a)
    elif a.cmd == "lane":
        sys.exit(cmd_lane(a))
    else:
        cmd_compare(a)


if __name__ == "__main__":
    main()
