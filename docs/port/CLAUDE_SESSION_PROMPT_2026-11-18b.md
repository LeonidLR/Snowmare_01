# Claude (lead) resume, 2026-11-18 evening (supersedes 18a)

Read: CLAUDE.md, MEMORY.md (worker-load-jobs, pipeline-decisions-2026-11-17, grok-budget, no-unasked-rule-changes, codex-light-tasks),
docs/design/PMC_DEFENCE.md, docs/design/levelgen/BUILDING_GENERATOR.md (sections 1, 2, 5, 6, 7), TASK_BOARD "Сделано (2026-11-18)".
Start the chat listener in the background (`python Scripts/Tools/agent_chat.py wait --as claude --timeout 3600`), restart it after every
message; NEVER start a second `wait` with its output thrown away (it eats messages - that hid Codex's 044 FINISH for 4 h on 11-18).
Morning: get_usage. Rules: API Opus review before every merge of agent work (`Scripts/Tools/api_task.py`, --effort low/medium); the
designer wants the API credit USED (58.6 of 100 USD spent on 11-18) - prefer API agents (`Scripts/Tools/claude_api_agent.ps1`, Opus,
budget 12-25 USD, prompt with "greps first, all edits, ONE build, ONE smoke batch") over subscription subagents (weekly limit ~75 %+).
Keep codex-worker loaded: farm window 08:00-16:30 batches with -Jobs 4, agents' smoke_parallel default -Remote auto (never "-Remote off"),
builds `Scripts/build.ps1 -Remote worker`; chat tab «Машины» shows both machines with progress bars (server: `Scripts/AgentChatApp.ps1`,
restart = stop the pythonw on :8765, start it again WITH -RedirectStandardOutput/-Error like that script, or it drops connections).
Never build in main while the designer's editor is open (check the UnrealEditor command line).

## State (main c4132f8e, pushed)
Merged 11-18: FormationMantle red ccdec8d3, X6 props spawn off-thread 13be5a16, Antigravity 083 b41973d8 + 083c d72b9629 (QD archive),
generated maps cold + weather 6dbc9e38, G007 ramp fight e24a6d6f (Grok + 3 API rounds), CoverCrouch e4558688 (cherry-pick from Codex 044;
its PeekMines / Suppression smoke changes REJECTED - they weakened checks), Codex 045 f4b5edde, chat «Машины» tab 3e1bbaa9 / 6b44e57e /
4b3da941, API tools second key b9170e47 (ANTHROPIC_API_KEY_EXTRA holds an invalid key ID - ignored, designer: forget it),
BUILDING_GENERATOR.md section 7 kit contract c4132f8e.
Main was built 11-18 ~15:55 (Succeeded) but Codex 045 (a SquadSubsystem.h member) was merged DURING that build -> do one more
incremental build of main before the designer plays (when his editor is closed).

## In flight
| Who | What | Next |
|---|---|---|
| API agent (CodexTactics-claude, branch claude-api/g008-fixes) | G008 round 2: occupier / retaker ping-pong post <-> point, FallBack off the roof (review G008-api-review-2.md) | review its "API follow-up 2", one API review, merge G008 (Grok 8b79a5f2 + fixes) |
| Worker | 8-fail re-run batch on fresh main (-Jobs 4): CoverCrouch, PeekMines, Suppression, LevelGenRamp, SightConditions, CorpseCost, MantleDescent (NEW: 140 cm wreck island descent plays fall states), HitchProbe (NEW: first-use spawn 149 ms > 50) | triage: CoverCrouch / LevelGenRamp should pass now; MantleDescent + HitchProbe -> API agent / Grok |
| Worker session "Worker-ops: overwatch_kills + камера клипов X4" (local_83f5d1d5) | X4 clip camera fix (own branch); overwatch_kills = 0 diagnosis written to docs/port/reports/worker-overwatch-kills.md | review + merge its 'ready'; tonight QD trainer from 16:30 (freeze posted, topic trainer-night) -> report 08:20 |
| Antigravity (gemday) | 085 orphan fence (plan approved: remove parent fence by GEOMETRY) -> 086 buildings P0 validator + stair fixes A/B (designer's top priority) -> 084 weather per map / map name / props regen in frame | plans first, review, merge; wake it if it goes silent (designer pastes a prompt) |
| Codex (agents) | 046 PeekMines cover-slot hand-over game fix; designer exception 11-18 ONLY: works down to 10 % of the DAILY norm; then out until 11-23 | review FINISH topic codex-046 |
| Grok | idle; next G008b overwatch hold (PMC_DEFENCE P1b) after G008 merges | ~17 %/day, ask the designer for /usage |
| Kit session "Download Fab building kits into BuildingKitLab" (local_d6dc1267) | art track headless in BuildingKitLab (Power Plant modules, collision, offscreen corner render) | keep separate; integrate only when P2/P4 builder work starts |

## Designer questions open
- Building generator: does "новая закладка" mean a separate Training Ground tab to generate / validate ONE building? If yes add it to P1.
- 6 kit questions (BUILDING_GENERATOR.md 7.5): window sizes, ribbon windows, 150 cm exterior door, 20 cm slab collision, Soviet kit 280 -> 300,
  texel density 256 / 512.
- Rappel pack (Fab "Rappel System Singleplayer", voxel vision): recommended single-player version, buy + test in a lab, integrate after
  this week as content (anims / rope / leg IK) with our own C++ (ARappelAnchor + nav link + order + TTraversalQueue); open: fixed anchors vs
  a rope item, PMC use.

## Queue after
G008 merge -> G008b (Grok) -> PMC_DEFENCE P2 deploy orders (API agent) -> P3 attackers vs mines; buildings P0b C++ LevelGenBuildingsSmoke
(API agent) -> P1 grammar (Antigravity); MantleDescent / HitchProbe reds; TASK_BOARD done-list each merge; evening phone report.
</content>
</invoke>

## Update 2026-11-18 ~17:00 (lead)
- Merged since: worker camera 5915bd8a, G008 PMC defence 80f7d55b (= tonight's freeze; worker: frozen build -> full sweep -Jobs 4 ->
  trainer pmc-v6-qd2 X4 tactical, start <= 18:30, report 08:20). Re-run on fresh main: CoverCrouch, LevelGenRamp, MantleDescent PASS;
  still red Suppression, PeekMines (Codex 046), CorpseCost, HitchProbe (API agent, branch claude-api/hitchprobe in CodexTactics-claude).
- BLD-GEN building generator moved to the building-kit Claude session (local_d6dc1267, branch claude/building-generator) - designer:
  "as fast as possible, ideally by tomorrow"; Antigravity's 086 plan kept at docs/port/building-generator/086-antigravity-plan.md.
  ENV-TRIM trim sheets = standing task of the same session. Materials / master material / snow layer = Claude, not Gemini.
- PMC_DEFENCE D2 rewritten (designer): no runtime setup phase - the generator places PMC squads at strategic places + their mines /
  tripwires (task 087); C++ spawn-at-post = API agent after 087.
- HANDOVER: Antigravity offline ~18:00, Codex ~19:00 (designer). They commit WIP + a handover note (topics gemini-085, codex-046).
  Then: API agent in CodexTactics-gemday: 085b remainder (if any) -> 087; API agent in CodexTactics-agents: 046 remainder. 084 waits
  for Antigravity's return. API ledger 66.9 / 100 USD at 17:00 - budget 087 ~12, 046 ~8; keep >= 10 USD for tomorrow's reviews.
- Grok: 44 % used at ~17:00 (today's share spent). Tonight ONLY Codex's 046 remainder (after Codex's handover commit) on
  grok-4.7-build-fast, low --max-turns, in CodexTactics-grok (branch from codex/046-peekmines); if it grows - stop, API agent finishes.
  11-19 from the new share: G008b overwatch hold, SuppressionSmoke red (grok-4.7). So the API agent takes only 087 tonight.
- Morning 11-19 first steps: get_usage; read the worker night report (trainer pmc-v6-qd2 with G008: cells filled, defender metrics,
  clips -> designer's phone) + the frozen-tree sweep FAIL list; review / merge what landed overnight (HitchProbe, 087, 046, 085b,
  building-kit session parts); ask the designer for Grok /usage; open designer questions are in "Designer questions open" above.
