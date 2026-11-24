# Gemini 102-la-reward-fix

Assigned by Lead 2026-11-24 ~15:20. Follows your 083 diagnosis (d5fc9b7e). Code on a branch from origin/ai/la-probe
(afade329): gemini/102-la-reward-fix, worktree under C:/Work/Lanes.

Do (one change per hypothesis, each behind a CVar / config field so the night run can A/B them):
1. Cause 1: a fallen agent keeps his team's RoundWon / PointCaptured reward (PmcPolicyTraining.cpp ~453): credit at
   round end to every agent of the team, dead ones included (keep OwnDeath -1). Default = new behaviour.
2. Cause 3: bigger PPO batches (your measured micro-batches of ~6 episodes) - the episodes-per-iteration setting.
3. Cause 2 (mask): only a switch Codex.Pmc.Policy.LooseMask (default 0) that also allows Suppress / TakeCover / FallBack
   before contact; do not change the scorer.
Check: build, -Smart tests, a 10-min local probe per change is NOT allowed on the main PC (designer plays) - run the probe
on the worker slot2 (worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot2 -ShipHead), short, before 16:30.
Report: what changed, probe reward curve + action share (Hold/Ambush %), the exact command for tonight's run.
Rules: docs/port/TANDEM.md 'WORK PROCESS'. Report in outbox + chat FINISH. Never push.
