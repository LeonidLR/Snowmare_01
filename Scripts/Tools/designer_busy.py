"""Read-only, bounded admission priority for the designer's MAIN checkout.

No process termination or global mutex preemption: already-running work and the
race between this check and launch cannot be prevented by admission polling.
"""
import argparse
import json
import math
import os
from pathlib import Path
import re
import time

import editor_status

DEFAULTS = {"startup_grace_minutes": 15, "max_wait_minutes": 30, "poll_seconds": 20}
ROOT = Path(__file__).resolve().parents[2]


def load_config(path):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        data = {}
    result = DEFAULTS.copy()
    if isinstance(data, dict):
        for key, default in DEFAULTS.items():
            value = data.get(key, default)
            if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value):
                if value >= 0 and (key != "poll_seconds" or value > 0):
                    result[key] = value
    return result


def main_checkout(root=ROOT):
    common = editor_status._run(["git", "rev-parse", "--git-common-dir"], cwd=str(root), timeout=10)
    if not common:
        raise RuntimeError("cannot discover main checkout")
    path = Path(common)
    if not path.is_absolute():
        path = Path(root) / path
    main = path.resolve().parent
    if not (main / "CodexTactics.uproject").is_file():
        raise RuntimeError("main checkout project not found")
    return main


def list_processes():
    # Include every PID for job-owner liveness, and CreationDate for GUI startup age.
    script = ("[Console]::OutputEncoding=[Text.Encoding]::UTF8; "
              "@(Get-CimInstance Win32_Process -ErrorAction Stop | ForEach-Object { "
              "[pscustomobject]@{pid=$_.ProcessId;name=$_.Name;cmd=$_.CommandLine;"
              "started_epoch=([DateTimeOffset]$_.CreationDate).ToUnixTimeSeconds()} }) | ConvertTo-Json -Compress")
    out = editor_status._run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script], timeout=10)
    try:
        data = json.loads(out)
    except ValueError as exc:
        raise RuntimeError("process discovery unavailable") from exc
    if isinstance(data, dict):
        data = [data]
    if not isinstance(data, list) or not data:
        raise RuntimeError("process discovery unavailable")
    return data


def busy_reason(main, processes, now, grace_minutes):
    project = str(Path(main) / "CodexTactics.uproject").replace("\\", "/").lower()
    live = {p.get("pid") for p in processes}
    for proc in processes:
        cmd = (proc.get("cmd") or "").replace("\\", "/").lower()
        # Full project argument boundaries avoid prefix/filename lookalikes.
        matches = re.search(r'(?:^|[\s"])(?:-project=["]?)?' + re.escape(project) + r'(?=$|[\s"])', cmd)
        if ((proc.get("name") or "").lower() != "unrealeditor.exe" or not matches
                or re.search(r'(?:^|\s)["]?-(?:game(?=[\s"]|$)|run=)', cmd)):
            continue
        started = proc.get("started_epoch")
        if not isinstance(started, (int, float)) or not math.isfinite(started):
            return "editor start time unavailable"
        if now - started < grace_minutes * 60:
            return "editor starting"
    for path in (Path(main) / "Saved" / "LevelGen").glob("build_*.json"):
        try:
            status = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError):
            continue
        if (isinstance(status, dict) and status.get("schema") == "codex.job_status/1"
                and status.get("job") == "build_map" and status.get("state") == "running"
                and isinstance(status.get("pid"), int) and status["pid"] > 0
                and status["pid"] in live):
            return "levelgen build running"
    return None


def wait_for_designer(probe, config, *, environ=None, clock=time.monotonic,
                      sleep=time.sleep, log=print):
    if (os.environ if environ is None else environ).get("CODEX_IGNORE_DESIGNER") == "1":
        return False
    deadline = clock() + config["max_wait_minutes"] * 60
    next_log = clock()
    while True:
        try:
            reason = probe()
        except Exception as exc:
            reason = "detection unavailable: " + str(exc)
        if not reason:
            return False
        now = clock()
        if now >= deadline:
            log("[agent-priority] designer admission wait expired; continuing (" + reason + ").")
            return True
        if now >= next_log:
            log("[agent-priority] waiting for the designer's editor/build (" + reason + ")")
            next_log = now + 60
        sleep(min(config["poll_seconds"], deadline - now))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default=str(ROOT / "Scripts" / "agent_priority.json"))
    args = parser.parse_args()
    if os.environ.get("CODEX_IGNORE_DESIGNER") == "1":
        return
    config = load_config(args.config)

    def probe():
        return busy_reason(main_checkout(), list_processes(), time.time(), config["startup_grace_minutes"])

    wait_for_designer(probe, config, log=lambda line: print(line, flush=True))


if __name__ == "__main__":
    main()
