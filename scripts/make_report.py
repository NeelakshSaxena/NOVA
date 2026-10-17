#!/usr/bin/env python3
"""Generate the NOVA evaluation report from SAVED raw data only (Python standard library only).

Inputs (all under benchmarks/results/, produced by the nova CLI):
  V0.8-table-*        exhaustive measurement tables (manifest.json + results.json), tuning and holdout sizes
  V0.8-study          replay study (manifest.json, study_runs.csv, study_summary.csv)
  V0.8-real-*         real end-to-end tuning runs and their *-holdout evaluations
  V0.6-baseline-pinned  GCC -O2/-O3 baselines (pinned condition)
Template: report/template.md, with placeholders
  {{num:KEY}}    a value computed here (formatted)        {{table:KEY}}  a generated Markdown table
  {{fig:KEY}}    a generated SVG figure (relative link)   {{verdict:KEY}} a decision computed from the data
Outputs (--out, default report/): report.md, figures/*.svg, tables/*.csv, traceability.json, manifest.json.

Every number that appears in the report is registered with its source files and the formula that produced it
(traceability.json). Missing or malformed input fields are errors (exit code 1) unless --warn-only is given, in
which case they are reported as warnings and the affected values are marked UNAVAILABLE. The template is linted:
measured-looking numbers typed into the prose (not coming from a placeholder) are errors.
"""
import argparse
import csv
import glob
import hashlib
import json
import math
import os
import random
import re
import statistics
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

WARNINGS = []
ERRORS = []


def problem(msg, warn_only):
    target = WARNINGS if warn_only else ERRORS
    if msg not in target:  # report each distinct problem once
        target.append(msg)


# ------------------------------------------------------------------------------------------------------------------
# traceability registry
# ------------------------------------------------------------------------------------------------------------------
class Registry:
    def __init__(self):
        self.items = {}

    def put(self, key, value, fmt, sources, formula):
        if key in self.items:
            raise KeyError("duplicate registry key " + key)
        self.items[key] = {"key": key, "value": value, "text": None if value is None else format(value, fmt) if fmt else str(value),
                           "sources": sorted(set(sources)), "formula": formula}
        return self.items[key]["text"] if value is not None else "UNAVAILABLE"

    def text(self, key):
        return self.items[key]["text"] if self.items[key]["value"] is not None else "UNAVAILABLE"


REG = Registry()
USED_FILES = set()


BASE = [ROOT]  # repository root of the data being reported (set from --data)


def rel(path):
    """Repository-relative path with forward slashes (absolute if the path is on another drive)."""
    try:
        return os.path.relpath(path, BASE[0]).replace("\\", "/")
    except ValueError:
        return path.replace("\\", "/")


def load_json(path, warn_only):
    USED_FILES.add(path)
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        problem("cannot read %s: %s" % (rel(path), e), warn_only)
        return None


def load_csv(path, required, warn_only):
    USED_FILES.add(path)
    try:
        with open(path, "r", encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f))
    except OSError as e:
        problem("cannot read %s: %s" % (rel(path), e), warn_only)
        return []
    if rows:
        missing = [c for c in required if c not in rows[0]]
        if missing:
            problem("%s lacks required columns %s" % (rel(path), missing), warn_only)
            return []
    else:
        problem("%s has no rows" % rel(path), warn_only)
    return rows


REQUIRED_RECORD_FIELDS = ["benchmark", "candidate_id", "input_size", "size_role", "median_ms", "iqr_ms", "q1_ms", "q3_ms", "correctness_status",
                          "build_status", "timing_samples_ms", "repetitions", "binary_size_bytes", "lifecycle", "flags", "compiler_version", "source_hash"]


def load_records(directory, warn_only):
    manifest = load_json(os.path.join(directory, "manifest.json"), warn_only)
    results = load_json(os.path.join(directory, "results.json"), warn_only)
    if manifest is None or results is None:
        return None, []
    recs = results.get("records")
    if not isinstance(recs, list) or not recs:
        problem("%s/results.json has no records" % rel(directory), warn_only)
        return manifest, []
    good = []
    for i, r in enumerate(recs):
        missing = [k for k in REQUIRED_RECORD_FIELDS if k not in r]
        if missing:
            problem("%s/results.json record %d lacks %s" % (rel(directory), i, missing), warn_only)
            continue
        if r["correctness_status"] == "PASS" and len(r["timing_samples_ms"]) != r["repetitions"]:
            problem("%s/results.json record %d (%s n=%s): %d samples but repetitions=%d" % (rel(directory), i, r["candidate_id"], r["input_size"],
                                                                                             len(r["timing_samples_ms"]), r["repetitions"]), warn_only)
            continue
        good.append(r)
    return manifest, good


def f(x, digits=3):
    return format(x, "." + str(digits) + "f")


# ------------------------------------------------------------------------------------------------------------------
# statistics
# ------------------------------------------------------------------------------------------------------------------
def bootstrap_ci(diffs, seed, resamples=4000, alpha=0.05):
    """Percentile bootstrap CI of the mean of `diffs` (resampling the paired differences). Deterministic."""
    if not diffs:
        return None, None
    rng = random.Random(seed)
    n = len(diffs)
    means = []
    for _ in range(resamples):
        s = 0.0
        for _ in range(n):
            s += diffs[rng.randrange(n)]
        means.append(s / n)
    means.sort()
    lo = means[int((alpha / 2) * resamples)]
    hi = means[min(resamples - 1, int((1 - alpha / 2) * resamples))]
    return lo, hi


def verdict_clustered(flags):
    """flags: per budget (ci_lo, ci_hi, workloads_positive, workloads_negative, workloads_total) or (None, None, None).
    A budget counts as POSITIVE if the bootstrap CI of the paired mean difference is entirely above 0 AND the mean
    difference is positive in at least 2/3 of the workloads (the seeds of one workload are not independent evidence
    about other workloads). It counts as NEGATIVE in the mirror-image case.
    SUPPORTED: positive at >= 3/4 of the budgets and negative at none. NOT SUPPORTED: negative at >= 3/4 and positive at none.
    Anything else is INCONCLUSIVE (including 'no difference at all')."""
    valid = [f for f in flags if f[0] is not None]
    if not valid:
        return "UNAVAILABLE"
    need = math.ceil(0.75 * len(valid))
    pos = neg = 0
    for lo, hi, p, n, total in valid:
        if lo > 0 and p >= math.ceil(2.0 * total / 3.0):
            pos += 1
        if hi < 0 and n >= math.ceil(2.0 * total / 3.0):
            neg += 1
    if pos >= need and neg == 0:
        return "SUPPORTED"
    if neg >= need and pos == 0:
        return "NOT SUPPORTED"
    return "INCONCLUSIVE"


# ------------------------------------------------------------------------------------------------------------------
# SVG
# ------------------------------------------------------------------------------------------------------------------
PALETTE = ["#1b6ca8", "#d95f02", "#2a9d5b", "#8e44ad", "#c0392b", "#7f8c8d", "#b8860b", "#2c3e50"]


def svg_lines(title, xlabels, series, ylabel, ymin=None, ymax=None):
    """series: list of (name, [y or None per x]). Returns an SVG string (self-contained, light theme)."""
    W, H, ml, mr, mt, mb = 760, 430, 70, 220, 46, 56
    ys = [y for _, v in series for y in v if y is not None]
    lo = min(ys) if ymin is None else ymin
    hi = max(ys) if ymax is None else ymax
    if hi - lo < 1e-9:
        hi = lo + 1e-3
    pad = (hi - lo) * 0.08
    lo, hi = lo - pad, hi + pad
    pw, ph = W - ml - mr, H - mt - mb

    def X(i):
        return ml + (pw * i / max(1, len(xlabels) - 1))

    def Y(v):
        return mt + ph * (1 - (v - lo) / (hi - lo))

    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" font-family="Segoe UI, Arial, sans-serif" font-size="12">' % (W, H, W, H),
           '<rect width="100%%" height="100%%" fill="#ffffff"/>',
           '<text x="%d" y="24" font-size="15" font-weight="600" fill="#222">%s</text>' % (ml, title)]
    for k in range(6):
        v = lo + (hi - lo) * k / 5
        y = Y(v)
        out.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#e3e3e3"/>' % (ml, y, ml + pw, y))
        out.append('<text x="%d" y="%.1f" text-anchor="end" fill="#555">%s</text>' % (ml - 8, y + 4, f(v, 3)))
    for i, lab in enumerate(xlabels):
        out.append('<text x="%.1f" y="%d" text-anchor="middle" fill="#555">%s</text>' % (X(i), mt + ph + 20, lab))
    out.append('<text x="%.1f" y="%d" text-anchor="middle" fill="#333">budget B (searched candidates evaluated, baselines not counted)</text>' % (ml + pw / 2, H - 12))
    out.append('<text transform="translate(16,%.1f) rotate(-90)" text-anchor="middle" fill="#333">%s</text>' % (mt + ph / 2, ylabel))
    for si, (name, vals) in enumerate(series):
        color = PALETTE[si % len(PALETTE)]
        pts = [(X(i), Y(v)) for i, v in enumerate(vals) if v is not None]
        if len(pts) > 1:
            out.append('<polyline fill="none" stroke="%s" stroke-width="2" points="%s"/>' % (color, " ".join("%.1f,%.1f" % p for p in pts)))
        for (x, y) in pts:
            out.append('<circle cx="%.1f" cy="%.1f" r="3.2" fill="%s"/>' % (x, y, color))
        ly = mt + 14 + si * 20
        out.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="2"/>' % (ml + pw + 16, ly, ml + pw + 40, ly, color))
        out.append('<text x="%d" y="%d" fill="#333">%s</text>' % (ml + pw + 46, ly + 4, name))
    out.append("</svg>")
    return "\n".join(out) + "\n"


# ------------------------------------------------------------------------------------------------------------------
# report assembly
# ------------------------------------------------------------------------------------------------------------------
def md_table(header, rows):
    lines = ["| " + " | ".join(header) + " |", "|" + "|".join("---" for _ in header) + "|"]
    for r in rows:
        lines.append("| " + " | ".join(str(c) for c in r) + " |")
    return "\n".join(lines)


def write_csv(path, header, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(header)
        w.writerows(rows)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        h.update(fh.read())
    return h.hexdigest()


def build(args):
    warn = args.warn_only
    res_dir = os.path.join(args.data, "benchmarks", "results")
    out_dir = args.out
    tables_out, figs_out = {}, {}
    nums = {}
    verdicts = {}

    # ---- exhaustive tables ---------------------------------------------------------------------------------------
    table_dirs = sorted(glob.glob(os.path.join(res_dir, "V0.8-table-*")))
    if not table_dirs:
        problem("no V0.8-table-* directories under %s" % rel(res_dir), warn)
    workloads = {}  # (bench,size) -> {role, entries{id->record}, env, manifest}
    envs = []
    for d in table_dirs:
        manifest, recs = load_records(d, warn)
        if manifest is None:
            continue
        envs.append((rel(d), manifest.get("environment")))
        for r in recs:
            w = workloads.setdefault((r["benchmark"], r["input_size"]), {"role": r["size_role"], "entries": {}, "dir": rel(d), "hash": r["source_hash"]})
            w["entries"][r["candidate_id"]] = r
    env0 = envs[0][1] if envs and envs[0][1] else None
    if env0:
        for name, e in envs[1:]:
            if e != env0:
                problem("environment in %s differs from %s" % (name, envs[0][0]), warn)
        proto = None
        for d in table_dirs:
            m = load_json(os.path.join(d, "manifest.json"), warn)
            if m and "protocol" in m:
                proto = m["protocol"]
                break
        src = [os.path.join(table_dirs[0], "manifest.json")]
        nums["env_os"] = REG.put("env_os", env0.get("os"), None, [rel(s) for s in src], "manifest.environment.os")
        nums["env_cpu"] = REG.put("env_cpu", env0.get("cpu"), None, [rel(s) for s in src], "manifest.environment.cpu")
        nums["env_cpus"] = REG.put("env_cpus", env0.get("logical_cpus"), "d", [rel(s) for s in src], "manifest.environment.logical_cpus")
        nums["env_ram_mib"] = REG.put("env_ram_mib", env0.get("ram_bytes", 0) // (1024 * 1024), "d", [rel(s) for s in src], "ram_bytes / 2^20")
        nums["env_compiler"] = REG.put("env_compiler", env0.get("compiler_version"), None, [rel(s) for s in src], "manifest.environment.compiler_version")
        nums["env_target"] = REG.put("env_target", env0.get("compiler_target"), None, [rel(s) for s in src], "manifest.environment.compiler_target")
        if proto:
            nums["proto_reps"] = REG.put("proto_reps", proto.get("repetitions"), "d", [rel(src[0])], "protocol.repetitions")
            nums["proto_warmup"] = REG.put("proto_warmup", proto.get("warmup_runs"), "d", [rel(src[0])], "protocol.warmup_runs")
            nums["proto_tol"] = REG.put("proto_tol", proto.get("relative_tolerance"), ".0e", [rel(src[0])], "protocol.relative_tolerance")
            nums["proto_affinity"] = REG.put("proto_affinity", "0x%x" % int(proto.get("cpu_affinity_mask", "0")), None, [rel(src[0])], "protocol.cpu_affinity_mask (stored as a decimal string), shown in hex")
    else:
        problem("no environment metadata found in the table manifests", warn)

    wl_keys = sorted(workloads)
    n_tuning = sum(1 for k in wl_keys if workloads[k]["role"] == "tuning")
    n_holdout = sum(1 for k in wl_keys if workloads[k]["role"] == "holdout")
    nums["n_workloads"] = REG.put("n_workloads", len(wl_keys), "d", [w["dir"] for w in workloads.values()], "count of (benchmark,size) tables")
    nums["n_tuning"] = REG.put("n_tuning", n_tuning, "d", [w["dir"] for w in workloads.values()], "tables with size_role=tuning")
    nums["n_holdout"] = REG.put("n_holdout", n_holdout, "d", [w["dir"] for w in workloads.values()], "tables with size_role=holdout")
    all_recs = [r for w in workloads.values() for r in w["entries"].values()]
    nums["n_table_records"] = REG.put("n_table_records", len(all_recs), "d", [w["dir"] for w in workloads.values()], "number of table records")
    failed = [r for r in all_recs if r["correctness_status"] != "PASS" or r["build_status"] != "OK"]
    nums["n_table_failed"] = REG.put("n_table_failed", len(failed), "d", [w["dir"] for w in workloads.values()], "records with build!=OK or correctness!=PASS")
    spaces = {len(w["entries"]) for w in workloads.values()}
    nums["space_total"] = REG.put("space_total", max(spaces) if spaces else None, "d", [w["dir"] for w in workloads.values()], "configurations per table (2 baselines + searchable)")
    nums["space_search"] = REG.put("space_search", (max(spaces) - 2) if spaces else None, "d", [w["dir"] for w in workloads.values()], "configurations per table minus 2 baselines")

    # workload table -----------------------------------------------------------------------------------------------
    wl_rows, csv_rows = [], []
    oracle_gt1 = 0
    for k in wl_keys:
        w = workloads[k]
        e = w["entries"]
        if "O2" not in e or "O3" not in e:
            problem("table %s n=%s lacks a baseline" % (k[0], k[1]), warn)
            continue
        o2, o3 = e["O2"], e["O3"]
        best_base = min(o2["median_ms"], o3["median_ms"])
        elig = [r for r in e.values() if r["correctness_status"] == "PASS" and r["lifecycle"] != "REJECTED_BY_POLICY"]
        best = min(elig, key=lambda r: r["median_ms"])
        osp = best_base / best["median_ms"]
        if osp > 1.0 + 1e-12:
            oracle_gt1 += 1
        src = [w["dir"] + "/results.json"]
        pre = "wl_%s_%d_" % (k[0], k[1])
        cells = [k[0], "{:,}".format(k[1]), w["role"],
                 REG.put(pre + "o2", o2["median_ms"], ".3f", src, "O2 median_ms"), REG.put(pre + "o2iqr", o2["iqr_ms"] / o2["median_ms"] * 100, ".1f", src, "100*iqr_ms/median_ms of O2"),
                 REG.put(pre + "o3", o3["median_ms"], ".3f", src, "O3 median_ms"), REG.put(pre + "o3iqr", o3["iqr_ms"] / o3["median_ms"] * 100, ".1f", src, "100*iqr_ms/median_ms of O3"),
                 REG.put(pre + "ratio", o2["median_ms"] / o3["median_ms"], ".3f", src, "O2 median / O3 median"),
                 best["candidate_id"], REG.put(pre + "oracle", osp, ".3f", src, "min(O2,O3 median) / best configuration median")]
        wl_rows.append(cells)
        csv_rows.append(cells)
    tables_out["workloads"] = md_table(["benchmark", "size n", "role", "O2 ms", "O2 IQR%", "O3 ms", "O3 IQR%", "O2/O3", "best configuration", "best vs best baseline"], wl_rows)
    write_csv(os.path.join(out_dir, "tables", "workloads.csv"), ["benchmark", "size", "role", "o2_ms", "o2_iqr_pct", "o3_ms", "o3_iqr_pct", "o2_over_o3", "best_config", "oracle_speedup"], csv_rows)
    nums["oracle_gt1"] = REG.put("oracle_gt1", oracle_gt1, "d", [w["dir"] for w in workloads.values()], "workloads where some searched configuration beats both baselines")
    # which flags are in the best configuration most often (descriptive)
    iqr_vals = []
    for k in wl_keys:
        for cid in ("O2", "O3"):
            r = workloads[k]["entries"].get(cid)
            if r:
                iqr_vals.append(r["iqr_ms"] / r["median_ms"] * 100)
    if iqr_vals:
        nums["baseline_iqr_median"] = REG.put("baseline_iqr_median", statistics.median(iqr_vals), ".1f", [w["dir"] + "/results.json" for w in workloads.values()], "median over workloads and {O2,O3} of 100*IQR/median")
        nums["baseline_iqr_max"] = REG.put("baseline_iqr_max", max(iqr_vals), ".1f", [w["dir"] + "/results.json" for w in workloads.values()], "max over workloads and {O2,O3} of 100*IQR/median")
    sizes_b = [r["binary_size_bytes"] for r in all_recs if r["build_status"] == "OK"]
    if sizes_b:
        nums["bin_min"] = REG.put("bin_min", min(sizes_b), "d", [w["dir"] + "/results.json" for w in workloads.values()], "min binary_size_bytes")
        nums["bin_max"] = REG.put("bin_max", max(sizes_b), "d", [w["dir"] + "/results.json" for w in workloads.values()], "max binary_size_bytes")

    # ---- replay study ---------------------------------------------------------------------------------------------------
    sdir = os.path.join(res_dir, "V0.8-study")
    study_manifest = load_json(os.path.join(sdir, "manifest.json"), warn)
    runs = load_csv(os.path.join(sdir, "study_runs.csv"),
                    ["method", "budget", "seed", "benchmark", "tuning_size", "selected", "evaluations", "tuning_speedup", "oracle_speedup", "regret", "holdout_mean_speedup"], warn)
    summary = load_csv(os.path.join(sdir, "study_summary.csv"), ["method", "budget", "benchmark", "runs", "tuning_mean", "tuning_stdev", "regret_mean", "holdout_mean", "holdout_stdev"], warn)
    sfile = rel(os.path.join(sdir, "study_summary.csv"))
    rfile = rel(os.path.join(sdir, "study_runs.csv"))
    methods = ["fixed", "random", "adaptive", "adaptive-no-features", "adaptive-no-history", "adaptive-no-feedback"]
    budgets = sorted({int(r["budget"]) for r in runs if r["budget"] != "0"}) if runs else []
    if study_manifest:
        cfg = study_manifest.get("config", {})
        nums["study_seeds"] = REG.put("study_seeds", cfg.get("seeds"), "d", [sfile.replace("study_summary.csv", "manifest.json")], "config.seeds")
        nums["study_pilot"] = REG.put("study_pilot", cfg.get("pilot_reps"), "d", [sfile.replace("study_summary.csv", "manifest.json")], "config.pilot_reps")
        nums["study_rows"] = REG.put("study_rows", study_manifest.get("rows"), "d", [sfile.replace("study_summary.csv", "manifest.json")], "number of replayed runs")
        nums["study_budgets"] = REG.put("study_budgets", ", ".join(str(b) for b in cfg.get("budgets", [])), None, [sfile.replace("study_summary.csv", "manifest.json")], "config.budgets")
        leak = study_manifest.get("leakage_rules")
        nums["leakage_rules"] = REG.put("leakage_rules", leak, None, [sfile.replace("study_summary.csv", "manifest.json")], "manifest.leakage_rules")
    if ERRORS:
        return None  # a mandatory input is missing or malformed; do not compute anything from partial data
    # The study must have been computed from exactly the tables that are present (same sizes, roles and source hashes).
    if study_manifest:
        for t in study_manifest.get("tables", []):
            w = workloads.get((t.get("benchmark"), t.get("size")))
            if w is None:
                problem("the study used a table for %s n=%s but no such table data was found under benchmarks/results" % (t.get("benchmark"), t.get("size")), warn)
            elif w["role"] != t.get("role") or w["hash"] != t.get("source_hash"):
                problem("table %s n=%s differs from the one the study used (role or source hash mismatch)" % (t.get("benchmark"), t.get("size")), warn)
        if len(study_manifest.get("tables", [])) != len(workloads):
            problem("the study used %d tables but %d tables were loaded" % (len(study_manifest.get("tables", [])), len(workloads)), warn)
    if ERRORS:
        return None
    by_key = {}
    for r in summary:
        by_key[(r["method"], int(r["budget"]), r["benchmark"])] = r

    def cell(method, budget, bench, col):
        row = by_key.get((method, budget, bench))
        if row is None or row.get(col, "") == "":
            problem("study_summary.csv has no %s for method=%s budget=%s benchmark=%s" % (col, method, budget, bench), warn)
            return None
        return float(row[col])

    main_rows, main_csv = [], []
    fig_t, fig_h, fig_r = [], [], []
    shown = [b for b in budgets if b in (1, 2, 3, 4, 6, 8, 12, 22)]
    for m in methods + ["baseline-O2", "baseline-O3"]:
        tv, hv, rv = [], [], []
        for b in shown:
            bb = 0 if m.startswith("baseline-") else b
            tv.append(cell(m, bb, "ALL", "tuning_mean"))
            hv.append(cell(m, bb, "ALL", "holdout_mean"))
            rv.append(cell(m, bb, "ALL", "regret_mean"))
        if not m.startswith("baseline-"):
            fig_t.append((m, tv))
            fig_h.append((m, hv))
            fig_r.append((m, rv))
    for b in shown:
        for m in methods:
            pre = "st_%s_%d_" % (m, b)
            t = cell(m, b, "ALL", "tuning_mean")
            ts = cell(m, b, "ALL", "tuning_stdev")
            h = cell(m, b, "ALL", "holdout_mean")
            hs = cell(m, b, "ALL", "holdout_stdev")
            g = cell(m, b, "ALL", "regret_mean")
            row = [str(b), m, REG.put(pre + "t", t, ".3f", [sfile], "tuning_mean, benchmark=ALL"), REG.put(pre + "ts", ts, ".3f", [sfile], "tuning_stdev"),
                   REG.put(pre + "h", h, ".3f", [sfile], "holdout_mean"), REG.put(pre + "hs", hs, ".3f", [sfile], "holdout_stdev"), REG.put(pre + "g", g, ".3f", [sfile], "regret_mean")]
            main_rows.append(row)
            main_csv.append(row)
    for m in ("baseline-O2", "baseline-O3"):
        pre = "st_%s_" % m
        row = ["-", m, REG.put(pre + "t", cell(m, 0, "ALL", "tuning_mean"), ".3f", [sfile], "tuning_mean (no search)"), "-",
               REG.put(pre + "h", cell(m, 0, "ALL", "holdout_mean"), ".3f", [sfile], "holdout_mean (no search)"), "-", "-"]
        main_rows.append(row)
        main_csv.append(row)
    tables_out["study_main"] = md_table(["B", "method", "tuning mean", "tuning sd", "holdout mean", "holdout sd", "regret mean"], main_rows)
    write_csv(os.path.join(out_dir, "tables", "study_main.csv"), ["budget", "method", "tuning_mean", "tuning_sd", "holdout_mean", "holdout_sd", "regret_mean"], main_csv)
    if shown and fig_t and any(v is not None for _, vs in fig_t for v in vs):
        xl = [str(b) for b in shown]
        figs_out["tuning_vs_budget"] = svg_lines("Tuning-size quality vs budget (mean over 6 tuning workloads x seeds)", xl, fig_t, "speedup vs best of -O2/-O3")
        figs_out["holdout_vs_budget"] = svg_lines("Holdout quality vs budget (selection scored on unseen sizes)", xl, fig_h, "speedup vs best of -O2/-O3")
        figs_out["regret_vs_budget"] = svg_lines("Regret vs budget (selected median / best configuration median; 1 = optimal)", xl, fig_r, "regret")
        write_csv(os.path.join(out_dir, "tables", "figure_data.csv"), ["figure", "method"] + ["B=" + x for x in xl],
                  [["tuning_vs_budget", n] + [("" if v is None else v) for v in vs] for n, vs in fig_t] + [["holdout_vs_budget", n] + [("" if v is None else v) for v in vs] for n, vs in fig_h] +
                  [["regret_vs_budget", n] + [("" if v is None else v) for v in vs] for n, vs in fig_r])

    REG.put("boot_n", 4000, "d", ["scripts/make_report.py"], "BOOTSTRAP_RESAMPLES constant in bootstrap_ci()")

    # paired comparisons and hypothesis verdicts ----------------------------------------------------------------------
    pair_idx = {}
    for r in runs:
        pair_idx[(r["method"], int(r["budget"]), r["benchmark"], r["tuning_size"], r["seed"])] = r

    def paired(a, b, budget, col):
        """List of (workload, difference) for runs paired by (benchmark, tuning size, seed)."""
        d = []
        for (m, bud, bench, size, seed), row in pair_idx.items():
            if m != a or bud != budget:
                continue
            o = pair_idx.get((b, budget, bench, size, seed))
            if o is None:
                continue
            d.append(((bench, size), float(row[col]) - float(o[col])))
        return d

    hyps = [("H1", "adaptive", "random", "tuning_speedup", "adaptive finds better configurations than random search at the same budget (tuning sizes)"),
            ("H2", "adaptive", "random", "holdout_mean_speedup", "that advantage carries over to input sizes the search never saw (holdout)"),
            ("H3", "adaptive", "adaptive-no-features", "holdout_mean_speedup", "workload descriptors help (holdout)"),
            ("H3t", "adaptive", "adaptive-no-features", "tuning_speedup", "workload descriptors help (tuning sizes)"),
            ("H4", "adaptive", "adaptive-no-history", "holdout_mean_speedup", "prior experiments help (holdout)"),
            ("H4t", "adaptive", "adaptive-no-history", "tuning_speedup", "prior experiments help (tuning sizes)"),
            ("H5", "adaptive", "adaptive-no-feedback", "holdout_mean_speedup", "within-run feedback helps (holdout)"),
            ("H5t", "adaptive", "adaptive-no-feedback", "tuning_speedup", "within-run feedback helps (tuning sizes)")]
    hyp_rows = []
    cmp_rows = []
    hyp_budgets = [b for b in (1, 2, 4, 8) if b in budgets]
    n_workloads_study = len({(r["benchmark"], r["tuning_size"]) for r in runs}) if runs else 0
    for hi_, (hid, a, b, col, desc) in enumerate(hyps):
        budget_flags = []  # per budget: (ci_lo, ci_hi, positive_workloads)
        for bud in hyp_budgets:
            pairs = paired(a, b, bud, col)
            if not pairs:
                problem("no paired runs for %s vs %s at B=%d" % (a, b, bud), warn)
                budget_flags.append((None, None, None))
                continue
            d = [x for _, x in pairs]
            lo, hi = bootstrap_ci(d, seed=1000 * (1 + hi_) + bud)
            per_wl = {}
            for wl, x in pairs:
                per_wl.setdefault(wl, []).append(x)
            pos = sum(1 for xs in per_wl.values() if sum(xs) / len(xs) > 1e-9)
            neg = sum(1 for xs in per_wl.values() if sum(xs) / len(xs) < -1e-9)
            budget_flags.append((lo, hi, pos, neg, len(per_wl)))
            mean = sum(d) / len(d)
            wins = sum(1 for x in d if x > 1e-9)
            losses = sum(1 for x in d if x < -1e-9)
            ties = len(d) - wins - losses
            pre = "pair_%s_%d_" % (hid, bud)
            srcs = [rfile]
            cmp_rows.append([hid, a + " - " + b, col.replace("_speedup", "").replace("_mean", ""), str(bud), str(len(d)),
                             REG.put(pre + "mean", mean, "+.4f", srcs, "mean of paired differences (same benchmark, tuning size, seed)"),
                             REG.put(pre + "lo", lo, "+.4f", srcs, "2.5th percentile of 4000 bootstrap means of the paired differences"),
                             REG.put(pre + "hi", hi, "+.4f", srcs, "97.5th percentile of 4000 bootstrap means of the paired differences"),
                             "%s/%s/%s" % (REG.put(pre + "w", wins, "d", srcs, "pairs with difference > 1e-9"), REG.put(pre + "t", ties, "d", srcs, "pairs with |difference| <= 1e-9"),
                                           REG.put(pre + "l", losses, "d", srcs, "pairs with difference < -1e-9")),
                             "%s/%s" % (REG.put(pre + "pw", pos, "d", srcs, "workloads whose mean paired difference > 1e-9"),
                                        REG.put(pre + "nw", len(per_wl), "d", srcs, "number of workloads (benchmark, tuning size)"))])
        v = verdict_clustered(budget_flags)
        verdicts[hid] = v
        hyp_rows.append([hid, desc, v])
        REG.put("verdict_" + hid, v, None, [rfile], "rule (see report): CI lower bound > 0 AND >= 2/3 of workloads positive at >= 3/4 of budgets {%s}, and no budget with CI upper < 0 AND >= 2/3 of workloads negative -> SUPPORTED; mirror image -> NOT SUPPORTED; else INCONCLUSIVE" % ",".join(map(str, hyp_budgets)))
    tables_out["paired"] = md_table(["hyp", "comparison (A - B)", "metric", "B", "pairs", "mean diff", "CI low", "CI high", "A wins/ties/B wins (pairs)", "workloads A>B"], cmp_rows)
    tables_out["verdicts"] = md_table(["hypothesis", "statement", "verdict"], hyp_rows)
    write_csv(os.path.join(out_dir, "tables", "paired_comparisons.csv"), ["hyp", "comparison", "metric", "budget", "pairs", "mean_diff", "ci_lo", "ci_hi", "wins_ties_losses", "workloads_positive_of_total"], cmp_rows)

    # per-benchmark at B=4 -----------------------------------------------------------------------------------------------
    pb_rows = []
    pb_b = 4 if 4 in budgets else (budgets[0] if budgets else None)
    benches = sorted({r["benchmark"] for r in runs})
    if pb_b is not None:
        for bench in benches:
            for m in ("random", "adaptive", "adaptive-no-features"):
                pre = "pb_%s_%s_" % (bench, m)
                pb_rows.append([bench, m, REG.put(pre + "t", cell(m, pb_b, bench, "tuning_mean"), ".3f", [sfile], "tuning_mean at B=%d" % pb_b),
                                REG.put(pre + "h", cell(m, pb_b, bench, "holdout_mean"), ".3f", [sfile], "holdout_mean at B=%d" % pb_b),
                                REG.put(pre + "g", cell(m, pb_b, bench, "regret_mean"), ".3f", [sfile], "regret_mean at B=%d" % pb_b)])
        nums["pb_budget"] = REG.put("pb_budget", pb_b, "d", [sfile], "budget used in the per-benchmark table")
    tables_out["per_benchmark"] = md_table(["benchmark", "method", "tuning mean", "holdout mean", "regret mean"], pb_rows)

    # regressions on holdout ---------------------------------------------------------------------------------------------
    reg_rows = []
    for m in ("random", "adaptive"):
        for b in [x for x in (1, 4, 8) if x in budgets]:
            vals = []
            for (mm, bud, bench, size, seed), row in pair_idx.items():
                if mm == m and bud == b:
                    vals.append(float(row["holdout_mean_speedup"]))
            if not vals:
                continue
            slower = sum(1 for v in vals if v < 1.0 - 1e-9)
            pre = "reg_%s_%d_" % (m, b)
            reg_rows.append([m, str(b), str(len(vals)), REG.put(pre + "n", slower, "d", [rfile], "runs whose mean holdout speedup < 1 (selected config slower than best baseline)"),
                             REG.put(pre + "p", 100.0 * slower / len(vals), ".1f", [rfile], "100 * slower / runs"), REG.put(pre + "min", min(vals), ".3f", [rfile], "minimum holdout_mean_speedup")])
    tables_out["regressions"] = md_table(["method", "B", "runs", "holdout slower than best baseline", "%", "worst holdout speedup"], reg_rows)

    # ---- real end-to-end runs -------------------------------------------------------------------------------------------------
    real_rows, real_csv = [], []
    groups = {}
    for d in sorted(glob.glob(os.path.join(res_dir, "V0.8-real-*-B6"))):
        manifest, recs = load_records(d, warn)
        hman, hrecs = load_records(d + "-holdout", warn)
        if not manifest or not recs or not hrecs:
            continue
        m = re.match(r"V0\.8-real-(.+?)-(random|adaptive-no-features|adaptive)-s(\d+)-B6$", os.path.basename(d))
        if not m:
            problem("unexpected real-run directory name %s" % rel(d), warn)
            continue
        bench, method, seed = m.group(1), m.group(2), int(m.group(3))
        winner = manifest.get("winner")
        if not winner or "tuning_wall_time_s" not in manifest:
            problem("%s manifest lacks winner/tuning_wall_time_s" % rel(d), warn)
            continue

        def med(rs, cid, size):
            xs = [r for r in rs if r["candidate_id"] == cid and r["input_size"] == size]
            return xs[0]["median_ms"] if xs else None
        tsize = next(r["input_size"] for r in recs if r["candidate_id"] == "O2")
        b0 = min(med(recs, "O2", tsize), med(recs, "O3", tsize))
        tspeed = b0 / med(recs, winner, tsize)
        hs = []
        for size in sorted({r["input_size"] for r in hrecs}):
            hb = min(med(hrecs, "O2", size), med(hrecs, "O3", size))
            hw = med(hrecs, winner, size)
            if hw is None:
                problem("%s-holdout lacks the winner %s at n=%s" % (rel(d), winner, size), warn)
                continue
            hs.append(hb / hw)
        groups.setdefault((bench, method), []).append((manifest["tuning_wall_time_s"], tspeed, sum(hs) / len(hs) if hs else None, rel(d)))
    for (bench, method), items in sorted(groups.items()):
        srcs = []
        for it in items:
            srcs += [it[3] + "/manifest.json", it[3] + "/results.json", it[3] + "-holdout/results.json"]
        pre = "real_%s_%s_" % (bench, method)
        walls = [it[0] for it in items]
        ts = [it[1] for it in items]
        hh = [it[2] for it in items if it[2] is not None]
        row = [bench, method, str(len(items)), REG.put(pre + "wall", statistics.mean(walls), ".1f", srcs, "mean manifest.tuning_wall_time_s over seeds"),
               REG.put(pre + "t", statistics.mean(ts), ".3f", srcs, "mean over seeds of best-baseline median / winner median at the tuning size"),
               REG.put(pre + "h", statistics.mean(hh) if hh else None, ".3f", srcs, "mean over seeds of mean holdout speedup (winner vs best of -O2/-O3, holdout re-measurement)"),
               REG.put(pre + "hmin", min(hh) if hh else None, ".3f", srcs, "minimum over seeds")]
        real_rows.append(row)
        real_csv.append(row)
    tables_out["real"] = md_table(["benchmark", "method", "runs", "tuning wall s", "tuning speedup", "holdout speedup", "worst holdout"], real_rows)
    write_csv(os.path.join(out_dir, "tables", "real_runs.csv"), ["benchmark", "method", "runs", "wall_s", "tuning_speedup", "holdout_speedup", "worst_holdout"], real_csv)
    allm = {}
    for (bench, method), items in groups.items():
        allm.setdefault(method, []).extend(items)
    for method, items in allm.items():
        srcs = [it[3] + "/manifest.json" for it in items]
        nums["real_wall_" + method] = REG.put("real_wall_" + method, statistics.mean(it[0] for it in items), ".1f", srcs, "mean tuning_wall_time_s over all runs of this method")
        nums["real_h_" + method] = REG.put("real_h_" + method, statistics.mean(it[2] for it in items if it[2] is not None), ".3f", srcs + [it[3] + "-holdout/results.json" for it in items], "mean holdout speedup over all runs")
        nums["real_n_" + method] = REG.put("real_n_" + method, len(items), "d", srcs, "number of real runs")
    if "random" in allm and "adaptive" in allm:
        r_ = statistics.mean(it[0] for it in allm["random"])
        a_ = statistics.mean(it[0] for it in allm["adaptive"])
        nums["real_overhead_pct"] = REG.put("real_overhead_pct", 100.0 * (a_ / r_ - 1.0), "+.0f", [it[3] + "/manifest.json" for it in allm["random"] + allm["adaptive"]], "100*(mean adaptive wall / mean random wall - 1)")

    # ---- fill the template --------------------------------------------------------------------------------------------------
    tpath = os.path.join(args.data, "report", "template.md")
    try:
        template = open(tpath, "r", encoding="utf-8").read()
    except OSError as e:
        problem("cannot read template: %s" % e, False)
        return None
    lint_errors = lint_template(template)
    for e in lint_errors:
        problem("template lint: " + e, False)

    def sub(m):
        kind, key = m.group(1), m.group(2)
        if kind == "num":
            if key in REG.items:
                return REG.text(key)
            problem("template placeholder {{num:%s}} has no value" % key, warn)
            return "UNAVAILABLE"
        if kind == "table":
            if key in tables_out:
                return tables_out[key]
            problem("template placeholder {{table:%s}} unknown" % key, warn)
            return "UNAVAILABLE"
        if kind == "fig":
            if key in figs_out:
                return "![%s](figures/%s.svg)" % (key.replace("_", " "), key)
            problem("template placeholder {{fig:%s}} unknown" % key, warn)
            return "UNAVAILABLE"
        if kind == "verdict":
            if key in verdicts:
                return "**" + verdicts[key] + "**"
            problem("template placeholder {{verdict:%s}} unknown" % key, warn)
            return "UNAVAILABLE"
        problem("template placeholder kind %s unknown" % kind, False)
        return "UNAVAILABLE"
    body = re.sub(r"\{\{(num|table|fig|verdict):([A-Za-z0-9_\-\.]+)\}\}", sub, template)
    body = re.sub(r"<!--\s*allow:.*?-->\n?", "", body)  # lint metadata is not part of the report
    return body, figs_out, tables_out, verdicts


NUMBER_RE = re.compile(r"(?<![A-Za-z0-9_\.\-])\d+(\.\d+)?%?(?![A-Za-z0-9_])")


def lint_template(text):
    """Prose may not contain typed-in measurement numbers. Allowed: numbers inside placeholders, in inline code spans,
    in headings' section numbers, version/phase ids (V0.8, H1, R3 ...), the budgets/years named in ALLOW lines."""
    errs = []
    allow = set()
    for m in re.finditer(r"<!--\s*allow:\s*(.*?)\s*-->", text):
        allow.update(x.strip() for x in m.group(1).split(","))
    in_code = False
    for n, line in enumerate(text.split("\n"), 1):
        if line.strip().startswith("```"):
            in_code = not in_code
            continue
        if in_code or line.strip().startswith("<!--"):
            continue
        s = re.sub(r"\{\{[^}]*\}\}", "", line)
        s = re.sub(r"`[^`]*`", "", s)
        s = re.sub(r"\[[^\]]*\]\([^)]*\)", "", s)          # links
        s = re.sub(r"https?://\S+|arXiv:\S+|doi:\S+", "", s)  # URLs and identifiers
        s = re.sub(r"\b(19|20)\d\d\b", "", s)               # publication years
        s = re.sub(r"\b[A-Z][A-Za-z]*\d+(\.\d+)*\b", "", s)  # V0.8, H1, R3, GCC6 ...
        s = re.sub(r"^\s*#+\s*[\d\.]*", "", s)
        s = re.sub(r"^\s*\d+[\.\)]\s", "", s)               # list numbering
        s = re.sub(r"\bB\s*=\s*\d+", "", s) if "B=" in allow else s
        for m in NUMBER_RE.finditer(s):
            if m.group(0) in allow:
                continue
            errs.append("line %d: number '%s' typed into prose (use a {{num:...}} placeholder or `<!-- allow: ... -->`)" % (n, m.group(0)))
    return errs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=ROOT, help="repository root holding benchmarks/results and report/template.md")
    ap.add_argument("--out", default=os.path.join(ROOT, "report"), help="output directory")
    ap.add_argument("--warn-only", action="store_true", help="report missing/malformed data as warnings instead of errors")
    args = ap.parse_args()
    BASE[0] = os.path.abspath(args.data)
    built = build(args)
    if built is None or ERRORS:
        for e in ERRORS:
            print("ERROR:", e, file=sys.stderr)
        for w in WARNINGS:
            print("WARNING:", w, file=sys.stderr)
        print("report generation failed", file=sys.stderr)
        return 1
    body, figs, tables, verdicts = built
    os.makedirs(os.path.join(args.out, "figures"), exist_ok=True)
    for k, svg in figs.items():
        with open(os.path.join(args.out, "figures", k + ".svg"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(svg)
    with open(os.path.join(args.out, "report.md"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(body)
    trace = sorted(REG.items.values(), key=lambda x: x["key"])
    with open(os.path.join(args.out, "traceability.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump({"note": "every value in report.md that is not prose comes from one of these entries; 'sources' are repository-relative raw data files",
                   "entries": trace}, fh, indent=1)
        fh.write("\n")
    inputs = sorted(USED_FILES)
    manifest = {"generator": "scripts/make_report.py", "inputs": [{"path": rel(p), "sha256": sha256_file(p)} for p in inputs if os.path.exists(p)] +
                [{"path": "report/template.md", "sha256": sha256_file(os.path.join(args.data, "report", "template.md"))}],
                "verdicts": verdicts, "warnings": WARNINGS, "registered_values": len(trace)}
    with open(os.path.join(args.out, "manifest.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(manifest, fh, indent=1)
        fh.write("\n")
    for w in WARNINGS:
        print("WARNING:", w, file=sys.stderr)
    print("report written to %s (%d traced values, %d warnings)" % (args.out, len(trace), len(WARNINGS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
