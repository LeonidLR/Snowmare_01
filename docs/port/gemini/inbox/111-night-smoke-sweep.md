# Gemini 111-night-smoke-sweep (Hydrogen, night 2026-11-24/25)

Goal: by 07:30 a short, trustworthy health picture of origin/main for the Lead's morning merges.
1. Wait until origin/main contains "Merge codex/107-tg-ai-training-tab" (the Lead pushes the sectors merge tonight; check every 15 min
   with git fetch; if it is not there by 02:00, use origin/main as it is and say so).
2. Run ALL smokes of Scripts/Tools/worker_daily_smokes.json (your 108 list, 23 smokes on the 5 kept maps) + SectorBoundarySmoke on the
   worker, slot1, -Jobs 2, after 23:30 (Codex and Grok use slot2 tonight; if slot1 is busy, wait - never kill another agent's run):
   worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot1 -Ref origin/main -Jobs 2 -Commands <list>
3. Every FAIL: rerun once alone; then classify NEW (not failing on the previous night's / today's logs) vs PRE-EXISTING (show the
   earlier log line) vs FLAKY (passes alone). Logs via typesafe_triage.py / digest tools, never pasted whole.
4. Report docs/port/gemini/outbox/111-night-smoke-sweep.report.md: one table (smoke, result, class, one-line cause), plus chat FINISH.
Checklist #1104 applies. No code changes in this task.

ADDED by Lead 20:55: LakePauseOrdersSmoke FAILS on main now ("Smoke FAIL: Medic-Sapper runs the planned pause order", log Saved/Logs/Worker/20261124-195825) but PASSED on 2026-11-19 (worker sweeps). Not from the sectors merge (sector code is X6-only). Find the commit: bisect on the worker between the 11-19 PASS (sweep-1119-be5f1ece) and origin/main, building on the worker only if needed (worker_sync -ShipRef <sha> -Build), slot1, Jobs 1. Report the first bad commit + the exact cause line. No fix in this task.
