"""Self-test: python Scripts/Tools/audit/test_audit.py  (fake git log + fake analyzer log -> expected report; no engine, no network)."""

import datetime
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import audit_lib as A  # noqa: E402
import run_audit  # noqa: E402
import build_health_dashboard  # noqa: E402

LOG = r"""
Some noise line
C:\Proj\Source\CodexTactics\Private\Combat\Shooter.cpp(10): warning C6011: Dereferencing NULL pointer 'p'. [C:\Proj\Intermediate\X.vcxproj]
C:\Proj\Source\CodexTactics\Private\Combat\Shooter.cpp(10): warning C6011: Dereferencing NULL pointer 'p'. [C:\Proj\Intermediate\X.vcxproj]
C:\Proj\Source\CodexTactics\Private\UI\Hud.cpp(5,3): warning C26495: Variable 'X' is uninitialized.
C:\Proj\Source\CodexTactics\Private\UI\Hud.cpp(9): warning C4996: deprecated
C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Core\Foo.h(1): warning C6011: engine noise
"""
CHURN = "@@a1\nSource/CodexTactics/Private/Combat/Shooter.cpp\nSource/CodexTactics/Public/Combat/Shooter.h\n@@a2\nSource/CodexTactics/Private/Combat/Shooter.cpp\n@@a3\nSource/CodexTactics/Private/Combat/Shooter.cpp\nContent/X.uasset\n"


def make_root():
    root = tempfile.mkdtemp()
    for rel, body in {"Source/CodexTactics/Private/Combat/Shooter.cpp": "a\n" * 4000, "Source/CodexTactics/Public/Combat/Shooter.h": "a\n" * 10,
                      "Source/CodexTactics/Private/UI/Hud.cpp": "a\n" * 20,
                      "Source/CodexTacticsTests/Private/UI/HudTest.cpp": "IMPLEMENT_SIMPLE_AUTOMATION_TEST(A,\"x\",0)\nIMPLEMENT_SIMPLE_AUTOMATION_TEST(B,\"y\",0)\nHud"}.items():
        p = os.path.join(root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w").write(body)
    os.makedirs(os.path.join(root, "Scripts"))
    json.dump({"rules": [{"name": "ui", "globs": ["Source/**/UI/*"], "tests": ["CodexTactics.UI"], "smokes": ["HudSmoke"]}]}, open(os.path.join(root, "Scripts", "test_map.json"), "w"))
    return root


def main():
    w = A.parse_analyzer_log(LOG)
    assert len(w) == 3, w                       # engine noise out, duplicate collapsed
    assert [x["severity"] for x in w] == ["high", "medium", "low"] or {x["code"]: x["severity"] for x in w} == {"C6011": "high", "C26495": "medium", "C4996": "low"}
    assert A.diff_baseline(w, None) == ([], [])
    new, fixed = A.diff_baseline(w, w[:2] + [{"file": "Source/CodexTactics/Private/X.cpp", "line": 1, "code": "C6011", "message": "gone", "severity": "high"}])
    assert [x["code"] for x in new] == ["C4996"] and len(fixed) == 1
    assert A.first_sunday(datetime.date(2026, 11, 1)) and not A.first_sunday(datetime.date(2026, 11, 10))

    root = make_root()
    log = os.path.join(root, "a.log")
    open(log, "w").write(LOG)
    d = run_audit.run(root, "2026-11-01", log, "5m00s", "test", "7 days ago", False, churn_text=CHURN)
    assert d["commits"] == 3 and d["analyzer"]["total"] == 3 and d["analyzer"]["high"] == 1 and not d["analyzer"]["baseline_existed"]
    assert os.path.exists(os.path.join(root, "docs/port/audit/static_baseline.json"))
    kinds = [a["kind"] for a in d["actions"]]
    assert kinds[0] == "analyzer" and "risk" in kinds, kinds          # baseline high first, then Shooter.cpp (3 changes, untested)
    assert d["architecture_review_due"] is True
    md = open(os.path.join(root, "docs/port/audit/AUDIT_2026-11-01.md"), encoding="utf-8").read()
    assert "Действия на неделю" in md and "Shooter.cpp" in md and "architecture review" in md.lower()
    # second run: nothing new -> no analyzer action
    d2 = run_audit.run(root, "2026-11-08", log, "5m00s", "test", "7 days ago", False, churn_text=CHURN)
    assert d2["analyzer"]["new"] == 0 and d2["analyzer"]["baseline_existed"] and d2["actions"][0]["kind"] != "analyzer"
    page = build_health_dashboard.build(root)
    assert "Combat" in page and "http" not in page.replace("http-equiv", "")
    shutil.rmtree(root)
    print("audit self-test OK")


if __name__ == "__main__":
    main()
