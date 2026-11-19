# codex-worker as the designer's dev checkout (docs/port/MAIN_PC_FORMAT.md 5.5, switch day): copy from the mirror
# C:/Mirror/Main/CodexTactics into C:/Work/CodexTactics only what git does not track (outside Saved/, plus Saved/AgentChat and
# Saved/Backups), never a tracked file; an existing untracked file that differs is moved to Saved/Worker/replaced-<date> first;
# then the designer's uncommitted files (NINE - update the list from `git status` on the main PC that day) byte-exact, with sha1.
# Run ON the worker with its Python 3.12: python -I Scripts\Tools\worker_dev_restore.py [--dry]. Tested 2026-11-19.
import hashlib, json, os, shutil, subprocess, sys
MIRROR = r"C:\Mirror\Main\CodexTactics"
DEV = r"C:\Work\CodexTactics"
BACKUP = os.path.join(DEV, r"Saved\Worker\replaced-20261119")
NINE = ["Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x6.json",
        "Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x6.meta.json",
        "Content/Data/LevelGen/objectives/L_Gen_Depot_1_X6.objectives.json",
        "Content/Data/LevelGen/props/L_Gen_Depot_1_X6.props.json",
        "Content/Data/LevelGen/tactical/L_Gen_Depot_1_X6.tactical.json",
        "Content/Data/LevelJson/L_Gen_Depot_1_X6.json",
        "Content/Maps/Generated/L_Gen_Depot_1_X6.umap",
        "Scripts/Tools/local_task.py",
        "docs/port/project-health.html"]
SAVED_OK = ("Saved/AgentChat/", "Saved/Backups/")
SANITISED = {".claude.json", "settings.json", "settings.local.json", "config.toml", "config.json", ".mcp.json", "mcp.json", "mcp_config.json"}
dry = "--dry" in sys.argv

def sha1(p):
    h = hashlib.sha1()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()

tracked = set(subprocess.run(["git", "-C", DEV, "ls-files", "-z"], capture_output=True).stdout.decode("utf-8", "replace").split("\0"))
tracked.discard("")
stats = {"copied": 0, "same": 0, "replaced": 0, "skip_tracked": 0, "skip_saved": 0, "skip_sanitised_exists": 0, "bytes": 0}
for root, dirs, files in os.walk(MIRROR):
    rel_root = os.path.relpath(root, MIRROR).replace("\\", "/")
    rel_root = "" if rel_root == "." else rel_root + "/"
    for name in files:
        rel = rel_root + name
        if rel in NINE:
            continue
        if rel in tracked:
            stats["skip_tracked"] += 1
            continue
        if rel.startswith("Saved/") and not rel.startswith(SAVED_OK):
            stats["skip_saved"] += 1
            continue
        src, dst = os.path.join(root, name), os.path.join(DEV, rel.replace("/", os.sep))
        if os.path.exists(dst):
            if name.lower() in SANITISED:
                stats["skip_sanitised_exists"] += 1
                continue
            if os.path.getsize(dst) == os.path.getsize(src):
                stats["same"] += 1
                continue
            if not dry:
                bk = os.path.join(BACKUP, rel.replace("/", os.sep))
                os.makedirs(os.path.dirname(bk), exist_ok=True)
                shutil.move(dst, bk)
            stats["replaced"] += 1
        if not dry:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
        stats["copied"] += 1
        stats["bytes"] += os.path.getsize(src)
print("untracked copy:", json.dumps(stats))
# The 9 designer files: byte-exact from the mirror, sha1 printed (compare with the main PC's originals).
for rel in NINE:
    src, dst = os.path.join(MIRROR, rel.replace("/", os.sep)), os.path.join(DEV, rel.replace("/", os.sep))
    if not os.path.exists(src):
        print("NINE MISSING in mirror", rel)
        continue
    if not dry:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
    print("NINE", sha1(src), sha1(dst) if os.path.exists(dst) else "-", rel)
st = subprocess.run(["git", "-C", DEV, "status", "--porcelain", "--untracked-files=no"], capture_output=True, text=True).stdout.strip().splitlines()
print("git status M:", len(st))
for l in st:
    print("  ", l)
