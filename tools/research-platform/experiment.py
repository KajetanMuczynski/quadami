#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Research only (platform divergence study, 2026-10-26); removed from the branch before its PR.

Builds de6ac3a (release-check #5's candidate) as release-check did, and once more with
research-hash.patch (bit hashes of intermediate results as "rh" stage records, plus a Windows
switch for the UCRT's FMA3 code paths), runs the cases in cases.txt and prints, for the job log:

    RES <platform> <tag> <case> rc=.. sha=.. t=.. parsed=..
    RH  <platform> <tag> <case> <island> <point> <hash>
    STG <platform> <tag> <case> <stage record without timings>
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


def build_tree(work, name, cmake_args, jobs, patch, generator_args=()):
    """REF exported, research-hash.patch applied when asked, CMake Release build of the CLI."""
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
    try:
        for ln in open(os.path.join(d, "stages.jsonl")):
            s = json.loads(ln)
            if s.get("stage") == "rh":
                lines.append(f"RH {platform} {tag} {case} {s.get('island', '-')} {s.get('point')} {s.get('h')}")
            elif tag == "H" and s.get("stage") in STG_KEEP:
                keep = {k2: v for k2, v in s.items()
                        if k2 not in ("schema", "version", "input", "target_quads") and not any(x in k2 for x in TIMING)}
                lines.append(f"STG {platform} {tag} {case} {json.dumps(keep, separators=(',', ':'))}")
    except (OSError, ValueError):
        pass
    return lines


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--platform", required=True)
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--cmake-arg", action="append", default=[])
    a = p.parse_args()
    work = os.environ["AR_WORK"]
    out = os.path.join(work, "research")
    cases = [c.strip() for c in open(os.path.join(HERE, "cases.txt")) if c.strip()]
    base_args = ["-DAUTOREMESHER_BUNDLED_TBB=ON"] + a.cmake_arg  # release-check's order, so the key matches run #5

    key, cli_a = ab.build(REF, work, a.jobs, base_args)
    print(f"BUILD A key={key} {compiler_line(os.path.join(work, 'builds', key, 'build.log'))}", flush=True)
    windows = a.platform.startswith("windows")
    # On Windows the hash build also reports the loops MSVC vectorized (a vectorized math call
    # takes another code path in the C library); the report changes no code.
    cli_h = build_tree(work, "H", base_args + (["-DCMAKE_CXX_FLAGS_INIT=/Qvec-report:1"] if windows else []), a.jobs, patch=True)
    print(f"BUILD H ok={bool(cli_h)}", flush=True)
    if windows:
        for ln in open(os.path.join(work, "rbuilds", "H", "build.log"), errors="replace"):
            if "C500" in ln and "AutoRemesher" in ln:
                print("VEC " + ln.strip().split("AutoRemesher", 1)[1], flush=True)
    variants = [("A1", cli_a, {}), ("A2", cli_a, {}), ("H", cli_h, {}), ("H2", cli_h, {})]
    if a.platform.startswith("linux"):
        shim = os.path.join(work, "portm.so")
        subprocess.check_call(["gcc", "-O2", "-fPIC", "-shared", "-fno-builtin", "-o", shim, os.path.join(HERE, "portm.c"), "-lm"])
        mask = "glibc.cpu.hwcaps=-AVX2,-FMA,-AVX512F,-AVX512DQ,-AVX512VL,-AVX512BW,-AVX,-FMA4"
        variants += [("H_nofma", cli_h, {"GLIBC_TUNABLES": mask}),
                     ("H_shim", cli_h, {"LD_PRELOAD": shim}),
                     ("H_shim_nofma", cli_h, {"LD_PRELOAD": shim, "GLIBC_TUNABLES": mask})]
    if windows:
        variants.append(("H_nofma3", cli_h, {"AR_RESEARCH_NO_FMA3": "1"}))
        cli_c = build_tree(work, "H_clangcl", base_args, a.jobs, patch=True, generator_args=["-T", "ClangCL"])
        print(f"BUILD H_clangcl ok={bool(cli_c)} {compiler_line(os.path.join(work, 'rbuilds', 'H_clangcl', 'build.log'))}", flush=True)
        if cli_c:
            variants.append(("H_clangcl", cli_c, {}))
    if a.platform.startswith("macos"):
        # No contraction by the compiler; H_nofma also keeps Eigen from its explicit fused
        # multiply-adds (NEON vfmaq, scalar std::fma), so the arithmetic is the x86 build's.
        for tag, flags in (("H_nocontract", "-ffp-contract=off"),
                           ("H_nofma", "-ffp-contract=off -U__ARM_FEATURE_FMA -DEIGEN_SCALAR_MADD_USE_FMA=0")):
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


if __name__ == "__main__":
    main()
