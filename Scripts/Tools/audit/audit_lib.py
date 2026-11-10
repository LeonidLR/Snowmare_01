"""Weekly code audit helpers (pure functions, no engine needed): analyzer-log parsing + baseline diff, git churn, per-system
coverage map, action selection, markdown report. Driven by Scripts/Tools/weekly_audit.ps1 -> run_audit.py. Tested by test_audit.py."""

import codecs
import datetime
import json
import os
import re
import subprocess

# MSVC /analyze (C6xxx / C26xxx) codes treated as high severity: null deref, uninitialised use, buffer overrun, dangling /
# use-after-move, unchecked return values of must-check calls, mismatched allocation.
HIGH_CODES = {"C6001", "C6011", "C6031", "C6053", "C6054", "C6066", "C6067", "C6200", "C6201", "C6217", "C6269", "C6282", "C6284",
              "C6285", "C6308", "C6385", "C6386", "C6387", "C6388", "C6400", "C6401", "C6993", "C26110", "C26111", "C26115",
              "C26116", "C26117", "C26800", "C26815", "C26816", "C26819", "C28182", "C28183"}
WARN_RE = re.compile(r"^(?P<file>[A-Za-z]:[\\/][^()]+?)\((?P<line>\d+)(?:,\d+)?\)\s*:\s*warning\s+(?P<code>[A-Z]+\d+)\s*:\s*(?P<msg>.*)$")
SYSTEM_SRC = re.compile(r"^Source/(CodexTactics[A-Za-z]*)/(?:Private|Public)/([^/]+)/")


def severity(code):
    if code in HIGH_CODES:
        return "high"
    return "medium" if code.startswith(("C6", "C26", "C28")) else "low"


def norm_path(path):
    path = path.replace("\\", "/")
    i = path.find("/Source/CodexTactics")
    return path[i + 1:] if i >= 0 else path


def parse_analyzer_log(text):
    """Project-code warnings only (Source/CodexTactics*), normalized to file / line / code / message; duplicates collapsed."""
    seen, out = set(), []
    for raw in text.splitlines():
        m = WARN_RE.match(raw.strip())
        if not m:
            continue
        f = norm_path(m["file"])
        if not f.startswith("Source/CodexTactics"):
            continue
        msg = re.sub(r"\s*\[[A-Za-z]:[\\/].*?\]\s*$", "", m["msg"]).strip()
        key = (f, int(m["line"]), m["code"], msg)
        if key in seen:
            continue
        seen.add(key)
        out.append({"file": f, "line": int(m["line"]), "code": m["code"], "message": msg, "severity": severity(m["code"])})
    return sorted(out, key=lambda w: (w["file"], w["line"], w["code"]))


def read_log(path):
    """build.ps1 redirects with `*>` (Windows PowerShell 5.1 writes UTF-16 LE with BOM); accept UTF-8 too."""
    raw = open(path, "rb").read()
    if raw.startswith(codecs.BOM_UTF16_LE) or raw.startswith(codecs.BOM_UTF16_BE):
        return raw.decode("utf-16", errors="replace")
    return raw.decode("utf-8-sig", errors="replace")


def warn_key(w):
    """Line numbers drift, so the baseline identity is file + code + message."""
    return "%s|%s|%s" % (w["file"], w["code"], w["message"])


def diff_baseline(current, baseline):
    """baseline: list of warnings or None (first run). Returns (new, fixed) as multiset differences."""
    if baseline is None:
        return [], []
    left = {}
    for w in baseline:
        left.setdefault(warn_key(w), []).append(w)
    new = []
    for w in current:
        bucket = left.get(warn_key(w))
        if bucket:
            bucket.pop()
        else:
            new.append(w)
    fixed = [w for ws in left.values() for w in ws]
    return new, fixed


def top_by_severity(warnings, n=20):
    order = {"high": 0, "medium": 1, "low": 2}
    return sorted(warnings, key=lambda w: (order[w["severity"]], w["file"], w["line"]))[:n]


def git_churn(root, since="7 days ago", ref="main"):
    """Per-file touch counts (a file changed in 3 commits = 3) from `git log --name-only` on main."""
    out = subprocess.run(["git", "-C", root, "log", ref, "--since=" + since, "--name-only", "--pretty=format:@@%H"],
                         capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    return parse_churn(out)


def parse_churn(text):
    counts, commits = {}, 0
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("@@"):
            commits += 1
        elif line:
            counts[line] = counts.get(line, 0) + 1
    return counts, commits


def system_of(path):
    """'Combat' for Source/CodexTactics/Private/Combat/X.cpp; the tests module maps to the same system name."""
    m = SYSTEM_SRC.match(path)
    return m.group(2) if m else None


def glob_regex(glob):
    g = re.escape(glob.replace("\\", "/"))
    g = g.replace(r"\*\*/", "(?:.*/)?").replace(r"\*\*", ".*").replace(r"\*", "[^/]*")
    return re.compile("^" + g + "$")


def load_rules(test_map_path):
    with open(test_map_path, encoding="utf-8") as f:
        data = json.load(f)
    rules = []
    for r in data.get("rules", []):
        rules.append({"name": r.get("name", ""), "res": [glob_regex(g) for g in r.get("globs", [])],
                      "tests": r.get("tests", []), "smokes": r.get("smokes", [])})
    return rules


def rules_for(path, rules):
    return [r for r in rules if any(rx.match(path) for rx in r["res"])]


def list_sources(root):
    files = []
    base = os.path.join(root, "Source")
    for d, _, names in os.walk(base):
        for n in names:
            if n.endswith((".cpp", ".h")):
                files.append(os.path.relpath(os.path.join(d, n), root).replace("\\", "/"))
    return files


def count_lines(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return sum(1 for _ in f)
    except OSError:
        return 0


TEST_MACRO = re.compile(r"IMPLEMENT_(?:SIMPLE|COMPLEX)_AUTOMATION_TEST|\bDEFINE_SPEC\b|BEGIN_DEFINE_SPEC")


def build_coverage(root, churn, rules, grey_threshold=2.0):
    """Per system: loc, unit tests (automation test macros in CodexTacticsTests/Private/<System>), mapped smokes, churn."""
    files = list_sources(root)
    systems = {}

    def sysrec(name):
        return systems.setdefault(name, {"system": name, "loc": 0, "files": 0, "tests": 0, "smokes": [], "churn": 0, "filters": set()})

    test_text = []
    for f in files:
        if f.startswith("Source/CodexTacticsTests/"):
            try:
                text = open(os.path.join(root, f), encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            test_text.append(text)
            s = system_of(f)
            if s:
                sysrec(s)["tests"] += len(TEST_MACRO.findall(text))
        elif f.startswith("Source/CodexTactics/"):
            s = system_of(f)
            if s:
                r = sysrec(s)
                r["loc"] += count_lines(os.path.join(root, f))
                r["files"] += 1
    for path, n in churn.items():
        s = system_of(path)
        if s and path.startswith("Source/CodexTactics/"):
            sysrec(s)["churn"] += n
    sm = {}
    for f in files:
        s = system_of(f)
        if s and f.startswith("Source/CodexTactics/"):
            for r in rules_for(f, rules):
                sm.setdefault(s, set()).update(r["smokes"])
                sysrec(s)["filters"].update(r["tests"])
    out = []
    for r in systems.values():
        r["smokes"] = sorted(sm.get(r["system"], []))
        r["filters"] = len(r["filters"])
        r["grey_score"] = round((r["loc"] / 1000.0 + r["churn"] / 5.0) / (1 + r["tests"] / 5.0 + len(r["smokes"]) / 10.0), 2)
        r["grey"] = r["grey_score"] >= grey_threshold
        out.append(r)
    out.sort(key=lambda r: -r["grey_score"])
    return out, "\n".join(test_text)


def file_has_tests(path, rules, test_blob):
    stem = os.path.splitext(os.path.basename(path))[0]
    if len(stem) >= 5 and re.search(r"\b" + re.escape(stem) + r"\b", test_blob):
        return True
    return False  # test_map rules are too broad to prove a unit test covers this very file


def risk_files(root, churn, rules, test_blob, top=40):
    """Changed source files (not tests), most-changed first, with LOC and the unit-test flag."""
    rows = []
    for p, n in churn.items():
        if p.startswith("Source/CodexTactics/") and p.endswith((".cpp", ".h")):
            rows.append({"file": p, "changes": n, "loc": count_lines(os.path.join(root, p)), "tested": file_has_tests(p, rules, test_blob)})
    rows.sort(key=lambda r: (-r["changes"], -r["loc"], r["file"]))
    return rows[:top]


def jev_risk(rows, ask):
    """One batched Jev call scoring regression risk 1..5 per file; ask = jev_client.ask-like callable (None -> skipped)."""
    if not rows or ask is None:
        return None, "no Jev"
    questions = {}
    for i, r in enumerate(rows):
        questions["f%d" % i] = {
            "type": "score",
            "instructions": "Regression risk (1 low .. 5 high) of this week's changes to %s (%d lines, changed in %d commits, unit-tested: %s)." %
                            (r["file"], r["loc"], r["changes"], "yes" if r["tested"] else "no"),
            "criteria": ["Trivial / cosmetic", "Small isolated change", "Moderate logic change", "Core gameplay logic, many callers",
                         "Central, high-fan-out system"]}
    answers, err = ask({"week_files": [{"file": r["file"], "loc": r["loc"], "changes": r["changes"], "tested": r["tested"]} for r in rows]}, questions)
    if answers is None:
        return None, err
    for i, r in enumerate(rows):
        r["risk"] = answers.get("f%d" % i, {}).get("score")
    return rows, None


def first_sunday(date):
    return date.weekday() == 6 and date.day <= 7


def choose_actions(new_warnings, first_run, baseline_high, risky, coverage):
    """Deterministic: (1) new high-severity warnings (first run: the high baseline), (2) riskiest high-churn untested file,
    (3) worst grey zone. Max 3, in that order."""
    actions = []
    highs = [w for w in new_warnings if w["severity"] == "high"]
    if highs:
        files = sorted({w["file"] for w in highs})
        actions.append({"kind": "analyzer",
                        "ru": "Разобрать %d новых серьёзных предупреждений анализатора (%s)." % (len(highs), ", ".join(os.path.basename(f) for f in files[:3])),
                        "en": "Fix %d new high-severity analyzer warnings (%s)." % (len(highs), ", ".join(files[:3]))})
    elif first_run and baseline_high:
        actions.append({"kind": "analyzer",
                        "ru": "Разобрать %d серьёзных предупреждений из базовой линии анализатора (топ в отчёте)." % len(baseline_high),
                        "en": "Triage the %d high-severity baseline analyzer warnings (top list below)." % len(baseline_high)})
    cand = [r for r in risky if not r["tested"] and r["changes"] >= 3]
    cand.sort(key=lambda r: (-(r.get("risk") or 0), -r["changes"], -r["loc"], r["file"]))
    if cand:
        r = cand[0]
        jr_ru = ", риск Jev %s" % r["risk"] if r.get("risk") else ""
        jr_en = ", Jev risk %s" % r["risk"] if r.get("risk") else ""
        actions.append({"kind": "risk",
                        "ru": "Покрыть тестом %s (изменён %d раз за неделю, %d строк%s)." % (os.path.basename(r["file"]), r["changes"], r["loc"], jr_ru),
                        "en": "Add a unit test for %s (%d changes this week, %d lines%s)." % (r["file"], r["changes"], r["loc"], jr_en)})
    greys = [c for c in coverage if c["grey"]]
    if greys:
        g = greys[0]
        actions.append({"kind": "grey",
                        "ru": "Серая зона: система %s (%d строк, %d правок, тестов %d, смоков %d) - добавить тесты." % (g["system"], g["loc"], g["churn"], g["tests"], len(g["smokes"])),
                        "en": "Grey zone: %s (%d LOC, %d changes, %d tests, %d smokes) - add tests." % (g["system"], g["loc"], g["churn"], g["tests"], len(g["smokes"]))})
    return actions[:3]


def build_report(d):
    an = d["analyzer"]
    greys = [c["system"] for c in d["coverage"] if c["grey"]]
    if an["ran"]:
        a_ru = "Анализатор: время %s, предупреждений проекта %d (серьёзных %d); %s." % (
            an["time"] or "?", an["total"], an["high"],
            ("новых %d, исправлено %d" % (an["new"], an["fixed"])) if an["baseline_existed"] else "базовая линия создана")
    else:
        a_ru = "Анализатор не запускался (%s)." % an.get("note", "")
    ru = ["Коммитов за 7 дней: %d, изменённых файлов исходников: %d." % (d["commits"], d["changed_source_files"]), a_ru,
          "Оценка риска Jev: %s." % ("есть" if d["jev"]["used"] else "недоступна (только churn)"),
          "Серых зон: %d%s." % (len(greys), (" (" + ", ".join(greys) + ")") if greys else "")]
    if d["architecture_review_due"]:
        ru.append("Первое воскресенье месяца: пора на архитектурное ревью.")
    L = ["# Weekly audit %s" % d["date"], "", "## Сводка (RU)"]
    L += ["- " + x for x in ru]
    L += ["", "**Действия на неделю:**"]
    for i, a in enumerate(d["actions"], 1):
        L.append("%d. %s" % (i, a["ru"]))
    if not d["actions"]:
        L.append("1. Нет - всё чисто.")
    L += ["", "## Actions (EN)"]
    for i, a in enumerate(d["actions"], 1):
        L.append("%d. %s" % (i, a["en"]))
    if d["architecture_review_due"]:
        L.append("- Architecture review due (first Sunday of the month).")
    L += ["", "## Static analysis"]
    L.append("Analyzer: %s; run time %s; project warnings %d (high %d, medium %d, low %d)." %
             (an["flags"], an["time"] or "n/a", an["total"], an["high"], an["medium"], an["low"]))
    if an["baseline_existed"]:
        L.append("New %d, fixed %d vs baseline." % (an["new"], an["fixed"]))
        for w in an["new_list"][:15]:
            L.append("- NEW [%s %s] %s:%d %s" % (w["severity"], w["code"], w["file"], w["line"], w["message"][:110]))
    elif an["ran"]:
        L.append("First run: baseline created (`docs/port/audit/static_baseline.json`). Top %d by severity:" % len(an["top"]))
        for w in an["top"]:
            L.append("- [%s %s] %s:%d %s" % (w["severity"], w["code"], w["file"], w["line"], w["message"][:110]))
    L += ["", "## Churn and risk (top %d changed files)" % len(d["risk_files"]), "| File | Changes | LOC | Unit-tested | Jev risk |", "|---|---|---|---|---|"]
    for r in d["risk_files"][:25]:
        L.append("| %s | %d | %d | %s | %s |" % (r["file"].replace("Source/CodexTactics/", ""), r["changes"], r["loc"], "yes" if r["tested"] else "no", r.get("risk") or "-"))
    L += ["", "## Coverage map", "| System | LOC | Tests | Smokes | Changes 7d | Grey score |", "|---|---|---|---|---|---|"]
    for c in d["coverage"]:
        L.append("| %s%s | %d | %d | %d | %d | %s |" % (c["system"], " (GREY)" if c["grey"] else "", c["loc"], c["tests"], len(c["smokes"]), c["churn"], c["grey_score"]))
    return "\n".join(L) + "\n"


def now_str():
    return datetime.date.today().isoformat()
