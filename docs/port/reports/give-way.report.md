# give-way - idle squadmate steps out of a squadmate's path (bug 11-02 item 7)

Agent: claude-api. Branch `claude-api/give-way` (from origin/main, not pushed). Commits: 292ff548, 337c343d, ba6dcdbb, 54a47518, f8440275 (+ this report).

## Handover
- **Done:** a give-way rule. An idle squadmate whose capsule a squadmate's own walk is about to cross steps aside, in Solo / holds and in squad mode (with the limits below). It is a pure rules function with 4 unit tests (`CodexTactics.Squad.GiveWay.*`), and its numbers are in `Content/Data/AI/squad_regroup.json` under `give_way`. It fixed X6 leg 4 (B_steelworks_stair_0).
- **NOT done:** X6 `legs=solo` is still 6 / 17. **Legs 7-17 do not fail because of squadmates.** In the X6 runs and in the designer's own baseline log, all three operatives go `STANDING -> PRONE` during leg 6 and then crawl at 15 cm/s for the rest of the run.
- **Next exact step:** find where that prone comes from. My guess is the cold-survival frostbite collapse, but I have not confirmed it (see below). A trace of `ColdLevel` per member on X6 would settle it. Then decide whether it is a smoke-fixture issue (warm the squad between legs) or a game issue. Either way that is a separate task: the smoke and its fixture are frozen for this task.

## Cause (evidence)
Baseline: the designer's log `../CodexTactics/Saved/Logs/BgenTest-Ramp.log` (wall clock), 4 / 17.
1. **Squadmates in the way (legs 4 and 7 in the baseline).** The existing Solo holder yield (`UpdateHolderYield`) only acts **once per order** and only for the selected man.
   - Leg 4: the Engineer was not on the cached path corridor when the order started, so he was never asked. The ordered man pushed against him.
   - Leg 7: the Commander's step was refused (`FollowTo -> 3`, Refused) and nobody asked him again. The leader waited 6 s, then pushed into him.
2. **Everyone prone (legs 7-17 in every run).** During leg 6 (~100 s of game time) the log shows `Commander / Medic-Sapper / Engineer: stance STANDING -> PRONE`. After that the selected man's trace reads `v 15 ... low` in open floor, more than 150 cm from anyone. For example, `Smoke-LevelGenRampSmoke-X6-GiveWayOn.log` leg 13: Medic-Sapper (-13932, 1374) -> (-13794, 1377) in 9 s with nobody nearby. That is the "5-15 cm/s for 10 s" pattern: a prone crawl, not pushing. The baseline log has the same three PRONE lines.
   - What I checked: there are no `[CriticalWound]` lines, no under-fire lines and no tripwire lines. Only `[WarmIdle] cold idle variant` lines appear, so this is a cold map. The only other automatic prone path is `ColdSurvivalComponent.cpp:256`, the frostbite collapse. It does not write a log line, so the frostbite cause is **likely but not proven**.
   - The lead's statement "ALL 13 failures: pushes behind the two idle squadmates" holds only for legs 4 and 7.

## Rule and numbers
`SquadGiveWayRules` (`Source/CodexTactics/Public/Characters/SquadGiveWayRules.h`, pure) and `USquadSubsystem::UpdateGiveWay` (`SquadSubsystemGiveWay.cpp`), run every tick after `UpdateRampFile`. CVar `Codex.Squad.GiveWay` (1; 0 turns it off).
- **Mover:** a member with an own move order who is currently walking (path following status Moving), and not in cover or heading into it. In squad mode with the formation following, the leader's walk is skipped; the formation's own `YieldFromLeaderWay` handles that.
- **Idle:** (`IsIdle`) not on a ladder, not knocked down, not mounted; no own order, not in cover; no combat action (object or sniper shot, corner peek, rage, panic, mantle, vault, prone dive, pending cover, ledge drop, critical wound); standing still; not held by a traffic or ramp-file hold. In squad mode he must also not be standing on his last ordered spot. A man behind a barricade stays where he is.
- **Crossing** (`FindCrossing`): uses the mover's string-pulled navmesh path from where he is now, cached per order serial and destination. It triggers when, within `look_ahead_cm` ahead of the mover, the path passes closer than the two capsule radii + `clearance_cm` to the idle man's feet. The path point must be within `same_level_cm` of his feet height, so a flight above him does not count.
- **Spot** (`AsideSpots`): perpendicular to the crossing segment at `step_aside_cm` × 1 / 1.5 / 2. His own side comes first; each distance is tried straight out, then 45° back. Every candidate must be at least the crossing distance from the path within along + look-ahead, so the next leg (the flight up) is never a spot.
  - At runtime the spot must also: project onto the navmesh within `nav_snap_cm`; be within `max_rise_cm` of his feet; not be on a ramp; stay clear of the path **after** projection; be free of the others; and be reachable in a straight line.
  - He then walks there with `FollowTo` at the formation walk pace and stays idle. He is asked again every `retry_s` while he is still in the way.
- **squad_regroup.json `give_way`:** enabled 1, look_ahead_cm 400, clearance_cm 20, step_aside_cm 120, same_level_cm 150, nav_snap_cm 30, max_rise_cm 60, retry_s 1.5.
- **Unit tests** `CodexTactics.Squad.GiveWay.{IdleOnly, PathCrossesCapsule, StepsAsideOffThePath, TuningFromJson}`: all pass.

## Before -> after (LevelGenRampSmoke on L_Gen_BgenTest_X6, legs=solo)
| run | clock | legs passed |
|---|---|---|
| designer baseline (main checkout, before) | wall | 4 / 17 |
| this build, `Codex.Squad.GiveWay 0` (A/B, same binary) | fixed 60 fps | 5 / 17 (leg 4 FAIL) |
| this build, give-way on (first build) | fixed 60 fps | 6 / 17 (legs 1-6 all ok) |
| after fix 1 (cover / projection re-check) | fixed 60 fps | 6 / 17 |
In every run, legs 7-17 fail after the squad-wide PRONE (cause 2).

How I ran it: `smoke_parallel.ps1` cannot run X6, because the test_map `map` option overrides `-Map`. I ran it directly with the same clock arguments: `UnrealEditor-Cmd ... /Game/Maps/Generated/L_Gen_BgenTest_X6 -ExecCmds="[Codex.Squad.GiveWay 0,]CodexTactics.LevelGenRampSmoke legs=solo trace" -UseFixedTimeStep -FPS=60 -FixedSeed -SquadStart=formation`. Logs: `Saved/Logs/Smoke-LevelGenRampSmoke-X6-GiveWay{On,Off,On2}.log`. The X6 files were copied from the main checkout and left untracked, not committed.

## Gate
- **Build:** `Scripts/build.ps1`, run locally (no worker). I needed **3 more builds after the first**, not one: `CoverSmoke` and `GroupCoverSmoke` went red because of this change. Each later build only narrows the rule.
  - Build 2: skip walks into cover; re-check the projected spot. This fixed CoverSmoke.
  - Build 3: in squad mode, skip the formation leader's walk.
  - Build 4: in squad mode, a man on his ordered spot stays. This fixed GroupCoverSmoke (a member was moved 62 cm off his group-cover spot by another member's walk).
  - The first build's unity-clash warnings for my local names were fixed by renaming.
- **Smoke batch** (`smoke_parallel -Remote off -Jobs 3`), first build: LevelGenRampSmoke (Depot_1, 12 / 12) PASS, FormationMantleSmoke PASS, LevelGenLaddersSmoke PASS, MovementSmoke PASS; CoverSmoke and GroupCoverSmoke FAIL (mine).
- **Re-runs:** CoverSmoke PASS after build 2; GroupCoverSmoke PASS after build 4.
  - Not re-run on the final build: LevelGenRampSmoke default, FormationMantleSmoke, LevelGenLaddersSmoke and MovementSmoke. Builds 2-4 only narrow when the rule fires. MovementSmoke and LevelGenLaddersSmoke did pass again in the `test.ps1 -Smart` smoke set on the final build.
- **`Scripts/test.ps1 -Smart`** (final build): unit 0 / 35 failed, 21 smokes run, 0 failed.
- **`test.ps1 -Filter CodexTactics.Squad -SkipSmokes`:** 45 tests, 0 failed (GiveWay 4 / 4, Regroup TuningJson included).
- **`typesafe_triage --audit-diff`:** clean. **`git diff origin/main --check`:** clean.

## Left / risks
- Squad mode is deliberately narrow. The formation leader's walk is left to `FormationRuntime::YieldFromLeaderWay`, and a man on his ordered spot does not move. Give-way acts in squad mode only for a non-leader's own walk past a man who is not at his post.
- The prone / crawl on X6 (cause 2) blocks the remaining 11 legs. It needs its own task; "Do NOT change the smoke" ruled it out here.
- `Scripts/test_map.json` has no group for `SquadGiveWayRules*` / `SquadSubsystemGiveWay*`, so `-Smart` does not select `CodexTactics.Squad.GiveWay`. Suggested group: the RampFile group's smokes plus CoverSmoke and GroupCoverSmoke.