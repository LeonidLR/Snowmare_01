"""Build the mechanics register page from its data file.

docs/port/mechanics_register.json + docs/port/mechanics-register.template.html -> docs/port/mechanics-register.html
The page is published as a claude.ai artifact (the designer reads it on the phone). Update the JSON with every merge
(new mechanics, status branch -> main, planned -> branch), run this script, republish the page.

Usage: python Scripts/Tools/build_mechanics_register.py [--check]
  --check  validate the data only (ids unique, statuses, planned items name a known group); no output written.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "docs/port/mechanics_register.json"
TEMPLATE = ROOT / "docs/port/mechanics-register.template.html"
OUT = ROOT / "docs/port/mechanics-register.html"
STATUSES = {"main", "branch", "planned"}


def validate(data: dict) -> list:
    errors = []
    ids = set()
    group_ids = {g.get("id") for g in data.get("groups", [])}
    items = [(g.get("id"), it) for g in data.get("groups", []) for it in g.get("items", [])]
    items += [(it.get("group"), it) for it in data.get("planned", [])]
    for group, it in items:
        iid = it.get("id")
        if not iid or not it.get("name"):
            errors.append(f"item without id/name in group {group}: {it}")
        elif iid in ids:
            errors.append(f"duplicate id: {iid}")
        ids.add(iid)
        if it.get("status", "planned") not in STATUSES:
            errors.append(f"{iid}: bad status {it.get('status')}")
    for it in data.get("planned", []):
        if it.get("group") not in group_ids:
            errors.append(f"planned {it.get('id')}: unknown group {it.get('group')}")
    return errors


def main() -> int:
    data = json.loads(DATA.read_text(encoding="utf-8"))
    errors = validate(data)
    for e in errors:
        print("ERROR", e)
    if errors:
        return 1
    if "--check" in sys.argv:
        print("ok")
        return 0
    payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    page = TEMPLATE.read_text(encoding="utf-8").replace("/*__DATA__*/", payload)
    OUT.write_text(page, encoding="utf-8")
    n = sum(len(g.get("items", [])) for g in data.get("groups", []))
    print(f"wrote {OUT.relative_to(ROOT)}: {n} items in {len(data.get('groups', []))} groups, {len(data.get('planned', []))} planned")
    return 0


if __name__ == "__main__":
    sys.exit(main())
