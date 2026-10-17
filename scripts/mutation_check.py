#!/usr/bin/env python3
"""Mutation controls: prove that the test-suite can fail.

For every mutation in a JSON spec the script copies the source tree into a scratch directory, applies one
textual change to a source file, builds the named test executable with g++ directly (no CMake) and runs it.
A mutation is KILLED when the test reports at least one failure (non-zero exit or "FAIL" output) and
SURVIVED otherwise. The real source tree is never modified.

Spec format (JSON list):
  {"name": "...", "file": "src/optimizer.cpp", "old": "text to find", "new": "replacement", "test": "test_optimizer"}
Usage: python scripts/mutation_check.py scripts/mutations/v05.json [--cxx g++] [--out log.txt]
Exit code: 0 if every mutation was killed, 1 otherwise.
"""
import argparse
import concurrent.futures
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def compile_one(cxx, src, obj, includes, defs):
    cmd = [cxx, "-std=c++17", "-O0", "-c", src, "-o", obj] + includes + defs
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stderr


def run_mutation(m, cxx, helper):
    work = tempfile.mkdtemp(prefix="nova-mut-")
    try:
        for d in ("src", "include", "tests", "benchmarks"):
            shutil.copytree(os.path.join(ROOT, d), os.path.join(work, d))
        target = os.path.join(work, m["file"])
        with open(target, "r", encoding="utf-8", newline="") as f:
            text = f.read()
        if m["old"] not in text:
            return m["name"], "ERROR", "anchor text not found in " + m["file"]
        with open(target, "w", encoding="utf-8", newline="") as f:
            f.write(text.replace(m["old"], m["new"], 1))
        fwd = work.replace("\\", "/")
        includes = ["-I" + fwd + "/include", "-I" + fwd + "/tests"]
        defs = [
            '-DNOVA_VERSION="0.1.0"',
            '-DNOVA_SOURCE_DIR="%s"' % fwd,
            '-DNOVA_TEST_COMPILER="%s"' % cxx,
            '-DNOVA_TEST_TMP_DIR="%s/tmp"' % fwd,
            '-DNOVA_HELPER_CHILD="%s"' % helper.replace("\\", "/"),
        ]
        sources = sorted(glob.glob(os.path.join(work, "src", "*.cpp")) + glob.glob(os.path.join(work, "src", "*", "*.cpp")))
        sources = [s for s in sources if os.path.basename(s) != "main.cpp"]
        test_src = os.path.join(work, "tests", m["test"] + ".cpp")
        objs = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
            futs = []
            for i, s in enumerate(sources + [test_src]):
                o = os.path.join(work, "obj%d.o" % i)
                objs.append(o)
                futs.append(ex.submit(compile_one, cxx, s, o, includes, defs))
            for f in futs:
                code, err = f.result()
                if code != 0:
                    return m["name"], "ERROR", "mutant does not compile: " + err[:400]
        exe = os.path.join(work, "mutant.exe" if os.name == "nt" else "mutant")
        link = [cxx, "-o", exe] + objs + (["-ladvapi32"] if os.name == "nt" else [])
        r = subprocess.run(link, capture_output=True, text=True)
        if r.returncode != 0:
            return m["name"], "ERROR", "link failed: " + r.stderr[:400]
        os.makedirs(os.path.join(work, "tmp"), exist_ok=True)
        r = subprocess.run([exe], capture_output=True, text=True, cwd=work, timeout=1200)
        fails = (r.stdout + r.stderr).count("FAIL")
        killed = r.returncode != 0 or fails > 0
        return m["name"], "KILLED" if killed else "SURVIVED", "exit=%d, FAIL lines=%d" % (r.returncode, fails)
    finally:
        if os.environ.get("NOVA_MUT_KEEP"):
            print("kept", work)
        else:
            shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("spec")
    ap.add_argument("--cxx", default="g++")
    ap.add_argument("--out")
    ap.add_argument("--helper", default=os.path.join(ROOT, "build", "helper_child.exe" if os.name == "nt" else "helper_child"))
    args = ap.parse_args()
    with open(args.spec, "r", encoding="utf-8") as f:
        muts = json.load(f)
    lines = []
    ok = True
    for m in muts:
        name, status, detail = run_mutation(m, args.cxx, args.helper)
        line = "%-9s %s  [%s]" % (status, name, detail)
        print(line, flush=True)
        lines.append(line)
        ok = ok and status == "KILLED"
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
