"""Weekly audit driver. Usage: python run_audit.py --root <checkout of main> [--date YYYY-MM-DD] [--analyzer-log file]
[--analyzer-time 12m] [--analyzer-flags txt] [--since "7 days ago"] [--no-jev].
Writes docs/port/audit/AUDIT_<date>.md + audit_<date>.json under --root; creates static_baseline.json on the first analyzer run."""

import argparse
import datetime
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import audit_lib as A  # noqa: E402


def run(root, date, analyzer_log, analyzer_time, analyzer_flags, since, use_jev, churn_text=None, ask=None):
    out_dir = os.path.join(root, "docs", "port", "audit")
    os.makedirs(out_dir, exist_ok=True)
    base_path = os.path.join(out_dir, "static_baseline.json")
    baseline = json.load(open(base_path, encoding="utf-8"))["warnings"] if os.path.exists(base_path) else None

    ran = bool(analyzer_log and os.path.exists(analyzer_log))
    current = A.parse_analyzer_log(A.read_log(analyzer_log)) if ran else []
    new, fixed = A.diff_baseline(current, baseline) if ran else ([], [])
    if ran and baseline is None:
        json.dump({"created": date, "warnings": current}, open(base_path, "w", encoding="utf-8"), indent=0)

    if churn_text is not None:
        churn, commits = A.parse_churn(churn_text)
    else:
        churn, commits = A.git_churn(root, since)
    rules = A.load_rules(os.path.join(root, "Scripts", "test_map.json"))
    coverage, test_blob = A.build_coverage(root, churn, rules)
    risky = A.risk_files(root, churn, rules, test_blob)
    jev_used = False
    if use_jev:
        if ask is None:
            try:
                import jev_client
                ask = jev_client.ask
            except Exception:
                ask = None
        res, _ = A.jev_risk(risky, ask)
        jev_used = res is not None

    def sev(s):
        return sum(1 for w in current if w["severity"] == s)

    d = {"date": date, "commits": commits, "changed_source_files": sum(1 for p in churn if p.startswith("Source/")),
         "analyzer": {"ran": ran, "flags": analyzer_flags or "-", "time": analyzer_time, "note": "" if ran else "no log",
                      "total": len(current), "high": sev("high"), "medium": sev("medium"), "low": sev("low"),
                      "baseline_existed": baseline is not None, "new": len(new), "fixed": len(fixed), "new_list": new,
                      "top": A.top_by_severity(current, 20) if baseline is None else []},
         "jev": {"used": jev_used}, "risk_files": risky, "coverage": coverage,
         "architecture_review_due": A.first_sunday(datetime.date.fromisoformat(date))}
    d["actions"] = A.choose_actions(new, baseline is None and ran, [w for w in current if w["severity"] == "high"], risky, coverage)
    with open(os.path.join(out_dir, "audit_%s.json" % date), "w", encoding="utf-8") as f:
        json.dump(d, f, indent=1)
    with open(os.path.join(out_dir, "AUDIT_%s.md" % date), "w", encoding="utf-8") as f:
        f.write(A.build_report(d))
    return d


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--date", default=A.now_str())
    ap.add_argument("--analyzer-log")
    ap.add_argument("--analyzer-time", default="")
    ap.add_argument("--analyzer-flags", default="")
    ap.add_argument("--since", default="7 days ago")
    ap.add_argument("--no-jev", action="store_true")
    a = ap.parse_args()
    d = run(os.path.abspath(a.root), a.date, a.analyzer_log, a.analyzer_time, a.analyzer_flags, a.since, not a.no_jev)
    print("audit %s: warnings %d (high %d) new %d; commits %d; actions %d" %
          (d["date"], d["analyzer"]["total"], d["analyzer"]["high"], d["analyzer"]["new"], d["commits"], len(d["actions"])))
