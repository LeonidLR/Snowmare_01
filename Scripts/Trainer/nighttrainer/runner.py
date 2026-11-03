"""Runs arena jobs: parallel game processes with timeouts / retries, plus the guards that make the trainer polite.

Guards (checked before every wave of parallel jobs):
  - STOP file in the run dir (or the deadline)  -> stop cleanly (the run resumes later from the cached jobs);
  - PAUSE file in the run dir or Saved/Trainer/  -> wait;
  - Saved/agent.lock held by a live process (Scripts/agent_lock.ps1 convention) -> wait; while a wave runs the trainer
    holds the lock itself (agent "night-trainer") so a build does not fight the game processes for Binaries/;
  - --yield-to-editor: a CodexTactics editor is open (UnrealEditor command line with \\CodexTactics\\CodexTactics.uproject
    and neither -run= nor -game) -> wait.
A job whose process crashes / times out / writes no parsable result (after retries) becomes penalised crash rounds.
"""
from __future__ import annotations

import datetime as _dt
import json
import os
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor

from .adapter import ArenaAdapter, ArenaResultError
from .fitness import crash_round
from .genome import load_json, write_json


def is_codex_editor_cmdline(cmdline: str | None) -> bool:
    if not cmdline:
        return False
    c = cmdline.replace("/", "\\").lower()
    return "\\codextactics\\codextactics.uproject" in c and "-run=" not in c and "-game" not in c


def list_unreal_cmdlines() -> list:
    if os.name != "nt":
        return []
    ps = ("Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | "
          "ForEach-Object { $_.CommandLine }")
    try:
        out = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", ps],
                             capture_output=True, text=True, timeout=60)
        return [line for line in out.stdout.splitlines() if line.strip()]
    except (OSError, subprocess.SubprocessError):
        return []


def pid_alive(pid) -> bool:
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return False
    if pid <= 0:
        return False
    if os.name == "nt":
        import ctypes
        handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        ctypes.windll.kernel32.CloseHandle(handle)
        return code.value == 259  # STILL_ACTIVE
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def kill_tree(proc: subprocess.Popen) -> None:
    try:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], capture_output=True, timeout=60)
        else:
            proc.kill()
    except (OSError, subprocess.SubprocessError):
        pass


class BuildLock:
    """Python side of Scripts/agent_lock.ps1 (Saved/agent.lock JSON: agent, purpose, pid, manual, started)."""

    def __init__(self, repo_root: str):
        self.path = os.path.join(repo_root, "Saved", "agent.lock")

    def holder(self):
        try:
            lock = load_json(self.path)
        except (OSError, ValueError):
            return None
        if lock.get("manual") or pid_alive(lock.get("pid")):
            return None if int(lock.get("pid") or 0) == os.getpid() and not lock.get("manual") else lock
        return None  # stale

    def acquire(self) -> bool:
        if self.holder():
            return False
        write_json(self.path, {"agent": "night-trainer", "purpose": "night-trainer arena games", "pid": os.getpid(),
                               "manual": False, "started": _dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")})
        return True

    def release(self) -> None:
        try:
            lock = load_json(self.path)
            if int(lock.get("pid") or 0) == os.getpid() and not lock.get("manual"):
                os.remove(self.path)
        except (OSError, ValueError):
            pass


class Guard:
    def __init__(self, run_dir: str, repo_root: str, deadline: float | None, check_lock: bool, hold_lock: bool,
                 yield_to_editor: bool, on_status=None, poll_s: float = 30.0):
        self.run_dir, self.repo_root, self.deadline = run_dir, repo_root, deadline
        self.check_lock, self.hold_lock, self.yield_to_editor = check_lock, hold_lock, yield_to_editor
        self.lock = BuildLock(repo_root)
        self.on_status = on_status or (lambda state, note="": None)
        self.poll_s = poll_s
        self.stopped_reason = None

    def stop_requested(self) -> bool:
        if os.path.exists(os.path.join(self.run_dir, "STOP")):
            self.stopped_reason = "STOP file"
        elif self.deadline is not None and time.time() >= self.deadline:
            self.stopped_reason = "time budget / stop-at reached"
        return self.stopped_reason is not None

    def _blocker(self):
        for p in (os.path.join(self.run_dir, "PAUSE"), os.path.join(self.repo_root, "Saved", "Trainer", "PAUSE")):
            if os.path.exists(p):
                return "paused", f"PAUSE file {p}"
        if self.check_lock:
            h = self.lock.holder()
            if h:
                return "waiting-lock", f"build lock held by {h.get('agent')} ({h.get('purpose')})"
        if self.yield_to_editor and any(is_codex_editor_cmdline(c) for c in list_unreal_cmdlines()):
            return "waiting-editor", "CodexTactics editor is open"
        return None

    def wait_clear(self) -> bool:
        """Blocks while paused / locked / editor open. False = stop (STOP file or deadline)."""
        while True:
            if self.stop_requested():
                return False
            blocker = self._blocker()
            if not blocker:
                if self.hold_lock and not self.lock.acquire():
                    blocker = ("waiting-lock", "build lock taken by someone else")
                else:
                    self.on_status("running")
                    return True
            self.on_status(*blocker)
            time.sleep(self.poll_s)

    def end_wave(self) -> None:
        if self.hold_lock:
            self.lock.release()


class JobRunner:
    def __init__(self, adapter: ArenaAdapter, arena_cfg: dict, parallel: int, mock_in_process: bool = False,
                 mock_cfg: dict | None = None, log=print):
        self.adapter, self.cfg, self.parallel = adapter, arena_cfg, max(1, int(parallel))
        self.mock_in_process, self.mock_cfg, self.log = mock_in_process, mock_cfg, log

    def run_jobs(self, jobs: list, guard: Guard, on_job_done=None):
        """Runs every job whose cached result is missing. Returns {job_id: rounds} or None when stopped."""
        results, todo = {}, []
        for job in jobs:
            cached = job["dir"] and os.path.join(job["dir"], job["id"] + ".rounds.json")
            if cached and os.path.exists(cached):
                results[job["id"]] = load_json(cached)["rounds"]
            else:
                todo.append(job)
        for i in range(0, len(todo), self.parallel):
            if not guard.wait_clear():
                return None
            wave = todo[i:i + self.parallel]
            try:
                with ThreadPoolExecutor(max_workers=len(wave)) as pool:
                    for job, rounds in zip(wave, pool.map(self.run_one, wave)):
                        results[job["id"]] = rounds
                        if on_job_done:
                            on_job_done(job)
            finally:
                guard.end_wave()
        return results

    def run_one(self, job: dict) -> list:
        os.makedirs(job["dir"], exist_ok=True)  # the arena / mock writes its Out file here
        out_path = os.path.join(job["dir"], job["id"] + ".arena.json")
        log_path = os.path.join(job["dir"], job["id"] + ".log")
        attempts = 1 + int(self.cfg.get("retries", 1))
        error = "not run"
        for attempt in range(attempts):
            try:
                rounds = self._attempt(job, out_path, log_path)
                write_json(os.path.join(job["dir"], job["id"] + ".rounds.json"),
                           {"job": {k: v for k, v in job.items() if k != "dir"}, "attempt": attempt + 1,
                            "rounds": rounds})
                return rounds
            except (ArenaResultError, OSError, ValueError, subprocess.SubprocessError, RuntimeError) as e:
                error = f"{type(e).__name__}: {e}"
                self.log(f"  job {job['id']} attempt {attempt + 1}/{attempts} failed: {error}")
        rounds = [crash_round() for _ in range(int(job["rounds"]))]
        write_json(os.path.join(job["dir"], job["id"] + ".rounds.json"),
                   {"job": {k: v for k, v in job.items() if k != "dir"}, "attempt": attempts, "error": error,
                    "rounds": rounds})
        return rounds

    def _attempt(self, job: dict, out_path: str, log_path: str) -> list:
        if os.path.exists(out_path):
            os.remove(out_path)
        if self.mock_in_process:
            from . import mock_arena
            doc = mock_arena.simulate(load_json(job["weights_a"]), job["opponent"],
                                      load_json(job["weights_b"]) if job.get("weights_b") else None,
                                      load_json(self.mock_cfg["reference_weights"]), int(job["rounds"]),
                                      int(job["seed"]), float(self.mock_cfg.get("bot_strength", -0.03)),
                                      track_prefix=(os.path.splitext(out_path)[0]
                                                    if self.mock_cfg.get("tracks", True) else None))
            with open(out_path, "w", encoding="utf-8") as f:
                json.dump(doc, f)
            return self.adapter.parse_result(out_path, int(job["rounds"]))
        started = time.time()
        cmd = self.adapter.build_command(job, out_path, log_path)
        with open(log_path + ".stdout", "w", encoding="utf-8", errors="replace") as so:
            proc = subprocess.Popen(cmd, stdout=so, stderr=subprocess.STDOUT, cwd=self.adapter.repo_root,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                code = proc.wait(timeout=float(self.cfg.get("timeout_s", 1800)))
            except subprocess.TimeoutExpired:
                kill_tree(proc)
                raise RuntimeError(f"timeout after {self.cfg.get('timeout_s', 1800)} s")
        found = self.adapter.find_output(out_path, started)
        if not found:
            raise RuntimeError(f"exit code {code}, no result JSON")
        if found != out_path:
            os.replace(found, out_path)  # adopt the arena's own Saved/Arena/<timestamp>.json
        return self.adapter.parse_result(out_path, int(job["rounds"]))


def now_iso() -> str:
    return _dt.datetime.now().isoformat(timespec="seconds")
