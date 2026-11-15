#!/usr/bin/env python3
"""Editor availability indicator: "can I open the editor, and which worktree copy?".

Usage:
  python editor_status.py                  short human summary
  python editor_status.py --json           JSON
  python editor_status.py set <worktree> --note "text"   (worktree: main | gemc5 | interim)
      writes <main>/Saved/AgentChat/editor_recommend.json

Stdlib only, Windows. Process list comes from PowerShell Get-CimInstance (one call per run).
"""
import argparse
import ctypes
import glob
import json
import os
import subprocess
import sys
import time

MAIN = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
RECOMMEND_FILE = os.path.join(MAIN, "Saved", "AgentChat", "editor_recommend.json")
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def _run(args, cwd=None, timeout=30):
    try:
        r = subprocess.run(args, cwd=cwd, capture_output=True, text=True, timeout=timeout,
                           encoding="utf-8", errors="replace", creationflags=NO_WINDOW)
        return r.stdout.strip()
    except Exception:
        return ""


def _norm(p):
    return os.path.normcase(os.path.abspath(p)).replace("\\", "/")


def list_processes():
    ps = ("[Console]::OutputEncoding=[Text.Encoding]::UTF8; "
          "Get-CimInstance Win32_Process | Select ProcessId,Name,CommandLine | ConvertTo-Json -Compress")
    out = _run(["powershell", "-NoProfile", "-NonInteractive", "-Command", ps], timeout=60)
    try:
        data = json.loads(out) if out else []
    except json.JSONDecodeError:
        return []
    if isinstance(data, dict):
        data = [data]
    procs = []
    for p in data:
        procs.append({"pid": p.get("ProcessId"), "name": (p.get("Name") or ""),
                      "cmd": (p.get("CommandLine") or "")})
    return procs


def list_worktrees():
    out = _run(["git", "worktree", "list", "--porcelain"], cwd=MAIN)
    trees, cur = [], {}
    for line in out.splitlines() + [""]:
        if not line.strip():
            if cur.get("path"):
                trees.append(cur)
            cur = {}
        elif line.startswith("worktree "):
            cur["path"] = line[9:].strip()
        elif line.startswith("branch "):
            cur["branch"] = line[7:].strip().replace("refs/heads/", "")
        elif line.startswith("detached"):
            cur["branch"] = None
    result = []
    main_norm = _norm(MAIN)
    for t in trees:
        path = t["path"].replace("/", os.sep)
        if not os.path.exists(os.path.join(path, "CodexTactics.uproject")):
            continue
        if _norm(path) == main_norm:
            name = "main"
        else:
            base = os.path.basename(path.rstrip("\\/"))
            name = base.split("CodexTactics-", 1)[1] if "CodexTactics-" in base else base
        result.append({"name": name, "path": path, "branch": t.get("branch")})
    return result


def pid_alive(pid):
    try:
        h = ctypes.windll.kernel32.OpenProcess(0x1000, False, int(pid))  # QUERY_LIMITED_INFORMATION
        if not h:
            return False
        code = ctypes.c_ulong()
        ctypes.windll.kernel32.GetExitCodeProcess(h, ctypes.byref(code))
        ctypes.windll.kernel32.CloseHandle(h)
        return code.value == 259  # STILL_ACTIVE
    except Exception:
        return False


def read_lock(path):
    f = os.path.join(path, "Saved", "agent.lock")
    if not os.path.exists(f):
        return None
    try:
        with open(f, "r", encoding="utf-8-sig") as fh:
            lock = json.load(fh)
    except Exception:
        return None
    if lock.get("manual") or pid_alive(lock.get("pid", 0)):
        return {"agent": lock.get("agent"), "purpose": lock.get("purpose")}
    return None


def newest_mtime(root):
    newest = 0.0
    for dp, _dn, fns in os.walk(root):
        for fn in fns:
            try:
                m = os.path.getmtime(os.path.join(dp, fn))
            except OSError:
                continue
            if m > newest:
                newest = m
    return newest


def binaries_fresh(path):
    dlls = glob.glob(os.path.join(path, "Binaries", "Win64", "UnrealEditor-CodexTactics*.dll"))
    if not dlls:
        return False
    dll_m = max(os.path.getmtime(d) for d in dlls)
    return dll_m >= newest_mtime(os.path.join(path, "Source"))


def git_head(path):
    out = _run(["git", "log", "-1", "--format=%h%x09%s"], cwd=path)
    if "\t" in out:
        h, s = out.split("\t", 1)
        return h, s[:60]
    return "", ""


def collect():
    procs = list_processes()
    result = []
    for wt in list_worktrees():
        key = _norm(wt["path"])
        uproj = key + "/codextactics.uproject"
        editor_open, smokes, build = False, 0, False
        for p in procs:
            cmd = p["cmd"].replace("\\", "/").lower()
            if not cmd:
                continue
            name = p["name"].lower()
            if name.startswith("unrealeditor") and uproj in cmd:
                if "-game" in cmd or "-run=" in cmd:
                    smokes += 1
                else:
                    editor_open = True
            elif key in cmd and ("unrealbuildtool" in cmd or "build.bat" in cmd):
                build = True
        head, subj = git_head(wt["path"])
        result.append({
            "name": wt["name"], "path": wt["path"], "branch": wt["branch"] or "detached",
            "head": head, "subject": subj, "editor_open": editor_open,
            "smokes_running": smokes, "build_running": build,
            "lock": read_lock(wt["path"]), "binaries_fresh": binaries_fresh(wt["path"]),
        })
    return result


def read_recommend():
    try:
        with open(RECOMMEND_FILE, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except Exception:
        return None


def _idle(w):
    return not w["build_running"] and not w["lock"] and not w["smokes_running"]


def verdict(worktrees, rec):
    by = {w["name"]: w for w in worktrees}
    rname = (rec or {}).get("worktree")
    w = by.get(rname) if rname else None
    note = (rec or {}).get("note", "")

    if w:
        if w["build_running"] or w["lock"]:
            who = "build"
            if w["lock"]:
                who = "%s, %s" % (w["lock"]["agent"], w["lock"]["purpose"])
            return {"level": "RED", "recommended": rname,
                    "reason": "Подожди: идёт сборка в %s (%s)" % (rname, who)}
        if w["smokes_running"]:
            return {"level": "RED", "recommended": rname,
                    "reason": "Подожди: в %s идут смоук-тесты (%d)" % (rname, w["smokes_running"])}
        if w["binaries_fresh"]:
            r = "Можно открывать %s (%s, собрано)" % (rname, note or w["head"])
            if w["editor_open"]:
                r = "Редактор уже открыт в %s (%s) — already open" % (rname, note or w["head"])
            return {"level": "GREEN", "recommended": rname, "reason": r,
                    "already_open": w["editor_open"]}
        return {"level": "YELLOW", "recommended": rname,
                "reason": "Сборка устарела в %s — подожди, Claude соберёт" % rname}
    alt = next((x for x in worktrees if x["name"] != "interim" and _idle(x) and x["binaries_fresh"]), None)
    if alt:
        return {"level": "YELLOW", "recommended": None, "suggested": alt["name"],
                "reason": "Копия не выбрана; можно попробовать %s (%s, собрано)" % (alt["name"], alt["head"])}
    return {"level": "YELLOW", "recommended": None,
            "reason": "Копия не выбрана и нет свободной собранной — подожди"}


def build_status():
    wts = collect()
    rec = read_recommend()
    return {"ts": int(time.time()), "verdict": verdict(wts, rec), "recommend": rec, "worktrees": wts}


def human(st):
    v = st["verdict"]
    lines = ["[%s] %s" % (v["level"], v["reason"])]
    for w in st["worktrees"]:
        flags = []
        if w["editor_open"]:
            flags.append("editor")
        if w["smokes_running"]:
            flags.append("smokes=%d" % w["smokes_running"])
        if w["build_running"]:
            flags.append("build")
        if w["lock"]:
            flags.append("lock=%s/%s" % (w["lock"]["agent"], w["lock"]["purpose"]))
        flags.append("fresh" if w["binaries_fresh"] else "STALE")
        lines.append("  %-8s %s %s %s | %s" % (w["name"], w["branch"], w["head"], w["subject"], " ".join(flags)))
    return "\n".join(lines)


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    if len(sys.argv) > 1 and sys.argv[1] == "set":
        ap = argparse.ArgumentParser(prog="editor_status.py set")
        ap.add_argument("cmd")
        ap.add_argument("worktree")
        ap.add_argument("--note", default="")
        a = ap.parse_args()
        os.makedirs(os.path.dirname(RECOMMEND_FILE), exist_ok=True)
        data = {"worktree": a.worktree, "note": a.note, "set_by": "claude", "ts": int(time.time())}
        with open(RECOMMEND_FILE, "w", encoding="utf-8") as fh:
            json.dump(data, fh, ensure_ascii=False)
        print("recommended:", json.dumps(data, ensure_ascii=False))
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    st = build_status()
    print(json.dumps(st, ensure_ascii=False) if a.json else human(st))


if __name__ == "__main__":
    main()
