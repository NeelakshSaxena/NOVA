#!/usr/bin/env python3
"""Generates NOVA_Project_Presentation.pptx (and NOVA_Slide_Content.md) for the NOVA project review.

Usage:  python NOVA_Presentation_Source.py [project_root]

The project root defaults to the parent of this file's folder. The script only READS
report/report.md (for the numbers shown on slide 12); it writes its outputs next to itself
and never touches the project's source code. Requires: python-pptx (pip install python-pptx).

Every number on a slide comes from one of these places: report/report.md (parsed below, which the
project generates from its saved raw measurements), output captured in docs/DEMO.md, the saved
state/evidence/V0.7 explain logs, or the source code (constants such as 1e-9 and 2.0). Worked examples are labelled illustrative.
Nothing is typed in from memory.
"""
import re
import sys
from pathlib import Path

from lxml import etree
from pptx import Presentation
from pptx.chart.data import CategoryChartData
from pptx.dml.color import RGBColor
from pptx.enum.chart import XL_CHART_TYPE, XL_LEGEND_POSITION, XL_MARKER_STYLE
from pptx.enum.dml import MSO_LINE
from pptx.enum.shapes import MSO_CONNECTOR, MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Inches, Pt

HERE = Path(__file__).resolve().parent
ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else HERE.parent
OUT_PPTX = HERE / "NOVA_Project_Presentation.pptx"
OUT_MD = HERE / "NOVA_Slide_Content.md"

# ---- look: white background, one dark-blue accent, Arial / Courier New ----------------------------------------------
NAVY = RGBColor(0x1F, 0x3A, 0x5F)
INK = RGBColor(0x1A, 0x1A, 0x1A)
GREY = RGBColor(0x5A, 0x62, 0x70)
LIGHT = RGBColor(0xEE, 0xF2, 0xF7)   # very light blue tint for boxes
CODEBG = RGBColor(0xF5, 0xF6, 0xF8)
RULE = RGBColor(0xC5, 0xCD, 0xD8)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
AMBER = RGBColor(0x8A, 0x55, 0x00)
FONT = "Arial"
MONO = "Courier New"

LM = 0.6            # left margin (inches)
CW = 12.13          # content width
SLIDE_W, SLIDE_H = 13.333, 7.5

# =====================================================================================================================
# read the numbers from the project's own report
# =====================================================================================================================
REPORT = (ROOT / "report" / "report.md").read_text(encoding="utf-8")


def section(text, heading):
    """Text of the '### heading...' section (up to the next '##' heading)."""
    m = re.search(r"^###? " + re.escape(heading) + r".*?$(.*?)(?=^##)", text, re.S | re.M)
    if not m:
        raise SystemExit("report.md: section not found: " + heading)
    return m.group(1)


def table_rows(block):
    rows = []
    for line in block.splitlines():
        if line.startswith("|") and not re.match(r"^\|[-| ]+\|$", line):
            rows.append([c.strip() for c in line.strip().strip("|").split("|")])
    return rows


WORKLOADS = [r for r in table_rows(section(REPORT, "8.1")) if r[0] in ("matmul", "stencil", "vector_add")]
STUDY = [r for r in table_rows(section(REPORT, "8.2")) if len(r) == 7 and r[0].isdigit()]
BUDGETS = sorted({int(r[0]) for r in STUDY})


def study(method, col):
    """col: 2 tuning mean, 4 holdout mean."""
    return [float(next(r for r in STUDY if int(r[0]) == b and r[1] == method)[col]) for b in BUDGETS]


m = re.search(r"random ([\d.]+) s,\s+adaptive ([\d.]+) s", REPORT)
n = re.search(r"\+(\d+) percent more wall time", REPORT.replace("\n", " "))
if not (m and n and WORKLOADS and STUDY):
    raise SystemExit("report.md did not have the expected tables; slide 12 cannot be built")
T_RANDOM, T_ADAPT, T_EXTRA = m.group(1), m.group(2), n.group(1)
H_ADAPT_B4 = float(next(r for r in STUDY if r[0] == "4" and r[1] == "adaptive")[4])
H_RANDOM_B4 = float(next(r for r in STUDY if r[0] == "4" and r[1] == "random")[4])


def workload(bench, size):
    return next(r for r in WORKLOADS if r[0] == bench and r[1].replace(",", "") == str(size))


# =====================================================================================================================
# drawing helpers
# =====================================================================================================================
prs = Presentation()
prs.slide_width = Inches(SLIDE_W)
prs.slide_height = Inches(SLIDE_H)
LAYOUT_TITLE, LAYOUT_TITLE_ONLY = prs.slide_layouts[0], prs.slide_layouts[5]
NOTES = {}      # slide number -> speaker notes
TOTAL = 15


def runs_from_md(s):
    """'**bold**' and '`code`' mini-markup -> [(text, bold, code)]."""
    out = []
    for part in re.split(r"(\*\*.+?\*\*|`.+?`)", s):
        if not part:
            continue
        if part.startswith("**"):
            out.append((part[2:-2], True, False))
        elif part.startswith("`"):
            out.append((part[1:-1], False, True))
        else:
            out.append((part, False, False))
    return out


def set_bullet(p, indent=0.22):
    pPr = p._p.get_or_add_pPr()
    pPr.set("marL", str(int(Inches(indent))))
    pPr.set("indent", str(-int(Inches(indent))))
    bu = etree.SubElement(pPr, qn("a:buChar"))
    bu.set("char", "•")


def fill_tf(tf, items, size=16, color=INK, bullets=False, space=6, align=None, anchor=None, bold=False, font=FONT,
            margin=0.05):
    """items: list of str or (str, dict(size,color,bullet,bold,space,font,align))."""
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = Inches(margin)
    tf.margin_top = tf.margin_bottom = Inches(0.04)
    if anchor is not None:
        tf.vertical_anchor = anchor
    first = True
    for item in items:
        text, o = (item, {}) if isinstance(item, str) else item
        p = tf.paragraphs[0] if first else tf.add_paragraph()
        first = False
        p.space_after = Pt(o.get("space", space))
        p.alignment = o.get("align", align) or PP_ALIGN.LEFT
        for t, b, code in runs_from_md(text):
            r = p.add_run()
            r.text = t
            r.font.size = Pt(o.get("size", size))
            r.font.bold = b or o.get("bold", bold)
            r.font.italic = o.get("italic", False)
            r.font.name = MONO if code else o.get("font", font)
            r.font.color.rgb = o.get("color", color)
        if o.get("bullet", bullets):
            set_bullet(p)


def add_text(slide, x, y, w, h, items, name=None, **kw):
    tb = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    if name:
        tb.name = name
    fill_tf(tb.text_frame, items, **kw)
    return tb


def add_box(slide, x, y, w, h, items=None, fill=LIGHT, line=RULE, lw=0.75, dash=False, name=None, **kw):
    sh = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h))
    sh.shadow.inherit = False
    style = sh._element.find(qn("p:style"))
    if style is not None:                      # drop the theme's shadow reference
        style.find(qn("a:effectRef")).set("idx", "0")
    if name:
        sh.name = name
    if fill is None:
        sh.fill.background()
    else:
        sh.fill.solid()
        sh.fill.fore_color.rgb = fill
    if line is None:
        sh.line.fill.background()
    else:
        sh.line.color.rgb = line
        sh.line.width = Pt(lw)
        if dash:
            sh.line.dash_style = MSO_LINE.DASH
    if items:
        kw.setdefault("margin", 0.1)
        kw.setdefault("anchor", MSO_ANCHOR.TOP)
        fill_tf(sh.text_frame, items, **kw)
    return sh


def add_code(slide, x, y, w, h, lines, size=12, name=None):
    """Monospaced block; lines is a list of plain strings (no markup)."""
    sh = add_box(slide, x, y, w, h, None, fill=CODEBG, line=RULE, name=name)
    tf = sh.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = MSO_ANCHOR.TOP
    tf.margin_left = tf.margin_right = Inches(0.1)
    tf.margin_top = tf.margin_bottom = Inches(0.06)
    for i, line in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = PP_ALIGN.LEFT
        p.space_after = Pt(0)
        r = p.add_run()
        r.text = line
        r.font.name = MONO
        r.font.size = Pt(size)
        r.font.color.rgb = INK
    return sh


def arrow(slide, x1, y1, x2, y2, color=NAVY, w=1.75, dash=False, head=True):
    c = slide.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, Inches(x1), Inches(y1), Inches(x2), Inches(y2))
    c.line.color.rgb = color
    c.line.width = Pt(w)
    if dash:
        c.line.dash_style = MSO_LINE.DASH
    if head:
        ln = c.line._get_or_add_ln()
        tail = etree.SubElement(ln, qn("a:tailEnd"))
        tail.set("type", "triangle")
    return c


def cell_borders(cell, color="C5CDD8", width_pt=0.75):
    tcPr = cell._tc.get_or_add_tcPr()
    for tag in ("a:lnL", "a:lnR", "a:lnT", "a:lnB"):
        ln = etree.SubElement(tcPr, qn(tag))
        ln.set("w", str(int(Pt(width_pt))))
        sf = etree.SubElement(ln, qn("a:solidFill"))
        etree.SubElement(sf, qn("a:srgbClr")).set("val", color)


def add_table(slide, x, y, col_w, rows, size=13, row_h=0.4, header=True, mono_cols=(), bold_first_col=False, name=None):
    """rows: list of rows of str (mini-markup allowed). Returns the graphic frame."""
    nrows, ncols = len(rows), len(rows[0])
    gf = slide.shapes.add_table(nrows, ncols, Inches(x), Inches(y), Inches(sum(col_w)), Inches(row_h * nrows))
    if name:
        gf.name = name
    tbl = gf.table
    tbl.first_row = header
    tbl.horz_banding = False
    for j, w in enumerate(col_w):
        tbl.columns[j].width = Inches(w)
    for i, row in enumerate(rows):
        tbl.rows[i].height = Inches(row_h)
        for j, text in enumerate(row):
            cell = tbl.cell(i, j)
            cell_borders(cell)
            is_head = header and i == 0
            cell.fill.solid()
            cell.fill.fore_color.rgb = NAVY if is_head else WHITE
            cell.margin_left = cell.margin_right = Inches(0.08)
            cell.margin_top = cell.margin_bottom = Inches(0.03)
            cell.vertical_anchor = MSO_ANCHOR.MIDDLE
            tf = cell.text_frame
            tf.word_wrap = True
            p = tf.paragraphs[0]
            for t, b, code in runs_from_md(text):
                r = p.add_run()
                r.text = t
                r.font.size = Pt(size)
                r.font.bold = is_head or b or (bold_first_col and j == 0)
                r.font.name = MONO if (code or (j in mono_cols and not is_head)) else FONT
                r.font.color.rgb = WHITE if is_head else INK
    return gf


def new_slide(num, title, notes):
    s = prs.slides.add_slide(LAYOUT_TITLE_ONLY)
    t = s.shapes.title
    t.left, t.top, t.width, t.height = Inches(LM), Inches(0.35), Inches(CW), Inches(0.85)
    tf = t.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    tf.margin_left = Inches(0.05)
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.LEFT
    r = p.add_run()
    r.text = title
    r.font.size = Pt(32 if len(title) <= 45 else 26)
    r.font.bold = True
    r.font.name = FONT
    r.font.color.rgb = NAVY
    add_text(s, LM, 7.0, 8, 0.3, ["NOVA  |  Compiler design project review"], size=11, color=GREY, name="Footer")
    add_text(s, SLIDE_W - LM - 1.5, 7.0, 1.5, 0.3, [(f"{num} / {TOTAL}", {"align": PP_ALIGN.RIGHT})], size=11,
             color=GREY, name="Slide number")
    NOTES[num] = notes
    return s


def finish_notes():
    for i, slide in enumerate(prs.slides, start=1):
        slide.notes_slide.notes_text_frame.text = NOTES[i]


# =====================================================================================================================
# slide 1 - title (the author removed the unfilled placeholders from the earlier version; this keeps that edit)
# =====================================================================================================================
s = prs.slides.add_slide(LAYOUT_TITLE)
t, sub = s.shapes.title, s.placeholders[1]
t.left, t.top, t.width, t.height = Inches(LM), Inches(1.0), Inches(11.6), Inches(2.3)
tf = t.text_frame
tf.word_wrap = True
tf.vertical_anchor = MSO_ANCHOR.TOP
tf.margin_left = Inches(0.05)
p = tf.paragraphs[0]
p.alignment = PP_ALIGN.LEFT
r = p.add_run()
r.text = "NOVA: An Explainable, Workload-Aware Compiler Optimization Framework for Numerical Programs"
r.font.size, r.font.bold, r.font.name, r.font.color.rgb = Pt(38), True, FONT, NAVY
sub.left, sub.top, sub.width, sub.height = Inches(LM), Inches(3.45), Inches(11.6), Inches(0.9)
sub.text_frame.clear()
fill_tf(sub.text_frame, [
    "Two parts: **NOVA Compiler** (educational compiler for NOVA Script) and **NOVA SmartPass** (research track: tuning GCC flags for numerical C++ kernels)",
], size=18, color=INK, align=PP_ALIGN.LEFT)
add_text(s, LM, 4.75, 9.5, 0.6, ["Course: Compiler Design"], size=16, color=GREY, space=5, name="Course details")
NOTES[1] = (
    "Opening. NOVA is one project with two separate parts. The first is a small compiler I built for a toy language "
    "called NOVA Script, so I could show every compiler stage working. The second, SmartPass, is a research tool: it "
    "does not compile C++ itself. It asks GCC to compile numerical C++ programs with different optimization flags, "
    "checks the answers, times them, and explains which flags won.\n\n"
    "Roadmap of this talk: the problem (slide 2), the two parts (3, 4), how SmartPass chooses and tests flags (5 to 7), "
    "how correctness is checked and why fast-math is a risk (8, 9), two live-style demos (10, 11), what NOVA adds (12), "
    "status and limits (13, 14), and a glossary (15).\n\n"
    "Likely question: 'Does NOVA compile C++?' Short answer: no. GCC compiles the C++; SmartPass only chooses the "
    "flags, checks correctness, measures and records.\n\n"
    "Team, guide and date details are not in the project files and have been left off this slide; add them if your "
    "department requires them."
)


# =====================================================================================================================
# slide 2 - problem statement
# =====================================================================================================================
s = new_slide(2, "Problem statement", (
    "In everyday language: GCC can make a program faster, but picking the right settings is not obvious, and a 'faster' "
    "result is only useful if the answer is still right and the speedup is repeatable.\n\n"
    "How it works: GCC has optimization levels (-O2, -O3) and many individual flags such as -funroll-loops. The GCC "
    "manual itself says unrolling 'may or may not make it run faster', so the benefit depends on the program. Some flags "
    "(like -Ofast) can change floating-point results. And timings on a real machine vary from run to run, so one "
    "measurement proves little.\n\n"
    "Relation to NOVA: these three questions (which flags, still correct, speedup real) are exactly what SmartPass "
    "tries to handle for a narrow case.\n\n"
    "Likely question: 'Do developers really struggle with this?' Honest answer: we did not run a survey. The sources "
    "show the configuration space and the measurement problems exist; they do not tell us how common the struggle is."
))
cards = [
    ("1. Which flags?", [
        "`-O2` and `-O3` are only two of many GCC optimization flags.",
        "Their effect depends on the program; for loop unrolling the manual says it “may or may not make it run faster” [1].",
        "Every candidate costs one compile and one run to test.",
    ]),
    ("2. Still correct?", [
        "A faster binary is useless if its answer changes.",
        "`-Ofast` enables transformations “not valid for all standard-compliant programs” [1].",
        "So each candidate must be checked against a reference.",
    ]),
    ("3. Speedup real?", [
        "Run times vary: on our machine the baseline spread (IQR) reached about 7% pinned and about 25% unpinned.",
        "So we need repeated runs, medians and saved raw samples [3, 4].",
        "A choice tuned on one input size may not hold on others.",
    ]),
]
cw, gap = 3.88, 0.245
for i, (head, bl) in enumerate(cards):
    x = LM + i * (cw + gap)
    add_box(s, x, 1.5, cw, 4.0, None, name=f"Card {i + 1}")
    add_text(s, x + 0.1, 1.6, cw - 0.2, 0.5, [(head, {"bold": True, "color": NAVY, "size": 20})])
    add_text(s, x + 0.1, 2.2, cw - 0.2, 3.2, bl, size=16, bullets=True, space=9)
add_text(s, LM, 5.75, CW, 0.9, [
    "**Scope of NOVA:** three small numerical C++ kernels (matmul, vector_add, stencil) and GCC flags only. It does not claim that every developer has this problem; no survey was done.",
], size=15)

# =====================================================================================================================
# slide 5 - proposed solution
# =====================================================================================================================
s = new_slide(3, "Proposed solution: two parts of NOVA", (
    "In everyday language: NOVA answers two different questions. The compiler answers 'how does a compiler turn a "
    "program into something it can optimize and run?'. SmartPass answers 'which GCC settings work best for this "
    "numerical program, and can we trust the answer?'.\n\n"
    "How it works: both are commands of one tool, nova. The compiler reads a .nova file and shows tokens, AST, TAC, "
    "optimized TAC, or runs it. SmartPass takes a built-in C++ kernel, asks g++ to build it with chosen flags, checks "
    "output, times it and writes a record. In the measurements the two parts share no code path.\n\n"
    "Relation to NOVA: keeping them separate is a design decision (D-001 in state/DECISIONS.md), so no result from one "
    "is used to claim anything about the other.\n\n"
    "Likely question: 'Why two parts?' The compiler is the course-aligned learning part; SmartPass is the research "
    "question. Mixing them would blur what each one proves."
))
add_box(s, 2.2, 1.4, 8.93, 0.6, [("nova  (one command-line tool, C++17)", {"align": PP_ALIGN.CENTER, "bold": True, "color": WHITE, "size": 16})],
        fill=NAVY, line=None, anchor=MSO_ANCHOR.MIDDLE, name="nova CLI")
arrow(s, 3.55, 2.0, 3.55, 2.45, head=True)
arrow(s, 9.78, 2.0, 9.78, 2.45, head=True)
pw = 5.9
for k, (px, head, tag, steps, desc) in enumerate([
    (LM, "NOVA Compiler", "educational track",
     ["NOVA Script (.nova)", "TAC", "Optimized TAC", "Run (interpreter)"],
     "Shows how a compiler works inside: tokens, syntax tree, simple intermediate code, and four safe optimizations. Runs programs on reference interpreters."),
    (LM + pw + 0.33, "NOVA SmartPass", "research track",
     ["C++ kernel", "GCC (g++)", "Check + time", "Explain"],
     "Asks GCC to build a numerical kernel with chosen flags, checks the output, measures, and records why a configuration won. **GCC does all the compiling.**"),
]):
    add_box(s, px, 2.5, pw, 3.55, None, fill=WHITE, line=NAVY, lw=1.25, name=head)
    add_text(s, px + 0.15, 2.58, pw - 0.3, 0.45, [(f"{head}  ", {"bold": True, "color": NAVY, "size": 20})])
    add_text(s, px + 0.15, 3.02, pw - 0.3, 0.35, [(tag, {"color": GREY, "size": 14, "italic": True})])
    bw, bg = 1.2, 0.21
    for i, st in enumerate(steps):
        bx = px + 0.2 + i * (bw + bg)
        add_box(s, bx, 3.5, bw, 0.85, [(st, {"align": PP_ALIGN.CENTER, "size": 12})], fill=LIGHT, anchor=MSO_ANCHOR.MIDDLE,
                margin=0.04, name=f"{head} step {i + 1}")
        if i < len(steps) - 1:
            arrow(s, bx + bw, 3.925, bx + bw + bg, 3.925, w=1.5)
    add_text(s, px + 0.15, 4.55, pw - 0.3, 1.45, [desc], size=14)
add_text(s, LM, 6.25, CW, 0.6, [
    "**Objectives:** (A) show every compiler stage working on a small language; (B) test a bounded, workload-aware, correctness-checked search over GCC flags and keep the evidence. The two parts are kept separate on purpose.",
], size=14, color=GREY)

# =====================================================================================================================
# slide 6 - compiler architecture
# =====================================================================================================================
s = new_slide(4, "NOVA Compiler: pipeline", (
    "In everyday language: the compiler reads a small program, understands its structure, turns it into very simple "
    "step-by-step instructions, cleans those up, and runs them.\n\n"
    "How it works: the lexer produces tokens with line and column. The parser is recursive descent: one function per "
    "precedence level (additive, term, unary, primary), which gives correct precedence for * and / over + and -. The AST "
    "records the meaning. Semantic analysis uses a symbol table to reject undeclared and duplicate variables. TAC breaks "
    "every expression into instructions with at most one operator, for example $t1 = x * 2; that is easier to analyze than "
    "a tree. The optimizer rewrites TAC. The interpreters execute the AST or the TAC.\n\n"
    "Relation to NOVA: every stage in the diagram exists in the source (src/lexer.cpp, parser.cpp, semantic.cpp, tac.cpp, "
    "optimizer.cpp, interp.cpp). There is no machine-code generator; programs run on the interpreters.\n\n"
    "Likely question: 'Where is code generation?' We did not build one. The language is tiny (integers, let, assignment, "
    "print, + - * /), so the goal was front end, IR and optimization, not a native backend."
))
stages = ["Source (.nova)", "Lexer", "Parser", "AST", "Semantic analysis", "TAC", "Optimizer", "Interpreter"]
bw, bg = 1.25, 0.3
for i, st in enumerate(stages):
    bx = LM + i * (bw + bg)
    plain = i in (0,)
    add_box(s, bx, 1.4, bw, 0.8, [(st, {"align": PP_ALIGN.CENTER, "size": 13, "bold": not plain})], fill=WHITE if plain else LIGHT,
            line=RULE if plain else NAVY, anchor=MSO_ANCHOR.MIDDLE, margin=0.04, name=f"Stage {st}")
    if i < len(stages) - 1:
        arrow(s, bx + bw, 1.8, bx + bw + bg, 1.8, w=1.5)
add_text(s, LM, 2.25, CW, 0.35, [("All stages shown are implemented. No machine-code backend: programs run on the AST and TAC interpreters.", {"color": GREY, "size": 13, "italic": True})])
rows = [
    ["Stage", "What it does and why it matters"],
    ["Lexer", "Splits text into tokens (keyword, identifier, integer, operator) with line:column for error messages."],
    ["Parser", "Recursive descent, one function per precedence level; reports the first syntax error with its location."],
    ["AST", "Tree of the program (Let, Print, Binary…), independent of how the text was laid out."],
    ["Semantic analysis", "Symbol table: rejects use of an undeclared variable and duplicate declarations."],
    ["TAC", "Each instruction has at most one operator, e.g. `$t1 = x * 2`: simple enough to analyze and optimize."],
    ["Optimizer", "Four passes repeated until nothing changes (a fixed point); see slide 10."],
    ["Interpreter", "Runs the AST or the TAC; division by zero is a runtime error with a location."],
]
add_table(s, LM, 2.7, [2.2, 9.93], rows, size=13, row_h=0.43, bold_first_col=True, name="Stage descriptions")
add_text(s, LM, 6.2, CW, 0.6, [
    "NOVA Script: integers only (64-bit wrapping), `let`, assignment, `print`, `+ - * /`, parentheses; no loops or branches yet.",
], size=13, color=GREY)

# =====================================================================================================================
# slide 5 - how SmartPass chooses compiler flags
# =====================================================================================================================
s = new_slide(5, "How NOVA SmartPass Chooses Compiler Flags", (
    "In everyday language: SmartPass cannot try every flag combination, so it tries a few, chosen with some care, and "
    "keeps a record. Each try is called a candidate: one flag combination from a fixed list. For the demonstration we "
    "allow at most four searched candidates (the budget).\n\n"
    "The loop in the top row is the same for every candidate: examine the workload, select a candidate, compile with "
    "GCC, check correctness, measure, record evidence.\n\n"
    "The five signals, and what is REALLY implemented (be exact in the viva):\n"
    "1. Source-code features: the features (size n, working-set bytes, streaming or not, reduction or not) are written "
    "per benchmark in src/tune/features.cpp and computed from the benchmark definition and n. NOVA does not read or "
    "understand arbitrary C++ source.\n"
    "2. Loop depth: recorded in the features (matmul 3, vector_add 2, stencil 2) but NOT used in the current scoring. "
    "Example for teaching: a matrix-vector loop has two nested levels (i outer, j inner). Deeper nesting does not mean "
    "faster code; it only describes the structure.\n"
    "3. Arithmetic intensity = floating-point operations divided by bytes moved. NOVA computes an estimate "
    "(flops_per_byte) from the kernel definition and n, and records it. It is not used in the current scoring.\n"
    "4. Memory-bandwidth demand: there is no bandwidth model and no hardware counter. The only memory signal used is "
    "the working-set size: above 4 MiB the prefetch flag is weighted up. That is an estimate of demand, never a "
    "measurement of bandwidth.\n"
    "5. Historical evidence: implemented. A history file (--history=FILE) stores, per benchmark, size and candidate, the "
    "log of (O2 median / candidate median) from earlier tuning runs. Similarity: same benchmark counts with weight "
    "exp(-|difference in log2 n| / 4); another benchmark counts with weight 0.25. Failures count as a loss of 0.3 in "
    "log-gain. History only changes the ORDER in which candidates are tried; it never skips the correctness check. "
    "Without a history file the adaptive strategy still runs, using the prior and the pilot feedback.\n\n"
    "The score used today is: prior (streaming, reduction, working set above 4 MiB) + history + feedback from the short "
    "pilot of already-built candidates. It is a hand-written heuristic, not machine learning.\n\n"
    "Likely questions: 'Does it understand my source code?' No. 'Is deeper nesting faster?' No claim. 'Where does "
    "bandwidth come in?' Only through the working-set proxy."
))
flow = ["1 Examine the workload", "2 Select a candidate", "3 Compile with GCC", "4 Check correctness", "5 Measure", "6 Record evidence"]
fw, fg = 1.82, 0.242
for i, st in enumerate(flow):
    fx = LM + i * (fw + fg)
    add_box(s, fx, 1.3, fw, 0.62, [(st, {"align": PP_ALIGN.CENTER, "size": 12.5, "bold": True, "color": NAVY})], fill=LIGHT, line=NAVY,
            anchor=MSO_ANCHOR.MIDDLE, margin=0.04, name=f"Flow {i + 1}")
    if i < len(flow) - 1:
        arrow(s, fx + fw, 1.61, fx + fw + fg, 1.61, w=1.5)
add_text(s, LM, 1.95, CW, 0.3, [("Repeat until the search budget is used (demonstration setting: at most 4 searched candidates).",
                                 {"color": GREY, "size": 12.5, "italic": True})])
add_text(s, LM, 2.3, CW, 0.35, [("Five signals used to choose the next candidate, and their real status", {"bold": True, "color": NAVY, "size": 16})])
signals = [
    ("1  Source-code features", "Per-kernel facts (size n, working set, streaming?, reduction?) registered for matmul, vector_add, stencil. Not parsed from arbitrary C++.", "Used"),
    ("2  Loop depth", "How deeply the kernel's loops nest (recorded: matmul 3, stencil 2). Deeper does not mean faster.", "Recorded, not scored"),
    ("3  Arithmetic intensity", "FLOPs ÷ bytes moved, estimated from the kernel definition and n.", "Recorded, not scored"),
    ("4  Memory-bandwidth demand", "Proxy only: working set above 4 MiB favors prefetch flags. No bandwidth model, no hardware counters.", "Proxy used"),
    ("5  Historical evidence", "Earlier log-gains vs -O2; closer input sizes count more. Needs a history file.", "Used if file given"),
]
cw5, cg5 = 2.3, 0.1575
for i, (h, body, tag) in enumerate(signals):
    cx = LM + i * (cw5 + cg5)
    add_box(s, cx, 2.7, cw5, 2.55, [(h, {"bold": True, "color": NAVY, "size": 14, "space": 5}), (body, {"size": 13, "space": 6}),
                                    (tag, {"size": 12, "bold": True, "color": AMBER if "not" in tag else NAVY})],
            fill=WHITE, line=NAVY, lw=1.0, name=f"Signal {i + 1}")
add_text(s, LM, 5.38, 5.4, 0.3, [("Teaching example for signal 2: two nested loop levels", {"bold": True, "color": NAVY, "size": 13})])
add_code(s, LM, 5.7, 5.4, 1.2, [
    "for (int i = 0; i < n; i++) {",
    "  for (int j = 0; j < n; j++) {",
    "    result[i] += a[i][j] * x[j];",
    "  }",
    "}",
], size=11.5, name="Loop depth example")
add_box(s, 6.3, 5.4, 6.43, 1.5, [
    "**Score used today** = prior (streaming, reduction, working set > 4 MiB) + history + pilot feedback. A hand-written heuristic, not machine learning.",
    "Loop depth and FLOPs/byte are recorded for explanation; they do not change the order yet.",
], fill=LIGHT, line=RULE, size=13, space=6, anchor=MSO_ANCHOR.MIDDLE, margin=0.15, name="Scoring note")


# =====================================================================================================================
# slide 6 - candidates, budget and accounting
# =====================================================================================================================
s = new_slide(6, "Candidates, Budget and Accounting", (
    "In everyday language: the budget B is the number of searched candidates SmartPass may build. We demonstrate B = 4. "
    "The two baselines, GCC -O2 and -O3, are always built in addition and do not use the budget, so B = 4 means 6 builds.\n\n"
    "Selected vs compiled vs valid. In the implementation a candidate is counted the moment it is selected and its build is "
    "attempted (src/tune/tuner.cpp: add_candidate increments builds_attempted and evaluations). So 4 selected = 4 "
    "compile attempts. How many of those become valid measurements depends on what happens next: a build can fail "
    "(BUILD_FAILED), the program can run but give a wrong answer (CORRECTNESS_FAILED), the binary can be rejected by the "
    "size policy (REJECTED_BY_POLICY, more than 2.0 times the -O2 binary), or it can be timed and ranked. Failed "
    "candidates still count against the budget, and there are no automatic retries. So you get between 0 and 4 valid "
    "measured candidates from a budget of 4, never more.\n\n"
    "Invalid flags (not in the whitelist, duplicates, or not exactly one -O level) are caught BEFORE anything is built: the "
    "run is refused with an error and nothing is counted.\n\n"
    "The A to D table is ILLUSTRATIVE. The flag IDs are real members of the 22-candidate space and all four were built in "
    "the saved B = 4 runs, but those saved runs used the random strategy. A real adaptive run would choose its own four, "
    "ordered by the score from slide 5. Do not say these are adaptive picks, and do not claim any of them is a global "
    "optimum: only 22 combinations exist, and the result applies to one machine and one input size.\n\n"
    "Likely questions: 'Why does a failed build count?' So random and adaptive search are compared at equal cost, and "
    "so a search cannot hide bad luck by retrying. 'Is B fixed at 4?' No: --budget is required and can be 0 to 100000; 4 is "
    "the demonstration setting."
))
add_text(s, LM, 1.3, 6.2, 0.35, [("Illustrative candidates for B = 4", {"bold": True, "color": NAVY, "size": 16})])
rows = [
    ["", "Candidate (flag ID)", "Why test it?", "Status"],
    ["A", "`O2+prefetch+native`", "Conservative: stays at -O2, adds prefetch", "Illustrative"],
    ["B", "`O3+unroll+native`", "Stronger optimization (-O3, unrolling)", "Illustrative"],
    ["C", "`O2+prefetch+vec+native`", "Different supported flag combination", "Illustrative"],
    ["D", "`O3+prefetch+unroll`", "Chosen from workload features or history", "Illustrative"],
]
add_table(s, LM, 1.7, [0.4, 2.35, 2.5, 1.2], rows, size=11.5, row_h=0.62, name="Illustrative candidates")
add_text(s, LM, 4.85, 6.45, 0.9, [
    "Real IDs from the 22-candidate space (`-march=native` appears as `native`). A to D are not a measured adaptive run and not a claim of a global optimum.",
], size=11.5, color=GREY)
add_text(s, 7.35, 1.3, 5.4, 0.35, [("What uses the budget? (B = 4)", {"bold": True, "color": NAVY, "size": 16})])
rows = [
    ["Event", "Uses budget?"],
    ["Invalid flag set", "No: refused before any build"],
    ["Baselines -O2, -O3", "No: always built"],
    ["Build fails", "Yes: still counted"],
    ["Wrong answer", "Yes: counted, cannot win"],
    ["Valid measurement", "Yes"],
    ["Retry of a failure", "None exist"],
]
add_table(s, 7.35, 1.7, [2.35, 3.03], rows, size=12.5, row_h=0.47, name="Accounting rules")
# selected / built / valid strip
sw = 3.85
for i, (h, b) in enumerate([
    ("Selected", "4 candidates (the budget)"),
    ("Compiled", "4 attempts + 2 baselines = 6 builds"),
    ("Valid measurements", "0 to 4 searched, plus baselines"),
]):
    sx = LM + i * (sw + 0.29)
    add_box(s, sx, 5.8, sw, 1.0, [(h, {"bold": True, "color": NAVY, "size": 14, "align": PP_ALIGN.CENTER, "space": 3}),
                                  (b, {"size": 13, "align": PP_ALIGN.CENTER})], fill=LIGHT, line=NAVY, anchor=MSO_ANCHOR.MIDDLE,
            name=f"Count {h}")
    if i < 2:
        arrow(s, sx + sw, 6.3, sx + sw + 0.29, 6.3, w=1.5)


# =====================================================================================================================
# slide 7 - candidate lifecycle
# =====================================================================================================================
s = new_slide(7, "Candidate Lifecycle", (
    "In everyday language: this is the life of ONE candidate, from being proposed to being ranked. Two things can go "
    "wrong, and both are recorded rather than hidden: the build can fail, or the program can build but give a wrong answer.\n\n"
    "Stage table (what happens / what is recorded):\n"
    "1 Proposed: the strategy (fixed, random or adaptive) picks the next unevaluated candidate / its flag ID and position in the sequence.\n"
    "2 Budget checked: the search stops when B searched candidates have been used / the evaluation count.\n"
    "3 Compilation attempted: g++ with the fixed flags plus the candidate's whitelisted flags, passed as an argument "
    "list, never a shell string / build status, binary size, build log. A failure becomes BUILD_FAILED.\n"
    "4 Correctness validation: one validation run, discarded for timing, is compared with the independent reference / "
    "pass or the list of failed checks. A failure becomes CORRECTNESS_FAILED and its timing samples are cleared.\n"
    "5 Performance measurement: 2 warm-up runs, then 15 timed rounds, with all candidates interleaved and the CPU pinned / "
    "raw samples, median, IQR.\n"
    "6 Result recorded: manifest.json, results.json/csv, raw_timings.csv, logs.\n"
    "7 Compared: eligible candidates (built, correct, fully timed, binary within 2.0 times the -O2 binary) are ranked by "
    "median; rank 1 wins. REJECTED_BY_POLICY marks a too-large binary.\n"
    "8 Next or stop: back to stage 1 while budget remains; otherwise stop and explain.\n\n"
    "The anchor is the plain -O2 baseline. It is built first and does not use the budget. Adaptive search compares each "
    "new candidate with it in the short pilot, history log-gains are measured against it, and the binary-size limit is "
    "relative to it.\n\n"
    "Order in the code, for accuracy: first the two baselines and then the searched candidates are BUILT one by one "
    "(adaptive: after each build a short pilot of interleaved anchor and candidate rounds, 5 by default, updates the "
    "scores). Only after the search ends do validation and the 15-round timing run for all built candidates together, "
    "interleaved. The slide shows the lifecycle of one candidate, which is the easiest way to explain it.\n\n"
    "Likely question: 'What if the fastest candidate gives a wrong answer?' It is marked CORRECTNESS_FAILED, kept in the "
    "record with the reason, and can never be ranked."
))
colx = [LM + i * 2.5 for i in range(5)]
bw7, bh7 = 2.1, 1.0


def node(x, y, w, h, head, sub, fill=LIGHT, line=NAVY, dash=False, name=None):
    return add_box(s, x, y, w, h, [(head, {"bold": True, "color": NAVY, "size": 14, "align": PP_ALIGN.CENTER, "space": 2}),
                                   (sub, {"size": 11.5, "align": PP_ALIGN.CENTER})],
                   fill=fill, line=line, lw=1.25, dash=dash, anchor=MSO_ANCHOR.MIDDLE, margin=0.05, name=name)


top = [("1 Candidate proposed", "from the strategy"), ("2 Budget checked", "budget left? if not, stop"),
       ("3 Compile with GCC", "executable produced?"), ("4 Correctness check", "vs reference, tolerance 1e-9"),
       ("5 Measure", "warm-ups + 15 timed runs")]
for i, (h, sb) in enumerate(top):
    node(colx[i], 1.4, bw7, bh7, h, sb, name=f"Lifecycle {i + 1}")
    if i < 4:
        arrow(s, colx[i] + bw7, 1.9, colx[i + 1], 1.9, w=1.75)
# failure box under stages 3 and 4
FAIL = RGBColor(0xFF, 0xF6, 0xE5)
add_box(s, colx[2], 3.2, colx[3] + bw7 - colx[2], 1.05, [
    ("Failed candidate: BUILD_FAILED or CORRECTNESS_FAILED", {"bold": True, "color": AMBER, "size": 13, "align": PP_ALIGN.CENTER, "space": 3}),
    ("recorded with the reason; still counts toward B; can never win", {"size": 12, "align": PP_ALIGN.CENTER}),
], fill=FAIL, line=AMBER, lw=1.25, anchor=MSO_ANCHOR.MIDDLE, name="Failure branch")
arrow(s, colx[2] + bw7 / 2, 2.4, colx[2] + bw7 / 2, 3.2, color=AMBER, w=1.5)
arrow(s, colx[3] + bw7 / 2, 2.4, colx[3] + bw7 / 2, 3.2, color=AMBER, w=1.5)
add_text(s, colx[2] + bw7 / 2 + 0.08, 2.6, 0.9, 0.3, [("build fails", {"size": 11, "color": AMBER, "italic": True})])
add_text(s, colx[3] + bw7 / 2 + 0.08, 2.6, 1.2, 0.3, [("wrong answer", {"size": 11, "color": AMBER, "italic": True})])
# result recorded
node(colx[4], 3.2, bw7, 1.05, "6 Result recorded", "state, median, IQR, size", name="Lifecycle 6")
arrow(s, colx[4] + bw7 / 2, 2.4, colx[4] + bw7 / 2, 3.2, w=1.75)
arrow(s, colx[3] + bw7, 3.725, colx[4], 3.725, color=AMBER, w=1.5)
node(colx[4], 4.95, bw7, 1.05, "7 Compared", "rank correct candidates", name="Lifecycle 7")
arrow(s, colx[4] + bw7 / 2, 4.25, colx[4] + bw7 / 2, 4.95, w=1.75)
node(colx[3], 4.95, bw7, 1.05, "8 Next or stop", "back to 1, or stop and explain", name="Lifecycle 8")
arrow(s, colx[4], 5.475, colx[3] + bw7, 5.475, w=1.75)
add_box(s, LM, 3.2, 4.6, 1.05, [
    "**Reference (oracle):** an independent long-double calculation inside the harness. No reference executable is built.",
], fill=WHITE, line=RULE, size=12.5, anchor=MSO_ANCHOR.MIDDLE, margin=0.12, name="Reference note")
add_box(s, LM, 4.95, 7.0, 1.05, [
    "**Anchor = the plain -O2 baseline.** Built first and not counted in B. Pilot runs, history log-gains and the binary-size limit (at most 2.0x the `-O2` binary) are measured against it.",
], fill=WHITE, line=RULE, size=12.5, anchor=MSO_ANCHOR.MIDDLE, margin=0.12, name="Anchor note")
add_text(s, LM, 6.2, CW, 0.7, [
    "In the code, candidates are built one by one first (adaptive: with a short pilot against the anchor); validation and timing then run for all built candidates together, interleaved. States: BUILD_FAILED, CORRECTNESS_FAILED, REJECTED_BY_POLICY, ELIGIBLE.",
], size=12, color=GREY)


# =====================================================================================================================
# slide 8 - numerical checks
# =====================================================================================================================
s = new_slide(8, "How NOVA Checks That Optimization Has Not Changed the Answer", (
    "In everyday language: after GCC builds a candidate, SmartPass runs it and compares a few numbers from its output with "
    "numbers computed independently. If any differ by more than a tiny allowance, the candidate is thrown out, however fast it is.\n\n"
    "What is implemented (src/bench/registry.cpp and include/nova/bench/registry.hpp). Every kernel prints five "
    "statistics of its output array v of length len: sum (total of all elements), s0 = v[0] (first), s1 = v[len/2] "
    "(middle), s2 = v[len-1] (final), and wsum, a weighted checksum: the sum of ((i mod 7) + 1) * v[i]. The weights cycle "
    "1 to 7, so an element moving to another position changes the checksum.\n\n"
    "Comparison rule. The tolerance is 1e-9 and it is MIXED, not purely absolute: |actual - expected| <= 1e-9 * "
    "max(1, |expected|). For values with |expected| up to 1 that is an absolute tolerance of 1e-9; for large values such as a "
    "total of about 1000 it is a relative tolerance, so the allowance grows to about 1e-6. This suits sums of many "
    "doubles, where rounding error grows with the magnitude. Please do not say it is purely absolute: the code uses the "
    "max(1, |expected|) form.\n\n"
    "Other rules. The kernel name, input size n, seed and output length must match exactly. A value that is not a number "
    "(NaN) or infinite fails: the parser rejects non-finite output, and the comparison is written as 'not (difference <= "
    "limit)', so NaN can never pass. There is no separate rule for empty output: the length must equal the expected length.\n\n"
    "Where the expected values come from: a long-double reference computed inside the harness from the kernel's "
    "definition and the input seed, independent of any GCC-built binary.\n\n"
    "The worked example uses ILLUSTRATIVE numbers, not values from a run: the first pair passes with a difference of "
    "4e-10 against an allowance of about 1e-6; the second fails with a difference of 2.1e-9 against 1e-9.\n\n"
    "Limits to say out loud: this is a lightweight gate, not a proof that the optimized program is equivalent. Five "
    "statistics of an array can match while individual elements differ, and the project does not compare every element. "
    "It was tested with a deliberately wrong kernel, and the -Ofast run (a separate disclosure experiment, V0.6) still "
    "passed it on all three kernels, which shows how forgiving the tolerance is. A scalar-only program would need its "
    "own outputs to be checked; the current kernels all produce arrays.\n\n"
    "Likely question: 'Why not compare all elements?' A full comparison is possible for small outputs and would be a good "
    "extension; the current design keeps output cheap and uniform across kernels."
))
add_text(s, LM, 1.3, 6.5, 0.35, [("Five numerical checks (each kernel prints them)", {"bold": True, "color": NAVY, "size": 16})])
rows = [
    ["#", "Check", "Value compared"],
    ["1", "Total scalar sum", "`sum` = v[0] + … + v[len−1]"],
    ["2", "First element", "`s0` = v[0]"],
    ["3", "Middle element", "`s1` = v[len / 2]"],
    ["4", "Final element", "`s2` = v[len − 1]"],
    ["5", "Weighted-index checksum", "`wsum` = Σ ((i mod 7) + 1) · v[i]"],
]
add_table(s, LM, 1.7, [0.45, 2.55, 3.5], rows, size=12.5, row_h=0.5, name="Five checks")
add_box(s, LM, 4.85, 6.5, 1.25, [
    ("Rule (tolerance 1e-9)", {"bold": True, "color": NAVY, "size": 13, "space": 3}),
    ("`|actual − expected| ≤ 1e-9 · max(1, |expected|)`", {"size": 14, "space": 3}),
    ("Absolute near zero, relative for large values.", {"size": 12}),
], fill=LIGHT, line=NAVY, anchor=MSO_ANCHOR.MIDDLE, margin=0.15, name="Comparison rule")
add_text(s, LM, 6.2, 6.5, 0.7, [
    "Kernel, n, seed and output length must also match. NaN or infinity fails. No separate empty-output rule: the length must equal the expected length.",
], size=11.5, color=GREY)
add_text(s, 7.4, 1.3, 5.33, 0.35, [("Worked example (illustrative values)", {"bold": True, "color": NAVY, "size": 16})])
rows = [
    ["Check", "Expected", "Candidate", "|diff|", "Allowed", "Result"],
    ["`sum`", "1048.5732", "1048.5732000004", "4.0e-10", "1.05e-6", "Pass"],
    ["`s1`", "0.5", "0.5000000021", "2.1e-9", "1.0e-9", "Fail"],
]
gf = add_table(s, 7.4, 1.7, [0.6, 0.95, 1.45, 0.8, 0.75, 0.78], rows, size=10.5, row_h=0.55, name="Worked example")
add_text(s, 7.4, 3.45, 5.33, 0.9, [
    "One failed check rejects the whole candidate: it is recorded as CORRECTNESS_FAILED and cannot win, even if it is the fastest.",
], size=13)
add_box(s, 7.4, 4.5, 5.33, 2.1, [
    ("A gate, not a proof", {"bold": True, "color": NAVY, "size": 14, "space": 5}),
    ("Five statistics can match while individual elements differ; the project does not compare every element.", {"size": 12.5, "bullet": True, "space": 4}),
    ("Where practical, a full comparison would be stronger; a scalar-only program would need its own output checks.", {"size": 12.5, "bullet": True, "space": 4}),
    ("The expected values come from a long-double reference inside the harness, not from a GCC-built executable.", {"size": 12.5, "bullet": True, "space": 4}),
], fill=WHITE, line=RULE, anchor=MSO_ANCHOR.TOP, margin=0.15, name="Limits note")


# =====================================================================================================================
# slide 9 - fast-math vs strict floating point
# =====================================================================================================================
s = new_slide(9, "Faster Code vs. Strict Floating-Point Behavior", (
    "In everyday language: strict floating-point rules make the compiler do the arithmetic in exactly the order you wrote. "
    "GCC's -ffast-math (spelled with two f's, not '-fastmath') tells it to relax those rules so it can reorder and "
    "simplify the arithmetic, which often makes loops faster, but the last digits of the answer can change.\n\n"
    "Reassociation. Real-number addition is associative, floating-point addition is not, because every addition rounds. "
    "With a = 1e16, b = -1e16, c = 1.0 in double precision: (a + b) + c = 1.0, but a + (b + c) = 0.0, because b + c rounds "
    "back to -1e16 (the spacing of doubles near 1e16 is 2). -ffast-math (through -fassociative-math, part of "
    "-funsafe-math-optimizations) allows GCC to pick either order, which also lets it vectorize a sum, since a vector "
    "sum adds in a different order.\n\n"
    "NaN and infinity. -ffast-math includes -ffinite-math-only: the compiler may assume values are never NaN or infinity, "
    "so a test such as x != x (is it NaN?) can be optimized away and an overflow may not be reported.\n\n"
    "Signed zero. -fno-signed-zeros lets -0.0 and +0.0 be treated as the same; that matters for functions like 1/x or atan2 "
    "that behave differently at -0.0.\n\n"
    "Reproducibility. With strict semantics, the same source gives the same bits on any IEEE-conforming build. With "
    "fast-math the result can depend on compiler version, flags and target, so two builds of one program can disagree "
    "in the last digits.\n\n"
    "Why some fields need strict semantics. Financial software must match audited, legally defined rounding and must "
    "reproduce the same figure later. Aerospace and space software is verified against specified numerical behaviour and "
    "relies on exceptional values (NaN, infinity) being detected, not assumed away. That does NOT mean every use of "
    "-ffast-math is unsafe: many graphics, game and simulation codes accept small differences on purpose.\n\n"
    "Relation to NOVA. -ffast-math is NOT in the SmartPass whitelist; docs/smartpass.md says a candidate such as "
    "'-O2 -ffast-math' is refused with the flag named. The only fast-math-style run is -Ofast in a separate disclosure "
    "experiment (V0.6), kept apart from the strict comparisons. In that experiment -Ofast passed the five-check gate on all "
    "three kernels. So passing the gate means 'matched within 1e-9 relative on these statistics', not 'safe for a "
    "high-stakes program'.\n\n"
    "Say clearly: NOVA does not advise using -ffast-math in safety-critical systems without domain-specific validation.\n\n"
    "Likely question: 'Can SmartPass choose -ffast-math?' No, it cannot reach GCC unless it is whitelisted."
))
add_text(s, LM, 1.3, 6.0, 0.35, [("What -ffast-math relaxes", {"bold": True, "color": NAVY, "size": 16})])
add_text(s, LM, 1.75, 6.0, 3.3, [
    "**Reassociation:** may regroup `(a + b) + c` as `a + (b + c)`; the rounding differs.",
    "**NaN / infinity:** may assume values are never NaN or infinite, so checks like `x != x` can disappear.",
    "**Signed zero:** may treat `-0.0` and `+0.0` as the same value.",
    "**Reproducibility:** the last digits can depend on compiler version, flags and target.",
], size=15, bullets=True, space=9)
add_text(s, LM, 4.55, 6.0, 0.3, [("Operation-order example (double precision)", {"bold": True, "color": NAVY, "size": 13})])
add_code(s, LM, 4.9, 6.0, 1.0, [
    "a = 1e16;  b = -1e16;  c = 1.0;",
    "(a + b) + c   ->  1.0",
    "a + (b + c)   ->  0.0   // b + c rounds to -1e16",
], size=12, name="Order example")
add_text(s, 6.95, 1.3, 5.78, 0.35, [("Why strict rules matter in some fields", {"bold": True, "color": NAVY, "size": 16})])
add_text(s, 6.95, 1.75, 5.78, 2.0, [
    "**Finance:** audited rounding and the same figure on every re-run.",
    "**Aerospace / space:** specified, verifiable numerics; NaN and overflow must be detected, not assumed away.",
    "This does **not** mean every use of `-ffast-math` is unsafe; many simulation and graphics programs accept small differences.",
], size=15, bullets=True, space=9)
add_box(s, 6.95, 4.2, 5.78, 2.65, [
    ("In NOVA", {"bold": True, "color": NAVY, "size": 14, "space": 5}),
    ("`-ffast-math` is not in the SmartPass whitelist; such a candidate is refused.", {"size": 14, "bullet": True, "space": 4}),
    ("`-Ofast` appears only in a separate disclosure run (V0.6) and passed the five checks on all three kernels.", {"size": 14, "bullet": True, "space": 4}),
    ("So the gate cannot show a result is safe for a high-stakes program. Use of fast-math there needs domain-specific validation.", {"size": 14, "bullet": True, "space": 4}),
], fill=LIGHT, line=NAVY, anchor=MSO_ANCHOR.TOP, margin=0.15, name="NOVA note")


# =====================================================================================================================
# slide 10 - running NOVA on a program (A): the compiler
# =====================================================================================================================
s = new_slide(10, "Running NOVA on a Program (A): NOVA Compiler", (
    "In everyday language: this is the educational compiler running on a tiny program. The source is NOVA Script, a "
    "small language of my own with integers, let, print and + - * /. It is not C++.\n\n"
    "How to build: with CMake and MinGW GCC on Windows: cmake -S . -B build -G \"MinGW Makefiles\" and then cmake --build "
    "build (README.md). The program is build/nova (nova.exe on Windows). The same tool also contains SmartPass.\n\n"
    "Commands, exactly as captured in docs/DEMO.md: 'nova tac examples/basic.nova' prints three-address code; 'nova run "
    "examples/basic.nova' prints 40; 'nova opt examples/optimize_demo.nova --show-all' shows each optimizer pass. Other "
    "commands: tokens, ast. The output on the slide is copied from docs/DEMO.md (generated by scripts/make_demo.sh on "
    "2026-10-14), not retyped from memory.\n\n"
    "What happened in the optimizer demo: fold evaluates 1 - 1 to 0 and 3 + 4 to 7; propagate replaces a by 4; simplify "
    "turns b + 0 into b, b - b into 0 and b * 1 into b; dead-code elimination removes assignments never used. The passes repeat "
    "until nothing changes (a fixed point, after 3 iterations here). The program prints 8 and 28 before and after.\n\n"
    "Compare with C++: there are no loops or branches in NOVA Script and no machine-code backend; programs run on "
    "interpreters. The C++ appears on the next slide, where GCC, not NOVA, compiles it.\n\n"
    "Likely question: 'How do you know the optimizer is safe?' It never folds a division by zero, so the runtime error "
    "still happens (nova opt examples/divzero.nova keeps $t1 = 10 / 0), and it is tested on 4000 pseudo-random programs "
    "before and after, comparing output, errors and error locations."
))
add_text(s, LM, 1.3, 3.85, 0.3, [("1. Build once", {"bold": True, "color": NAVY, "size": 14})])
add_code(s, LM, 1.65, 3.85, 1.0, ["cmake -S . -B build \\", "      -G \"MinGW Makefiles\"", "cmake --build build"], size=12, name="Build commands")
add_text(s, LM, 2.85, 3.85, 0.3, [("2. A NOVA Script program (not C++)", {"bold": True, "color": NAVY, "size": 14})])
add_text(s, LM, 3.13, 3.85, 0.3, [("examples/basic.nova", {"size": 11.5, "color": GREY})])
add_code(s, LM, 3.45, 3.85, 1.15, ["let x = 10;", "let y = 20;", "let z = x * 2 + y;", "print z;"], size=13, name="basic.nova")
add_text(s, LM, 4.8, 3.85, 1.5, [
    "NOVA Script has integers, `let`, `print` and `+ - * /` only. GCC and C++ are not involved in this part.",
], size=13, color=INK)
add_text(s, 4.7, 1.3, 3.85, 0.3, [("3. Compile to TAC, then run", {"bold": True, "color": NAVY, "size": 14})])
add_code(s, 4.7, 1.65, 3.85, 3.0, [
    "$ build/nova tac examples/basic.nova",
    "x = 10",
    "y = 20",
    "$t1 = x * 2",
    "z = $t1 + y",
    "print z",
    "",
    "$ build/nova run examples/basic.nova",
    "40",
], size=12, name="tac and run")
add_text(s, 4.7, 4.85, 3.85, 1.4, [
    "TAC = three-address code: at most one operator per line; `$t1` is a compiler temporary.",
], size=13)
add_text(s, 8.75, 1.3, 3.98, 0.3, [("4. Optimize", {"bold": True, "color": NAVY, "size": 14})])
add_code(s, 8.75, 1.65, 3.98, 1.8, [
    "// examples/optimize_demo.nova",
    "let a = 4;",
    "let b = a * 2 + 0;",
    "let unused = b - b;",
    "let z = 1 - 1;",
    "print b * 1;",
    "print a * (3 + 4);",
], size=12, name="optimize_demo.nova")
add_code(s, 8.75, 3.6, 3.98, 1.75, [
    "$ build/nova opt \\",
    "    examples/optimize_demo.nova \\",
    "    --show-all",
    "... (every pass)",
    "== final (fixed point after",
    "   3 iterations) ==",
    "print 8",
    "print 28",
], size=11, name="opt result")
add_text(s, 8.75, 5.45, 3.98, 1.0, [
    "Four passes (fold, propagate, simplify, dce) repeat to a fixed point. Output is unchanged: 8 and 28.",
], size=13)
add_text(s, LM, 6.45, CW, 0.45, [
    "Output copied from docs/DEMO.md (scripts/make_demo.sh, 2026-10-14). `--show-all` prints every pass. No machine-code backend: programs run on interpreters.",
], size=11.5, color=GREY)


# =====================================================================================================================
# slide 11 - running NOVA on a program (B): SmartPass
# =====================================================================================================================
s = new_slide(11, "Running NOVA on a Program (B): SmartPass", (
    "In everyday language: now the research part, on a real numerical C++ kernel: stencil, a smoothing loop that updates "
    "each cell from its neighbours (benchmarks/kernels/stencil.cpp). GCC compiles it; SmartPass only chooses flags, checks, "
    "times and records.\n\n"
    "Step by step.\n"
    "1. Baseline: GCC is run with the fixed flags -std=c++17 -msse2 -mfpmath=sse plus -O2 (and, separately, -O3). These two "
    "baselines are built in every experiment. The fixed flags force SSE2 arithmetic because the 32-bit MinGW compiler would "
    "otherwise use x87, which would make results incomparable across optimization levels.\n"
    "2. Reference: there is no reference executable. The expected five statistics are computed by an independent "
    "long-double calculation inside the harness.\n"
    "3. Tune: 'nova tune stencil --budget=4 --strategy=adaptive --history=FILE --record-history'. Options verified against the "
    "tune usage text: --budget is required; --strategy is fixed, random or adaptive (default random); --history and "
    "--record-history are adaptive-only; --record-history needs --history.\n"
    "4. Each candidate is compiled, validated against the reference, then timed (2 warm-ups and 15 interleaved rounds).\n"
    "5. 'nova explain <results dir>' prints every candidate, its state, the failure reason if any, and the winner. Raw "
    "evidence is in the results folder: manifest.json (flags, budget accounting, sequence, whitelist, environment), "
    "results.json and results.csv, raw_timings.csv and logs/. With --record-history, the tuning-role run adds entries "
    "(log of O2 median / candidate median) to the history file for later runs.\n\n"
    "IMPORTANT, what the table shows: it is the saved real run V0.7-stencil-random-s1-B4 (strategy RANDOM, seed 1, budget 4, "
    "tuning size n = 200,000, 15 timed rounds). The repository does not contain a saved adaptive B = 4 run, so the adaptive "
    "command on the slide is the supported syntax, not a captured result. Same source, compiler, machine and input for every "
    "row. The winner was O3+unroll+native with a ratio of medians of 1.317 against the better baseline (-O3), and the "
    "interquartile ranges do not overlap. That is selection on the tuning size only. It is NOT evidence for other sizes "
    "(see the holdout study on slide 12). Tuning overhead: 11.49 s wall time for 6 builds plus timing.\n\n"
    "How to read the winner: lowest median among ELIGIBLE candidates (built, correct, fully timed, binary within 2.0 times "
    "the -O2 binary). -march=native makes code for the build machine, so it is not portable.\n\n"
    "Likely question: 'Is SmartPass a new compiler?' No. GCC produces every binary; SmartPass is an experiment controller."
))
add_text(s, LM, 1.25, 6.4, 0.3, [("Kernel (C++): benchmarks/kernels/stencil.cpp", {"bold": True, "color": NAVY, "size": 13})])
add_code(s, LM, 1.55, 6.4, 0.7, [
    "for (std::size_t i = 1; i + 1 < n; ++i)",
    "  next[i] = 0.25 * cur[i-1] + 0.5 * cur[i] + 0.25 * cur[i+1];",
], size=11, name="Stencil kernel")
steps11 = [
    ("1 Baseline: GCC alone", ["g++ -std=c++17 -msse2 -mfpmath=sse -O2 …", "(same with -O3; built in every run)"]),
    ("2 Reference", ["built-in long-double oracle; no reference executable"]),
    ("3 Tune (budget = 4)", ["nova tune stencil --budget=4 --strategy=adaptive \\", "     --history=FILE --record-history"]),
    ("4 Explain", ["nova explain benchmarks/results/<experiment-id>"]),
]
yy = 2.4
for head, lines in steps11:
    h = 0.3 + 0.24 * len(lines) + 0.12
    add_text(s, LM, yy, 6.4, 0.28, [(head, {"bold": True, "color": NAVY, "size": 12.5})])
    add_code(s, LM, yy + 0.28, 6.4, 0.18 + 0.24 * len(lines), lines, size=10.5, name=f"Command {head}")
    yy += 0.28 + 0.18 + 0.24 * len(lines) + 0.08
add_text(s, LM, yy, 6.4, 0.6, [
    "Recorded in `benchmarks/results/<id>/`: manifest.json, results.json/csv, raw_timings.csv, logs/. `--record-history` also appends to the history file.",
], size=11.5, color=GREY)
add_text(s, 7.35, 1.25, 5.38, 0.3, [("Comparison: saved run, stencil n = 200,000", {"bold": True, "color": NAVY, "size": 13})])
rows = [
    ["Configuration", "Median (ms)", "IQR", "Binary (B)", "Correct"],
    ["GCC `-O2`", "16.178", "0.550", "81,660", "Pass"],
    ["GCC `-O3`", "11.677", "0.233", "81,746", "Pass"],
    ["Selected: `O3+unroll+native`", "8.869", "0.248", "84,306", "Pass"],
]
add_table(s, 7.35, 1.6, [2.1, 0.9, 0.62, 0.85, 0.91], rows, size=10.5, row_h=0.5, name="Comparison table")
add_text(s, 7.35, 3.75, 5.38, 3.1, [
    "Same source, compiler, machine and input; 15 timed rounds, median and IQR; raw samples are saved.",
    "Selected vs better baseline (`-O3`): ratio of medians **1.317**; IQRs do not overlap.",
    "Tuning overhead: **11.49 s** for 6 builds plus timing.",
    "This saved run used the **random** strategy (seed 1, B = 4). No adaptive B = 4 run is saved, so the adaptive command is the supported syntax, not a result.",
    "Tuning size only: not evidence for other sizes.",
], size=12, bullets=True, space=5)
add_text(s, LM, 6.68, CW, 0.3, [
    "SmartPass is not a compiler: GCC builds every binary. Real output: state/evidence/V0.7/20_explain_V0.7-stencil-random-s1-B4.log.",
], size=11.5, color=GREY)


# =====================================================================================================================
# slide 12 - what NOVA adds
# =====================================================================================================================
s = new_slide(12, "What NOVA Adds", (
    "In everyday language: NOVA SmartPass makes compiler-flag tuning more controlled and explainable. It does not make "
    "GCC faster by itself.\n\n"
    "The five reasons, each tied to something that exists in the code: (1) the budget bounds the experiment: at most B "
    "searched candidates plus two baselines, with failures counted; (2) workload features prioritize candidates: the "
    "prior favours vectorization and unrolling for streaming loops and prefetching for large working sets, and "
    "down-weights vectorization for reductions under strict floating point; (3) history helps when a history file is "
    "supplied, otherwise the other two signals still work; (4) numerical validation rejects a candidate whose answer "
    "differs, however fast; (5) recorded evidence (flags, raw timings, failures, environment, nova explain) shows why "
    "each candidate was accepted or rejected.\n\n"
    "Labels to keep straight. 'Proposed contribution' is the design: the combination of those five pieces. 'Demonstrated so far' "
    "is only what the measurements show: in the replay study, adaptive search was ahead of random at small budgets by a "
    "small margin (B = 4 on holdout sizes: adaptive 1.0xx vs random 1.0xx, values on the chart), the gap shrinks as B grows and "
    "everything ties at B = 22, and the individual signals (descriptors, history, feedback) were inconclusive. The study "
    "replays saved measurements on one machine with GCC 6.3.0 and three kernels. In real end-to-end runs adaptive spent "
    "more tuning time because of its pilot runs.\n\n"
    "Likely question: 'What is the benefit in one sentence?' A fixed, small number of tries that are checked for "
    "correctness, measured repeatedly, and fully recorded, so a reader can see why a configuration was chosen."
))
add_box(s, LM, 1.3, CW, 1.1, [
    "**Proposed contribution.** NOVA SmartPass combines workload-feature-guided candidate selection, a strict four-candidate experimental budget, numerical correctness gating, and recorded evidence to make compiler-configuration tuning more controlled and explainable.",
], fill=LIGHT, line=NAVY, lw=1.25, size=14.5, anchor=MSO_ANCHOR.MIDDLE, margin=0.18, name="Contribution statement")
add_text(s, LM, 2.6, 5.9, 0.35, [("Why each piece helps", {"bold": True, "color": NAVY, "size": 16})])
add_text(s, LM, 3.0, 5.9, 3.9, [
    "**Budget:** bounds the experiment; failed builds still count.",
    "**Workload features:** decide which candidates are tried first.",
    "**History:** helps when a history file is supplied (implemented, optional).",
    "**Numerical validation:** rejects results whose answer changed.",
    "**Recorded evidence:** explains why a candidate was accepted or rejected (`nova explain`).",
], size=14.5, bullets=True, space=9)
add_text(s, 6.9, 2.6, 5.83, 0.35, [("Demonstrated so far (replay study)", {"bold": True, "color": NAVY, "size": 16})])
cd = CategoryChartData()
cd.categories = [str(b) for b in BUDGETS]
for label, key in (("Fixed", "fixed"), ("Random", "random"), ("Adaptive", "adaptive")):
    cd.add_series(label, study(key, 4))
gf = s.shapes.add_chart(XL_CHART_TYPE.LINE_MARKERS, Inches(6.9), Inches(2.95), Inches(5.83), Inches(2.75), cd)
gf.name = "Holdout speedup vs budget"
ch = gf.chart
ch.has_title = False
ch.has_legend = True
ch.legend.position = XL_LEGEND_POSITION.BOTTOM
ch.legend.include_in_layout = False
ch.legend.font.size = Pt(11)
ch.font.name = FONT
va, ca = ch.value_axis, ch.category_axis
va.minimum_scale, va.maximum_scale, va.major_unit = 1.0, 1.12, 0.02
va.tick_labels.number_format, va.tick_labels.number_format_is_linked = "0.00", False
va.tick_labels.font.size = ca.tick_labels.font.size = Pt(11)
va.major_gridlines.format.line.color.rgb = RGBColor(0xDD, 0xE1, 0xE7)
va.has_title = True
va.axis_title.text_frame.text = "Mean holdout speedup (×)"
ca.has_title = True
ca.axis_title.text_frame.text = "Search budget B"
for ax in (va, ca):
    for r_ in ax.axis_title.text_frame.paragraphs[0].runs:
        r_.font.size, r_.font.bold, r_.font.name = Pt(11), False, FONT
styles = {"Fixed": (RGBColor(0xA3, 0xAA, 0xB5), MSO_LINE.ROUND_DOT, 1.75), "Random": (RGBColor(0x6B, 0x72, 0x80), MSO_LINE.DASH, 2.0),
          "Adaptive": (NAVY, MSO_LINE.SOLID, 3.0)}
for ser in ch.plots[0].series:
    col, dash, wid = styles[ser.name]
    ser.smooth = False
    ser.format.line.color.rgb, ser.format.line.width = col, Pt(wid)
    ser.format.line.dash_style = dash
    ser.marker.style, ser.marker.size = XL_MARKER_STYLE.CIRCLE, 6
    ser.marker.format.fill.solid()
    ser.marker.format.fill.fore_color.rgb = col
    ser.marker.format.line.color.rgb = col
add_text(s, 6.9, 5.75, 5.83, 1.15, [
    f"At B = 4: adaptive {H_ADAPT_B4:.3f} vs random {H_RANDOM_B4:.3f}: a small gap that shrinks as B grows (tie at B = 22).",
    "One machine, GCC 6.3.0, three kernels. Individual signals: inconclusive.",
], size=12, bullets=True, space=3)
add_text(s, LM, 6.6, 5.9, 0.3, ["Source: report/report.md §8.2 and §9."], size=11, color=GREY)


# =====================================================================================================================
# slide 12 - current progress
# =====================================================================================================================
s = new_slide(13, "Current progress", (
    "In everyday language: this is the honest status. The project tracks ten phases from V0.1 to V1.0, and each one is "
    "marked complete only when its required commands were actually run and the logs saved.\n\n"
    "How it works: state/ROADMAP_STATUS.md lists each phase with its evidence; state/evidence holds the logs; the final "
    "gate (docs/RELEASE.md) re-ran the full test suite: 49 of 49 tests pass, including the report tests. The release "
    "is scoped to the verified configuration: Windows 11, CMake, MinGW GCC 6.3.0.\n\n"
    "What is not finished: the Linux/POSIX process runner is written but has never been compiled or run, and "
    "AddressSanitizer or UBSan could not be used on this old toolchain. As a substitute the whole test suite was run with "
    "checked standard-library containers, which found and fixed one real bug.\n\n"
    "Likely question: 'Why is the status RELEASED and not just COMPLETE?' Because the release claim is limited to "
    "what was verified; other platforms and compilers are explicitly outside the claim."
))
rows = [
    ["Component", "Status", "Evidence"],
    ["Lexer, parser, AST, semantic checks, TAC", "Complete", "Phases V0.2 to V0.4; located diagnostics; AST and TAC interpreters agree"],
    ["TAC optimizer (4 passes)", "Complete", "V0.5; differential test on 4000 random programs"],
    ["Benchmark lab, -O2/-O3 baselines, oracle", "Complete", "V0.6; 3 kernels; raw samples and manifests saved"],
    ["SmartPass fixed and random search, correctness gate", "Complete", "V0.7; 12 equal-budget runs saved"],
    ["Adaptive search, ablations, holdout evaluation", "Complete", "V0.8; 7260 replayed runs, 27 real runs"],
    ["Evaluation report", "Complete", "report/report.md generated from saved data (V0.9)"],
    ["Whole test suite", "Complete", "49/49 CTest pass in the V1.0 log"],
    ["Linux (POSIX) process runner", "Partial", "Written, never compiled or run"],
    ["Sanitizers (ASan, UBSan)", "Not run", "Not available for this GCC; checked containers used instead"],
    ["Newer GCC, other machines", "Not done", "Outside the verified configuration"],
]
gf = add_table(s, LM, 1.4, [4.4, 1.35, 6.38], rows, size=12.5, row_h=0.43, bold_first_col=False, name="Progress table")
tbl = gf.table
for i in range(1, len(rows)):
    for r_ in tbl.cell(i, 1).text_frame.paragraphs[0].runs:
        r_.font.bold = True
        r_.font.color.rgb = NAVY if rows[i][1] == "Complete" else (AMBER if rows[i][1] == "Partial" else GREY)
add_text(s, LM, 6.3, CW, 0.6, [
    "Release status: V1.0 RELEASED for the verified configuration only (Windows 11, MinGW.org GCC 6.3.0). See state/ROADMAP_STATUS.md and docs/RELEASE.md.",
], size=13, color=GREY)

# =====================================================================================================================
# slide 14 - limitations, future scope, conclusion
# =====================================================================================================================
s = new_slide(14, "Limitations, Future Scope and Conclusion", (
    "In everyday language: the results are real but narrow, and the report says so itself.\n\n"
    "Limitations, one by one. One machine and an old 32-bit GCC 6.3.0: newer compilers may behave differently. Three small "
    "kernels (12 workloads): not representative of real programs. The searchable space has only 22 configurations, so a "
    "small budget already covers a lot of it and everything ties at B = 22. Timing noise: baselines had a median spread of "
    "2.1 percent and at most 7.2 percent, so smaller differences are inconclusive. The correctness check uses five output "
    "statistics, not every element. Adaptive search costs extra tuning time. The adaptive priors were written after "
    "seeing earlier timings of the same kernels. The scoring uses only part of the workload description: loop depth and "
    "arithmetic intensity are recorded but not scored, and there is no bandwidth model. Source features are registered "
    "per kernel, not extracted from arbitrary C++.\n\n"
    "Future scope is only a plan, not functionality: test on Linux and newer GCC, add sanitizers, more kernels and a "
    "bigger flag space, use loop depth and arithmetic intensity in scoring, compare all output elements where practical, "
    "and compare against an established tuner.\n\n"
    "Conclusion in plain words: NOVA keeps two questions apart. The compiler shows how a compiler is built and how safe "
    "optimizations work. SmartPass shows how to compare compiler settings carefully: a bounded budget, a correctness gate, "
    "repeated measurements, saved raw data and held-out sizes. In the measured setting adaptive search was slightly ahead of "
    "random at small budgets. We cannot say NOVA is faster than GCC or that the result holds on other machines or programs.\n\n"
    "References [1] to [5] are the sources behind claims on the slides; the full list with links is in NOVA_Slide_Content.md. "
    "Souper and egg are related work only; NOVA does not use them. The OpenTuner venue details were only partly re-verified "
    "(state/evidence/V0.9/10_references_check.md); check them before final submission.\n\n"
    "Likely question: 'What would you do first with more time?' Run the same study on a newer GCC and a second machine, "
    "because that tests whether the conclusions depend on this setup."
))
add_text(s, LM, 1.3, 6.0, 0.4, [("Limitations (documented)", {"bold": True, "color": NAVY, "size": 17})])
add_text(s, LM, 1.7, 6.0, 3.6, [
    "One machine, old 32-bit GCC 6.3.0; three small kernels.",
    "Only 22 searchable configurations.",
    "Timing noise: baseline spread up to 7.2% (median 2.1%).",
    "Correctness gate: five statistics, not every element.",
    "Loop depth and arithmetic intensity are recorded but not yet scored; no bandwidth model.",
    "Adaptive search costs extra tuning time (pilot runs).",
], size=16, bullets=True, space=10)
add_text(s, 6.9, 1.3, 5.83, 0.4, [("Future scope (not implemented)", {"bold": True, "color": NAVY, "size": 17})])
add_text(s, 6.9, 1.7, 5.83, 3.6, [
    "Linux and newer GCC; compile and test the POSIX runner.",
    "Sanitizer runs on a toolchain that supports them.",
    "More kernels, larger inputs, a bigger flag space.",
    "Use loop depth and arithmetic intensity in scoring.",
    "Compare every output element where practical.",
], size=16, bullets=True, space=10)
add_box(s, LM, 5.2, CW, 1.05, [
    "**Conclusion.** NOVA keeps two questions apart: how a compiler works (NOVA Compiler) and how to compare GCC settings carefully (SmartPass). In the measured setting, adaptive search was slightly ahead of random at small budgets; this does not show NOVA is faster than GCC or that results generalize.",
], fill=LIGHT, line=NAVY, size=13.5, anchor=MSO_ANCHOR.MIDDLE, margin=0.18, name="Conclusion")
add_text(s, LM, 6.35, CW, 0.6, [
    "References: [1] GNU Project, GCC manual, “Options That Control Optimization”  [2] Ansel et al., OpenTuner, PACT 2014  [3] Google Benchmark, “Random Interleaving”  [4] SPEC CPU 2026 Run and Reporting Rules  [5] LLVM, “My First Language Frontend”. Links: NOVA_Slide_Content.md.",
], size=10.5, color=GREY)


# =====================================================================================================================
# slide 15 - glossary
# =====================================================================================================================
s = new_slide(15, "Glossary", (
    "Use this slide to answer 'what do you mean by ...?' in the viva. The definitions follow the code, not a general "
    "textbook.\n\n"
    "Workload: a kernel plus its input size (stencil, n = 200,000). Candidate: one flag combination from the closed "
    "whitelist (-O2, -O3, -funroll-loops, -ftree-vectorize, -fprefetch-loop-arrays, -march=native; exactly one -O level, "
    "no duplicates). Flag: one option given to GCC. Binary: the executable GCC produced for one candidate. Budget: the "
    "number of searched candidates that may be built; baselines are extra. Adaptive search: next candidate chosen by "
    "prior + history + feedback. Accounting rules: every searched candidate that is selected and built counts, including "
    "failures; baselines do not; no retries; invalid flags are refused before building. Correctness gate: the five "
    "statistics must match the reference within the tolerance. Tolerance: |actual - expected| <= 1e-9 * max(1, |expected|). "
    "Arithmetic intensity: floating-point operations per byte moved. Tuning overhead: wall-clock time of the whole search "
    "(11.49 s for the saved stencil B = 4 run). Anchor: in the code, the plain -O2 baseline; it is the reference point for "
    "the pilot comparison, the history log-gains (log of anchor median over candidate median) and the binary-size limit.\n\n"
    "'Unaccounted anchor' is NOT a term defined anywhere in the project files, code or documents. The closest wording is "
    "'uncounted anchor baselines' in docs/NOVA_NotebookLM_Master_Source.md: the -O2 and -O3 baselines that are always "
    "evaluated and not counted against the budget. The slide says so rather than inventing a definition. If your guide or "
    "the original brief meant something else by it, the author needs to supply that definition.\n\n"
    "Likely question: 'Is the budget counted in builds or in measurements?' Searched candidates built (so failures count); "
    "B = 4 means 4 searched builds plus 2 baseline builds."
))
left = [
    ["Term", "Meaning in NOVA"],
    ["Workload", "A kernel plus its input size, e.g. stencil with n = 200,000."],
    ["Candidate", "One whitelisted flag combination, e.g. `O3+unroll+native`."],
    ["Flag", "A single GCC option, e.g. `-funroll-loops`."],
    ["Binary", "The executable GCC builds for one candidate."],
    ["Budget (B)", "Max searched candidates built; the -O2/-O3 baselines are extra."],
    ["Adaptive search", "Next candidate chosen by prior + history + pilot feedback."],
    ["Accounting rules", "Selected and built = counted, even if it fails; no retries."],
]
right = [
    ["Term", "Meaning in NOVA"],
    ["Correctness gate", "Five output statistics must match the reference or the candidate cannot win."],
    ["Tolerance", "Allowed gap: `1e-9 · max(1, |expected|)`"],
    ["Anchor", "The plain `-O2` baseline: reference for pilot runs, history and binary size."],
    ["“Unaccounted anchor”", "Not defined in the project. Closest: the uncounted -O2/-O3 anchor baselines. Author to confirm."],
    ["Arithmetic intensity", "Floating-point operations per byte moved."],
    ["Tuning overhead", "Wall-clock time of the whole search (11.49 s in the saved stencil run)."],
    ["Reference", "Independent long-double calculation of the expected statistics."],
]
add_table(s, LM, 1.3, [1.65, 4.25], left, size=11.5, row_h=0.64, bold_first_col=True, name="Glossary left")
gf = add_table(s, 6.83, 1.3, [1.75, 4.15], right, size=11.5, row_h=0.64, bold_first_col=True, name="Glossary right")
for r_ in gf.table.cell(4, 0).text_frame.paragraphs[0].runs + gf.table.cell(4, 1).text_frame.paragraphs[0].runs:
    r_.font.color.rgb = AMBER
add_text(s, LM, 6.55, CW, 0.4, [
    "Definitions follow the code (src/tune/, src/bench/) and docs/smartpass.md; the budget is configurable, 4 is the demonstration setting.",
], size=11.5, color=GREY)


# =====================================================================================================================
# save + markdown dump
# =====================================================================================================================
finish_notes()
prs.core_properties.title = "NOVA: An Explainable, Workload-Aware Compiler Optimization Framework for Numerical Programs"
prs.core_properties.author = ""
prs.core_properties.subject = "Project presentation"
prs.save(OUT_PPTX)
print("wrote", OUT_PPTX)

SOURCES = {
    1: "(course name only: team, guide and date are not in the project files)",
    2: "docs/01-product-requirements.md; docs/08-references.md; state/KNOWN_ISSUES.md (N-004); report/report.md section 7",
    3: "README.md; state/DECISIONS.md D-001; docs/02-architecture.md",
    4: "docs/language.md; docs/grammar.md; src/*.cpp; include/nova/tac.hpp; docs/DEMO.md",
    5: "src/tune/features.cpp, include/nova/tune/features.hpp; src/tune/adaptive.cpp; docs/adaptive.md; src/tune/cli.cpp (tune_usage)",
    6: "src/tune/tuner.cpp (add_candidate, eligibility); src/tune/space.cpp (whitelist, 22+2 space); docs/smartpass.md (Budget accounting); state/evidence/V0.7 explain logs",
    7: "src/bench/harness.cpp (validate_and_time); src/tune/tuner.cpp (anchor, lifecycle states); docs/smartpass.md",
    8: "src/bench/registry.cpp; include/nova/bench/registry.hpp (kRelativeTolerance); benchmarks/kernels/common.hpp; src/bench/harness.cpp (tolerance_rule); docs/benchmarks.md",
    9: "docs/smartpass.md (whitelist, refused flags); docs/benchmarks.md; benchmarks/results/V0.6-ofast-disclosure; state/evidence/V0.6/12_ofast_disclosure_run.log",
    10: "README.md (build); docs/DEMO.md (captured output); examples/basic.nova, examples/optimize_demo.nova",
    11: "src/tune/cli.cpp (tune_usage); benchmarks/kernels/stencil.cpp; state/evidence/V0.7/20_explain_V0.7-stencil-random-s1-B4.log; docs/benchmarks.md",
    12: "report/report.md section 8.2, section 9 (numbers parsed by this script); docs/adaptive.md; src/tune/adaptive.cpp",
    13: "state/ROADMAP_STATUS.md; docs/RELEASE.md; state/KNOWN_ISSUES.md; state/evidence/V1.0/02_ctest.log",
    14: "report/report.md sections 7, 10, 11; state/KNOWN_ISSUES.md; docs/08-references.md; state/evidence/V0.9/10_references_check.md",
    15: "src/tune/*.cpp; src/bench/*.cpp; docs/smartpass.md; docs/NOVA_NotebookLM_Master_Source.md (line 139: 'uncounted anchor baselines')",
}


def dump_markdown():
    lines = ["# NOVA presentation: slide-by-slide content", "",
             "Generated by `NOVA_Presentation_Source.py` from the finished deck, so it always matches the .pptx.",
             "Slide text is shown as it appears; speaker notes follow each slide. 'Sources' lists the project files each slide relies on.", ""]
    for i, slide in enumerate(prs.slides, start=1):
        title = slide.shapes.title.text_frame.text if slide.shapes.title is not None else f"Slide {i}"
        lines += [f"## Slide {i}: {title}", ""]
        for sh in slide.shapes:
            if sh.name in ("Footer", "Slide number") or sh == slide.shapes.title:
                continue
            if sh.has_text_frame and sh.text_frame.text.strip():
                for para in sh.text_frame.paragraphs:
                    t = "".join(r.text for r in para.runs).strip()
                    if t:
                        lines.append(("- " if para._p.pPr is not None and para._p.pPr.find(qn("a:buChar")) is not None else "") + t)
                lines.append("")
            elif getattr(sh, "has_table", False) and sh.has_table:
                tb = sh.table
                for ri, row in enumerate(tb.rows):
                    lines.append("| " + " | ".join(c.text_frame.text for c in row.cells) + " |")
                    if ri == 0:
                        lines.append("|" + "---|" * len(row.cells))
                lines.append("")
            elif getattr(sh, "has_chart", False) and sh.has_chart:
                c = sh.chart
                lines.append("Chart: mean holdout speedup vs search budget (values from report/report.md §8.2)")
                lines.append("")
                lines.append("| B | " + " | ".join(s_.name for s_ in c.plots[0].series) + " |")
                lines.append("|---|" + "---|" * len(c.plots[0].series))
                for k, cat in enumerate(c.plots[0].categories):
                    lines.append(f"| {cat} | " + " | ".join(f"{s_.values[k]:.3f}" for s_ in c.plots[0].series) + " |")
                lines.append("")
        lines += ["**Sources:** " + SOURCES[i], "", "**Speaker notes:**", ""]
        lines += ["> " + n_ if n_ else ">" for n_ in NOTES[i].split("\n")]
        lines.append("")
    lines += ["## References", "",
              "1. GNU Project, “Options That Control Optimization”, GCC manual. https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html",
              "2. J. Ansel et al., “OpenTuner: An Extensible Framework for Program Autotuning”, PACT 2014. https://doi.org/10.1145/2628071.2628092 (venue details only partly re-verified in the project: state/evidence/V0.9/10_references_check.md)",
              "3. Google Benchmark, “Random Interleaving”. https://github.com/google/benchmark/blob/main/docs/random_interleaving.md",
              "4. SPEC, “SPEC CPU 2026 Run and Reporting Rules”. https://www.spec.org/cpu2026/docs/runrules.html",
              "5. LLVM, “My First Language Frontend with LLVM”. https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/",
              "6. R. Sasnauskas et al., “Souper: A Synthesizing Superoptimizer”, arXiv:1711.04422 (related work only).",
              "7. M. Willsey et al., “egg: Fast and Extensible Equality Saturation”, arXiv:2004.03082 (related work only).", ""]
    OUT_MD.write_text("\n".join(lines), encoding="utf-8")
    print("wrote", OUT_MD)


dump_markdown()
