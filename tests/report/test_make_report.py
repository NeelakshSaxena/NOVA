#!/usr/bin/env python3
"""Tests of scripts/make_report.py (standard library only). Run: python tests/report/test_make_report.py"""
import csv
import importlib.util
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "make_report.py")
RESULTS = os.path.join(ROOT, "benchmarks", "results")


def load_module():
    spec = importlib.util.spec_from_file_location("make_report", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(args, cwd=None):
    r = subprocess.run([sys.executable, SCRIPT] + args, capture_output=True, text=True, cwd=cwd)
    return r.returncode, r.stdout, r.stderr


def copy_data(dst):
    """Copies exactly the inputs the report needs into a scratch repository root."""
    os.makedirs(os.path.join(dst, "benchmarks"))
    os.makedirs(os.path.join(dst, "report"))
    shutil.copy(os.path.join(ROOT, "report", "template.md"), os.path.join(dst, "report", "template.md"))
    shutil.copytree(RESULTS, os.path.join(dst, "benchmarks", "results"),
                    ignore=shutil.ignore_patterns("bin", "logs", "raw_timings.csv", "V0.6-*", "V0.7-*"))


class GenerateOnSavedData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="nova-report-")
        cls.out1 = os.path.join(cls.tmp, "o1")
        cls.out2 = os.path.join(cls.tmp, "o2")
        cls.rc1, cls.so1, cls.se1 = run(["--out", cls.out1])
        cls.rc2, cls.so2, cls.se2 = run(["--out", cls.out2])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_runs_and_writes_everything(self):
        self.assertEqual(self.rc1, 0, self.se1)
        for f in ["report.md", "traceability.json", "manifest.json", "figures/tuning_vs_budget.svg", "figures/holdout_vs_budget.svg",
                  "figures/regret_vs_budget.svg", "tables/study_main.csv", "tables/paired_comparisons.csv", "tables/workloads.csv"]:
            self.assertTrue(os.path.exists(os.path.join(self.out1, f)), f)

    def test_deterministic(self):
        self.assertEqual(self.rc2, 0, self.se2)
        for f in ["report.md", "traceability.json", "figures/holdout_vs_budget.svg", "tables/paired_comparisons.csv"]:
            a = open(os.path.join(self.out1, f), "rb").read()
            b = open(os.path.join(self.out2, f), "rb").read()
            self.assertEqual(a, b, f + " differs between two runs")

    def test_no_unresolved_placeholders_or_unavailable(self):
        body = open(os.path.join(self.out1, "report.md"), encoding="utf-8").read()
        self.assertNotIn("{{", body)
        self.assertNotIn("UNAVAILABLE", body)

    def test_every_registered_value_has_existing_sources_and_formula(self):
        trace = json.load(open(os.path.join(self.out1, "traceability.json"), encoding="utf-8"))["entries"]
        self.assertGreater(len(trace), 300)
        for e in trace:
            self.assertTrue(e["sources"], e["key"])
            self.assertTrue(e["formula"], e["key"])
            for s in e["sources"]:
                self.assertTrue(os.path.exists(os.path.join(ROOT, s)), "%s cites missing file %s" % (e["key"], s))

    def test_every_decimal_number_in_the_report_is_traced(self):
        body = open(os.path.join(self.out1, "report.md"), encoding="utf-8").read()
        trace = json.load(open(os.path.join(self.out1, "traceability.json"), encoding="utf-8"))["entries"]
        texts = [str(e["text"]) for e in trace if e["text"] is not None]
        # drop code fences, inline code (commands), links/URLs, identifiers, years and image links
        body = re.sub(r"```.*?```", "", body, flags=re.S)
        body = re.sub(r"`[^`]*`", "", body)
        body = re.sub(r"!?\[[^\]]*\]\([^)]*\)", "", body)
        body = re.sub(r"https?://\S+|arXiv:\S+", "", body)
        body = re.sub(r"\b[A-Z][A-Za-z]*\d+(\.\d+)*\b", "", body)
        body = re.sub(r"(?m)^\s*#+.*$", "", body)
        body = re.sub(r"(?i)\b(sections?|in|see)\s+\d+\.\d+(\.\d+)?", "", body)  # cross references such as "section 8.2"
        # numbers that appear inside registered text of the compiler/OS strings are fine because they are substrings of a traced text
        for m in re.finditer(r"(?<![\w.])[+-]?\d+\.\d+(?![\w.])", body):
            tok = m.group(0)
            self.assertTrue(any(tok in t for t in texts), "number %s in the report is not a traced value" % tok)

    def test_values_match_an_independent_recomputation(self):
        trace = {e["key"]: e for e in json.load(open(os.path.join(self.out1, "traceability.json"), encoding="utf-8"))["entries"]}
        # 1) replay-study cell recomputed from the raw per-run CSV
        rows = list(csv.DictReader(open(os.path.join(RESULTS, "V0.8-study", "study_runs.csv"), encoding="utf-8")))
        vals = [float(r["tuning_speedup"]) for r in rows if r["method"] == "adaptive" and r["budget"] == "4"]
        self.assertAlmostEqual(trace["st_adaptive_4_t"]["value"], statistics.mean(vals), places=5)
        hv = [float(r["holdout_mean_speedup"]) for r in rows if r["method"] == "random" and r["budget"] == "8"]
        self.assertAlmostEqual(trace["st_random_8_h"]["value"], statistics.mean(hv), places=5)
        # 2) a workload cell recomputed from the raw table records
        recs = json.load(open(os.path.join(RESULTS, "V0.8-table-stencil", "results.json"), encoding="utf-8"))["records"]
        o2 = [r for r in recs if r["candidate_id"] == "O2" and r["input_size"] == 200000][0]
        self.assertAlmostEqual(trace["wl_stencil_200000_o2"]["value"], o2["median_ms"], places=9)
        samples = sorted(o2["timing_samples_ms"])
        self.assertAlmostEqual(statistics.median(samples), o2["median_ms"], places=6)  # the stored median really is the median of the stored samples
        # 3) a real-run wall time recomputed from manifests
        walls = []
        for d in sorted(os.listdir(RESULTS)):
            if re.match(r"V0\.8-real-stencil-random-s\d-B6$", d):
                walls.append(json.load(open(os.path.join(RESULTS, d, "manifest.json"), encoding="utf-8"))["tuning_wall_time_s"])
        self.assertEqual(len(walls), 3)
        self.assertAlmostEqual(trace["real_stencil_random_wall"]["value"], statistics.mean(walls), places=6)
        # 4) a paired comparison recomputed from raw rows
        by = {(r["method"], r["benchmark"], r["tuning_size"], r["seed"]): r for r in rows if r["budget"] == "4"}
        diffs = [float(by[("adaptive",) + k[1:]]["holdout_mean_speedup"]) - float(r["holdout_mean_speedup"]) for k, r in by.items() if k[0] == "random"]
        self.assertAlmostEqual(trace["pair_H2_4_mean"]["value"], statistics.mean(diffs), places=6)

    def test_verdicts_present_and_reproducible(self):
        m = json.load(open(os.path.join(self.out1, "manifest.json"), encoding="utf-8"))
        for h in ["H1", "H2", "H3", "H4", "H5"]:
            self.assertIn(m["verdicts"][h], ("SUPPORTED", "NOT SUPPORTED", "INCONCLUSIVE"))
        self.assertEqual(m["verdicts"], json.load(open(os.path.join(self.out2, "manifest.json"), encoding="utf-8"))["verdicts"])
        for i in m["inputs"]:
            self.assertEqual(len(i["sha256"]), 64)


class MissingOrBadData(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="nova-report-bad-")
        copy_data(self.tmp)
        self.out = os.path.join(self.tmp, "out")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def gen(self, *extra):
        return run(["--data", self.tmp, "--out", self.out] + list(extra))

    def test_clean_copy_works(self):
        rc, so, se = self.gen()
        self.assertEqual(rc, 0, se)

    def test_missing_study_file_is_an_error(self):
        os.remove(os.path.join(self.tmp, "benchmarks", "results", "V0.8-study", "study_runs.csv"))
        rc, so, se = self.gen()
        self.assertNotEqual(rc, 0)
        self.assertIn("study_runs.csv", se)

    def test_missing_csv_column_is_an_error(self):
        p = os.path.join(self.tmp, "benchmarks", "results", "V0.8-study", "study_summary.csv")
        text = open(p, encoding="utf-8").read().replace("holdout_stdev", "renamed_column", 1)
        open(p, "w", encoding="utf-8", newline="").write(text)
        rc, so, se = self.gen()
        self.assertNotEqual(rc, 0)
        self.assertIn("holdout_stdev", se)

    def test_record_without_required_field_is_an_error(self):
        p = os.path.join(self.tmp, "benchmarks", "results", "V0.8-table-matmul", "results.json")
        j = json.load(open(p, encoding="utf-8"))
        del j["records"][0]["iqr_ms"]
        json.dump(j, open(p, "w", encoding="utf-8"))
        rc, so, se = self.gen()
        self.assertNotEqual(rc, 0)
        self.assertIn("iqr_ms", se)

    def test_sample_count_mismatch_is_an_error(self):
        p = os.path.join(self.tmp, "benchmarks", "results", "V0.8-table-stencil", "results.json")
        j = json.load(open(p, encoding="utf-8"))
        j["records"][3]["timing_samples_ms"].pop()
        json.dump(j, open(p, "w", encoding="utf-8"))
        rc, so, se = self.gen()
        self.assertNotEqual(rc, 0)
        self.assertIn("samples", se)

    def test_missing_table_directory_is_an_error_and_warn_only_degrades(self):
        shutil.rmtree(os.path.join(self.tmp, "benchmarks", "results", "V0.8-table-vector_add"))
        rc, so, se = self.gen()
        self.assertNotEqual(rc, 0)
        rc2, so2, se2 = self.gen("--warn-only")
        # with --warn-only the run completes (possibly with warnings); anything it could not compute is marked, never invented
        self.assertIn(rc2, (0, 1))
        if rc2 == 0:
            body = open(os.path.join(self.out, "report.md"), encoding="utf-8").read()
            self.assertNotIn("vector_add | 1,000,000", body)

    def test_missing_manifest_environment_is_reported(self):
        for d in os.listdir(os.path.join(self.tmp, "benchmarks", "results")):
            if d.startswith("V0.8-table-"):
                p = os.path.join(self.tmp, "benchmarks", "results", d, "manifest.json")
                j = json.load(open(p, encoding="utf-8"))
                j.pop("environment", None)
                json.dump(j, open(p, "w", encoding="utf-8"))
        rc, so, se = self.gen()
        self.assertNotEqual(rc, 0)
        self.assertIn("environment", se)

    def test_missing_template_is_an_error(self):
        os.remove(os.path.join(self.tmp, "report", "template.md"))
        rc, so, se = self.gen()
        self.assertNotEqual(rc, 0)


class TemplateLint(unittest.TestCase):
    def setUp(self):
        self.mod = load_module()

    def test_typed_measurement_numbers_are_rejected(self):
        for bad in ["Adaptive was 1.23 times faster.", "It gained 12% on average.", "We ran 30 seeds.", "The speedup is 7."]:
            self.assertTrue(self.mod.lint_template(bad), bad)

    def test_allowed_forms_pass(self):
        ok = ["# 8.2 Results\n", "Adaptive was {{num:x}} times faster.", "See `--seeds=30` and V0.8, H1 and [R2] https://example.org/a/1.5 (2014).",
              "```\ncmd --reps=15\n```\n", "1. first item\n", "Section <!-- allow: 7 -->\n see section 7."]
        for t in ok:
            self.assertEqual(self.mod.lint_template(t), [], t)

    def test_shipped_template_is_clean(self):
        self.assertEqual(self.mod.lint_template(open(os.path.join(ROOT, "report", "template.md"), encoding="utf-8").read()), [])

    def test_unknown_placeholder_is_an_error(self):
        tmp = tempfile.mkdtemp(prefix="nova-report-tpl-")
        try:
            copy_data(tmp)
            t = os.path.join(tmp, "report", "template.md")
            open(t, "w", encoding="utf-8").write("# t\n{{num:does_not_exist}}\n")
            rc, so, se = run(["--data", tmp, "--out", os.path.join(tmp, "o")])
            self.assertNotEqual(rc, 0)
            self.assertIn("does_not_exist", se)
            open(t, "w", encoding="utf-8").write("# t\nspeedup 1.37 observed\n")
            rc, so, se = run(["--data", tmp, "--out", os.path.join(tmp, "o")])
            self.assertNotEqual(rc, 0)
            self.assertIn("typed into prose", se)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


class Statistics(unittest.TestCase):
    def setUp(self):
        self.mod = load_module()

    def test_bootstrap_is_deterministic_and_sensible(self):
        d = [0.1, 0.2, 0.15, 0.12, 0.18, 0.09]
        a = self.mod.bootstrap_ci(d, seed=5)
        self.assertEqual(a, self.mod.bootstrap_ci(d, seed=5))
        self.assertGreater(a[0], 0)
        self.assertLess(a[0], statistics.mean(d))
        self.assertGreater(a[1], statistics.mean(d))
        z = self.mod.bootstrap_ci([0.0] * 10, seed=1)
        self.assertEqual(z, (0.0, 0.0))
        self.assertEqual(self.mod.bootstrap_ci([], seed=1), (None, None))

    def test_verdict_rule(self):
        v = self.mod.verdict_clustered
        # (lo, hi, workloads_positive, workloads_negative, total)
        good = (0.01, 0.05, 6, 0, 6)
        bad = (-0.05, -0.01, 0, 6, 6)
        zero = (0.0, 0.0, 0, 0, 6)
        straddle = (-0.01, 0.03, 4, 2, 6)
        ci_ok_but_few_workloads = (0.01, 0.05, 3, 3, 6)
        self.assertEqual(v([good, good, good, good]), "SUPPORTED")
        self.assertEqual(v([good, good, good, straddle]), "SUPPORTED")  # three of four budgets positive, none negative
        self.assertEqual(v([good, good, straddle, straddle]), "INCONCLUSIVE")
        self.assertEqual(v([good, good, good, bad]), "INCONCLUSIVE")  # a negative budget blocks SUPPORTED
        self.assertEqual(v([bad, bad, bad, bad]), "NOT SUPPORTED")
        self.assertEqual(v([zero, zero, zero, zero]), "INCONCLUSIVE")
        self.assertEqual(v([ci_ok_but_few_workloads] * 4), "INCONCLUSIVE")  # seeds alone are not enough: needs two thirds of workloads
        self.assertEqual(v([(None, None, None)] * 4), "UNAVAILABLE")
        self.assertEqual(v([good, (None, None, None), good, good]), "SUPPORTED")


if __name__ == "__main__":
    unittest.main(verbosity=1)
