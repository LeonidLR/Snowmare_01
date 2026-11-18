# G008b report - overwatch roof hold

Branch `grok/G008b-overwatch-hold`. Base `4dd38cb9` (origin/main at dispatch). Nothing pushed. `main` not touched. Maps, `Content/VFX`, `Config/DefaultScalability.ini`, and the designer's untracked `Content/` were not staged.

## Handover

Done:

- Local diagnosis on X4 tactical seeds 11, 22, 33, before any scoring change.
- The posted defender stays on the overwatch roof until the post's engagement range has held a known attacker for `ambush_max_hold_s` (20 s), or he is hit, suppressed, wounded, or too close for his band. A mate dying does not close the hold. The sector post on the ground is not the roof.
- Commits `07c27808` and `089c480e`. Local editor build of that tree succeeded.

Left:

- `overwatch_kills` is still 0 on every before and after seed. Shots from the post are still 0. The sniper on seed 11 held the full 20 s window at full health and never emitted a ballistic round.

Next exact step:

- One local X4 tactical seed 11 with console `Codex.Debug.FireTrace 1` (CVar in `OperativeCharacterCombat.cpp`, default 0). Read the first `[FireTrace]` reason on team-1 Operator Lind during the Ambush 2.39 window. The hold must stay as it is. The round never leaves because `FindShootTarget` / `PrepareSniperShot` (`OperativeCharacterCombat.cpp`, `OperativeCharacterSniper.cpp`) never reaches a shot clip: the log shows `AS_Knee_Aim_Idle` and no `[Sniper]` line for those 20 s. Those files are outside this task's allowed paths.

## Cause

The worker chain (Ambush gated on `!bEverContact`, so a threat at 120-155 m zeroed the roof before the post's 110 m) is the trainer's binary. On this branch G008 is already in, and the local before-fix leave is the defence-post walk.

Seed 11 before (`Saved/Logs/G008b-before-11.log`), same frame the sniper is on `B_hall_n` with a threat at 85.4 m (inside the roof's 110 m):

- `:1661` `Operator Lind opens fire (threat at 8544 cm, attention off 1.00)` - posture, not a round.
- `:1663` `walks to his sector_shooter post V(X=10175.11, Y=4793.52, Z=10.00)`.
- `:1664` `-> Ambush (score 1.00; threat seen at 8544 cm, hp 100%)`.

`07c27808` stopped that walk. The first after run then closed the hold when a mate died: squad outnumbered made FallBack 0.54, Objective 0.70 won, and he left at full health about 6 s in. Off the roof, `PostKind` stayed Overwatch and `bAtPost` became the ground sector post, so Ambush 2.38 came back for the rest of the round. `089c480e` removed the outnumbered close and requires his feet on the tactical overwatch post (`bOnOverwatchRoof`).

Seed 11 after that fix (`Saved/Logs/G008b-after2-11.log`), binary `089c480e`:

- `:1661` contact in range, nearest 8544 cm, post range 11000 cm.
- `:1664` Ambush 2.39, threat seen at 8544 cm, hp 100%.
- `:1679` `[SniperAnim] stance 0 -> 1: AS_Knee_Aim_Start`. `:1699` loop `AS_Knee_Aim_Idle`.
- `:1728` still Ambush 2.39 at 8005 cm, hp 100%, after the other side's Lind killed Vance. The mate's death did not close the hold.
- `:1787` Objective 0.70, threat remembered at 8005 cm, hp 100%. Track time of that order is 20.0 s (`ambush_max_hold_s` in `pmc_commander.json`). Feet left at 20.6 s. Side-aware shots from that post: 0 of 0 for the whole round. No second "opens fire" / "holds fire", and no `[Sniper]` line, until the later wounded FallBack.

Seeds 22 and 33 leave earlier for an allowed reason. Roux (`G008b-after2-22.log:1688` and `:1691`) is already at 1826 cm with a weapon cap of 6000 cm, then FallBack 0.92 at full health. Haas (`G008b-after2-33.log:1754`) FallBack 0.94 at 1724 cm, full health. That is the standoff band, not the 20 s clock. After they leave, the order is Ambush 1.05 (the ground defence walk), not a fake roof 2.38.

`ShouldHoldFire` was not retargeted. Attention off on the seed-11 open is 1.00. The "opens fire" line is the posture order (`SquadOrderCatalog` FirePosture). Standing fire is not a workaround: the standing shot clip is unused (`OperativeAnimInstance.h`).

## What changed

`07c27808` `feat(ai): hold overwatch defenders until the post's range`

- `PmcCommanderRules.cpp:199-206` zeros Objective / TakeCover / Advance / Flank while `IsOverwatchHoldOpen`. Ambush at `:305-311` uses that hold instead of `Step(!S.bEverContact)`.
- `PmcCommanderRules.cpp:1014-1051` engagement is `min(roof range, weapon reach)`. The hold stays open until in-range contact has lasted `ambush_max_hold_s`, and closes on hit (`bUnderFire`), suppression, a personal wound (`HealthFrac < fall_back_below`), or too close.
- `PmcSquadSubsystem.cpp:1445-1492` stamps `bContactInPostRange` / `PostRangeContactSeconds` from the post, not from the man. The clock restarts if every known enemy leaves the range or he steps off.
- `PmcSquadSubsystem.cpp:2210-2219` Ambush does not walk the G008 sector post while his feet are on the overwatch roof.
- `PmcTacticalRules` copies `overwatch_roofs[].range_m` onto the post as `RangeCm`. No new gene. `pmc_commander.json` comment only. `ambush_max_hold_s` stays 20.
- Unit test `CodexTactics.AI.PmcCommander.OverwatchHold` in `PmcCommanderRulesTest.cpp`.

`089c480e` `fix(ai): keep the overwatch hold when a mate dies`

- `IsOverwatchHoldOpen` (`PmcCommanderRules.cpp:1029-1045`) no longer closes when `Alive - EnemiesKnown <= -1`. That FallBack score remains for everyone else.
- `bOnOverwatchRoof` (`PmcSquadSubsystem.cpp:1486`) is feet on the tactical overwatch post. `bAtPost` from the sector post does not reopen the hold.

## Before to after

Side-aware. The posted fighter is the first track index of that display name (two men are named Operator Lind). Radius 300 cm planar, same as `IsAtOverwatchSpot`. Shots from post are shot events of that fighter while his feet are inside the radius. Source: `Saved/Arena/G008b-{before,after,after2}-{11,22,33}.json` and each round's track.

| Run | Defender | On post | First leave | Shots from post | Overwatch kills | First orders |
| --- | --- | --- | --- | --- | --- | --- |
| before-11 Lind A | A | 0.6 s | 0.6 s | 0/5 | 0 | Ambush 1.00, then the sector walk |
| before-22 Roux B | B | 0.8 s | 0.8 s | 0/44 | 0 | Ambush 1.00 at 0 s |
| before-33 Haas A | A | 0.6 s | 0.6 s | 0/30 | 0 | Ambush 1.00 at 0 s |
| after-11 Lind A | A | 12.6 s | 8.4 s | 0/0 | 0 | Ambush 2.39, Objective 0.70 at 6 s (mate died) |
| after-22 Roux B | B | 14.8 s | 14.8 s | 0/30 | 0 | Ambush 1.58, FallBack 0.92 at 14 s, later fake roof 2.38 |
| after-33 Haas A | A | 26.2 s | 24.6 s | 0/27 | 0 | Ambush 2.36, FallBack 0.94 at 24 s, later fake roof 2.36 |
| after2-11 Lind A | A | 20.6 s | 20.6 s | 0/0 | 0 | Ambush 2.39, Objective 0.70 at 20 s (the gene) |
| after2-22 Roux B | B | 14.8 s | 14.8 s | 0/0 | 0 | Ambush 1.58, FallBack 0.92 at 14 s, then Ambush 1.05 |
| after2-33 Haas A | A | 26.2 s | 24.6 s | 0/17 | 0 | Ambush 2.36, FallBack 0.94 at 24 s, then Ambush 1.05 |

Time on post, median of the three seeds: before 0.6 s, after2 20.6 s. The acceptance line `overwatch_kills > 0` and shots-from-post `> 0` is not met. The 17 and 30 shots in the Haas/Roux rows are after the allowed FallBack, not from the roof.

## Gate

Build: `Scripts/build.ps1` local, no `-Remote`, after `089c480e`. `Saved/Logs/Build.log`: `Result: Succeeded`, `Total execution time: 217.98 seconds`. The only warning is the pre-existing `ParseJson` unity clash.

Arena: seeds 11, 22, 33, scenario tactical, `L_Gen_Depot_X4`. Logs `Saved/Logs/G008b-before-*.log`, `G008b-after-*.log`, `G008b-after2-*.log`. All nine exited 0. `overwatch kills A 0 / B 0` on the behaviour line (before-11 `:2434`, after2-11 `:2034`).

`git diff --check` on the worktree and on `4dd38cb9..HEAD`: both exit 0, no whitespace errors. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok`: exit 0, `Clean workspace: No changes to audit` (the two commits were already HEAD; the audit reads the unstaged diff, not untracked files). Untracked designer `Content/` and `Config/DefaultScalability.ini` were not part of that diff and were not staged.

`Scripts/test.ps1 -Smart -Base 4dd38cb9 -Jobs 3 -SkipSmokes` with `CODEX_SMOKE_REMOTE=off`: `137 tests, 0 failed` in 28 s. Filter `CodexTactics.AI+CodexTactics.LevelJson+CodexTactics.Combat.Sight+CodexTactics.Combat.Cheats+CodexTactics.CodexTactics.AI+CodexTactics.LevelGen.TacticalMap+CodexTactics.Core.GameplayPreload`. Log `C:\Users\ZEPHYR~1\AppData\Local\Temp\g008b-units.log`. Report `Saved/TestReport`. `-Base 4dd38cb9` because `-Smart` against HEAD would see only the designer's untracked files. The first gate wrapper treated its own status line as the exit code and stopped; the unit process itself exited 0. Named smokes and the mapped `-Smart` smokes were re-launched after that.

Named smokes via `smoke_parallel.ps1 -Jobs 3 -Remote off -ExtraArgs -Multiprocess`, fixed step 60 FPS. Exit 0. `0 failed`, no re-run. Wall 93 s.

| Smoke | Result | Seconds |
| --- | --- | --- |
| PmcRoofDefenceSmoke | PASS | 58.4 |
| PmcStalemateSmoke | PASS | 45.4 |
| PmcArenaTacticalSmoke | PASS | 54.3 |
| PmcArenaSmoke | PASS | 44.1 |

Table from the batch log. Engine logs: `Saved/Logs/Smoke-PmcRoofDefenceSmoke.log` (`Smoke RESULT: PASS` at `:2352`; spread 9.8 m >= 5 m, guard share 0.73 > 0.6, `:2339` and `:2340`), `Smoke-PmcStalemateSmoke.log:1967`, `Smoke-PmcArenaTacticalSmoke.log:1962`, `Smoke-PmcArenaSmoke.log:1944`. Each ends `Smoke RESULT: PASS`.

`test.ps1 -Smart -Base 4dd38cb9 -Jobs 3 -SkipUnit` is the rest of the impact set (the PMC rule plus hitch-probe and tactical-pmc, including the four above again). Results filled in when that run finishes.
