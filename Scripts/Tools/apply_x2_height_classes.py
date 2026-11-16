"""Apply the existing Depot height rework to ONLY the two x2 layouts.

Run with --dry-run before applying; --check checks idempotence without writes.
The shared rework() owns legacy targets/rules; preserve existing correct nominal
classes and each file's newline style.
"""
import argparse
import copy
from collections import Counter
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from apply_depot_height_classes import HEIGHTS, ROOT, rework

LAYOUTS = (
    Path(ROOT) / "Content/Data/LevelGen/TestFixtures/L_Gen_Test_X2.layout.json",
    Path(ROOT) / "Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x2.json",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true", help="report changes without writing")
    mode.add_argument("--check", action="store_true", help="exit 1 if either layout needs changes; no writes")
    args = parser.parse_args()
    classes = json.loads(Path(HEIGHTS).read_text(encoding="utf-8"))["classes"]
    pending = []
    for path in LAYOUTS:
        raw = path.read_bytes()
        layout = json.loads(raw)
        updated, _ = rework(copy.deepcopy(layout), classes)
        # Expansion already uses these nominal classes (generate.ts:93,102,680;
        # expand.ts:233). Depot's ID whitelist must not raise its 135 cm fences,
        # or turn its 140 cm parapets / 85 cm crates into different valid classes.
        keep_cover = {"crate": ("low_mantle", "high_mantle")}
        keep_wall = {"fence": ("high_fence_over", "wall_no_traverse"),
                     "cut_wall": ("high_mantle", "tall_ledge")}
        changes = Counter()
        for key, keep, field in (("covers", keep_cover, "size"), ("walls", keep_wall, "height")):
            for old, new in zip(layout[key], updated[key]):
                before = old[field][2] if field == "size" else old[field]
                if before in [classes[c]["nominal_cm"] for c in keep.get(old.get("kind"), ())]:
                    if field == "size":
                        new[field][2] = before
                    else:
                        new[field] = before
                after = new[field][2] if field == "size" else new[field]
                if before != after:
                    changes[(old.get("kind"), before, after)] += 1
        report = ["%s: %d x %s -> %s cm" % (kind, n, before, after)
                  for (kind, before, after), n in sorted(changes.items())]
        newline = "\r\n" if b"\r\n" in raw else "\n"
        out = (json.dumps(updated, indent=1) + "\n").replace("\n", newline).encode("utf-8")
        print(path.relative_to(ROOT))
        print("\n".join(report))
        print("reworked" if out == raw else "NOT reworked")
        if out != raw:
            pending.append((path, out))
    if args.check:
        return int(bool(pending))
    if not args.dry_run:
        for path, out in pending:
            path.write_bytes(out)
            print("written %s" % path.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
