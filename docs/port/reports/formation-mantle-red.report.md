# FormationMantleSmoke red on main (night sweep 2026-11-18) - cause and fix

Branch `claude/formation-mantle-red` (from main `19c2c599`), worktree `CodexTactics-tactical`. Claude, 2026-11-18.

## Symptom

The night sweep on `d5c4ae34` (codex-worker, logs `C:/Work/CodexTactics/Saved/Logs/Smoke-FormationMantleSmoke[-alone].log`) failed in both runs with the same two lines:

- c (formation, double click on a 135 cm fence's face, `StaticMeshActor_44`): Engineer waited his turn ... 268 cm out, look at most **123 deg** off outwards, **29 bad**;
- e (formation, ground click beyond a fence, `StaticMeshActor_0`): Medic-Sapper ... 249 cm out, look at most **152 deg** off outwards, **4-5 bad**.

Every other check passed: the climb order, one climber at a time, all over, the turn lengths, the R17-3 click sequence (first click = cover ghost on a face, double click = run and climb). The "bad" frames came from the look direction only. The base distance was inside `squad.base_radius_cm`.

## Which commit broke it

It was not R17-3 (`0ae52037`). The last passing runs:
- `CodexTactics-claude` 2026-11-17 20:52: R17-1, before R17-3.
- `CodexTactics-gemcli` 2026-11-17 22:19: HEAD `40a1ccb7` + the R17-3 review edits, so R17-3 / F-3 click rules included.

In both, `[Props] spawned 0 props` on L_Gen_Depot_1. The failing sweep (`d5c4ae34`) and the local run log `[Props] spawned 61 props (8 game objects, greybox: 53)`.

The only gameplay merge between `40a1ccb7`/`531698df` and `d5c4ae34` that changes the map is **G003b `302b6ee5`** ("generated maps spawn REAL gameplay props", feature commit `17a176d6`). The other merges in that range don't touch the squad climb or what the Depot holds:
- G006 first aid `4d887687`;
- R17-3 `0ae52037` itself;
- the leader-switch facing fix `8dd80b37`;
- G004, report only.

G003b put crates and barrels on the Depot:
- the smoke's nearest 135 cm fence (`StaticMeshActor_28`) has no free stand-off spot any more, so case c takes `StaticMeshActor_44`;
- props now stand behind the waiters at case c and case e.

## Root cause (game, not the smoke)

`USquadSubsystem::TickSquadMantle` step 4 builds a waiter's look in two steps:
1. `SquadMantleRules::WaitLookYaw` gives a look clamped to `squad.wait_max_off_deg` (80) off the outward look. The design (mantle.json, MANTLING.md 6.4) says: "never more than wait_max_off_deg off the outward look (nobody covers into the obstacle)".
2. That look then went through `FormationSectorRules::ClearLook` with no limit.

With a prop right behind a waiter and the fence 250 cm in front (inside `look.wall_dist` 260), the outward arc was blocked. ClearLook then took the fence's free edge, or in the enclosed case the longest ray, round towards the obstacle. That is the game breaking its own rule: it was hidden while the Depot had no props.

Local proof, same sites as the worker (the new change-only log line shows what the old unlimited ClearLook would have returned):

```
[SquadMantle] Engineer waits (place 1): a wall turns his look -135 -> -168 (outward 180, 12 off; unlimited ClearLook -58, 122 off; 3 walls)
[SquadMantle] Medic-Sapper waits (place 1): a wall turns his look 135 -> 132 (outward 90, 42 off; unlimited ClearLook -118, 152 off; 5 walls)
```

122 / 152 deg are the worker's 123 / 152.

## Fix

- New pure rule `SquadMantleRules::ClearWaitLook(Walls, Pos, WantedYaw, CurrentYaw, OutwardYaw, MaxOffDeg, Config)`. It calls ClearLook with its existing `FFormationLookLimit` (contract 13.7) = the outward look +- `squad.wait_max_off_deg`. A wall behind a waiter still turns him aside, but never round to the obstacle. When he is enclosed, the longest-ray fallback stays inside the arc too.
- `SquadSubsystemMantleQueue.cpp` uses it for the waiters / holders. It also logs one line when a wall turns a waiter's look, only when the look changes by more than 5 deg, never every tick, so the next regression shows up in the log at once.
- The smoke is unchanged. Its input sequence is valid under R17-3 / F-3, and its "bad" checks are untouched.

## Tests

- New unit test `CodexTactics.Formation.MantleQueue.WaitLookWalls`. It covers:
  - only the fence in front: he looks straight out;
  - a crate behind: his look turns aside, 40-80 deg off;
  - boxed in behind and at both sides: the unlimited ClearLook leaves the arc (documents the bug), while ClearWaitLook stays within 80 deg for any body yaw;
  - a free fanned look is kept.
- `Scripts/test.ps1` (all CodexTactics.*): 1507 tests, 0 failed.
- `FormationMantleSmoke`, run twice locally (fixed step, `-Multiprocess`, L_Gen_Depot_1 with the 61 props): PASS 51.8 s, then PASS 53.0 s. Case c is on `StaticMeshActor_44` (12 deg at most, 0 bad), case e on `StaticMeshActor_0` (42 deg at most, 0 bad).
- `MantleSmoke` PASS, `MantleClickSmoke` PASS (same batch as the second run).
- `Scripts/test.ps1 -Smart`: see the commit message / the final report.

## Not verified

- Not run on codex-worker (local build and smokes only).
- The other smokes that use the squad climb's waiters on other maps (ladder smokes don't use this code) were not re-run; the change only narrows the waiters' look inside the squad mantle.
