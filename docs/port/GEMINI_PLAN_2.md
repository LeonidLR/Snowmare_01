# Gemini plan 2 (from 2026-11-07)

Written by Claude (the orchestrator). The rules of `docs/port/GEMINI_WEEKEND_2026-11.md` sections 1 and 3 apply unchanged:
own worktree (`CodexTactics-gemini`), one `gemini/<slug>` branch per task from `origin/main`, never main, never `git add -A`,
the no-touch list, one short report per task in `docs/port/gemini_reports/<slug>.md`. First `git fetch origin` and read the
CHANGELOG rows since 2026-11-04 (the code moved a lot: cones, weapon reach, combat fixes, PMC arena).

Quality note from the review of your first round: your file / function / line maps are accurate and valuable; some details
were invented (a wrong parameter type, a quoted line that doesn't exist, a wrong root cause for #44). **Quote code only by
copying it from the file; mark every guess as a guess.**

Do the tasks in this order. Stop a task that turns out bigger than described and report.

## A. Scouting for Claude's next sessions (read-only, highest value)
For each: root cause or design entry points with exact files / functions / lines (copied), 2-3 ranked hypotheses, how to verify
(full smoke name from `Scripts/test_map.json`, log line, console variable), the smallest change you'd propose. File:
`docs/port/gemini_reports/diag_<n>.md`, branch `gemini/diag-round2`.
- **#32** the cover peek sight is capped at 18 m (`PeekSightRules` `RangeCm`); a rifle's band is 19-24 m and its reach 60 m — how
  far should a peek see, what else reads that range (cones, PMC commander, sight subsystem).
- **#43 rest** under fire: rear men in a column don't shift to get a line, nobody takes cover automatically (`UnderFireRules`
  exists since a9f9e504) — where a "shift for a line" and "seek cover under fire" would plug in.
- **#36** no vaulting over low obstacles on `L_Gen_Depot_1` (vaulting works elsewhere: `VaultNavigation`, `VaultSmoke`) — why.
- **#17 / 16c** reversing direction mid-ladder / mid-ramp; a squad stuck on a ramp when combat starts; running up ramps too fast.
- **PMC formation**: what it takes to let the PMC commander use the player squad's formation / wall work through the order
  catalog (`SquadOrderCatalog`, `docs/systems/pmc-squad-ai.md`) — the seams, the blockers.

## B. Design drafts (no code; Claude edits and reviews them with the user)
Branch `gemini/design-round2`, files under `docs/design/`. Each: goals, player-facing behaviour, the algorithm / data, the
numbers (marked "proposal"), fairness / counter-play, the smokes that would prove it, open questions.
- `KILL_ZONE_AMBUSH.md` (#49): PMC find a chokepoint (navmesh corridor 1-3 m wide between blocks >= 3 m), split to high
  positions on both sides (roofs first; interiors later), hold fire until the enemy is deep inside AND not looking at them
  (#54 "target attention"), fallback triggers (spotted, the enemy turns back / flanks / throws a grenade / smoke, patience
  timer), tells for the player (scope glint, movement in a window, tracks in the snow, HQ warning).
- `AI_DIRECTOR.md` (#47 + #48): the director of the moment — tension model (HP, ammo, cold, noise, open sectors, time since the
  last fight, weather), build-up -> peak -> forced relax, per-level pacing profile, budgeted actions (patrol, ambush, flank,
  PMC squad, boss, horde, lure, weather shift / duration, music, story beats, HQ radio lines and intel hints from the real
  world state, HQ blackout in a blizzard), fairness, the log / seed, metrics of "good pacing". Read
  `docs/design/NARRATIVE_BIBLE.md` if it exists on your T4 branch.
- `TRAP_PLANNER.md` (#46a): seeded mines / tripwires / trapped loot crates / turrets at strategic spots, the scoring, fairness
  rules (not near the start, spacing, <= K per route, always a bypass or a disarm, subtle tells), the bot check, the heat map.
- `AUTO_QA.md` (#41): runtime monitors (stuck / off-navmesh, fire outside the facing / beyond reach, frame / cone cost, T-pose,
  fall-through, exposed while "in cover", order stall, log errors), incident capture (shot, clip, seed, log), the scenario bot,
  the morning report, the dedup.

## C. Data edit (small, safe)
- **#34** replace the Spitter with the Frostbitten in the enemy rosters: `Content/Data/Environment/environment.json`,
  `Content/Data/AI/horde.json`, `Content/Data/CombatLab/presets.json`, `Content/Data/LevelJson/L_FormationLabCombat.json`
  (do NOT touch `stage_*.json`, do NOT remove the Spitter class). Keep the JSON valid (`python -c "import json; json.load(open(...))"`),
  run `Scripts/test.ps1 -Filter CodexTactics.AI` and the smokes `HordeSmoke,FormationLabCombatSmoke,CombatLabAmbushSmoke`
  (`powershell -Command "& Scripts/smoke_parallel.ps1 -Commands ... -Jobs 2 -ExtraArgs '-Multiprocess'"`). Branch `gemini/t34-spitter`.

## D. Tool (TypeScript, tested)
- **Chokepoint analyzer** in `Tools/TrainingGround/src/` (the level generator's layout model): from a layout JSON find
  corridors 1-3 m wide between blocks >= 3 m tall, score ambush positions on both sides (height, cover from the corridor
  entrance, line of sight along the corridor), output JSON + a heat-map view in the generator page. Tests; `npm test`,
  `npm run typecheck`, `npm run golden -- --check`. Branch `gemini/chokepoint-analyzer`. Run it on
  `Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json` and put the result in your report.

## Hand-back
Push each branch, update your rows in `docs/port/TASK_BOARD.md` on that branch, and append to
`docs/port/gemini_reports/SUMMARY.md`. Claude reviews and merges.
