# Gemini 123 (Hydrogen) - "Машины" tab: progress bar + time left for long jobs (designer 2026-11-25) - after 119

Designer: "add a progress scale to the Machines tab so I roughly see how much time is left". Tools only (Python + the chat page);
no C++, no maps. Branch gemini/123-machines-eta, worktree C:/Work/Lanes/CodexTactics-123.
Files: Scripts/Tools/machine_tasks.py (collects the items per machine), Scripts/Tools/agent_chat_server.py (/api/machines),
Tools/AgentChat/index.html (tab "Машины": .mach-bar / .mach-fill already exist, today mostly indeterminate).
Jobs to cover, each item gets {stage, done, total, started, eta_s} where known:
1. Merge gate (Scripts/Tools/merge_gate.ps1): it already calls Set-GateStage (Merge / Build / Tests / Worker smokes / Push). Make it also
   write Saved/Logs/merge-gate-progress.json {stage, stage_index, stage_count, stage_started, gate_started}. ETA per stage = median of the
   last 5 runs of that stage (keep a small history file Saved/Logs/merge-gate-history.jsonl written at gate end). Do NOT change the gate's
   logic or exit codes - only add the progress / history writes (wrapped in try/catch so they can never fail the gate).
2. Remote build (Scripts/Tools/build_remote.ps1): phases sync / compile / DLLs back; UBT prints "[N/M]" lines - done/total from them;
   ETA from Saved/Logs/RemoteBuilds.jsonl history (median door-to-door).
3. Smoke batches (Scripts/smoke_parallel.ps1 local or -Remote): smokes done / total; ETA = sum of the remaining smokes' last durations
   (SmokeParallel-*.json history) / jobs.
UI: a filled bar with "Сборка 45/84 · ~6 мин осталось" (Russian), indeterminate only when nothing is known; colours as now.
Tests: pytest for the ETA math (median, missing history -> no ETA, never negative) and the progress-file reader (missing / stale file >
10 min old = ignored). Screenshot of the tab with a fake progress file in the report. Never run a real gate.
Checklist #1104; stage only your files by path. Report docs/port/gemini/outbox/123-machines-eta.report.md + "123 FINISH | ..." in chat. Never push.
