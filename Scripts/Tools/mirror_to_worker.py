#!/usr/bin/env python3
"""
One-way mirror of the main PC's irreplaceable data onto the second machine codex-worker (designer 2026-11-18: the main PC
is formatted next week; the work must not stop). Standard library only. UE-only tooling, no Godot reference.

What it copies is listed in Scripts/Tools/mirror_sources.json (absolute source folders + excludes). Where: the worker's
C:/Mirror/Main/<source name>/ - never the worker's own checkout C:/Work/CodexTactics. Git repositories also get a bundle
of every local commit that is on no remote (mirror_sources.json "git_bundles").

How (wired link only - never over the iPhone hotspot, see --max-mbit):
  1. walk each source (excludes pruned), compare with the local state file (path -> size, mtime) of the last good run;
  2. pack the new / changed files into tar parts of --part-mb, scp each part to the worker, unpack it there (Windows tar),
     delete the part; the state is saved after every part (a cut link resumes where it stopped);
  3. files gone here are moved on the worker into C:/Mirror/Trash/<date>/<source>/... (kept --trash-days, then removed);
  4. secrets are never sent: GLOBAL_EXCLUDES drops credential / token / key / password / cookie files, SANITISE_NAMES
     configs (.claude.json, settings*.json, config.toml ...) go as cleaned copies; re-enter keys / logins by hand
     (docs/port/MAIN_PC_FORMAT.md).

Usage:
  python Scripts/Tools/mirror_to_worker.py plan                 # what would go (counts / sizes per source), nothing sent
  python Scripts/Tools/mirror_to_worker.py sync [--only NAME]   # send the changes
  python Scripts/Tools/mirror_to_worker.py verify [--only NAME] # compare the worker's files with the state (size)
  python Scripts/Tools/mirror_to_worker.py restore NAME --to DIR  # pull a mirrored source from the worker into DIR
Exit: 0 ok, 1 some files failed / differ, 2 worker unreachable / config error.
"""

import argparse
import base64
import datetime as dt
import fnmatch
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_CONFIG = os.path.join(HERE, "mirror_sources.json")
STATE_DIR = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "CodexTactics", "mirror")
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)

# Never mirrored, whatever the config says (matched against the file name and the relative path, case-insensitive).
GLOBAL_EXCLUDES = [
    ".credentials.json", "credentials.json", "auth.json", "oauth_creds.json", "google_accounts.json", "*token*.json",
    "*.pem", "*.key", "*.pfx", "*.p12", "id_rsa*", "id_ed25519*", ".env", ".env.*", "*secret*", "*apikey*", "*api_key*",
    "*password*", "cookies", "cookies-journal", "login data*", "web data*", "vpn tokens*", "EdgeProfile", "*/EdgeProfile",
    ".git-credentials", ".netrc", "_netrc", ".npmrc", ".pypirc", "id_ecdsa*", "id_dsa*", "*.ppk", "*.kdbx", "*.kdb",
    "credentials", ".aws", "*/.aws", "local state",
    # locked / special files of running apps (never readable, never needed)
    "*.sock", "lockfile", "*.guard", "GrShaderCache", "ShaderCache", "GraphiteDawnCache",
]

# Config files that may carry keys inside (primaryApiKey, oauthAccount, env blocks, mcpServers.*.env, tokens): never sent
# as they are - a sanitised copy goes instead (sanitise_bytes), so "secrets are never sent" holds for them too.
SANITISE_NAMES = {".claude.json", "settings.json", "settings.local.json", "config.toml", "config.json", ".mcp.json", "mcp.json",
                  "mcp_config.json"}
SECRET_KEY_RE = re.compile(r"(key|token|secret|passw|oauth|auth|credential|cookie|session|bearer|^env$|^headers$)", re.I)


def _strip(obj):
    if isinstance(obj, dict):
        return {k: ("<removed by mirror_to_worker>" if SECRET_KEY_RE.search(str(k)) else _strip(v)) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_strip(v) for v in obj]
    return obj


def sanitise_bytes(name, data):
    """The sanitised content of a SANITISE_NAMES file: JSON with every key-like field (and env / headers blocks) replaced;
    TOML without key-like assignments and without [*env*] / [*headers*] tables. Unparsable JSON is not sent at all (None)."""
    text = data.decode("utf-8-sig", errors="replace")
    if name.lower().endswith(".toml"):
        out, skip = [], False
        for line in text.splitlines():
            st = line.strip()
            if st.startswith("["):
                hdr = st.strip("[]").lower()
                skip = any(w in hdr for w in ("env", "header", "auth", "token", "secret"))
                out.append(line if not skip else f"# [{st.strip('[]')}] removed by mirror_to_worker")
                continue
            if skip:
                continue
            key = st.split("=", 1)[0].strip().strip('"') if "=" in st and not st.startswith("#") else ""
            out.append(f"# {key} = <removed by mirror_to_worker>" if key and SECRET_KEY_RE.search(key) else line)
        return ("\n".join(out) + "\n").encode("utf-8")
    try:
        return (json.dumps(_strip(json.loads(text)), indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    except ValueError:
        return None


def is_sanitised(rel):
    return os.path.basename(rel).lower() in SANITISE_NAMES


def log(msg):
    print(f"[mirror {time.strftime('%H:%M:%S')}] {msg}", flush=True)


def load_config(path):
    with open(path, "r", encoding="utf-8-sig") as f:
        cfg = json.load(f)
    home = os.path.expanduser("~")
    for s in cfg.get("sources", []):
        s["src"] = os.path.normpath(os.path.expandvars(s["src"].replace("~", home, 1) if s["src"].startswith("~") else s["src"]))
    return cfg


def excluded(rel, name, patterns):
    low_rel, low_name = rel.lower(), name.lower()
    for p in patterns:
        lp = p.lower()
        if fnmatch.fnmatch(low_name, lp) or fnmatch.fnmatch(low_rel, lp) or fnmatch.fnmatch(low_rel, lp + "/*"):
            return True
    return False


def is_reparse(path):
    """A junction / symlink (Python 3.10 os.path.islink misses junctions): never followed - a junction into another tree
    would copy that tree twice (or, restored, overwrite it)."""
    try:
        st = os.stat(path, follow_symlinks=False)
    except OSError:
        return True
    return bool(getattr(st, "st_file_attributes", 0) & 0x400) or os.path.islink(path)


def walk(source):
    """(rel posix path -> (size, mtime int)) of the source's files, excludes pruned (dirs too). Single files are allowed."""
    src = source["src"]
    patterns = list(source.get("exclude", [])) + GLOBAL_EXCLUDES
    out = {}
    if os.path.isfile(src):
        st = os.stat(src)
        out[os.path.basename(src)] = (st.st_size, int(st.st_mtime))
        return out
    for root, dirs, files in os.walk(src):
        rel_root = os.path.relpath(root, src).replace("\\", "/")
        rel_root = "" if rel_root == "." else rel_root + "/"
        dirs[:] = [d for d in dirs if not excluded(rel_root + d, d, patterns) and not is_reparse(os.path.join(root, d))]
        for name in files:
            rel = rel_root + name
            if excluded(rel, name, patterns):
                continue
            try:
                st = os.stat(os.path.join(root, name))
            except OSError:
                continue
            out[rel] = (st.st_size, int(st.st_mtime))
    return out


def state_path(name):
    return os.path.join(STATE_DIR, f"{name}.state.json")


def load_state(name):
    try:
        with open(state_path(name), "r", encoding="utf-8") as f:
            return {k: tuple(v) for k, v in json.load(f).items()}
    except (OSError, ValueError):
        return {}


def save_state(name, state):
    os.makedirs(STATE_DIR, exist_ok=True)
    tmp = state_path(name) + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(state, f)
    os.replace(tmp, state_path(name))


class Worker:
    def __init__(self, cfg):
        self.host = cfg.get("worker", "codex-worker")
        self.root = cfg.get("remote_root", "C:/Mirror/Main").rstrip("/")
        self.trash = cfg.get("remote_trash", "C:/Mirror/Trash").rstrip("/")

    def ps(self, script, timeout=600, check=True):
        b64 = base64.b64encode(("$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; " + script)
                               .encode("utf-16-le")).decode()
        r = subprocess.run(["ssh", "-o", "ConnectTimeout=20", "-o", "BatchMode=yes", "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=4", self.host,
                            f"powershell -NoProfile -NonInteractive -EncodedCommand {b64}"],
                           capture_output=True, text=True, timeout=timeout, creationflags=NO_WINDOW)
        if check and r.returncode != 0:
            raise RuntimeError(f"worker command failed (exit {r.returncode}): {(r.stderr or r.stdout).strip()[-400:]}")
        return r.stdout

    def scp_to(self, local, remote, timeout=7200):
        r = subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=4", local, f"{self.host}:{remote}"],
                           capture_output=True, text=True, timeout=timeout, creationflags=NO_WINDOW)
        if r.returncode != 0:
            raise RuntimeError(f"scp failed: {r.stderr.strip()[-300:]}")

    def scp_from(self, remote, local, timeout=7200):
        r = subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=4", f"{self.host}:{remote}", local],
                           capture_output=True, text=True, timeout=timeout, creationflags=NO_WINDOW)
        if r.returncode != 0:
            raise RuntimeError(f"scp failed: {r.stderr.strip()[-300:]}")


def link_check(worker, max_mbit):
    """Refuse a slow (hotspot / DERP relay) link: a 16 MB probe must reach at least max_mbit Mbit/s."""
    if max_mbit <= 0:
        return True
    probe = os.path.join(tempfile.gettempdir(), "mirror_probe.bin")
    with open(probe, "wb") as f:
        f.write(os.urandom(16 * 1024 * 1024))
    worker.ps(f"New-Item -ItemType Directory -Force '{worker.root}' | Out-Null")
    t0 = time.time()
    worker.scp_to(probe, f"{worker.root}/.probe.bin", timeout=120)
    mbit = 16 * 8 / max(time.time() - t0, 0.001)
    worker.ps(f"Remove-Item '{worker.root}/.probe.bin' -ErrorAction SilentlyContinue", check=False)
    os.remove(probe)
    log(f"link: {mbit:.0f} Mbit/s (minimum {max_mbit})")
    return mbit >= max_mbit


def plan_source(source, state):
    files = walk(source)
    changed = [r for r, v in files.items() if state.get(r) != v]
    gone = [r for r in state if r not in files]
    return files, changed, gone


def send_parts(worker, source, files, changed, state, part_bytes):
    """tar parts of the changed files -> scp -> unpack on the worker; the state after every part. Returns failures."""
    name, src = source["name"], source["src"]
    dest = f"{worker.root}/{name}"
    worker.ps(f"New-Item -ItemType Directory -Force '{dest}' | Out-Null")
    base = os.path.dirname(src) if os.path.isfile(src) else src
    failed, i, part_no = 0, 0, 0
    tmpdir = tempfile.mkdtemp(prefix="mirror-")
    while i < len(changed):
        part_no += 1
        tar_path = os.path.join(tmpdir, f"{name}-{part_no:04d}.tar")
        batch, size = [], 0
        with tarfile.open(tar_path, "w", format=tarfile.PAX_FORMAT) as tar:
            while i < len(changed) and (size < part_bytes or not batch):
                rel = changed[i]
                i += 1
                try:
                    full = os.path.join(base, rel.replace("/", os.sep))
                    if is_sanitised(rel):
                        with open(full, "rb") as f:
                            clean = sanitise_bytes(os.path.basename(rel), f.read())
                        if clean is not None:
                            info = tarfile.TarInfo(rel)
                            info.size, info.mtime = len(clean), files[rel][1]
                            tar.addfile(info, io.BytesIO(clean))
                    else:
                        tar.add(full, arcname=rel, recursive=False)
                    batch.append(rel)
                    size += files[rel][0]
                except OSError as ex:
                    failed += 1
                    log(f"  skip {rel}: {ex}")
        if batch:
            remote_tar = f"{worker.root}/.part-{name}-{part_no:04d}.tar"
            worker.scp_to(tar_path, remote_tar)
            out = worker.ps(f"Set-Location '{dest}'; $o = cmd /c \"tar -xf `\"{remote_tar}`\" 2>&1\"; $c = $LASTEXITCODE; "
                            f"Remove-Item '{remote_tar}' -ErrorAction SilentlyContinue; 'TAR_EXIT ' + $c; $o | Select-Object -First 8")
            if "TAR_EXIT 0" not in out:
                raise RuntimeError("tar on the worker: " + " | ".join(l.strip() for l in out.splitlines() if l.strip())[:600])
            for rel in batch:
                state[rel] = files[rel]
            save_state(name, state)
            log(f"  {name}: part {part_no}: {len(batch)} files, {size / 1e6:.0f} MB ({i}/{len(changed)})")
        os.remove(tar_path)
    os.rmdir(tmpdir)
    return failed


def trash_gone(worker, source, gone, state, trash_days):
    name = source["name"]
    if gone:
        day = dt.date.today().isoformat()
        listing = os.path.join(tempfile.gettempdir(), f"mirror-gone-{name}.txt")
        with open(listing, "w", encoding="utf-8") as f:
            f.write("\n".join(gone))
        remote_list = f"{worker.root}/.gone-{name}.txt"
        worker.scp_to(listing, remote_list, timeout=600)
        worker.ps(f"""
$src = '{worker.root}/{name}'; $dst = '{worker.trash}/{day}/{name}'
foreach ($rel in Get-Content -Encoding UTF8 '{remote_list}') {{
    if (-not $rel) {{ continue }}
    $from = Join-Path $src $rel; if (-not (Test-Path -LiteralPath $from)) {{ continue }}
    $to = Join-Path $dst $rel; New-Item -ItemType Directory -Force (Split-Path $to -Parent) | Out-Null
    Move-Item -LiteralPath $from -Destination $to -Force
}}
Remove-Item '{remote_list}'""", timeout=1800)
        os.remove(listing)
        for rel in gone:
            state.pop(rel, None)
        save_state(name, state)
        log(f"  {name}: {len(gone)} files gone here -> worker trash {day}")
    # Old trash days.
    worker.ps(f"Get-ChildItem '{worker.trash}' -Directory -ErrorAction SilentlyContinue | "
              f"Where-Object {{ $_.LastWriteTime -lt (Get-Date).AddDays(-{trash_days}) }} | Remove-Item -Recurse -Force",
              check=False)


def git_bundles(worker, cfg):
    """A bundle of every local commit on no remote, per repository (cheap: old agent branches are a few MB)."""
    for repo in cfg.get("git_bundles", []):
        path = os.path.normpath(os.path.expandvars(os.path.expanduser(repo["path"])))
        out = os.path.join(tempfile.gettempdir(), f"{repo['name']}-local.bundle")
        r = subprocess.run(["git", "-C", path, "bundle", "create", out, "--branches", "--not", "--remotes"],
                           capture_output=True, text=True, creationflags=NO_WINDOW)
        if r.returncode != 0 or not os.path.exists(out):
            log(f"  bundle {repo['name']}: nothing local-only ({(r.stderr or '').strip()[-120:]})")
            continue
        refs = subprocess.run(["git", "-C", path, "for-each-ref", "--format=%(refname:short) %(objectname:short)", "refs/heads"],
                              capture_output=True, text=True, creationflags=NO_WINDOW).stdout
        refs_file = out + ".refs.txt"
        with open(refs_file, "w", encoding="utf-8") as f:
            f.write(refs)
        worker.ps(f"New-Item -ItemType Directory -Force '{worker.root}/_git' | Out-Null")
        worker.scp_to(out, f"{worker.root}/_git/{repo['name']}-local.bundle")
        worker.scp_to(refs_file, f"{worker.root}/_git/{repo['name']}-branches.txt")
        log(f"  bundle {repo['name']}: {os.path.getsize(out) / 1e6:.1f} MB (restore: git fetch <bundle> 'refs/heads/*:refs/heads/*')")
        os.remove(out)
        os.remove(refs_file)


def machine_info():
    """STATE_DIR/machine-info: what a reinstall needs besides files - installed apps (winget export), the NAMES of the user
    environment variables (never their values), git global config, CodexTactics scheduled tasks. Mirrored as a source."""
    d = os.path.join(STATE_DIR, "machine-info")
    os.makedirs(d, exist_ok=True)
    def run(args, out_name, timeout=300):
        try:
            r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout,
                               creationflags=NO_WINDOW)
            with open(os.path.join(d, out_name), "w", encoding="utf-8") as f:
                f.write(r.stdout or "")
        except (OSError, subprocess.TimeoutExpired) as ex:
            log(f"  machine-info {out_name}: {ex}")
    # winget (an app execution alias) needs a host process: run it through PowerShell. export = what winget can reinstall,
    # list = every installed app (also the ones winget cannot install - reinstall those by hand).
    # winget is a packaged app: its writes under AppData\Local are virtualised (0x80070003), %TEMP% is not - export there, move.
    wg_tmp = os.path.join(tempfile.gettempdir(), "mirror-winget-apps.json")
    run(["powershell", "-NoProfile", "-Command", f"winget export -o '{wg_tmp}' --accept-source-agreements | Out-Null; 'ok'"],
        ".winget-export.txt", 600)
    if os.path.exists(wg_tmp):
        os.replace(wg_tmp, os.path.join(d, "winget-apps.json"))
    run(["powershell", "-NoProfile", "-Command",
         "[Console]::OutputEncoding = [Text.Encoding]::UTF8; winget list --accept-source-agreements"], "installed-apps.txt", 600)
    run(["powershell", "-NoProfile", "-Command",
         "[Environment]::GetEnvironmentVariables('User').Keys | Sort-Object"], "user-env-var-NAMES.txt")
    run(["git", "config", "--global", "--list", "--show-origin"], "git-config-global.txt")
    run(["powershell", "-NoProfile", "-Command",
         "Get-ScheduledTask | Where-Object { $_.TaskName -like '*Codex*' -or $_.TaskName -like '*Claude*' } | "
         "ForEach-Object { $_.TaskName + ' | ' + (($_.Actions | ForEach-Object { $_.Execute + ' ' + $_.Arguments }) -join '; ') }"],
        "scheduled-tasks.txt")
    run(["powershell", "-NoProfile", "-Command",
         "Get-ChildItem 'C:/Program Files/Epic Games' -Directory -ErrorAction SilentlyContinue | ForEach-Object Name; "
         "Get-ChildItem 'C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Marketplace' -Directory -ErrorAction SilentlyContinue | "
         "ForEach-Object { 'marketplace plugin: ' + $_.Name }"], "engines-and-plugins.txt")


def selected(cfg, only):
    sources = cfg.get("sources", [])
    if only:
        sources = [s for s in sources if s["name"] in only]
    return sources


def cmd_plan(cfg, args):
    total_n = total_b = 0
    for s in selected(cfg, args.only):
        if not os.path.exists(s["src"]):
            log(f"{s['name']}: MISSING {s['src']}")
            continue
        files, changed, gone = plan_source(s, load_state(s["name"]))
        b = sum(files[r][0] for r in changed)
        total_n += len(changed)
        total_b += b
        log(f"{s['name']:<24} {len(files):>8} files {sum(v[0] for v in files.values()) / 1e9:8.2f} GB | to send {len(changed):>7} "
            f"({b / 1e9:.2f} GB), gone {len(gone)}")
    log(f"TOTAL to send: {total_n} files, {total_b / 1e9:.2f} GB")
    return 0


def pid_alive(pid):
    if pid <= 0:
        return False
    r = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True, creationflags=NO_WINDOW)
    return str(pid) in r.stdout


def take_lock():
    """One sync at a time (the scheduled task + a hand run would race on the state files). Returns the lock path or None."""
    os.makedirs(STATE_DIR, exist_ok=True)
    lock = os.path.join(STATE_DIR, "sync.lock")
    for _ in range(2):
        try:
            fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            return lock
        except FileExistsError:
            try:
                with open(lock, "r", encoding="utf-8") as f:
                    holder = int(f.read().strip() or 0)
            except (OSError, ValueError):
                holder = 0
            if pid_alive(holder):
                log(f"another sync runs (pid {holder}) - nothing to do")
                return None
            os.remove(lock)  # dead holder
    return None


def cmd_sync(cfg, args):
    lock = take_lock()
    if not lock:
        return 0
    try:
        return _sync(cfg, args)
    finally:
        try:
            os.remove(lock)
        except OSError:
            pass


def _sync(cfg, args):
    worker = Worker(cfg)
    try:
        if not link_check(worker, args.max_mbit if args.max_mbit is not None else cfg.get("min_link_mbit", 50)):
            log("link too slow (hotspot / relay?) - nothing sent; --max-mbit 0 forces it")
            return 2
    except (RuntimeError, subprocess.TimeoutExpired) as ex:
        log(f"worker unreachable: {ex}")
        return 2
    failed = 0
    part_bytes = int(args.part_mb * 1024 * 1024)
    if not args.only or "machine-info" in args.only:
        machine_info()
    for s in selected(cfg, args.only):
        if not os.path.exists(s["src"]):
            log(f"{s['name']}: MISSING {s['src']} - skipped")
            continue
        state = load_state(s["name"])
        files, changed, gone = plan_source(s, state)
        if state and len(gone) > 0.5 * len(state):
            log(f"{s['name']}: ABORT - {len(gone)} of {len(state)} mirrored files look gone (source unreadable / emptied?); "
                f"nothing sent or trashed for it - check {s['src']}")
            failed += 1
            continue
        log(f"{s['name']}: {len(changed)} to send ({sum(files[r][0] for r in changed) / 1e9:.2f} GB), {len(gone)} gone")
        try:
            if changed:
                failed += send_parts(worker, s, files, changed, state, part_bytes)
            trash_gone(worker, s, gone, state, cfg.get("trash_days", 14))
        except (RuntimeError, subprocess.TimeoutExpired) as ex:
            log(f"{s['name']}: FAILED {ex} - the next run resumes")
            failed += 1
    if not args.only:
        try:
            git_bundles(worker, cfg)
        except (RuntimeError, subprocess.TimeoutExpired) as ex:
            log(f"git bundles FAILED {ex}")
            failed += 1
    stamp = {"finished": dt.datetime.now().isoformat(timespec="seconds"), "failed": failed, "host": os.environ.get("COMPUTERNAME")}
    try:
        worker.ps(f"Set-Content -Encoding UTF8 '{worker.root}/LAST_SYNC.json' '{json.dumps(stamp)}'", check=False)
    except subprocess.TimeoutExpired:
        pass
    log(f"sync done: {failed} failures")
    return 1 if failed else 0


def cmd_verify(cfg, args):
    worker = Worker(cfg)
    bad = 0
    for s in selected(cfg, args.only):
        state = load_state(s["name"])
        out = worker.ps(f"$r = '{worker.root}/{s['name']}'; if (Test-Path $r) {{ Get-ChildItem -LiteralPath $r -Recurse -File -Force | "
                        f"ForEach-Object {{ $_.FullName.Substring($r.Length + 1).Replace('\\', '/') + \"`t\" + $_.Length }} }}",
                        timeout=3600)
        remote = {}
        for line in out.splitlines():
            if "\t" in line:
                rel, size = line.rsplit("\t", 1)
                remote[rel] = int(size)
        missing = [r for r in state if r not in remote]
        differ = [r for r in state if r in remote and remote[r] != state[r][0] and not is_sanitised(r)]
        bad += len(missing) + len(differ)
        log(f"{s['name']}: {len(state)} in state, {len(remote)} on the worker, missing {len(missing)}, size differs {len(differ)}"
            + (f" e.g. {(missing + differ)[:3]}" if missing or differ else ""))
    return 1 if bad else 0


def cmd_purge(cfg, args):
    """After the excludes / sanitise rules changed: delete on the worker (inside C:/Mirror/Main only, NOT to the trash) every
    mirrored file that is now excluded or sanitised, and drop it from the state - sanitised ones go again, cleaned, next sync."""
    worker = Worker(cfg)
    for s in selected(cfg, args.only):
        state = load_state(s["name"])
        patterns = list(s.get("exclude", [])) + GLOBAL_EXCLUDES
        hit = [r for r in state if is_sanitised(r) or excluded(r, os.path.basename(r), patterns)
               or any(excluded("/".join(r.split("/")[:i]), r.split("/")[i - 1], patterns) for i in range(1, r.count("/") + 1))]
        if not hit:
            continue
        listing = os.path.join(tempfile.gettempdir(), f"mirror-purge-{s['name']}.txt")
        with open(listing, "w", encoding="utf-8") as f:
            f.write("\n".join(hit))
        remote_list = f"{worker.root}/.purge-{s['name']}.txt"
        worker.scp_to(listing, remote_list, timeout=600)
        out = worker.ps(f"""
$src = '{worker.root}/{s['name']}'; $n = 0
foreach ($rel in Get-Content -Encoding UTF8 '{remote_list}') {{
    if (-not $rel) {{ continue }}
    $p = Join-Path $src $rel
    if (Test-Path -LiteralPath $p) {{ Remove-Item -LiteralPath $p -Force; $n++ }}
}}
Remove-Item '{remote_list}'; $n""", timeout=1800)
        os.remove(listing)
        for r in hit:
            state.pop(r, None)
        save_state(s["name"], state)
        log(f"{s['name']}: purged {out.strip().splitlines()[-1] if out.strip() else 0} worker copies of {len(hit)} now excluded / sanitised files "
            f"(sanitised ones are re-sent cleaned)")
    return 0


def safe_extract(tar_path, to):
    """Extract only regular files / dirs that land inside `to` (Python 3.10 has no tarfile data filter): links, devices,
    absolute paths and .. escapes are refused and logged. Returns the number refused."""
    root = os.path.realpath(to)
    refused = 0
    with tarfile.open(tar_path, "r") as tar:
        members = []
        for m in tar.getmembers():
            target = os.path.realpath(os.path.join(root, m.name))
            if not (m.isfile() or m.isdir()) or not (target == root or target.startswith(root + os.sep)):
                log(f"  refused tar member {m.name!r} (not a plain file / dir, or outside {to})")
                refused += 1
                continue
            members.append(m)
        tar.extractall(root, members=members)
    return refused


def cmd_restore(cfg, args):
    worker = Worker(cfg)
    name = args.name
    os.makedirs(args.to, exist_ok=True)
    remote_tar = f"{worker.root}/.restore-{name}.tar"
    log(f"packing {worker.root}/{name} on the worker ...")
    worker.ps(f"Set-Location '{worker.root}/{name}'; & tar -cf '{remote_tar}' .; if ($LASTEXITCODE -ne 0) {{ throw 'tar exit ' + $LASTEXITCODE }}",
              timeout=7200)
    local_tar = os.path.join(args.to, f".restore-{name}.tar")
    worker.scp_from(remote_tar, local_tar)
    worker.ps(f"Remove-Item '{remote_tar}'", check=False)
    safe_extract(local_tar, args.to)
    os.remove(local_tar)
    log(f"restored {name} -> {args.to}")
    return 0


def main():
    ap = argparse.ArgumentParser(description="One-way mirror of the main PC onto codex-worker (C:/Mirror/Main)")
    ap.add_argument("--config", default=DEFAULT_CONFIG)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for c in ("plan", "sync", "verify", "purge"):
        p = sub.add_parser(c)
        p.add_argument("--only", nargs="*", help="source names (default: all)")
        if c == "sync":
            p.add_argument("--part-mb", type=float, default=1024)
            p.add_argument("--max-mbit", type=float, default=None, help="minimum link speed (default config min_link_mbit; 0 = no check)")
    p = sub.add_parser("restore")
    p.add_argument("name")
    p.add_argument("--to", required=True)
    args = ap.parse_args()
    try:
        cfg = load_config(args.config)
    except (OSError, ValueError) as ex:
        log(f"config error: {ex}")
        return 2
    return {"plan": cmd_plan, "sync": cmd_sync, "verify": cmd_verify, "purge": cmd_purge, "restore": cmd_restore}[args.cmd](cfg, args)


if __name__ == "__main__":
    sys.exit(main())
