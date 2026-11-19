# Claude (lead) resume, 2026-11-19 ~08:30 (supersedes 19a and 18b)

Read first: CLAUDE.md, MEMORY.md (start with `current-state`, then grok-budget, backup-before-merge-main, main-pc-format-mirror,
worker-load-jobs, no-unasked-rule-changes), docs/port/TASK_TEMPLATE.md (inbox template + lead review checklist - binding),
TASK_BOARD "Сделано (2026-11-19)". Reply to the designer in Russian. Designer is away most of 11-20: gameplay decisions go to ONE
evening list; Antigravity cannot be woken without him.

## UPDATE 11-19 ~09:00 (read this first)
- WEEKLY limit was NOT reset: get_usage shows "Weekly - all models" 96 % (reset 11-21 09:00 UTC); only the 5-hour window was fresh.
  Team stop at 98 % still applies -> lead does only critical merges / reviews until the weekly reset shows.
- SMART ENEMIES re-plan (designer): a separate FABLE session runs docs/port/SMART_ENEMIES_FABLE_PROMPT_2026-11-19.md (GDD
  docs/design/gdd/smart-enemies.md + audit + roadmap). No new PMC / trainer / G008b tasks until the designer approves that roadmap;
  then the lead dispatches from it. docs/port/TRAINER_SESSION_PROMPT_2026-11-19.md is superseded by the Fable prompt.
- G008b parked: branch grok/G008b-overwatch-hold, WIP d38a1289 (spot snap to the parapet, still 0 overwatch kills) - not mergeable.
- Bgen: R3 done 94c14f5f (builder interior walls from partitions[]), stopped at 96 % (new bgen session id local_93362388).
- Still waiting: Codex 048 FINISH (if the designer woke it), Antigravity 088 plan (if woken), worker-ops 'ready' for the mirror.

## Mode and budgets (designer 2026-11-19 08:00 'поехали')
- Claude subscription limits were RESET by the designer -> NORMAL pace for a long autonomous week: lead + bgen session together
  ~18-20 %/day so it lasts to ~Tue 11-24 (get_usage each morning; the app may lag after a reset). The lead implements again
  (building-generator C++ when no agent is free), and tests / reviews every hand-over.
- NO Anthropic API tokens left -> no api_task.py reviews, no claude_api_agent.ps1. Review = your own read of the diff + the touched
  functions (optionally a Sonnet subagent), verify every finding in the code before applying, run the task's smokes yourself.
- Grok: 70 % used at 08:30 11-19 -> ~7 %/day to Mon 11-23: one task per day after G008b, grok-4.7-build-fast where it fits.
- Codex: limit reset 11-19 morning, ~5 h window (10 % rule) - worktree CodexTactics-agents.
- Gemini API: HTTP 402 (prepayment depleted) - Gemini CLI unusable. Antigravity (IDE, designer wakes it) = TS / Python implementer.

## State (main 3d265680, pushed; built ~06:00 + give-way b18 merge 0010ea3a built? -> build main when the editor is closed)
Merged since 11-18 evening: Codex 046 / 047, HitchProbe, Antigravity 085b, 087 PMC posts + charges (opt-in), worker night guard,
give-way v1 / v2 / v3 + 'spot never under a flight' (L_Gen_BgenTest_X6 LevelGenRampSmoke solo 4/17 -> 15-16 of 16 counted legs),
ramp / route smokes cold off. Designer's uncommitted X6 files in main (9 files) - never overwrite; backup of 08:18 in
Saved/Backups/designer-wip-20261119-0818 (autostash trap: restore byte-exact, see memory backup-before-merge-main).

## In flight
| Who | What | Next for the lead |
|---|---|---|
| Grok (CodexTactics-grok, session G008b) | G008b-4: the posted overwatch sniper fires from the roof EDGE (spot at the parapet or step to the rim), lip_cm 200 stays (round 2 shot through obstacles - rejected; round 3 safe but 0 shots + PmcRoofDefence spread 4.6 FAIL) | review (no API): diff, X4 seeds 11 / 33 shots from post > 0 + overwatch_kills > 0, PmcRoofDefence / Stalemate / ArenaTactical / Arena / Cover / CornerHold / PeekMines PASS -> merge |
| Codex (CodexTactics-agents, branch codex/048-buildings-smoke) | 048 LevelGenBuildingsSmoke (P0b: per enterable building, complete navmesh paths from every exterior door to rooms / landings / roof, JSON report) + LevelGenRampSmoke probe fix (upper flights, +-60 cm z window) | FINISH topic codex-048 -> review + run on L_Gen_BgenTest_X6 and old L_Gen_Depot_1_X6 -> merge |
| Antigravity (CodexTactics-gemday, needs the designer to paste its prompt) | 088 props route clearance (inbox 088): no mines / tripwires / blocking props on the mandatory route (fixes LevelGenRouteSmoke red since auto props) | approve its PLAN in topic gemini-088; after merge rebuild Depot_1_X6 / X4 props + run LevelGenRouteSmoke |
| Worker-ops session (local_83f5d1d5) | main-PC mirror (claude/main-pc-mirror ef26ba3d, secret-free; credentials moved on the designer's direct order); worker reboot 08:45 for VS 2026, then checklist lines 2 (build local) + 3 (smokes local) | merge after 'ready ... lines 2-3 PASS'; it then installs the 2 h schedule |
| Bgen session (local_84047e7a, branch claude/building-generator head 6dc78810) | stopped at 95 % before the reset; next: refactor #3 (builder walls from partitions[]), S2 strip grammar (S2_ROOMS_PLAN 5 + 8.1, designer: go); approved by the lead: approach box 200 + core wall (done 20b02316) | tell it the limits were reset -> continue at the shared pace; merge to main only after the DESIGNER'S test, then rebuild L_Gen_Train_1..3 maps |

## Queue (in order)
Grok, one per day: G008b-4 -> **guard_share None** (worker night report 11-19: the G008 access-guard role never resolves on X4 tactical,
1776 rounds) -> **B_x1_8** last flight (lead trace 11-19: leader's U-shaped switchback path, give-way spots land on its return leg beyond the
look-ahead; logs CodexTactics-claude/Saved/Logs/b18-trace2.log, b18-solo.log). If Grok runs out, these go to Codex.
Codex after 048 (within its window): PMC spawn at post (C++ consumer of 087: PmcEncounterRules already reads `spawn: [x,y,z]`; read
`post` {pos, yaw, kind}).
Antigravity after 088: 084 (weather per map / map name / props regen), 087 follow-ups (all posts' retreat lines, pmc_charges de-dup,
tuning key), QD mobility axis recalibration (worker report: edges 16 / 24 / 32 unreachable on X4, only 24 cells fillable), 085b shared
endpoint tolerance.
Lead: reviews + merges; worker report items (clip texture streaming ~1 s, world-space debug labels in clips) -> worker-ops.

## Open designer items (one evening list)
1. Building generator: test the TG page + a built map -> lead merges claude/building-generator -> rebuild training arena maps.
2. Worker: PowerShell execution policy (undefined; main PC CurrentUser = RemoteSigned) - his call; fresh OAuth sign-ins on the worker
   (Claude / Codex / Gemini / Grok).
3. Grok /usage each morning.

## Traps (11-18 / 19)
- Git Bash + UnrealEditor-Cmd: `MSYS_NO_PATHCONV=1` or `/Game/...` becomes `C:/Program Files/Git/Game/...`.
- smoke_parallel.ps1 cannot run a map other than test_map.json's -> UnrealEditor-Cmd directly for L_Gen_BgenTest_X6.
- `git pull --rebase --autostash` in main rewrote the designer's files LF -> CRLF: back up first, restore byte-exact (cmp).
- Never two `agent_chat.py wait --as claude` (it eats messages); stop restarting it when no agent is active.
- Agents launched from this session's background shell die with the session (HitchProbe 11-18).
