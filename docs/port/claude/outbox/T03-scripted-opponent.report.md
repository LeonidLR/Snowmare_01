# T-03 - a scripted opponent for the PMC arena: Opponent=Script Script=run (report)

Agent: Claude (sub-agent, worktree `CodexTactics-claude`), branch `claude/pmc-scripted-opponent` from origin/main 16b7b6ba. Not merged, main not touched.
Spec: `docs/design/gdd/smart-enemies-roadmap.md` T-03; GDD smart-enemies "Acceptance Criteria" (scenario smokes use a scripted opponent) and A-00A lines 5-6.

## Handover

**Done.** `CodexTactics.PmcArena ... opponent=script script=run` makes side B the scripted ATTACKER: a PMC squad spawned exactly as for `opponent=pmc`, the commander
off for it (`SetTeamCommanded(B, false)`, no assault point), a waypoint follower that walks every man along the round's route to its end, the men firing back by the
operative body's own rules. Nothing in the walk is random. `script=bound` is the seam T-07 fills (plays as `run` with a warning until then).

**Not done / open for the lead and T-07:** see "Open points" below (the first two matter: the walk speed vs. sprint, and the drift of a same-seed round).

## 1. Diagnosis: how the arena spawns side B today

`Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp`, `StartRound`:
- `Opponent=Pmc` (the default): side B = `UPmcSquadSubsystem::SpawnSquad` (or `SpawnSquadTactical` when B defends a tactical round) for TeamB (= team 2), weights B,
  `SetTeamCommanded(TeamB, true)`, `SetTeamAssault(TeamB, ...)` when B attacks. The player's squad is parked (`ParkPlayerSquad`). Both sides are the commander's.
- `Opponent=Player` / `Bot`: side B = the player's squad (team 0, the `USquadSubsystem` members) teleported to B's spawn; `Player` commands it with weights B,
  `Bot` starts the playtest bot (it does not perceive hostile operatives - the roadmap evidence). One round only (the player's fallen cannot be revived).
- The attacker alternates with the round seed's parity (even = A). A round's route: the tactical scenario's `TacticalRoute` (attackers walk it, defenders on posts along it),
  else no route (skirmish / street / chokepoint / roofs: the attackers are driven by the commander towards the objective).

**Stop-rule check (team control without a commander vs the operative's own autonomy): no conflict.**
- `USquadAutonomySubsystem` (Commander Mode) iterates only `USquadSubsystem::GetMembers()` = the player's squad; a PMC team is never in it.
- An uncommanded team already exists and works: `UPmcSquadSubsystem::Tick` skips `Decide` / `UpdateTeamRunning` when `bCommanded` is false (Patrol posture; `PmcArenaObjectiveSmoke`
  parks the defenders that way), and orders reach the body through the shared `SquadOrderCatalog` (`Move` -> `OrderMoveTo`).
- One body rule does act for an uncommanded team: `UpdateSight` gives a man who just lost sight of a target he fired at a `Suppress` order ("lost sight memory",
  logged as `[PmcAI] team 2: X -> Suppress (lost sight memory ...)`). That is the body's return fire, not a commander decision (it is not counted in `Stats.OrdersIssued`).
  The follower leaves a man alone while `HasSuppressOrder()`, so the two do not fight over him.

## 2. What was built

| File | What |
|---|---|
| `Source/CodexTactics/Public/AI/PmcArenaScriptRules.h`, `Private/AI/PmcArenaScriptRules.cpp` (new) | The pure half: `EMode {Run, Bound}`, `ParseMode`, `ResolveMode` (Bound -> Run + `bFellBack`: T-07's seam), `FConfig` + `ReadJson` (the new `pmc_arena` keys), `WaypointsFromRoute` (a point every N cm of 3D path length, the start left out, the end always the last waypoint, a last interval < N/2 merged into the end, repeated points ignored), `AdvanceWaypoint` (arrival: planar `ArriveCm` and `LevelCm` height; passes stacked waypoints, never goes back), `RouteLength`. |
| `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp` | `EOpponent::Script` + `IsPmcSideB()` (B is a PMC squad for Pmc and Script - used at the spawn, the parked player squad, `Sides[1].TeamId`); `script=run\|bound` parsed; Script forces `bAAttacks = false` (B attacks every round, not by seed parity); B spawned as for Pmc but `SetTeamCommanded(TeamB, false)` and no `SetTeamAssault`; `FScriptRun` (route, waypoints, per-man next / issued waypoint, order counters); `StartScript` (route = `TacticalRoute` else `RouteBetween(B.Spawn, objective)`; one log line), `StepScript` (10 Hz: advance, then a `SquadOrderCatalog Move` to the next waypoint - a new order when one was reached, the same order again 0.5 s after a refused / ended one; skipped while the man has his own Suppress order, is down / panicking), `SummariseScript` (round JSON + log line); the result JSON gets `opponent: "Script"`, `sides.B.team: "script"`, `weights_b: "None"`, `rounds[].script` (mode, route, waypoints, `commander_decisions`, per man progress). The doc header and the console help are updated. |
| `Source/CodexTacticsTests/Private/AI/PmcArenaScriptRulesTest.cpp` (new) | `CodexTactics.AI.PmcArena.ScriptOpponent`. |
| `Content/Data/LevelJson/L_PmcArena.json`, `L_PmcArenaDepot.json` | New keys only, with the code defaults: `script_waypoint_m` 6, `script_arrive_m` 2, `script_level_m` 1.5, `script_sprint` false. A level JSON without them (X4) uses the same defaults from `FConfig`. |

Existing opponents are untouched: every changed line outside `StartScript / StepScript / SummariseScript` is guarded by `Opponent == Script` or by `IsPmcSideB()` (true for Pmc exactly as before).
`SetTeamCommanded(TeamB, Opponent != Script)` keeps `true` for Pmc. Smokes force `Opponent = Pmc`, so none of them can reach the Script branch.

## 3. Proof

**Unit tests** (local run on the DLLs built on the worker, `Scripts/test.ps1 -Filter CodexTactics.AI.PmcArena`): **25 tests, 0 failed**, incl. `CodexTactics.AI.PmcArena.ScriptOpponent`
(mode parse + T-07 fallback, the JSON keys + clamps + defaults, waypoints on a straight route / an 18 m route / an L route / a 12 m climb / degenerate routes / a 12 km winding
route with every waypoint on the path, `AdvanceWaypoint` cases, a simulated man walking the route twice with the same step count).
Determinism of the rule: the same route (and a copy of it) gives the same list, the route is not modified.

**Smokes on the worker** (`Scripts/smoke_parallel.ps1 -Remote on`, binaries of the feature commit's tree): `PmcArenaSmoke` PASS (47.6 s), `PmcArenaObjectiveSmoke` PASS (42.2 s),
`PmcArenaTacticalSmoke` PASS (45.9 s); 3 run, 0 failed, no re-run.

**Manual rounds on `L_Gen_Depot_X4`** (tactical scenario, `spawns=random`, 120 s limit, fixed step 60 fps, run locally with the worker-built DLLs; logs
`Saved/Logs/T03-manual-script-run.log`, `...-run4.log`, `...-seed4b.log`; result JSON `Saved/Arena/t03_manual4.json`):
`CodexTactics.PmcArena rounds=4 seed=5 opponent=script script=run scenario=tactical spawns=random time=120`

| Round / seed | Route | Attackers (B) at the goal waypoint | Round end | Defenders fought | B returned fire |
|---|---|---|---|---|---|
| 1 / 5 | 188 m, 31 wp | 0 of 3 (waypoints 25 / 24 / 10, all down) | elimination, A wins at 81.7 s | A dealt 450 | B dealt 253 |
| 2 / 6 | 185 m, 31 wp | **3 of 3** (31/31; at the goal 86.7 / 87.8 / 89.3 s) | objective captured by B (15 s clock), 99.8 s | A dealt 97 | B dealt 300 |
| 3 / 7 | 176 m, 29 wp | 0 of 3 (21 / 21 / 11, not orderable at the end) | time limit 120 s, A holds | A dealt 150 | B dealt 151 |
| 4 / 8 | 185 m, 31 wp | **2 of 3** (30 / 31 / 31) | objective captured by B, 104.2 s | A dealt 145 | B dealt 212 |

- **No commander decisions for side B**: `Stats.OrdersIssued` of team 2 = 0 in every round (`rounds[].script.commander_decisions` 0, `commanded` false, the start line says
  `commander off`); the behaviour block shows `churn B 0`, `moves B 0` in the seed 4 round; no `[PmcAI] team 2: ... ->` line of the commander in either log - the only 16 team-2
  lines are the body's own `Suppress (lost sight memory ...)`.
- **Defenders fight** (side A on its tactical posts: overwatch / ambush kills, "A dealt" above) and the scripted side returns fire by the body's rules
  (`B dealt`, median engagement B 33-44 m). The attackers keep walking while they shoot (`CanShoot()` is false only while sprinting - see open point 1).
- **The attackers reach the goal area** in 2 of 4 rounds (3 of 3 and 2 of 3 men at the last waypoint, point captured); in the other two the defenders' posts stop them first.
  Route / waypoint counts are identical per seed: seed 4 gave 15 route points, 185 m, 31 waypoints in two separate runs.
- **Same seed, same walk - what is and is not proved**: the route, the waypoint list, the squad's names / weapons and the order of play are identical between two runs of seed 4
  (`Lind / Kessler / Vance`, 15 points, 185 m, 31 waypoints). The fight itself drifts like any arena round (the file header's known limit): run 1 ended with the men at waypoints
  23 / 23 / 27, run 2 at 28 / 22 / 27 (both: B eliminated by A, hp lost A 29 vs 13). No random number is drawn by the script; the drift comes from the shooting / physics.

## Open points (for the lead / T-07)

1. **Walk speed vs sprint.** `Script=run` walks (`script_sprint` false): `AOperativeCharacter::CanShoot()` returns false while `bSprinting`, so a sprinting attacker never fires back,
   which contradicts "firing back by the body's own rules". The walk takes ~87 s for 185 m (~2.1 m/s). If the designer's clip attackers ran faster, set `script_sprint` true (they then
   cannot shoot while running) - a one-key change; I did not decide it. Needs a call before the A-00A.5 numbers ("running straight through") are pinned.
2. **Determinism of the whole round** is the arena's known limit, not the script's (see above). If a scenario smoke needs bit-identical attacker timing, the shooting randomness has to be seeded
   per round (outside T-03).
3. **Side B always attacks under Script** (A defends): `bAAttacks` is forced false, so the seed's parity no longer picks the attacker. Scenario smokes that want A to attack need a switch - not built (no need yet).
4. **Panic / pinned men**: a man who is not orderable (dead, knocked down, panicking, raging) is not re-ordered and stays where he is (round 3: at the 120 s limit two of side B were still alive but none was orderable - waypoints 21 / 21 / 11; the cause - panic, knocked down, raging - was not examined).
   That is the body's rule; T-07's bounding follower should decide whether the others wait for him.
5. **T-07 seam**: `PmcArenaScriptRules::ResolveMode` (Bound -> Run + warning) and `StepScript` are the places. `StartScript` already gets `Run.Mode`; a bounding follower would split `Run.Men` into
   moving / covering halves inside `StepScript` and reuse `AdvanceWaypoint`.
6. **Remote build observation (infrastructure, not code):** the first `build.ps1 -Remote worker` returned `BUILT` after 20 min but the DLLs copied back were those another build on the worker
   had overwritten while my `scp` was still running (the worker lock was released at "DLLs back", the copy ran 15 min on, the 173 MB Shipping exe included). The binaries lacked my code; I caught it by searching the DLLs for the
   new strings, rebuilt (6 min compile, DLLs verified) and only then ran tests / smokes. Worth a look at `build_remote.ps1` (verify the copy, skip the Shipping exe).
7. `HANDOFF.md` / `TASK_BOARD` rows are the lead's (outside my allowed paths); nothing else outside the allowed list changed.

## Gate

build `Scripts/build.ps1 -Remote worker` (twice, see open point 6; the second: compile 5.9 min, DLLs verified) | unit `CodexTactics.AI.PmcArena` 25 / 25 | smokes `PmcArenaSmoke`,
`PmcArenaObjectiveSmoke`, `PmcArenaTacticalSmoke` PASS on the worker | manual rounds above | `git diff --check` clean | `Scripts/test.ps1 -Smart -SkipSmokes -Base origin/main` 175 tests, 0 failed | `typesafe_triage.py --audit-diff --agent claude` on the staged feature commit: boundary PASS, risk 1.8 / 4.
