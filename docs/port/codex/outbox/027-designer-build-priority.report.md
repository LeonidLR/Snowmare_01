# 027 - Designer build admission priority

Status: WIP limit stop. Implementation and 11 focused unit tests passed; source/test checkpoint182db232. Real-build completion remains PENDING.

## Changed paths

- Scripts/agent_lock.ps1: defines explicit Wait-DesignerPriority; Enter-AgentLock unchanged.
- Scripts/build.ps1: guard directly before UBT Build.bat launch.
- Scripts/test.ps1: guard directly before automation editor process.
- Scripts/smoke_parallel.ps1: guard inside Start-SmokeProcess, covering every batch, retry and exclusive launch.
- Scripts/Tools/designer_busy.py: read-only detector and bounded wait.
- Scripts/agent_priority.json: startup_grace_minutes=15, max_wait_minutes=30, poll_seconds=20.
- Scripts/Tools/tests/test_designer_busy.py: fake processes/status files and virtual-clock tests.
- docs/port/codex/outbox/027-designer-build-priority.report.md: this report.

## Protocol

Git common-dir identifies the MAIN checkout. Reuses editor_status._run without modifying editor_status.py; a read-only CIM snapshot adds creation epoch to pid/name/cmd. Only exact UnrealEditor.exe with the full main CodexTactics.uproject argument and no -game/-run= blocks during its configurable startup grace. Settled GUI editors, other worktrees and commandlets do not block.

Only MAIN Saved/LevelGen/build_*.json is read, with UTF-8 BOM tolerance. Actual job_status.ps1 schema is codex.job_status/1, job build_map, state running, pid the script owner. Running requires a live owner PID in the same snapshot. Dead/stale/completed/waiting_lock/malformed records are ignored. No main files are written and no process is terminated.

Busy detection polls every20 seconds, printing the requested waiting message at most once per minute. At30 minutes it warns and continues. Unknown discovery/CIM/start-time detection follows the same bounded wait. Missing or invalid config fields use defaults. If Python/helper cannot start or finishes unsuccessfully, PowerShell warns once and continues. CODEX_IGNORE_DESIGNER=1 exits before discovery/config/probing. Idle/override success is silent. Standard worktree locks remain active; no inherited-lock bypass was added.

## Validation

`python -B -m unittest discover -s Scripts/Tools/tests -p test_designer_busy.py -v`: 11/11 PASS, 0.150s. Cases cover exact main versus worktree project, quoted/unquoted game/run flags, explicit quoted/unquoted -project= arguments, exact project-path boundaries, commandlet exclusion, settled editor, creation-time unknown, running/live/BOM job, dead/completed/malformed/other-checkout jobs, common-dir resolution, config defaults/BOM, silent idle/override, busy-to-idle, bounded timeout, once-per-minute log cadence and unavailable detection. Virtual clock means no real waits.

PowerShell syntax validation: 4/4 PASS. Actual read-only preflight: main checkout resolution succeeded; CIM returned 636 processes; busy reason was None (idle). No process command lines were printed, no wait/override was invoked, and main remained read-only.

After parent review and checkpoint182db232, the actual build.ps1 run started while the real probe reported designer idle. No added priority output was observed; UBT reached20/35 actions. At the designer10%-remaining threshold, only the owned agents-project build process tree was stopped. This is NOT a successful full-build result. Re-run the real build after full reset and verify exit0 / no priority messages before marking this task done.

## Limits

Admission polling cannot preempt already-running UBT/editor jobs or guarantee hard priority across the check-to-launch race or UE's global mutex. A designer starting mid-smoke batch pauses subsequent process admission; already-running smokes continue. Stale running status with a reused live PID may wait until the configured deadline. Unknown detection can delay admission until that deadline, then continues. No central Enter-AgentLock auto-priority: Training Ground levelgen must not wait on its own running record.

## Limit checkpoint

Stopped at10% remaining; no further work until the full reset at2026-11-16 18:08 Asia/Jerusalem (16:08 UTC). Source/helper/config/tests saved at182db232; 11/11 Python tests and4/4 PowerShell parse checks passed. The one real build was interrupted before completion; Saved/Logs/codex027-real-build.txt and Build.log retain partial evidence. Next: rebuild/finish027, then028 and029 in order. Standard locks and designer files remain untouched; a dead owned build lock, if present, is handled by existing stale-lock logic. No CODEX_IGNORE_DESIGNER override was used.
