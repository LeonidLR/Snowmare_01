# 027 - The designer's work goes first in the build queue (Claude, 2026-11-16) - MEDIUM, tooling only
Why: today the designer's «Собрать в Unreal» (Training Ground) waited 13 min and his editor start hung at "Turnkey SDK detection"
while agents built in other worktrees (UE's global UBT/UAT mutex is shared by all worktrees). The designer must never wait for agents.
Branch codex/027-designer-priority (from main), worktree CodexTactics-agents.
Do:
1. Read Scripts/agent_lock.ps1 (shared lock), Scripts/build.ps1, Scripts/test.ps1, Scripts/smoke_parallel.ps1 (how they take the lock)
   and Scripts/Tools/editor_status.py (detects the designer's editor: UnrealEditor.exe with the MAIN checkout's .uproject, no -game / -run=).
2. Add a "designer busy" check used by build.ps1 / test.ps1 / smoke_parallel.ps1 BEFORE they start UBT or an editor process:
   designer busy = an UnrealEditor.exe of the MAIN checkout without -game/-run= is STARTING (process younger than N minutes, from
   config) OR a levelgen build started by Training Ground is running (Saved/LevelGen/build_*.json with state "running" in the main
   checkout) . While busy: wait (poll every 20 s, print one line per minute "waiting for the designer's editor/build") up to M minutes,
   then continue. An already-open, settled designer editor does NOT block agents (they build in their own worktrees).
   N, M and the poll interval in a small JSON (e.g. Scripts/agent_priority.json). Env override CODEX_IGNORE_DESIGNER=1 for emergencies.
3. Unit test (Scripts/Tools/tests/, unittest style) for the busy rule with fake process lists / status files.
Allowed paths: Scripts/agent_lock.ps1, Scripts/build.ps1, Scripts/test.ps1, Scripts/smoke_parallel.ps1, a new helper
Scripts/Tools/designer_busy.py (+ test), Scripts/agent_priority.json, report docs/port/codex/outbox/027-designer-build-priority.report.md.
Check: the unit test; one real build.ps1 run in your worktree prints nothing extra when the designer is idle. 10 % limit rule.
