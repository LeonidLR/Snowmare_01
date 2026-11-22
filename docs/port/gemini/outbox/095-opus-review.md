# Review: Gemini C++ task 095 (`PmcTraversalSmokeCommand.cpp`)

**No HIGH findings.**

I found no UObject/GC lifetime problems:
- The hostile is held in a `TWeakObjectPtr`.
- The world is held weakly and checked on every step.
- The ticker lambda captures only a `TSharedRef<FState>`.

The main concerns are weak verification that the link was actually crossed, and missing harness/acceptance artifacts.

## [MEDIUM]

- **[MEDIUM] Drop "bypass" check does nothing; detour routes count as crossed.**
  - `DropApproachOffsetCm` defaults to 150 and the start-passed test is `Dist2D(CurrentPos, LinkStart) <= 150`. So `bLinkStartPassed` is effectively true from the moment of teleport (exactly on the boundary, and nav projection can only nudge it).
  - `bLinkMidPassed` then only needs `FeetZ <= LinkEnd.Z + tolerance`. Arrival already requires feet within 70 cm of `NavGoal.Z`, which is at the bottom of the drop.
  - That mid flag is updated in the same tick, before the arrival check. So every arrival is automatically "verified".
  - Result: an operative that walks down a stair instead of taking the drop is counted as crossing it. The drops pass rate is inflated and "bypassed drop" can never fire.
  - Fix: require evidence of the drop itself, for example a Falling movement mode or a vertical descent seen while 2D-near `LinkStart`/`LinkEnd`.

- **[MEDIUM] Mantle and stair crossing checks can pass before the character moves.**
  - **Mantle:** the start spot is 120 cm behind `Link.Start` and the check is 3D distance to `LinkMid` ≤ 150. For short, low mantles (roughly 100 cm high, under 60 cm deep), the capsule centre at spawn is already within 150 cm of `LinkMid`.
  - **Stairs:** the check is 2D-only, distance ≤ 180 to `LinkMid`, from a start spot 100 cm before the foot. On short flights this is already satisfied at spawn. Because it is 2D, any path passing under or over the flight also satisfies it.
  - Result: bypass detection is unreliable, so the per-kind "N of M crossed" can overstate real traversal.
  - Fix: latch these flags only after the character has moved away from the spawn point, and/or require Z progression (for example, feet passing through roughly the mid-height near the link).

- **[MEDIUM] Test setup hides real enemy outcomes on drops and mantles.**
  - Before each leg the code sets `SetMaxHealth(100000, true)` and disables knockdown.
  - A drop that would knock down or kill a real team-1 operative is therefore reported as "crossed". The "died" reason is effectively unreachable.
  - The acceptance question is whether an enemy fighter can walk these links. Fall damage and knockdown are part of that answer for drop links.
  - Fix: keep default health and knockdown, at least for drops, or report knockdowns as a separate failure reason.

- **[MEDIUM] Missing acceptance artifacts.**
  - The task requires registering the smoke in `Scripts/test_map.json`, with a `knownFail` entry and the actual numbers if any kind is below 95%. No such change is in the diff.
  - Without a map entry, `smoke_parallel.ps1 -Commands PmcTraversalSmoke` may not bind the command to `L_Gen_Depot_X4`.
  - `docs/port/gemini/pmc_traversal_config.json` is referenced but not added. This is harmless, since defaults are used, but it is dead configuration.

## [LOW]

- **[LOW] Run continues with a dead hostile if respawn fails.**
  - In `PrepareLeg`, if the hostile is KIA or has no controller and `SpawnSquad` fails, the code keeps using the stale pawn.
  - `OrderMoveTo` then returns `NoController`, so every remaining leg is misreported as "no path" instead of the run aborting.
  - Fix: abort or fail when respawn does not produce a valid controlled pawn.

- **[LOW] Respawned hostile is not fully configured.**
  - It does not get `bTacticalCeaseFire = true`.
  - `SetTeamCommanded(1, false)` is not re-applied. This is fine only if the flag is team-wide and persistent.

- **[LOW] Other order results become "stuck".** Any `OrderMoveTo` result other than `Unreachable`, `NoController` or `Refused` (for example busy or incapacitated values, if they exist) is treated as accepted. It then times out and is reported as "stuck", which violates the required reason taxonomy.

- **[LOW] Bad `-TraversalOnly` id passes silently.**
  - When `OnlyFilter` is set, every kind counts as filter-excluded and passes with 0 sampled.
  - A typo or nonexistent id therefore yields `Smoke RESULT: PASS` with zero legs run.
  - Fix: fail if `Legs.Num() == 0`.

- **[LOW] Layout load failure is not logged.** `LayoutError` from `LoadLayout` is discarded. Stairs and ladders silently get 0 sampled and the run FAILs with no reason given.

- **[LOW] Possible null dereference on malformed JSON.** `BV->AsObject()`, `SV->AsObject()` and `LV->AsObject()` are dereferenced without null checks. `GetStringField`/`GetNumberField` on missing fields also log errors.

- **[LOW] Fragile hard-coded values.**
  - The spawn origin `(2356, -8700, 50)` is map-specific. If projection fails, the hostile spawns at the raw coordinates.
  - The cvar names `Codex.Pressure.Enabled` and `Codex.Pmc.Encounters` are silently skipped if they do not exist.

- **[LOW] Fixed timeout can misclassify long legs.** `LegTimeoutSeconds = 15` applies uniformly to all legs. Long ladder climbs (many rungs) or long nav detours may time out and be reported as "stuck (timed out)" rather than as genuine failures.

- **[LOW] Dead state.** `State.Time`, `Leg.GoalZ` and `LinkEnd` (for stairs and ladders) are never used. `bLinkStartPassed` is only consulted for drops.

- **[LOW] Exit code does not reflect the result.** `Finish` always calls `RequestExit(false)`, whether the run passed or failed. The harness must parse `Smoke RESULT` from the log; confirm this matches the other smoke commands.