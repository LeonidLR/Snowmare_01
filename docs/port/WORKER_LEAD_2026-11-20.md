# Claude on codex-worker: stand-in lead while the main PC is offline (2026-11-20)

The designer travels on 11-20: the MAIN PC is OFFLINE for at least 4 hours, the designer is unreachable until after lunch.
You run ON the worker (codex-worker), in `C:\Work\CodexTactics-slot1`. Reply to the designer in Russian.

Read first: CLAUDE.md, MEMORY.md (current-state, focus-bgen-and-enemies, smart-enemies-decisions-2026-11-19, lead-not-implementer,
argue-dont-agree), docs/design/gdd/smart-enemies-roadmap.md (sections 5, 5a, 6), docs/port/TASK_TEMPLATE.md,
docs/port/CLAUDE_SESSION_PROMPT_2026-11-19c.md (the main lead's state), the newest docs/port/claude/inbox/*.md.

## Hard rules (the main PC stays primary)
- Work ONLY on branch `worker/lead-2026-11-20` (created from origin/main). Push ONLY that branch. NEVER push main, never force-push,
  never touch other branches. The main-PC lead reviews and merges it when the main PC is back.
- Nothing from the worker is copied to the main PC. No edits outside this checkout. No login / credential files anywhere in git.
- You are the only user of this checkout today: worker_sync / smoke runs from the main PC are off. Do not start the night trainer
  (STOP file stays). Do not delete or rebuild the other slots / C:\Mirror.
- Economy: Sonnet subagents write code (Agent subagent_type "ue-l3-major", model "sonnet"), one at a time; you review.
- Behaviour changes are accepted only on repeats: 20-50 seeds or at least 10 repeats, never one green run.

## Build and test here
- Build: `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1` (UE 5.8 on this machine). Tests: `Scripts/test.ps1 -Filter ...`.
- Smokes: `Scripts/Tools/smoke_parallel.ps1` with FULL command names; maps outside test_map.json: UnrealEditor-Cmd directly
  (example: C:\Users\User\anchors_run.ps1). Day-loop digest: `python Scripts/Tools/dayloop_digest.py`.

## Plan, in order
1. `git fetch origin; git switch -c worker/lead-2026-11-20 origin/main`; build; run PmcCrossroadsSmoke x30 and PmcAlleySmoke x30
   (baseline digest).
2. Smart enemies T-03 (scripted opponent): if the main lead's Sonnet branch for T-03 is on origin, continue from it; else start it
   from docs/port/claude/inbox/T003-scripted-opponent.md. Build, tests, smokes, digest.
3. T-05 firing positions (roadmap 6; the main defect: defenders not in position at contact, bunched, first-shot gap p50 10 s vs
   <= 1.5 s). Lead designs the change in a short note docs/port/claude/outbox/T05-design.md, Sonnet writes it; accept on 30 seeds of
   Crossroads + Alley.
4. Do NOT start the machine-gun nest (queued after T-05) and do NOT merge the building generator.

## Every hour and at the end
Commit + push the branch. Keep `docs/port/claude/outbox/WORKER_LEAD_2026-11-20.report.md` current: what was done (commits), digest
numbers before / after, what is unfinished, what the main lead must review. When the 5-hour quota runs out: commit, push, write the
report, stop.
