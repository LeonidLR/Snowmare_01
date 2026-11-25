"""Dispatch lane builds and smokes onto whichever machine (main PC or worker) is free.

Usage:
  python Scripts/Tools/machine_dispatch.py status [--lane <worktree>]
  python Scripts/Tools/machine_dispatch.py build --lane <worktree> [--target <Target>] [--config <Config>]
  python Scripts/Tools/machine_dispatch.py smoke --lane <worktree> --smokes A,B [--map <Map>] [--jobs <N>]
"""

from __future__ import annotations

import argparse
import base64
import ctypes
from dataclasses import dataclass
import datetime as dt
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid
from typing import Callable, Optional


DEFAULT_WORKER = "codex-worker"
DEFAULT_REMOTE_ROOT = "C:/Work/CodexTactics"
MAIN_FREE_FLAG = Path("Saved") / "Lead" / "MAIN_PC_FREE"
QUEUE_REL_PATH = Path("Saved") / "Dispatch" / "queue.json"
DEFAULT_POLL_SECONDS = 5.0


@dataclass(frozen=True)
class LockInfo:
    agent: str
    purpose: str
    started: str
    pid: int = 0
    manual: bool = False
    source: str = ""


@dataclass(frozen=True)
class MachineState:
    name: str  # "main" | "worker"
    allowed: bool
    free: bool
    reason: str
    lock: Optional[LockInfo] = None


def is_pid_alive(pid: int) -> bool:
    """Return True if a process with the given PID exists right now."""
    if pid <= 0:
        return False
    if os.name == "nt":
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        STILL_ACTIVE = 259
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
        if not handle:
            return False
        try:
            exit_code = ctypes.c_ulong(0)
            if kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code)):
                return exit_code.value == STILL_ACTIVE
            return True
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def test_gate_editor_command_line(command_line: str, project_path: str) -> bool:
    """Mirror merge_gate.ps1 Test-GateEditorCommandLine: True if interactive Editor has project open."""
    if not command_line:
        return False
    norm_project = re.escape(project_path.replace("/", "\\"))
    norm_line = command_line.replace("/", "\\")
    if not re.search(rf'(?:^|[\s"]){norm_project}(?=[\s"]|$)', norm_line, re.IGNORECASE):
        return False
    if re.search(r'(?:^|[\s"])-(?:game(?=[\s"]|$)|run=)', norm_line, re.IGNORECASE):
        return False
    return True


def resolve_repo_roots(lane_path: Path) -> tuple[Path, Path]:
    """Return (lane_root, main_root) using git-common-dir when inside a worktree."""
    lane_root = lane_path.resolve()
    try:
        res = subprocess.run(
            ["git", "-C", str(lane_root), "rev-parse", "--path-format=absolute", "--git-common-dir"],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if res.returncode == 0 and res.stdout.strip():
            common_dir = Path(res.stdout.strip())
            return lane_root, common_dir.parent.resolve()
    except (OSError, subprocess.SubprocessError):
        pass
    return lane_root, lane_root


def list_worktree_roots(repo_root: Path) -> list[Path]:
    """List all git worktree roots belonging to repo_root (plus repo_root itself)."""
    roots: list[Path] = [repo_root.resolve()]
    try:
        res = subprocess.run(
            ["git", "-C", str(repo_root), "worktree", "list", "--porcelain"],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if res.returncode == 0:
            for line in res.stdout.splitlines():
                if line.startswith("worktree "):
                    candidate = Path(line[9:].strip()).resolve()
                    if candidate not in roots:
                        roots.append(candidate)
    except (OSError, subprocess.SubprocessError):
        pass
    return roots


def read_lock_file(lock_path: Path, pid_alive_fn: Callable[[int], bool] = is_pid_alive) -> Optional[LockInfo]:
    """Read an agent.lock JSON file; return LockInfo only if manual or PID is live."""
    if not lock_path.is_file():
        return None
    try:
        data = json.loads(lock_path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None
    if not isinstance(data, dict):
        return None
    manual = bool(data.get("manual", False))
    try:
        pid = int(data.get("pid") or 0)
    except (TypeError, ValueError):
        pid = 0
    if not manual and not pid_alive_fn(pid):
        return None
    return LockInfo(
        agent=str(data.get("agent") or "unknown"),
        purpose=str(data.get("purpose") or "busy"),
        started=str(data.get("started") or ""),
        pid=pid,
        manual=manual,
        source=str(lock_path),
    )


def query_local_editor_processes() -> list[dict[str, object]]:
    """Return [{ProcessId, CommandLine}] for local UnrealEditor.exe processes."""
    ps_script = (
        "$ErrorActionPreference='SilentlyContinue'; "
        "$items = @(Get-CimInstance Win32_Process -Filter \"Name = 'UnrealEditor.exe'\" | "
        "Select-Object ProcessId, CommandLine); "
        "ConvertTo-Json -InputObject $items -Compress"
    )
    encoded = base64.b64encode(ps_script.encode("utf-16-le")).decode("ascii")
    res = subprocess.run(
        ["powershell", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
        capture_output=True,
        text=True,
        timeout=15,
    )
    if res.returncode != 0 or not res.stdout.strip():
        return []
    try:
        parsed = json.loads(res.stdout.strip())
    except ValueError:
        return []
    if isinstance(parsed, dict):
        return [parsed]
    return [item for item in parsed if isinstance(item, dict)]


def has_open_designer_editor(
    main_root: Path,
    lane_root: Optional[Path] = None,
    process_reader: Callable[[], list[dict[str, object]]] = query_local_editor_processes,
) -> tuple[bool, str]:
    """Check whether the designer's CodexTactics editor is open in main or CodexTactics-play."""
    candidates = [
        (main_root / "CodexTactics.uproject").resolve(),
        (main_root.parent / "CodexTactics-play" / "CodexTactics.uproject").resolve(),
    ]
    if lane_root is not None:
        candidates.append((lane_root / "CodexTactics.uproject").resolve())
    targets: list[str] = []
    seen: set[str] = set()
    for path in candidates:
        key = str(path).lower()
        if key not in seen:
            seen.add(key)
            targets.append(str(path))

    try:
        editors = process_reader()
    except (OSError, subprocess.SubprocessError) as exc:
        return True, f"editor process query failed ({exc})"

    for proc in editors:
        pid = proc.get("ProcessId", "?")
        cmd = proc.get("CommandLine")
        if not isinstance(cmd, str) or not cmd.strip():
            return True, f"UnrealEditor.exe PID {pid} CommandLine unreadable"
        for proj in targets:
            if test_gate_editor_command_line(cmd, proj):
                return True, f"designer Unreal Editor open (PID {pid})"
    return False, ""


def find_active_local_lock(
    roots: list[Path],
    pid_alive_fn: Callable[[int], bool] = is_pid_alive,
) -> Optional[LockInfo]:
    """Find the first active Saved/agent.lock across main and worktree roots."""
    for root in roots:
        lock = read_lock_file(root / "Saved" / "agent.lock", pid_alive_fn=pid_alive_fn)
        if lock is not None:
            return lock
    return None


def read_main_state(
    main_root: Path,
    lane_root: Path,
    pid_alive_fn: Callable[[int], bool] = is_pid_alive,
    editor_procs_fn: Callable[[], list[dict[str, object]]] = query_local_editor_processes,
    worktrees_fn: Callable[[Path], list[Path]] = list_worktree_roots,
) -> MachineState:
    """Inspect MAIN_PC_FREE flag, designer Unreal Editor, and local agent.lock files."""
    if not (main_root / MAIN_FREE_FLAG).exists():
        return MachineState(
            name="main",
            allowed=False,
            free=False,
            reason=f"{MAIN_FREE_FLAG.as_posix()} absent",
        )
    editor_open, editor_reason = has_open_designer_editor(
        main_root, lane_root=lane_root, process_reader=editor_procs_fn
    )
    if editor_open:
        return MachineState(
            name="main",
            allowed=False,
            free=False,
            reason=editor_reason,
        )
    roots = worktrees_fn(main_root)
    if lane_root.resolve() not in [r.resolve() for r in roots]:
        roots = [lane_root.resolve(), *roots]
    lock = find_active_local_lock(roots, pid_alive_fn=pid_alive_fn)
    if lock is not None:
        return MachineState(
            name="main",
            allowed=True,
            free=False,
            reason=f"{lock.agent} ({lock.purpose})",
            lock=lock,
        )
    return MachineState(name="main", allowed=True, free=True, reason="free")


def query_worker_lock_ssh(worker: str = DEFAULT_WORKER, remote_root: str = DEFAULT_REMOTE_ROOT) -> dict[str, object]:
    """Query worker's Saved/agent.lock and UnrealEditor processes over SSH (same check as worker_sync.ps1)."""
    ps_script = f"""
$ErrorActionPreference='Continue'; $ProgressPreference='SilentlyContinue'
$l = '{remote_root}/Saved/agent.lock'
$lockObj = $null
if (Test-Path $l) {{
    try {{ $j = Get-Content $l -Raw | ConvertFrom-Json }} catch {{ $j = $null }}
    if ($j -and ($j.manual -or (Get-Process -Id ([int]$j.pid) -ErrorAction SilentlyContinue))) {{
        $lockObj = @{{ agent = [string]$j.agent; purpose = [string]$j.purpose; started = [string]$j.started; pid = [int]$j.pid; manual = [bool]$j.manual }}
    }}
}}
$root = '{remote_root}'.Replace('/', '\\').ToLower() + '\\'
$editors = @(Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" | Where-Object {{
    $_.Name -eq 'UnrealEditor.exe' -or ($_.CommandLine -match 'CodexTactics' -and $_.CommandLine.Replace('/', '\\').ToLower().Contains($root))
}})
$hasEditor = ($editors | Where-Object {{ $_.Name -eq 'UnrealEditor.exe' }}).Count -gt 0
$engines = @($editors | Where-Object {{ $_.Name -ne 'UnrealEditor.exe' }} | ForEach-Object {{ $_.Name }})
ConvertTo-Json -InputObject @{{ ok = $true; lock = $lockObj; editor = $hasEditor; engines = $engines }} -Compress
"""
    encoded = base64.b64encode(ps_script.encode("utf-16-le")).decode("ascii")
    res = subprocess.run(
        ["ssh", "-o", "ConnectTimeout=6", "-o", "BatchMode=yes", worker, "powershell", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
        capture_output=True,
        text=True,
        timeout=15,
    )
    if res.returncode != 0 or not res.stdout.strip():
        err = (res.stderr or res.stdout or f"ssh exit {res.returncode}").strip()
        return {"ok": False, "error": err}
    try:
        data = json.loads(res.stdout.strip().splitlines()[-1])
        return data if isinstance(data, dict) else {"ok": False, "error": "invalid worker response"}
    except ValueError as exc:
        return {"ok": False, "error": f"invalid JSON from worker: {exc}"}


def read_worker_state(
    worker: str = DEFAULT_WORKER,
    remote_root: str = DEFAULT_REMOTE_ROOT,
    ssh_reader: Callable[[str, str], dict[str, object]] = query_worker_lock_ssh,
) -> MachineState:
    """Inspect worker lock and running UnrealEditor processes."""
    try:
        info = ssh_reader(worker, remote_root)
    except (OSError, subprocess.SubprocessError) as exc:
        return MachineState(name="worker", allowed=False, free=False, reason=f"ssh error: {exc}")

    if not info.get("ok"):
        return MachineState(
            name="worker",
            allowed=False,
            free=False,
            reason=str(info.get("error") or "worker unreachable"),
        )
    lock_raw = info.get("lock")
    if isinstance(lock_raw, dict):
        lock = LockInfo(
            agent=str(lock_raw.get("agent") or "unknown"),
            purpose=str(lock_raw.get("purpose") or "busy"),
            started=str(lock_raw.get("started") or ""),
            pid=int(lock_raw.get("pid") or 0),
            manual=bool(lock_raw.get("manual", False)),
            source=f"{worker}:{remote_root}/Saved/agent.lock",
        )
        return MachineState(
            name="worker",
            allowed=True,
            free=False,
            reason=f"{lock.agent} ({lock.purpose})",
            lock=lock,
        )
    if info.get("editor"):
        return MachineState(
            name="worker",
            allowed=False,
            free=False,
            reason="UnrealEditor.exe open on worker",
        )
    engines = [str(e) for e in (info.get("engines") or [])]
    if engines:
        return MachineState(
            name="worker",
            allowed=True,
            free=False,
            reason=f"busy engines: {', '.join(engines)}",
        )
    return MachineState(name="worker", allowed=True, free=True, reason="free")


def pick_machine(main_state: MachineState, worker_state: MachineState) -> tuple[Optional[str], str]:
    """Choose 'main', 'worker', or None (both busy / unavailable) with human reason."""
    if main_state.free and worker_state.free:
        return "main", "both free"
    if main_state.free:
        return "main", f"worker busy: {worker_state.reason}"
    if worker_state.free:
        prefix = "main busy: " if main_state.allowed else ""
        return "worker", f"{prefix}{main_state.reason}"
    return None, f"both busy (main: {main_state.reason}; worker: {worker_state.reason})"


class _QueueFileLock:
    """Exclusive stdlib file lock for Saved/Dispatch/queue.json."""

    def __init__(self, lock_path: Path, timeout_seconds: float = 10.0) -> None:
        self.lock_path = lock_path
        self.timeout_seconds = timeout_seconds
        self._fh = None

    def __enter__(self) -> "_QueueFileLock":
        self.lock_path.parent.mkdir(parents=True, exist_ok=True)
        self._fh = open(self.lock_path, "a+b")
        if self._fh.tell() == 0:
            self._fh.write(b"0")
            self._fh.flush()
        deadline = time.monotonic() + self.timeout_seconds
        while True:
            try:
                if os.name == "nt":
                    import msvcrt

                    self._fh.seek(0)
                    msvcrt.locking(self._fh.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl

                    fcntl.flock(self._fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                return self
            except OSError:
                if time.monotonic() >= deadline:
                    self._fh.close()
                    self._fh = None
                    raise TimeoutError(f"Timed out locking {self.lock_path}")
                time.sleep(0.05)

    def __exit__(self, exc_type, exc, tb) -> None:
        if self._fh is None:
            return
        try:
            if os.name == "nt":
                import msvcrt

                self._fh.seek(0)
                msvcrt.locking(self._fh.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(self._fh.fileno(), fcntl.LOCK_UN)
        finally:
            self._fh.close()
            self._fh = None


class DispatchQueue:
    """Shared file-locked FIFO queue stored in Saved/Dispatch/queue.json."""

    def __init__(
        self,
        queue_path: Path,
        pid_alive_fn: Callable[[int], bool] = is_pid_alive,
    ) -> None:
        self.queue_path = queue_path
        self.lock_path = queue_path.with_suffix(queue_path.suffix + ".lock")
        self.pid_alive_fn = pid_alive_fn

    def _read_entries_unlocked(self) -> list[dict[str, object]]:
        if not self.queue_path.is_file():
            return []
        try:
            raw = json.loads(self.queue_path.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError):
            return []
        if not isinstance(raw, list):
            return []
        cleaned: list[dict[str, object]] = []
        for item in raw:
            if not isinstance(item, dict):
                continue
            try:
                pid = int(item.get("pid") or 0)
            except (TypeError, ValueError):
                pid = 0
            if pid > 0 and not self.pid_alive_fn(pid):
                continue
            cleaned.append(item)
        return cleaned

    def _write_entries_unlocked(self, entries: list[dict[str, object]]) -> None:
        self.queue_path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.queue_path.with_suffix(f".tmp.{os.getpid()}")
        tmp.write_text(json.dumps(entries, indent=2), encoding="utf-8")
        os.replace(tmp, self.queue_path)

    def list_entries(self) -> list[dict[str, object]]:
        with _QueueFileLock(self.lock_path):
            entries = self._read_entries_unlocked()
            if self.queue_path.is_file():
                self._write_entries_unlocked(entries)
            return list(entries)

    def enqueue(self, job: str, lane: Path, pid: Optional[int] = None) -> dict[str, object]:
        entry = {
            "id": uuid.uuid4().hex[:12],
            "job": job,
            "lane": str(lane),
            "pid": os.getpid() if pid is None else int(pid),
            "enqueued_at": dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        }
        with _QueueFileLock(self.lock_path):
            entries = self._read_entries_unlocked()
            entries.append(entry)
            self._write_entries_unlocked(entries)
        return entry

    def is_head(self, entry_id: str) -> bool:
        with _QueueFileLock(self.lock_path):
            entries = self._read_entries_unlocked()
            self._write_entries_unlocked(entries)
            return bool(entries) and entries[0].get("id") == entry_id

    def remove(self, entry_id: str) -> None:
        with _QueueFileLock(self.lock_path):
            entries = self._read_entries_unlocked()
            kept = [item for item in entries if item.get("id") != entry_id]
            if len(kept) != len(entries) or self.queue_path.is_file():
                self._write_entries_unlocked(kept)


def _print_line(msg: str) -> None:
    print(msg, flush=True)


def acquire_machine_for_job(
    job: str,
    lane_root: Path,
    main_root: Path,
    read_main_fn: Callable[[], MachineState],
    read_worker_fn: Callable[[], MachineState],
    queue: DispatchQueue,
    poll_seconds: float = DEFAULT_POLL_SECONDS,
    sleep_fn: Callable[[float], None] = time.sleep,
    print_fn: Callable[[str], None] = _print_line,
    max_wait_seconds: Optional[float] = None,
) -> tuple[str, str]:
    """Pick an idle machine immediately, or join Saved/Dispatch/queue.json FIFO until one frees."""
    existing = queue.list_entries()
    if not existing:
        chosen, reason = pick_machine(read_main_fn(), read_worker_fn())
        if chosen is not None:
            print_fn(f"DISPATCH {job} -> {chosen} ({reason})")
            return chosen, reason

    entry = queue.enqueue(job=job, lane=lane_root)
    entry_id = str(entry["id"])
    start_ts = time.monotonic()
    try:
        while True:
            if queue.is_head(entry_id):
                chosen, reason = pick_machine(read_main_fn(), read_worker_fn())
                if chosen is not None:
                    queue.remove(entry_id)
                    print_fn(f"DISPATCH {job} -> {chosen} ({reason})")
                    return chosen, reason
            if max_wait_seconds is not None and (time.monotonic() - start_ts) >= max_wait_seconds:
                raise TimeoutError(f"Timed out waiting in dispatch queue for {job}")
            sleep_fn(poll_seconds)
    except BaseException:
        queue.remove(entry_id)
        raise


def build_command_for(machine: str, lane_root: Path, target: Optional[str], config: Optional[str]) -> list[str]:
    cmd = [
        "powershell",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(lane_root / "Scripts" / "build.ps1"),
    ]
    if machine == "worker":
        cmd.extend(["-Remote", "worker"])
    if target:
        cmd.extend(["-Target", target])
    if config:
        cmd.extend(["-Config", config])
    return cmd


def smoke_command_for(
    machine: str,
    lane_root: Path,
    smokes: str,
    smoke_map: Optional[str],
    jobs: Optional[int],
    worker: str,
    worker_result_dir: Path,
) -> list[str]:
    names = [s.strip() for s in re.split(r"[,;\s]+", smokes) if s.strip()]
    if machine == "main":
        cmd = [
            "powershell",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(lane_root / "Scripts" / "smoke_parallel.ps1"),
            "-Commands",
            ",".join(names),
            "-Remote",
            "off",
        ]
        if smoke_map:
            cmd.extend(["-Map", smoke_map])
        if jobs and jobs > 0:
            cmd.extend(["-Jobs", str(jobs)])
        return cmd

    cmd = [
        "powershell",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(lane_root / "Scripts" / "Tools" / "worker_sync.ps1"),
        "-Worker",
        worker,
        "-ShipHead",
        "-BinariesFrom",
        str(lane_root),
        "-Commands",
        ",".join(names),
        "-AllLogs",
        "-ResultDir",
        str(worker_result_dir),
    ]
    if smoke_map:
        cmd.extend(["-SmokeExtra", f"-Map '{smoke_map}'"])
    if jobs and jobs > 0:
        cmd.extend(["-Jobs", str(jobs)])
    return cmd


def report_failure_logs(kind: str, machine: str, lane_root: Path, worker_result_dir: Optional[Path] = None) -> None:
    """Print log path(s) when a dispatched build or smoke fails."""
    if kind == "build":
        print(f"Log: {lane_root / 'Saved' / 'Logs' / 'Build.log'}", flush=True)
        return
    if machine == "worker" and worker_result_dir is not None:
        json_path = worker_result_dir / "SmokeParallel-worker.json"
        if json_path.is_file():
            try:
                data = json.loads(json_path.read_text(encoding="utf-8-sig"))
                failed = [r for r in data.get("results", []) if r.get("State") == "FAIL"]
                if failed:
                    for item in failed:
                        name = item.get("Name") or "smoke"
                        print(f"Log ({name}): {worker_result_dir / f'Smoke-{name}.log'}", flush=True)
                    return
            except (OSError, ValueError):
                pass
        print(f"Logs: {worker_result_dir}", flush=True)
        return

    summary = lane_root / "Saved" / "Logs" / "SmokeParallel-last.json"
    if summary.is_file():
        try:
            data = json.loads(summary.read_text(encoding="utf-8-sig"))
            failed = [r for r in data.get("results", []) if r.get("State") == "FAIL"]
            if failed:
                for item in failed:
                    print(f"Log ({item.get('Name', 'smoke')}): {item.get('Log', summary)}", flush=True)
                return
        except (OSError, ValueError):
            pass
    print(f"Logs: {lane_root / 'Saved' / 'Logs'}", flush=True)


def format_machine_line(state: MachineState) -> str:
    if state.free:
        status = "free"
    elif state.lock is not None:
        since = f" since {state.lock.started}" if state.lock.started else ""
        status = f"busy: {state.lock.agent} | {state.lock.purpose}{since}"
    else:
        status = f"blocked/busy: {state.reason}"
    flag = "allowed" if state.allowed else "disallowed"
    return f"{state.name:<6} | {flag:<10} | {status}"


def cmd_status(args: argparse.Namespace) -> int:
    lane_root, main_root = resolve_repo_roots(Path(args.lane))
    main_state = read_main_state(main_root, lane_root)
    worker_state = read_worker_state(worker=args.worker, remote_root=args.remote_root)
    queue = DispatchQueue(main_root / QUEUE_REL_PATH)
    entries = queue.list_entries()
    print(format_machine_line(main_state))
    print(format_machine_line(worker_state))
    if not entries:
        print("queue  | empty")
    else:
        print(f"queue  | {len(entries)} waiting:")
        for idx, item in enumerate(entries, start=1):
            print(f"  #{idx} {item.get('job')} | lane={item.get('lane')} | pid={item.get('pid')} | since={item.get('enqueued_at')}")
    return 0


def cmd_build(args: argparse.Namespace) -> int:
    lane_root, main_root = resolve_repo_roots(Path(args.lane))
    queue = DispatchQueue(main_root / QUEUE_REL_PATH)
    target_label = f"build {args.target}" if args.target else f"build {lane_root.name}"
    machine, _ = acquire_machine_for_job(
        job=target_label,
        lane_root=lane_root,
        main_root=main_root,
        read_main_fn=lambda: read_main_state(main_root, lane_root),
        read_worker_fn=lambda: read_worker_state(worker=args.worker, remote_root=args.remote_root),
        queue=queue,
    )
    cmd = build_command_for(machine, lane_root, args.target, args.config)
    res = subprocess.run(cmd, cwd=str(lane_root))
    if res.returncode != 0:
        report_failure_logs("build", machine, lane_root)
    return res.returncode


def cmd_smoke(args: argparse.Namespace) -> int:
    lane_root, main_root = resolve_repo_roots(Path(args.lane))
    queue = DispatchQueue(main_root / QUEUE_REL_PATH)
    job_label = f"smoke {args.smokes}"
    machine, _ = acquire_machine_for_job(
        job=job_label,
        lane_root=lane_root,
        main_root=main_root,
        read_main_fn=lambda: read_main_state(main_root, lane_root),
        read_worker_fn=lambda: read_worker_state(worker=args.worker, remote_root=args.remote_root),
        queue=queue,
    )
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    worker_result_dir = lane_root / "Saved" / "Logs" / "Worker" / f"dispatch-{stamp}-{os.getpid()}"
    cmd = smoke_command_for(
        machine=machine,
        lane_root=lane_root,
        smokes=args.smokes,
        smoke_map=args.map,
        jobs=args.jobs,
        worker=args.worker,
        worker_result_dir=worker_result_dir,
    )
    res = subprocess.run(cmd, cwd=str(lane_root))
    if res.returncode != 0:
        report_failure_logs("smoke", machine, lane_root, worker_result_dir=worker_result_dir)
    return res.returncode


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Dispatch CodexTactics builds/smokes onto main or worker.")
    sub = parser.add_subparsers(dest="command", required=True)

    p_status = sub.add_parser("status", help="Show main PC, worker, and dispatch queue status.")
    p_status.add_argument("--lane", default=".", help="Lane worktree path (default: cwd)")
    p_status.add_argument("--worker", default=DEFAULT_WORKER)
    p_status.add_argument("--remote-root", default=DEFAULT_REMOTE_ROOT)
    p_status.set_defaults(func=cmd_status)

    p_build = sub.add_parser("build", help="Run a lane build on whichever machine is free.")
    p_build.add_argument("--lane", default=".", help="Lane worktree path")
    p_build.add_argument("--target", default=None, help="Optional UBT target override")
    p_build.add_argument("--config", default=None, help="Optional build config override")
    p_build.add_argument("--worker", default=DEFAULT_WORKER)
    p_build.add_argument("--remote-root", default=DEFAULT_REMOTE_ROOT)
    p_build.set_defaults(func=cmd_build)

    p_smoke = sub.add_parser("smoke", help="Run lane smoke(s) on whichever machine is free.")
    p_smoke.add_argument("--lane", default=".", help="Lane worktree path")
    p_smoke.add_argument("--smokes", required=True, help="Comma-separated smoke names")
    p_smoke.add_argument("--map", default=None, help="Optional map path")
    p_smoke.add_argument("--jobs", type=int, default=None, help="Optional concurrency limit")
    p_smoke.add_argument("--worker", default=DEFAULT_WORKER)
    p_smoke.add_argument("--remote-root", default=DEFAULT_REMOTE_ROOT)
    p_smoke.set_defaults(func=cmd_smoke)

    return parser


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
