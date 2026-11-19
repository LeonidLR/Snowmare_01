# Live hand-over: main -> worker (2026-11-19 17:33)

Note: T-05 phase b WIP (accept on Crossroads/Alley x30), T-021 cold airtightness WIP, bgen bot mantle gating -> firing gaps -> S2.3, T-022/023 TG editing not started; night loop + guard running

## First steps for the receiving lead
1. `git fetch`; read this file, docs/port/LANES.json, docs/port/WORKER_LEAD_2026-11-20.md (worker) or the newest docs/port/CLAUDE_SESSION_PROMPT_*.md (main).
2. Main PC receiving: back up the designer's uncommitted files (Saved/Backups/designer-wip-*), then review + merge the hand-over branch `main` if it is not main; build main; restart the lanes listed below from their branches.
3. Worker receiving: work in C:\Work\CodexTactics-slot1 on a worker/lead-* branch; continue the lanes from their branches.
4. Update docs/port/LANES.json owners / states for the lanes you take, so the watchdog follows them.

## Lanes
| lane | state | owner | branch / head | switch |
|---|---|---|---|---|
| T-05 | waiting - main PC offline 11-20 >= 4 h: the stand-in lead on codex-worker continues from the branch (docs/port/WORKER_LEAD_2026-11-20.md) | lead Sonnet subagent | `claude/firing-positions` a1dae05a 11-19 17:33 wip(T-05): handover - phase a gated (0bf3fbd2), phase b partly effective, next step written | pushed claude/firing-positions a1dae05a |
| T-020 | done | lead Sonnet subagent | `claude/tg-map-management` fe21de93 11-19 17:04 docs(port): T-020 report + browser screenshots | done |
| T-021 | waiting - main PC offline 11-20 >= 4 h: the stand-in lead on codex-worker continues from the branch (docs/port/WORKER_LEAD_2026-11-20.md) | lead Sonnet subagent | `claude/cold-indoors` 01c6cb01 11-19 17:33 docs(port): T-021 report with handover (cold indoors, airtightness + draught; build/tests/smokes pending) | PUSH FAILED: To https://github.com/LeonidLR/Snowmare_01.git
 ! [remote rejected]   claude/cold-indoors -> claude/cold-indoors (cannot lock ref 'refs/heads/claude/cold-indoors': is at 01c6cb018bf8c778c88092762a58ac |
| bgen | waiting - main PC offline 11-20 >= 4 h: the stand-in lead on codex-worker continues from the branch (docs/port/WORKER_LEAD_2026-11-20.md) | session Генератор зданий local_93362388 | `claude/building-generator` 3a40a588 11-19 17:06 feat(bot): route mode mantles a leg over a mantle edge (climb click on the top, squad in turns) | pushed claude/building-generator 3a40a588 |
| worker-night | active | worker: C:/Work/NightLoop guard, task CodexTactics Night Guard | ``  | not on this machine |
| T-022/023 | waiting - queued: brief docs/port/claude/inbox/T022-tg-map-edit-fullscreen.md | not started - stand-in lead (Sonnet) or main lead | `claude/tg-map-edit`  | not on this machine |
