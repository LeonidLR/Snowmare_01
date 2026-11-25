#!/usr/bin/env python3
"""What runs right now on the main PC and on codex-worker (agent chat Machines tab).

Pure part: classify_processes(procs, now, ctx) turns [{name, pid, start_time, command_line}] into grouped, human readable items
with an optional progress block. ctx is the per-project-root progress data the collector reads in the SAME powershell call
(Build.log tail, Smoke-*.log results, test_map registry size, trainer status.json). Real bars only where a source exists:
  - UBT build: last "[N/M]" line of Saved/Logs/Build.log
  - smoke_parallel: finished Smoke-<Name>.log (Smoke RESULT: PASS|FAIL) since the batch started / the -Commands list
  - night trainer: Saved/Trainer/<run>/status.json jobs_done / jobs_total
  - API agent: spent is only known at the end (--output-format json), so no bar, just the budget
  - everything else: indeterminate bar + elapsed minutes.
Command lines never leave this module (only labels do) and are scrubbed of key-like strings anyway.
"""
import json
import os
import re
import subprocess
import time

REMOTE_HOST = "codex-worker"
REMOTE_ROOT = "C:/Work/CodexTactics"
LOCAL_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")).replace("\\", "/")
HIDDEN = "[скрыто]"

_SECRET_RES = [
    re.compile(r"sk-ant-[A-Za-z0-9_\-]+"),
    re.compile(r"(--api-key)(\s+|=)\S+", re.I),
    re.compile(r"((?:ANTHROPIC|XAI|OPENAI|GEMINI|GOOGLE)_API_KEY)(\s*=\s*|\s+)\S+", re.I),
]

_PS_SCRIPT = r"""
$ErrorActionPreference = 'SilentlyContinue'
$extra = @(__EXTRA_ROOTS__)
function Ep($d) { [int64]([DateTimeOffset]$d).ToUnixTimeSeconds() }
function Tail($f) {
  $fs = [IO.File]::Open($f, 'Open', 'Read', 'ReadWrite')
  try { $n = [int][Math]::Min(32768, $fs.Length); [void]$fs.Seek(-$n, 'End'); $b = New-Object byte[] $n; [void]$fs.Read($b, 0, $n); [Text.Encoding]::UTF8.GetString($b) } finally { $fs.Close() }
}
$procs = @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine } | ForEach-Object {
  $st = 0; if ($_.CreationDate) { $st = Ep $_.CreationDate }
  [pscustomobject]@{ pid = $_.ProcessId; name = $_.Name; start_time = $st; command_line = $_.CommandLine }
})
$roots = @{}
foreach ($e in $extra) { $roots[$e.Replace('\', '/').TrimEnd('/')] = 1 }
foreach ($p in $procs) {
  foreach ($rx in @('([A-Za-z]:[\\/](?:(?![A-Za-z]:[\\/])[^"])*?)[\\/]CodexTactics\.uproject', '-abslog="?([A-Za-z]:[\\/](?:(?![A-Za-z]:[\\/])[^"])*?)[\\/]Saved[\\/]Logs')) {
    $m = [regex]::Match($p.command_line, $rx, 'IgnoreCase')
    if ($m.Success) { $roots[$m.Groups[1].Value.Replace('\', '/')] = 1 }
  }
}
$prog = @()
foreach ($r in $roots.Keys) {
  $o = [ordered]@{ root = $r }
  $bl = "$r/Saved/Logs/Build.log"
  if (Test-Path $bl) {
    $l = Get-Content $bl -Tail 80 | Where-Object { $_ -match '^\[\d+/\d+\]' } | Select-Object -Last 1
    $o.build = @{ line = $l; t = (Ep (Get-Item $bl).LastWriteTime) }
  }
  $o.smokes = @(Get-ChildItem "$r/Saved/Logs/Smoke-*.log" | Where-Object { $_.LastWriteTime -gt (Get-Date).AddHours(-6) } | ForEach-Object {
    $res = ''; $tx = Tail $_.FullName
    if ($tx -match 'Smoke RESULT: (PASS|FAIL)') { $res = $Matches[1] }
    @{ n = $_.Name; t = (Ep $_.LastWriteTime); r = $res }
  })
  $tm = "$r/Scripts/test_map.json"
  if (Test-Path $tm) { $o.registry = @((Get-Content $tm -Raw | ConvertFrom-Json).smokeRegistry).Count }
  $sf = Get-ChildItem "$r/Saved/Trainer/*/status.json" | Sort-Object LastWriteTime | Select-Object -Last 1
  if ($sf) { $s = Get-Content $sf.FullName -Raw | ConvertFrom-Json; $o.trainer = @{ state = $s.state; gen = $s.generation; max = $s.max_generations; jd = $s.jobs_done; jt = $s.jobs_total; finish = $s.eta.finish_by; t = (Ep $sf.LastWriteTime) } }
  $mgp = "$r/Saved/Logs/merge-gate-progress.json"
  if (Test-Path $mgp) { $mgd = Get-Content $mgp -Raw | ConvertFrom-Json; if ($mgd) { $o.merge_gate = @{ stage = $mgd.stage; stage_index = $mgd.stage_index; stage_count = $mgd.stage_count; stage_started = $mgd.stage_started; gate_started = $mgd.gate_started; t = (Ep (Get-Item $mgp).LastWriteTime) } } }
  $mgh = "$r/Saved/Logs/merge-gate-history.jsonl"
  if (Test-Path $mgh) { $o.merge_gate_history = @(Get-Content $mgh -Tail 10 | ForEach-Object { $_ | ConvertFrom-Json }) }
  $rbp = "$r/Saved/Logs/remote-build-progress.json"
  if (Test-Path $rbp) { $rbd = Get-Content $rbp -Raw | ConvertFrom-Json; if ($rbd) { $o.remote_build = @{ stage = $rbd.stage; stage_index = $rbd.stage_index; stage_count = $rbd.stage_count; started = $rbd.started; t = (Ep (Get-Item $rbp).LastWriteTime) } } }
  $rbh = "$r/Saved/Logs/RemoteBuilds.jsonl"
  if (Test-Path $rbh) { $o.remote_build_history = @(Get-Content $rbh -Tail 10 | ForEach-Object { $_ | ConvertFrom-Json }) }
  $sp = Get-ChildItem "$r/Saved/Logs/SmokeParallel-*.json" | Sort-Object LastWriteTime | Select-Object -Last 1
  if ($sp) { $spd = Get-Content $sp.FullName -Raw | ConvertFrom-Json; if ($spd -and $spd.results) { $o.smoke_history = @($spd.results | ForEach-Object { @{ n = $_.Name; s = $_.Seconds } }) } }
  $prog += [pscustomobject]$o
}
$os = Get-CimInstance Win32_OperatingSystem
$cpu = (Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average
[pscustomobject]@{ cpu = $cpu; free_gb = [math]::Round($os.FreePhysicalMemory / 1MB, 1); total_gb = [math]::Round($os.TotalVisibleMemorySize / 1MB, 1); procs = $procs; progress = $prog } | ConvertTo-Json -Depth 6 -Compress
exit 0
"""

_KIND_ORDER = {"Редактор": 0, "Сборка": 1, "Smoke": 2, "Python": 3, "Commandlet": 4, "Агент": 5, "Скрипт": 6, "Тренер": 7}
_ROOT_RES = [
    re.compile(r"([A-Za-z]:[\\/](?:(?![A-Za-z]:[\\/])[^\"])*?)[\\/]CodexTactics\.uproject", re.I),
    re.compile(r"-abslog=\"?([A-Za-z]:[\\/](?:(?![A-Za-z]:[\\/])[^\"])*?)[\\/]Saved[\\/]Logs", re.I),
]


def scrub(text):
    """Remove anything that looks like a key / token from a string."""
    text = text or ""
    text = _SECRET_RES[0].sub(HIDDEN, text)
    for rx in _SECRET_RES[1:]:
        text = rx.sub(lambda m: m.group(1) + m.group(2) + HIDDEN, text)
    return text


def _norm(root):
    return (root or "").replace("\\", "/").rstrip("/").lower()


def _worktree(cmd):
    """Worktree label from a path in the command line (CodexTactics-claude -> claude, plain CodexTactics -> main)."""
    m = re.search(r"CodexTactics-([A-Za-z0-9_.\-]+)", cmd)
    if m:
        return m.group(1)
    if re.search(r"CodexTactics", cmd, re.I):
        return "main"
    return ""


def _script_name(cmd, pattern):
    m = re.search(pattern, cmd, re.I)
    if not m:
        return ""
    return re.split(r"[\\/]", m.group(1).strip('"'))[-1]


def root_of(cmd):
    """Project root named in a command line (-Project= / .uproject path / -abslog), normalized, or ''."""
    for rx in _ROOT_RES:
        m = rx.search(cmd or "")
        if m:
            return _norm(m.group(1))
    return ""


# ---------- pure progress parsers ----------

def parse_ubt_line(line):
    """'[14/15] Link ...' -> (14, 15) or None."""
    m = re.match(r"\s*\[(\d+)/(\d+)\]", line or "")
    return (int(m.group(1)), int(m.group(2))) if m else None


def parse_commands(cmd):
    """Smoke names of a smoke_parallel / worker_sync command line (-Commands a,b,c / 'all'), without the CodexTactics. prefix."""
    m = re.search(r"-Commands\s+((?:[^\s-][^\s]*\s*)+)", cmd or "", re.I)
    if not m:
        return []
    names = [re.sub(r"^CodexTactics\.", "", n) for n in re.split(r"[,;\s]+", m.group(1).replace('"', "").replace("'", "")) if n]
    return names


_GATE_STAGE_ORDER = ("Editor", "Backup", "Merge", "Build", "Tests", "Worker smokes", "Designer files")
_GATE_STAGE_RU = {
    "Editor": "Редактор",
    "Backup": "Бэкап",
    "Merge": "Мердж",
    "Build": "Сборка",
    "Tests": "Юнит-тесты",
    "Worker smokes": "Смоуки воркера",
    "Designer files": "Файлы дизайнера",
}
_REMOTE_STAGE_RU = {"sync": "Синхронизация", "compile": "Сборка", "dlls": "Забор DLL"}


def median(values):
    """Median of numeric values, or None if empty."""
    nums = sorted(float(v) for v in (values or []) if v is not None)
    if not nums:
        return None
    mid = len(nums) // 2
    if len(nums) % 2 == 1:
        return nums[mid]
    return (nums[mid - 1] + nums[mid]) / 2.0


def read_progress_if_fresh(entry, now, max_age_s=600):
    """Returns progress dict only if present and updated within max_age_s (10 min default)."""
    if not isinstance(entry, dict):
        return None
    ts = entry.get("t") if entry.get("t") is not None else entry.get("updated")
    if ts is None:
        return None
    if (now - float(ts)) > max_age_s:
        return None
    return entry


def estimate_merge_gate_eta(stage, stage_started, history_records, now):
    """Remaining seconds for merge gate from median of last 5 runs per stage, or None."""
    if not history_records:
        return None
    last5 = list(history_records)[-5:]
    per_stage = {}
    for rec in last5:
        stages = (rec or {}).get("stages") or {}
        for k, v in stages.items():
            if isinstance(v, (int, float)) and v >= 0:
                per_stage.setdefault(k, []).append(float(v))
    if not per_stage:
        return None
    idx = _GATE_STAGE_ORDER.index(stage) if stage in _GATE_STAGE_ORDER else -1
    cur_med = median(per_stage.get(stage))
    if cur_med is None and idx < 0:
        return None
    rem = 0.0
    if cur_med is not None:
        elapsed_stage = max(0.0, float(now) - float(stage_started or now))
        rem += max(0.0, cur_med - elapsed_stage)
    if idx >= 0:
        for nxt in _GATE_STAGE_ORDER[idx + 1:]:
            m = median(per_stage.get(nxt))
            if m is not None:
                rem += m
    return max(0, int(round(rem)))


def estimate_remote_build_eta(started, history_records, done, total, now):
    """Remaining seconds for remote build from RemoteBuilds.jsonl median, or linear [N/M], or None."""
    recs = [r for r in (history_records or []) if isinstance((r or {}).get("total_s"), (int, float)) and r["total_s"] > 0]
    ok_recs = [r for r in recs if r.get("exit") == 0]
    pool = ok_recs[-5:] if ok_recs else recs[-5:]
    med_total = median([r["total_s"] for r in pool])
    elapsed = max(0.0, float(now) - float(started or now))
    if med_total is not None:
        return max(0, int(round(med_total - elapsed)))
    if done and total and 0 < done < total and elapsed > 30:
        return max(0, int(round(elapsed / done * (total - done))))
    return None


def estimate_smoke_batch_eta(wanted_names, finished_names, smoke_durations, jobs, elapsed_s, done, total):
    """Remaining seconds for smoke batch = sum(remaining last durations) / jobs, or linear fallback."""
    jobs = max(1, int(jobs or 1))
    dur_map = {}
    for row in (smoke_durations or []):
        name = (row or {}).get("n") or (row or {}).get("Name")
        sec = (row or {}).get("s") if (row or {}).get("s") is not None else (row or {}).get("Seconds")
        if name and isinstance(sec, (int, float)) and sec >= 0:
            dur_map[name] = float(sec)
    if wanted_names and dur_map:
        remaining = [n for n in wanted_names if n not in (finished_names or set())]
        known = [dur_map[n] for n in remaining if n in dur_map]
        if known:
            return max(0, int(round(sum(known) / jobs)))
    if done and total and 0 < done < total and elapsed_s > 30:
        return max(0, int(round(elapsed_s / done * (total - done))))
    return None


def smoke_counts(logs, since, names=None):
    """logs = [{n: 'Smoke-X[-alone].log', t, r}] -> (done, fail) of unique smokes finished since `since` (and in `names` when given)."""
    seen = _smoke_finished(logs, since, names)
    return len(seen), sum(1 for _, r in seen.values() if r == "FAIL")


def _smoke_finished(logs, since, names=None):
    seen = {}
    for l in logs or []:
        m = re.match(r"Smoke-(.+?)(-alone)?\.log$", l.get("n", ""))
        if not m or (l.get("t") or 0) < since or l.get("r") not in ("PASS", "FAIL"):
            continue
        name = m.group(1)
        if names and name not in names:
            continue
        if name not in seen or l["t"] >= seen[name][0]:
            seen[name] = (l["t"], l["r"])
    return seen


def _eta(done, total, elapsed_s):
    if done and total and done < total and elapsed_s > 30:
        return max(1, int(round(elapsed_s / done * (total - done) / 60)))
    return None


def _bar(done, total, text, elapsed_s, warn=False, stage=None, started=None, eta_s=None):
    pct = round(100.0 * done / total, 1) if total else 0
    if eta_s is not None:
        eta_s = max(0, int(round(eta_s)))
        eta_min = max(1, int(round(eta_s / 60))) if eta_s > 0 else 0
    else:
        eta_min = _eta(done, total, elapsed_s)
        eta_s = eta_min * 60 if eta_min is not None else None
    out = {
        "indeterminate": False, "done": done, "total": total, "pct": min(100, pct),
        "text": text, "state": "warn" if warn else "running", "eta_min": eta_min,
    }
    if stage is not None:
        out["stage"] = stage
    if started is not None:
        out["started"] = started
    if eta_s is not None:
        out["eta_s"] = eta_s
    return out


def _indet(text=""):
    return {"indeterminate": True, "text": text, "state": "running"}


def _pick_root(ctx, cmd, start, score):
    r = root_of(cmd)
    if r and r in ctx:
        return ctx[r]
    best, best_s = None, 0
    for c in ctx.values():
        s = score(c)
        if s > best_s:
            best, best_s = c, s
    return best


def _progress_for(p, kind, label, ctx, now):
    """Progress block for one process, or None (the caller falls back to indeterminate)."""
    cmd = p.get("command_line") or ""
    low = cmd.lower()
    start = p.get("start_time") or 0
    elapsed = max(0, now - start) if start else 0
    ctx = ctx or {}
    if kind == "Сборка":
        if label.startswith("Удалённая сборка"):
            c = _pick_root(ctx, cmd, start, lambda c: 1 if (c.get("remote_build") or c.get("build")) else 0)
            rb = read_progress_if_fresh((c or {}).get("remote_build"), now)
            b = (c or {}).get("build") or {}
            nm = parse_ubt_line(b.get("line")) if b.get("t", 0) >= start - 120 else None
            hist = (c or {}).get("remote_build_history") or []
            rb_started = (rb or {}).get("started") or start
            if nm:
                eta_s = estimate_remote_build_eta(rb_started, hist, nm[0], nm[1], now)
                return _bar(nm[0], nm[1], f"Сборка {nm[0]}/{nm[1]}", elapsed, stage="Сборка", started=rb_started, eta_s=eta_s)
            if rb and rb.get("stage_count"):
                st_key = str(rb.get("stage") or "sync")
                st_ru = _REMOTE_STAGE_RU.get(st_key, st_key)
                idx = int(rb.get("stage_index") or 1)
                cnt = int(rb.get("stage_count") or 3)
                eta_s = estimate_remote_build_eta(rb_started, hist, idx, cnt, now)
                return _bar(idx, cnt, f"{st_ru} {idx}/{cnt}", elapsed, stage=st_ru, started=rb_started, eta_s=eta_s)
            return None
        c = _pick_root(ctx, cmd, start, lambda c: 1 if (c.get("build") or {}).get("t", 0) >= start - 120 and (c.get("build") or {}).get("line") else 0)
        b = (c or {}).get("build") or {}
        nm = parse_ubt_line(b.get("line")) if b.get("t", 0) >= start - 120 else None
        if nm:
            return _bar(nm[0], nm[1], f"Сборка {nm[0]}/{nm[1]}", elapsed, stage="Сборка", started=start)
        return None
    if kind == "Скрипт" and "merge_gate.ps1" in low:
        c = _pick_root(ctx, cmd, start, lambda c: 1 if (c.get("merge_gate") or {}).get("stage") else 0)
        mg = read_progress_if_fresh((c or {}).get("merge_gate"), now)
        if mg and mg.get("stage"):
            stage = str(mg["stage"])
            stage_ru = _GATE_STAGE_RU.get(stage, stage)
            idx = int(mg.get("stage_index") or 1)
            cnt = int(mg.get("stage_count") or len(_GATE_STAGE_ORDER))
            gate_started = mg.get("gate_started") or start
            eta_s = estimate_merge_gate_eta(stage, mg.get("stage_started") or start, (c or {}).get("merge_gate_history"), now)
            return _bar(idx, cnt, f"{stage_ru} {idx}/{cnt}", elapsed, stage=stage_ru, started=gate_started, eta_s=eta_s)
        return None
    if kind == "Скрипт" and "smoke_parallel.ps1" in low:
        names = parse_commands(cmd)
        every = "all" in names
        m_jobs = re.search(r"-Jobs\s+(\d+)", cmd, re.I)
        jobs = int(m_jobs.group(1)) if m_jobs else 1

        def score(c):
            return smoke_counts(c.get("smokes"), start - 5)[0]
        c = _pick_root(ctx, cmd, start, score)
        if not c:
            return None
        total = int(c.get("registry") or 0) if every else len(names)
        wanted = None if every else set(names)
        finished = _smoke_finished(c.get("smokes"), start - 5, wanted)
        done = len(finished)
        fail = sum(1 for _, r in finished.values() if r == "FAIL")
        if total <= 0:
            return None
        eta_s = estimate_smoke_batch_eta(wanted, set(finished.keys()), c.get("smoke_history"), jobs, elapsed, done, total)
        text = f"Смоуки {done}/{total}" + (f", {fail} FAIL" if fail else "")
        return _bar(done, total, text, elapsed, warn=fail > 0, stage="Смоуки", started=start, eta_s=eta_s)
    if kind == "Скрипт" and "worker_sync.ps1" in low:
        n = len([x for x in parse_commands(cmd) if x != "all"])
        return _indet(f"{n} smokes на воркере" if n else "")
    if kind == "Тренер":
        c = _pick_root(ctx, cmd, start, lambda c: 1 if (c.get("trainer") or {}).get("t", 0) >= now - 6 * 3600 else 0)
        t = (c or {}).get("trainer") or {}
        if t and t.get("t", 0) >= now - 6 * 3600 and t.get("jt"):
            txt = f"ген. {t.get('gen')} · задач {t.get('jd')} / {t.get('jt')}"
            if t.get("finish"):
                txt += f" · до {str(t['finish'])[11:16]}"
            return _bar(int(t.get("jd") or 0), int(t["jt"]), txt, 0)
        return None
    if label.startswith("API-агент"):
        m = re.search(r"(?:-BudgetUsd\s+|--max-budget-usd\s+)([0-9.]+)", cmd)
        return _indet(f"бюджет ${m.group(1)} (расход виден после завершения)" if m else "")
    return None


# ---------- classification ----------

def _classify_one(p):
    name = (p.get("name") or "").lower()
    cmd = p.get("command_line") or ""
    low = cmd.lower()
    wt = _worktree(cmd)
    wt_s = f" ({wt})" if wt else ""

    if name in ("unrealbuildtool.exe", "build.bat") or (name == "dotnet.exe" and "unrealbuildtool" in low):
        return "Сборка", f"Сборка{wt_s}"
    if name in ("unrealeditor-cmd.exe", "unrealeditor-win64-debuggame-cmd.exe"):
        m = re.search(r"-ExecCmds=\"?([^\s\"]+)", cmd, re.I)
        if m:
            smoke = re.split(r"[,;]", m.group(1))[0]
            if smoke.lower().startswith("codextactics."):
                smoke = smoke.split(".", 1)[1]
            return "Smoke", f"Smoke {smoke}{wt_s}"
        if re.search(r"-run=pythonscript", cmd, re.I):
            s = _script_name(cmd, r"-script=(\"[^\"]+\"|\S+)")
            return "Python", f"Python в редакторе: {s or '?'}{wt_s}"
        m = re.search(r"-run=(\w+)", cmd, re.I)
        return "Commandlet", f"Commandlet {m.group(1) if m else ''}{wt_s}".strip()
    if name == "unrealeditor.exe":
        if "codextactics.uproject" in low and "-game" not in low and "-run=" not in low:
            return "Редактор", f"Редактор дизайнера ({wt or 'main'})"
        return None
    if name in ("grok", "grok.exe"):
        return "Агент", "Грок"
    if name in ("claude", "claude.exe"):
        if "--bare" in low or "claude_api_agent" in low:
            return "Агент", "API-агент Claude"
        return None
    if name in ("codex", "codex.exe"):
        return "Агент", "Codex"
    if name in ("powershell.exe", "pwsh.exe", "python.exe", "pythonw.exe", "py.exe", "node.exe", "cmd.exe"):
        for key, label in (
            ("merge_gate.ps1", "Merge gate"),
            ("smoke_parallel.ps1", "Параллельные smoke"),
            ("worker_sync.ps1", "Синхронизация воркера"),
            ("grok_task.ps1", "Грок: задача"),
            ("claude_api_agent.ps1", "API-агент Claude"),
            ("qd_window.ps1", "QD-окно тренера"),
        ):
            if key in low:
                return "Скрипт", label + wt_s
        if "build_remote.ps1" in low:
            return "Сборка", "Удалённая сборка" + wt_s
        if "nighttrainer" in low or re.search(r"scripts[\\/]+trainer", low):
            return "Тренер", "Ночной тренер"
    return None


def classify_processes(procs, now=None, ctx=None):
    """-> list of {kind, label, pid, started, running_min, count, progress}; same kind+label are merged (count, longest run)."""
    now = now if now is not None else time.time()
    merged = {}
    for p in procs or []:
        r = _classify_one(p)
        if not r:
            continue
        kind, label = r
        st = p.get("start_time") or 0
        e = merged.setdefault((kind, label), {"kind": kind, "label": label, "pid": p.get("pid"), "start": st, "count": 0, "p": p})
        e["count"] += 1
        if st and (not e["start"] or st < e["start"]):
            e["start"], e["pid"], e["p"] = st, p.get("pid"), p
    items = []
    for e in merged.values():
        st = e["start"]
        run_min = int(max(0, now - st) // 60) if st else None
        try:
            prog = _progress_for(e["p"], e["kind"], e["label"], ctx, now)
        except Exception:
            prog = None
        if prog is None:
            prog = _indet()
        if prog.get("indeterminate") and run_min is not None:
            prog["text"] = (prog.get("text") + " · " if prog.get("text") else "") + f"{run_min} мин"
        items.append({
            "kind": e["kind"], "label": scrub(e["label"]), "pid": e["pid"], "count": e["count"],
            "started": time.strftime("%H:%M:%S", time.localtime(st)) if st else "",
            "running_min": run_min, "progress": prog,
        })
    items.sort(key=lambda i: (_KIND_ORDER.get(i["kind"], 9), -(i["running_min"] or 0)))
    return items


# ---------- collectors ----------

def _run_ps(prefix, extra_roots, timeout):
    roots = ", ".join("'" + r.replace("'", "''") + "'" for r in extra_roots)
    script = _PS_SCRIPT.replace("__EXTRA_ROOTS__", roots) + "\n"
    cmd = prefix + ["powershell", "-NoProfile", "-NonInteractive", "-Command", "-"]
    flags = 0x08000000 if os.name == "nt" else 0  # CREATE_NO_WINDOW
    res = subprocess.run(cmd, input=script.encode("utf-8"), capture_output=True, timeout=timeout, creationflags=flags)
    out = res.stdout.decode("utf-8", "replace").strip()
    err = res.stderr.decode("utf-8", "replace").strip()
    start = out.find('{"')
    if start < 0:
        raise RuntimeError(scrub(err or out or f"exit {res.returncode}")[:300])
    return json.loads(out[start:])


def _summarize(raw, now=None):
    procs = raw.get("procs") or []
    if isinstance(procs, dict):
        procs = [procs]
    prog = raw.get("progress") or []
    if isinstance(prog, dict):
        prog = [prog]
    ctx = {}
    for c in prog:
        if isinstance(c.get("smokes"), dict):
            c["smokes"] = [c["smokes"]]
        ctx[_norm(c.get("root"))] = c
    return {
        "status": "онлайн",
        "cpu": raw.get("cpu"),
        "free_gb": raw.get("free_gb"),
        "total_gb": raw.get("total_gb"),
        "items": classify_processes(procs, now, ctx),
    }


def collect_local():
    return _summarize(_run_ps([], [LOCAL_ROOT], 30))


def collect_remote(host=REMOTE_HOST):
    return _summarize(_run_ps(["ssh", "-o", "ConnectTimeout=8", "-o", "BatchMode=yes", host], [REMOTE_ROOT], 60))  # background thread: the progress scan over Wi-Fi takes 20-40 s
