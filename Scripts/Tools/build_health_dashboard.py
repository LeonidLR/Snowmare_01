"""Builds docs/port/project-health.html (self-contained, no CDN, phone-readable) from docs/port/audit/audit_*.json.
Usage: python build_health_dashboard.py --root <checkout>"""

import argparse
import glob
import html
import json
import os


def spark(values, w=160, h=36):
    if not values:
        return ""
    hi, lo = max(values), min(values)
    span = (hi - lo) or 1
    pts = " ".join("%.1f,%.1f" % (i * (w - 4) / max(1, len(values) - 1) + 2, h - 3 - (v - lo) * (h - 6) / span) for i, v in enumerate(values))
    return '<svg viewBox="0 0 %d %d" width="%d" height="%d" role="img"><polyline fill="none" stroke="currentColor" stroke-width="2" points="%s"/></svg>' % (w, h, w, h, pts)


def build(root):
    files = sorted(glob.glob(os.path.join(root, "docs", "port", "audit", "audit_*.json")))
    runs = [json.load(open(f, encoding="utf-8")) for f in files]
    e = html.escape
    out = ["<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>",
           "<title>Project Health</title><style>",
           ":root{--bg:#fff;--fg:#1a1a1a;--mut:#666;--line:#ddd;--bad:#b3261e;--ok:#1b7a3a}",
           "@media(prefers-color-scheme:dark){:root{--bg:#161616;--fg:#eee;--mut:#999;--line:#333;--bad:#ff8a80;--ok:#6fdc8c}}",
           "body{margin:0;padding:16px;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif;max-width:900px}",
           "h1{font-size:20px}h2{font-size:16px;margin-top:24px}table{border-collapse:collapse;width:100%}",
           "th,td{padding:4px 6px;border-bottom:1px solid var(--line);text-align:right}th:first-child,td:first-child{text-align:left}",
           ".wrap{overflow-x:auto}.bad{color:var(--bad);font-weight:600}.mut{color:var(--mut)}.card{display:flex;gap:16px;flex-wrap:wrap}",
           ".card div{border:1px solid var(--line);border-radius:6px;padding:8px 12px}</style></head><body><h1>CodexTactics project health</h1>"]
    if not runs:
        out.append("<p>No audit runs yet.</p></body></html>")
        return "\n".join(out)
    last = runs[-1]
    an = last["analyzer"]
    out.append("<p class='mut'>Latest audit: %s (%d runs). Generated from docs/port/audit/audit_*.json.</p>" % (e(last["date"]), len(runs)))
    out.append("<div class='card'><div><b>%d</b><br>analyzer warnings<br><span class='mut'>high %d</span></div><div><b>%d</b><br>commits (7d)</div><div><b>%d</b><br>grey zones</div></div>" %
               (an["total"], an["high"], last["commits"], sum(1 for c in last["coverage"] if c["grey"])))
    out.append("<h2>Actions of the week</h2><ol>" + "".join("<li>%s</li>" % e(a["ru"]) for a in last["actions"]) + "</ol>")
    out.append("<h2>Weekly trends</h2><div class='wrap'><table><tr><th>Date</th><th>Commits</th><th>Warnings</th><th>High</th><th>New</th><th>Grey</th></tr>")
    for r in reversed(runs):
        out.append("<tr><td>%s</td><td>%d</td><td>%d</td><td>%d</td><td>%s</td><td>%d</td></tr>" %
                   (e(r["date"]), r["commits"], r["analyzer"]["total"], r["analyzer"]["high"],
                    r["analyzer"]["new"] if r["analyzer"]["baseline_existed"] else "base", sum(1 for c in r["coverage"] if c["grey"])))
    out.append("</table></div>")
    out.append("<p>Warnings %s &nbsp; Commits %s</p>" % (spark([r["analyzer"]["total"] for r in runs]), spark([r["commits"] for r in runs])))
    out.append("<h2>Systems</h2><div class='wrap'><table><tr><th>System</th><th>LOC</th><th>Tests</th><th>Smokes</th><th>7d</th><th>Grey</th></tr>")
    for c in last["coverage"]:
        cls = " class='bad'" if c["grey"] else ""
        out.append("<tr><td%s>%s</td><td>%d</td><td>%d</td><td>%d</td><td>%d</td><td%s>%s</td></tr>" %
                   (cls, e(c["system"]), c["loc"], c["tests"], len(c["smokes"]), c["churn"], cls, c["grey_score"]))
    out.append("</table></div></body></html>")
    return "\n".join(out)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    a = ap.parse_args()
    path = os.path.join(a.root, "docs", "port", "project-health.html")
    with open(path, "w", encoding="utf-8") as f:
        f.write(build(a.root))
    print("wrote", path)
