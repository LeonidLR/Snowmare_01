"""Content snapshot / diff: proves a pipeline import touched nothing outside its own folders.

    python Scripts/Pipeline/content_snapshot.py snap <out.json> [--exclude /Game/A --exclude /Game/B]
    python Scripts/Pipeline/content_snapshot.py diff <before.json> <after.json>

Every .uasset / .umap under Content/ outside the excluded folders: path + size + mtime, plus the sha1 of the files git
tracks. `diff` lists added / removed / changed files (exit code 1 when anything changed).
"""
import hashlib
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONTENT = os.path.join(ROOT, "Content")


def to_rel(game_path):
    return "Content" + game_path[len("/Game"):] if game_path.startswith("/Game") else game_path


def snap(out, excludes):
    excl = [to_rel(e).replace("\\", "/").rstrip("/") + "/" for e in excludes]
    tracked = set(subprocess.run(["git", "ls-files", "Content"], cwd=ROOT, capture_output=True, text=True,
                                 check=True).stdout.splitlines())
    files = {}
    for d, _, names in os.walk(CONTENT):
        for n in names:
            if not n.endswith((".uasset", ".umap")):
                continue
            full = os.path.join(d, n)
            rel = os.path.relpath(full, ROOT).replace("\\", "/")
            if any(rel.startswith(e) for e in excl):
                continue
            st = os.stat(full)
            entry = {"size": st.st_size, "mtime": st.st_mtime_ns}
            if rel in tracked:
                h = hashlib.sha1()
                with open(full, "rb") as f:
                    for block in iter(lambda: f.read(1 << 20), b""):
                        h.update(block)
                entry["sha1"] = h.hexdigest()
            files[rel] = entry
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"excludes": excl, "files": files}, f)
    print("snapshot: %d files (%d hashed) -> %s" % (len(files), sum(1 for e in files.values() if "sha1" in e), out))


def diff(a_path, b_path):
    a = json.load(open(a_path, encoding="utf-8"))["files"]
    b = json.load(open(b_path, encoding="utf-8"))["files"]
    added = sorted(set(b) - set(a))
    removed = sorted(set(a) - set(b))
    changed = sorted(k for k in set(a) & set(b) if a[k] != b[k])
    for label, items in (("added", added), ("removed", removed), ("changed", changed)):
        for k in items:
            print("%s %s" % (label, k))
    print("diff: %d added, %d removed, %d changed (of %d files)" % (len(added), len(removed), len(changed), len(a)))
    return 1 if (added or removed or changed) else 0


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "snap":
        ex = [sys.argv[i + 1] for i, v in enumerate(sys.argv) if v == "--exclude" and i + 1 < len(sys.argv)]
        snap(sys.argv[2], ex)
    elif len(sys.argv) == 4 and sys.argv[1] == "diff":
        sys.exit(diff(sys.argv[2], sys.argv[3]))
    else:
        print(__doc__)
        sys.exit(2)
