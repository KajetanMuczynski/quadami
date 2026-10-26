#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Research only (platform divergence study, 2026-10-26); removed from the branch before its PR.

Builds de6ac3a (release-check #5's candidate) as release-check did, and once more with
research-hash.patch (bit hashes of intermediate results as "rh" stage records, plus a Windows
switch for the UCRT's FMA3 code paths), runs the cases in cases.txt and prints, for the job log:

    RES  <platform> <tag> <case> rc=.. sha=.. t=.. parsed=..
    RHC  <platform> <tag> <case> <point>=<island>:<hash>,... ...   (all hash points of the case)
    STGC <platform> <tag> <case> <JSON list of stage records without timings>
    LIBM/LIBMX lines of libm_table.py for every hash build and intervention

Tags: A1, A2 the plain build twice (determinism on one machine; A1 against run #5 across runners),
H the hash build (control: must equal A1), H2 the hash build again (the hashes are deterministic),
and per platform the interventions.
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import subprocess
import sys
import tarfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "bench"))
import ab  # noqa: E402
import libm_table  # noqa: E402

REF = "de6ac3a13f89d18778f5a109ee2f8a308fb24769"
TIMING = ("seconds", "_ms", "wall", "accumulated", "rss", "cpu", "time")
STG_KEEP = ("frame_field", "singularity_simplifier", "cover_input", "curl_correction", "cover_system", "cover_result", "map_check", "extractor")


MATH = ("sin", "cos", "acos", "atan2", "exp", "log10", "log", "pow", "hypot")
# Sources that call them: the core, the isotropic remesher, libQEx and OpenMesh's core. Not Eigen
# (the core instantiates none of its transcendental code) and not the OBJ reader (its pow calls
# are correctly rounded everywhere, LIBM pow_parse).
DM_GLOBS = ("src/AutoRemesher/*.cpp", "src/AutoRemesher/*.h", "include/AutoRemesher/*.h",
            "thirdparty/isotropicremesher/*.cpp", "thirdparty/isotropicremesher/*.h",
            "thirdparty/libQEx/src/*.cc", "thirdparty/libQEx/src/*.hh", "thirdparty/libQEx/interfaces/c/*.cc",
            "thirdparty/OpenMesh/src/OpenMesh/Core/**/*.cc", "thirdparty/OpenMesh/src/OpenMesh/Core/**/*.hh",
            "src/cli/main.cpp")  # only its --research-libm table: LIBM then measures ar_dm
DM_CMAKE = """
# research: same-bits math (tools/research-platform/dm)
target_sources(autoremesher_core PRIVATE src/AutoRemesher/researchdm.cpp)
foreach(t autoremesher_core autoremesher_qex autoremesher-cli)
    if(MSVC)
        target_compile_options(${t} PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/AutoRemesher/researchdm.h")
    else()
        target_compile_options(${t} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/src/AutoRemesher/researchdm.h")
    endif()
endforeach()
if(NOT MSVC)
    set_source_files_properties(src/AutoRemesher/researchdm.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
"""


def apply_dm(src):
    """Routes the nine math functions to ar_dm:: (dm/researchdm.cpp): std::f( and unqualified f( calls."""
    import glob
    import shutil
    for f in ("researchdm.h", "researchdm.cpp"):
        shutil.copy(os.path.join(HERE, "dm", f), os.path.join(src, "src", "AutoRemesher", f))
    names = "|".join(MATH)
    qualified = re.compile(r"(?<![\w:])(?:std)?::(" + names + r")\s*\(")
    bare = re.compile(r"(?<![\w:.>])(" + names + r")\s*\(")
    count = 0
    for g in DM_GLOBS:
        for path in glob.glob(os.path.join(src, g), recursive=True):
            if path.endswith("researchdm.h") or path.endswith("researchdm.cpp"):
                continue
            text = open(path, encoding="latin-1").read()
            new, n1 = qualified.subn(r"ar_dm::\1(", text)
            new, n2 = bare.subn(r"ar_dm::\1(", new)
            if n1 + n2:
                count += n1 + n2
                open(path, "w", encoding="latin-1", newline="").write(new)
    with open(os.path.join(src, "CMakeLists.txt"), "a") as fp:
        fp.write(DM_CMAKE)
    return count


def apply_tie(src):
    """Singularity pairs sorted by (hops, a, b): no ties left to the library's sort."""
    path = os.path.join(src, "src", "AutoRemesher", "singularitysimplifier.cpp")
    text = open(path).read()
    old = "[](const Candidate& x, const Candidate& y) { return x.hops < y.hops; }"
    new = "[](const Candidate& x, const Candidate& y) { return std::tie(x.hops, x.a, x.b) < std::tie(y.hops, y.a, y.b); }"
    assert text.count(old) == 1, "tie: sort not found"
    text = text.replace(old, new).replace("#include <set>\n", "#include <set>\n#include <tuple>\n", 1)
    open(path, "w", newline="").write(text)


def build_tree(work, name, cmake_args, jobs, patch, generator_args=(), dm=False, tie=False):
    """REF exported, research-hash.patch applied when asked (and dm/tie), CMake Release build of the CLI."""
    root = os.path.join(work, "rbuilds", name)
    cli = ab.built_cli(os.path.join(root, "build"))
    if cli:
        return cli
    src = os.path.join(root, "src")
    os.makedirs(src, exist_ok=True)
    archive = subprocess.Popen(["git", "-C", REPO, "archive", REF], stdout=subprocess.PIPE)
    with tarfile.open(fileobj=archive.stdout, mode="r|") as tar:
        tar.extractall(src, filter="data")
    archive.wait()
    if patch:
        subprocess.check_call(["git", "apply", os.path.join(HERE, "research-hash.patch")], cwd=src)
    if dm:
        print(f"DM {name} calls={apply_dm(src)}", flush=True)
    if tie:
        apply_tie(src)
    log = os.path.join(root, "build.log")
    with open(log, "w") as fp:
        for cmd in (["cmake", "-S", src, "-B", os.path.join(root, "build"), "-DCMAKE_BUILD_TYPE=Release"]
                    + list(generator_args) + list(cmake_args),
                    ["cmake", "--build", os.path.join(root, "build"), "--config", "Release", "--parallel", str(jobs),
                     "--target", "autoremesher-cli"]):
            if subprocess.call(cmd, stdout=fp, stderr=subprocess.STDOUT) != 0:
                print(f"BUILD-FAILED {name}; tail of {log}:")
                print("".join(open(log, errors="replace").readlines()[-30:]))
                return None
    return ab.built_cli(os.path.join(root, "build"))


def compiler_line(build_log):
    try:
        return " | ".join(l.strip() for l in open(build_log, errors="replace") if "compiler identification" in l)
    except OSError:
        return "?"


def run_case(cli, tag, case, work, out, env_extra, platform):
    st, mesh, res, target, k = case.split(":")
    corpus = json.load(open(ab.CORPUS))
    mfile = {m["id"]: m["file"] for m in corpus["meshes"]}
    inp = ab.perturbed(os.path.join(work, "meshes", mfile[mesh]), int(k), work)
    d = os.path.join(out, tag, st, mesh, f"{res}-t{target}", f"u{k}")
    os.makedirs(d, exist_ok=True)
    cmd = [cli, "--input", inp, "--output", os.path.join(d, "out.obj"), "--target-quads", target, "--resolution", res,
           "--report-json", os.path.join(d, "report.json"), "--stage-diagnostics", os.path.join(d, "stages.jsonl")]
    env = dict(os.environ)
    env.update(env_extra)
    t0 = time.time()
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1800)
    wall = time.time() - t0
    open(os.path.join(d, "log.txt"), "wb").write(p.stdout)
    text = p.stdout.decode(errors="replace")
    parsed = re.search(r"RH parsed (\w+)", text)
    sha = ab.file_hash(os.path.join(d, "out.obj")) if p.returncode == 0 and os.path.exists(os.path.join(d, "out.obj")) else None
    try:
        t = json.load(open(os.path.join(d, "report.json"))).get("time", {}).get("total_seconds")
    except (OSError, ValueError):
        t = None
    lines = [f"RES {platform} {tag} {case} rc={p.returncode} sha={sha} t={t} wall={wall:.3f} "
             f"parsed={parsed.group(1) if parsed else '-'} input_sha={ab.file_hash(inp)}"]
    # One line per case: the job log API returns only the last 5,000 lines.
    points, stages = {}, []
    try:
        for ln in open(os.path.join(d, "stages.jsonl")):
            s = json.loads(ln)
            if s.get("stage") == "rh":
                points.setdefault(s.get("point"), []).append(f"{s.get('island', '-')}:{s.get('h')}")
            elif tag == "H" and s.get("stage") in STG_KEEP:
                stages.append({k2: v for k2, v in s.items()
                               if k2 not in ("schema", "version", "input", "target_quads") and not any(x in k2 for x in TIMING)})
    except (OSError, ValueError):
        pass
    if points:
        lines.append(f"RHC {platform} {tag} {case} " + " ".join(f"{k}={','.join(v)}" for k, v in points.items()))
    if stages:
        lines.append(f"STGC {platform} {tag} {case} {json.dumps(stages, separators=(',', ':'))}")
    return lines


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--platform", required=True)
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--cmake-arg", action="append", default=[])
    p.add_argument("--round", default="dm", help="lib (round 2: libm, FMA, compiler) or dm (round 3: same-bits math)")
    a = p.parse_args()
    work = os.environ["AR_WORK"]
    out = os.path.join(work, "research")
    cases = [c.strip() for c in open(os.path.join(HERE, "cases.txt")) if c.strip()]
    base_args = ["-DAUTOREMESHER_BUNDLED_TBB=ON"] + a.cmake_arg  # release-check's order, so the key matches run #5

    if a.round == "dm":
        return round_dm(a, work, out, cases, base_args)
    key, cli_a = ab.build(REF, work, a.jobs, base_args)
    print(f"BUILD A key={key} {compiler_line(os.path.join(work, 'builds', key, 'build.log'))}", flush=True)
    windows = a.platform.startswith("windows")
    # On Windows the hash build also reports the loops MSVC vectorized (a vectorized math call
    # takes another code path in the C library); the report changes no code.
    cli_h = build_tree(work, "H", base_args + (["-DCMAKE_CXX_FLAGS_INIT=/Qvec-report:1"] if windows else []), a.jobs, patch=True)
    print(f"BUILD H ok={bool(cli_h)}", flush=True)
    if windows:
        vec = sorted({ln.strip().split("AutoRemesher", 1)[1] for ln in open(os.path.join(work, "rbuilds", "H", "build.log"), errors="replace")
                      if "C500" in ln and "AutoRemesher" in ln})
        print(f"VEC count={len(vec)}", flush=True)
        for ln in vec[:400]:
            print("VEC " + ln, flush=True)
    variants = [("A1", cli_a, {}), ("A2", cli_a, {}), ("H", cli_h, {}), ("H2", cli_h, {})]
    if a.platform.startswith("linux"):
        shim = os.path.join(work, "portm.so")
        subprocess.check_call(["gcc", "-O2", "-fPIC", "-shared", "-fno-builtin", "-o", shim, os.path.join(HERE, "portm.c"), "-lm"])
        mask = "glibc.cpu.hwcaps=-AVX2,-FMA,-AVX512F,-AVX512DQ,-AVX512VL,-AVX512BW,-AVX,-FMA4"
        variants += [("H_nofma", cli_h, {"GLIBC_TUNABLES": mask}),
                     ("H_shim", cli_h, {"LD_PRELOAD": shim}),
                     ("H_shim_nofma", cli_h, {"LD_PRELOAD": shim, "GLIBC_TUNABLES": mask})]
        # Scalar Eigen, the x86 counterpart of macOS's H_novec_nofma.
        cli_v = build_tree(work, "H_novec", base_args + ["-DCMAKE_CXX_FLAGS_INIT=-DEIGEN_DONT_VECTORIZE"], a.jobs, patch=True)
        print(f"BUILD H_novec ok={bool(cli_v)}", flush=True)
        if cli_v:
            variants.append(("H_novec", cli_v, {}))
    if windows:
        variants.append(("H_nofma3", cli_h, {"AR_RESEARCH_NO_FMA3": "1"}))
        cli_c = build_tree(work, "H_clangcl", base_args, a.jobs, patch=True, generator_args=["-T", "ClangCL"])
        print(f"BUILD H_clangcl ok={bool(cli_c)} {compiler_line(os.path.join(work, 'rbuilds', 'H_clangcl', 'build.log'))}", flush=True)
        if cli_c:
            variants.append(("H_clangcl", cli_c, {}))
    if a.platform.startswith("macos"):
        # H_nocontract: no contraction by the compiler. H_novec_nofma: no fused multiply-add at
        # all; Eigen uses them explicitly on arm64 (NEON vfmaq, scalar std::fma) whatever the
        # contraction setting, so it runs scalar without them. Compare with Linux's H_novec.
        for tag, flags in (("H_nocontract", "-ffp-contract=off"),
                           ("H_novec_nofma", "-ffp-contract=off -DEIGEN_DONT_VECTORIZE -DEIGEN_SCALAR_MADD_USE_FMA=0")):
            cli_n = build_tree(work, tag, base_args + ["-DCMAKE_C_FLAGS_INIT=-ffp-contract=off", f"-DCMAKE_CXX_FLAGS_INIT={flags}"],
                               a.jobs, patch=True)
            print(f"BUILD {tag} ok={bool(cli_n)}", flush=True)
            if cli_n:
                variants.append((tag, cli_n, {}))
    for tag, cli, env in variants:
        if not cli:
            continue
        with cf.ThreadPoolExecutor(a.jobs) as pool:
            for lines in pool.map(lambda c: run_case(cli, tag, c, work, out, env, a.platform), cases):
                print("\n".join(lines), flush=True)
    for tag, cli, env in variants:
        if cli and tag.startswith("H") and tag != "H2":
            print("\n".join(libm_table.evaluate(cli, work, tag, env, a.platform)), flush=True)
    print("EXPERIMENT-DONE", flush=True)


def round_dm(a, work, out, cases, base_args):
    """Round 3: what still differs once every platform computes the same math bits. H is the hash
    build (control, as in round 2); D adds ar_dm (dm/), v scalar Eigen, F no fused multiply-add
    (macOS: also no contraction, which fixes the OBJ parse), T the tie-broken pair sort."""
    windows, mac = a.platform.startswith("windows"), a.platform.startswith("macos")
    define = (lambda d: "/D" + d) if windows else (lambda d: "-D" + d)
    if mac:
        nofma = ["-DCMAKE_C_FLAGS_INIT=-ffp-contract=off",
                 "-DCMAKE_CXX_FLAGS_INIT=-ffp-contract=off -DEIGEN_DONT_VECTORIZE -DEIGEN_SCALAR_MADD_USE_FMA=0"]
        builds = [("H", [], False, False), ("DvF", nofma, True, False), ("DvFT", nofma, True, True)]
    else:
        novec = [f"-DCMAKE_CXX_FLAGS_INIT={define('EIGEN_DONT_VECTORIZE')}"]
        builds = [("H", [], False, False), ("D", [], True, False), ("Dv", novec, True, False),
                  ("DT", [], True, True), ("DvT", novec, True, True)]
    variants = []
    for tag, extra, dm, tie in builds:
        cli = build_tree(work, tag, base_args + extra, a.jobs, patch=True, dm=dm, tie=tie)
        print(f"BUILD {tag} ok={bool(cli)} {compiler_line(os.path.join(work, 'rbuilds', tag, 'build.log'))}", flush=True)
        if cli:
            variants.append((tag, cli, {}))
    clis = dict((t, c) for t, c, _ in variants)
    if "D" in clis and a.platform.startswith("linux"):
        mask = "glibc.cpu.hwcaps=-AVX2,-FMA,-AVX512F,-AVX512DQ,-AVX512VL,-AVX512BW,-AVX,-FMA4"
        variants.append(("D_nofma", clis["D"], {"GLIBC_TUNABLES": mask}))
    if "D" in clis and windows:
        variants.append(("D_nofma3", clis["D"], {"AR_RESEARCH_NO_FMA3": "1"}))
    for tag, cli, env in variants:
        with cf.ThreadPoolExecutor(a.jobs) as pool:
            for lines in pool.map(lambda c: run_case(cli, tag, c, work, out, env, a.platform), cases):
                print("\n".join(lines), flush=True)
    for tag, cli, env in variants:
        print("\n".join(libm_table.evaluate(cli, work, tag, env, a.platform)), flush=True)
    print("EXPERIMENT-DONE", flush=True)


if __name__ == "__main__":
    main()
