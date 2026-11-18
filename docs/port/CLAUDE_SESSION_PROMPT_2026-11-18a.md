# Claude (lead) resume, 2026-11-18 morning (supersedes 17d)

Read: CLAUDE.md, MEMORY.md (pipeline-decisions-2026-11-17, designer-decisions-2026-11-17, no-unasked-rule-changes, grok-budget,
paused-2026-11-18-night), docs/port/WEEK_PLAN_2026-11-17.md (B2), docs/design/TACTICAL_MAP.md, docs/design/TURRET_POWER.md,
TASK_BOARD (B17 + R17), docs/port/RESOURCES.md. Start the chat listener in the background
(`python Scripts/Tools/agent_chat.py wait --as claude --timeout 3600`), restart after each message. Morning: get_usage.
Rules: API Opus review before every merge of agent work (Scripts/Tools/api_task.py, --effort low, --max-tokens 10000). Builds
Scripts/build.ps1 -Remote worker (exit 2 -> local). Long commands via Scripts/Tools/run_hidden.ps1 (no Terminal windows). Never build in
main while the designer's editor is open (check the UnrealEditor command line).

## State
main 4db03229 (pushed): turret power phase 1 82190744, reds-1117 (NoCyrillic text, PauseBarrelOrderSmoke, TutorialSmoke), R17-1/2/3/5/6,
tactical map P1-P3, G002 QD, G003/G003b props, G006 first aid drag. Main was NOT rebuilt for the designer after the evening merges.

## Night results (copied to Saved/Reports/worker-2026-11-18/)
- Sweep 00:00-02:00 on 34a00a48: 296 run, 9 failed: GeneratedPropsX6, CoverCrouch, PauseBarrelOrder*, Tutorial*, PeekMines, Suppression,
  LevelGenRamp, CorpseCost, FormationMantle (* fixed in 4db03229). Flaky once: Rain, SightConditions.
- QD trial pmc-v6-qd 06:00-08:09 (X4 tactical): 5 generations, champions 6.9 -> 11.2 -> 4.2 (fresh re-eval swings a lot), archive 2/48
  every generation (behaviour space not explored -> Antigravity 083). 4 highlight clips (PMC flank + suppress + ambush + retreat + heal);
  report.html in pmc-v6-qd/. Send 1-2 clips to the designer's phone (SendUserFile) and decide ga vs qd for tonight after 083.
- Worker 08:09 daily subset: 18 smokes, only LevelGenRampSmoke FAIL (-> Grok G007).

## Today's goal (designer)
He tests the level generator with ALL assets (mines, barrels, loot, turrets, generators) and wants danger places to affect gameplay and
feed the trainer. So first: GeneratedPropsX6Smoke red (Claude, own subagent), FormationMantleSmoke red (R17-1 follow-up), then rebuild
main when his editor is closed and update the banner note.

## Lanes
| Who | Task | Where |
|---|---|---|
| Claude subagents | GeneratedPropsX6Smoke, FormationMantleSmoke; then R17-4 vertical scan, turret power phase 2 | CodexTactics-claude / -tactical |
| Codex (a few hours, 10 % rule) | 044 CoverCrouch + Suppression + PeekMines reds | CodexTactics-agents |
| Antigravity (gemday) | 082 props vs rotated buildings (TS) -> 083 QD archive coverage (Python) | CodexTactics-gemday |
| Grok (~17 %/day, one lane, build-fast for L1-2) | G007 ramp fight (LevelGenRampSmoke) -> G005 short orders | CodexTactics-grok |
| API Sonnet | gap filler only, with --budget-usd | - |
| G004 corpse cost | diagnosis only (1.8 ms outside Corpse*); next = Unreal Insights capture, Opus, later | - |

## Queue after
Turret power phases 2+ (sector / ammo, siphon, generator damage, cables, PMC behaviour, density); Medic order loss after group arrival
(design question); 42 + 072 jump-down; evening phone report; TASK_BOARD done-list each merge; trainer freeze commit in topic trainer-night
before 16:30.
