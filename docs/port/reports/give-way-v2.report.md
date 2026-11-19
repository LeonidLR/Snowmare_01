# give-way v2 - report (claude-api, branch claude-api/give-way-v2)

Goal: the selected operative walks up a stair flight on L_Gen_BgenTest_X6 instead of stalling at its foot.
Map files copied untracked from the main checkout (11-18 evening rebuild), not committed.

## Result
`CodexTactics.LevelGenRampSmoke legs=solo` on X6 (direct run, fixed step, -SquadStart=formation):
**14 / 18 -> 16 / 18 legs** (the main log `Smoke-LevelGenRampSmoke-X6-ColdOff.log` says 14 / 18; the brief's "12 / 16" is the same run
counted without two legs). Log: `Saved/Logs/gw2-after-solo.log`.
B_x1_4 / B_x1_16 now pass; B_x1_8 / B_x2_13 still fail (see Left).

## Per flight: cause -> fix
1. **B_x1_4_t7_hall_stair_0** - the idle Engineer stood in the stair mouth 48 cm off the string-pulled path line. v1 found him but stepped him
   "120 cm off the path" = 25 cm from where he stood (11839 -> 11863), inside the walk's acceptance radius: he never moved (log: the same
   step asked 5 times). Then the leader's move was ended `Blocked` -> order 'none'.
   Fix (a): spots closer than `min_step_cm` (60) to him are dropped (scales 2.5 / 3 added); a mate in the flight's **approach zone**
   (`approach_before_cm` 250 before the foot .. `approach_after_cm` 100 up it, `approach_half_width_cm` 100 + his radius to each side of the
   flight axis) counts as in the way even off the line, and steps to a spot out of the zone. Feet found once per order by probing the
   mover's path for ramp floor every `ramp_sample_cm` (axis = the ramp component's up vector). -> PASS.
2. **B_x1_8_t7_block_stair_0** - idle mates in the mouth; with v2 they step out of the zone (log `[GiveWay] ... flight approach ... steps
   aside`), but the RampFile holder yield (v1 rule, `UpdateHolderYield`) then puts the Commander back in front of the foot (8479, 15543) ->
   (8600, 15493), and the two systems hand him back and forth. Leader now keeps his order (retries 1-2 / 4 logged) but stays blocked. Still FAIL.
3. **B_x1_16_t7_block_stair_0** - trace run (`only=B_x1_16_t7_block_stair_0 trace`, MoveTrace on, `Saved/Logs/gw2-after-x1_16-trace.log`):
   squad / group / solo all PASS; the full solo run passes too. Not reproduced: the edge-hugging crawl was the leg ending blocked behind a
   mate (sequence-dependent: in an isolated run with the old binary it also passed). No path-point / string-pull change made - nothing of
   ours found in the path following.
4. **B_x2_13_t8_block_stair_0** - the leader stands sandwiched between the idle Commander and Engineer at the path START ("0 cm ahead"),
   and every aside spot for both is refused ("no free spot aside", repeated). The approach zone does not engage (he is not yet within
   the foot's zone conditions there). Order kept (retries logged) but STUCK. Still FAIL. (b) slot-walk holds did not trigger in this run:
   in the new run the Commander was idle, not on a slot walk.

(c) **The order drop**: UE's crowd path following block detection (`CrowdFollowingComponent.cpp:1018-1021`: under 10 cm in 5 s -> 
`OnPathFinished(Blocked)`) -> `AOperativeAIController::OnMoveCompleted` -> `HandleMoveFinished` cleared `bOwnMoveOrder` like an arrival.
Fix: a `Blocked` end of an own walk farther than `blocked_retry_min_goal_cm` (100) from its goal is re-requested (same destination, new
path) up to `blocked_retries` (4) per order (`AOperativeCharacter::RetryBlockedOwnMove`, OperativeCharacterMovement.cpp; not during cover
wrap / bypass steering / pending cover / mantle). Log `[GiveWay] X: his walk ... was ended as blocked ... asked again (n / 4)`. Both
remaining failures now show 'order own' instead of 'none'.

(b) **Slot walks**: a formation walker (no own order) whose straight walk to his slot enters the mover's flight approach zone from outside
is held (traffic hold sourced by the mover; the formation update stops him and re-paths on release) until the walk no longer crosses
the zone, the mover stops / passes, or `slot_hold_max_s` (6) - then not held again for 6 s.

## Changes
- `Source/CodexTactics/Public/Characters/SquadGiveWayRules.h`, `Private/Characters/SquadGiveWayRules.cpp`: tuning v2 keys; `AsideSpots`
  overload (min step, zone filter); `FindFlightFeet`, `SelectApproach`, `InApproachZone`, `ApproachCrossing`, `SlotWalkEntersApproach`,
  `ShouldRetryBlockedMove` (pure).
- `Private/Characters/SquadSubsystemGiveWay.cpp`, `Public/Characters/SquadSubsystem.h`: flight feet cached per order, zone give-way,
  slot-walk holds (`GiveWaySlotHolds`), `RampAxisUnder`.
- `Private/Characters/OperativeAIController.cpp`, `OperativeCharacterMovement.cpp`, `OperativeCharacter.h`: blocked-order retry.
- `Content/Data/AI/squad_regroup.json` give_way: min_step_cm, approach_before_cm, approach_after_cm, approach_half_width_cm,
  ramp_sample_cm, slot_hold_max_s, blocked_retries, blocked_retry_min_goal_cm.
- Tests `CodexTactics.Squad.GiveWay.{FlightFeet, ApproachZone, MinStep, SlotWalk, BlockedRetry}` + shipped-JSON checks.
Commits: 8e5242e7 (rules), 566a5e6d (runtime), 9dd7c705 (retry).

## Gate
- Build: `Scripts/build.ps1` local, one build, Succeeded (existing unity-clash warning in Tests ParseJson, unrelated).
- X6 solo: 16 / 18 (`Saved/Logs/gw2-after-solo.log`).
- smoke_parallel -Jobs 3: LevelGenRampSmoke, CoverSmoke, GroupCoverSmoke, FastRegroupSmoke, FormationMantleSmoke, LevelGenLaddersSmoke,
  MovementSmoke - 7 / 7 PASS, no re-run.
- `Scripts/test.ps1 -Filter CodexTactics.Squad`: 50 tests, 0 failed.

## Left / next step
- B_x1_8: the RampFile holder yield (`UpdateHolderYield`, SquadSubsystemRampFile.cpp) picks spots inside the give-way approach zone - it
  should reject spots with `SquadGiveWayRules::InApproachZone` (the mover's approach), or defer to give way for idle mates.
- B_x2_13: "no free spot aside" for both mates at the mover's start - log which check refuses each spot (nav snap / taken / wall trace /
  ramp) and allow a spot behind the mover (against his way) when the sides are walls.
- Not done: routing a slot walk round the zone (only waiting).
