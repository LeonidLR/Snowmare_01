# give-way v3 - report (claude-api, branch claude-api/give-way-v3)

Goal: the last 2 failing flights of `LevelGenRampSmoke legs=solo` on L_Gen_BgenTest_X6. Map files untracked (not committed, not rebuilt).

## Handover
- Done: B_x2_13 passes (behind-spot fallback). The RampFile holder yield now treats give-way targets as taken and refuses spots in the
  leader's flight approach zone.
- Left: B_x1_8 still fails, and now in a different way (see below).
- Next exact step: a trace run `only=B_x1_8_t7_block_stair_0 trace`. Log `InMoverFlightApproach` at each RampFile yield (approach valid?
  ToFootCm, zone hit) and log the leader's crowd neighbours while he is stuck at feet 160.

## Result
X6 solo (direct run, fixed step, -SquadStart=formation): **16 / 18 -> 17 / 18**. Log: `Saved/Logs/gw3-after-solo.log`.

## Per flight
1. **B_x2_13_t8_block_stair_0**: PASS. I added a temporary log of why each spot was refused. With it I could see that both mates' aside
   spots were refused by `wall trace` / `off the navmesh`. Fix: if no aside spot is free, `SquadGiveWayRules::BehindSpots` is tried
   (spots behind the mover, against his first leg: straight back, 45 deg to the idle man's side, then the other side, at step_aside_cm
   x 1 .. 3). It uses the same filters as AsideSpots, and the runtime runs the same checks on each spot.
2. **B_x1_8_t7_block_stair_0**: still FAIL, but the failure is different now. The log (temporary log, this run):
   - At the start, both mates' aside and behind spots were refused, mostly `wall trace` and a few `off the navmesh` / `nav snap too far`.
     Then give way moved them out of the zone: Commander -> (8455, 15544), Engineer -> (8455, 15764).
   - The RampFile yield still fired once for each: Engineer (8475, 15749) -> (8455, 15629), Commander (8479, 15543) -> (8600, 15493).
     The new zone check did not refuse (8600, 15493), even though give way had treated (8615, 15544) as inside the zone. My guess
     (UNVERIFIED) is that `SelectApproach` found no valid approach at that moment: the leader was more than look_ahead_cm from the foot on
     his switchback path. So the RampFile zone check is not proven to work on this flight.
   - After that, give way moved the Engineer to (8670, 15916) and the Commander "behind" to (8455, 16040). The ping-pong did not come back.
   - End state: the leader is ON the flight (feet 160, (8677, 15804)), order still own, speed 16 cm/s, still for 10 s. Both mates stand at
     z 2 within about 110-300 cm of him in 2D. Hypothesis (UNVERIFIED): detour crowd counts an agent as a neighbour when the height
     difference is less than the agent height (about 176 cm), so a mate on the ground beside or under the upper part of the flight still
     blocks the leader through avoidance. Possible fix: refuse aside / behind spots that lie (2D) near the mover's remaining path at any
     level within the crowd height, or exclude agents on another level from the crowd neighbours.

## Changes
- `Public/Characters/SquadGiveWayRules.h`, `Private/Characters/SquadGiveWayRules.cpp`: `BehindSpots` (pure).
- `Private/Characters/SquadSubsystemGiveWay.cpp`, `Public/Characters/SquadSubsystem.h`: `InMoverFlightApproach`. The per-spot checks are
  now one lambda (`TrySpot`), with the behind fallback after it. The success log says "steps aside" or "steps behind".
- `Private/Characters/SquadSubsystemRampFile.cpp` (`UpdateHolderYield`): skips a man who is walking to a give-way target. Give-way targets
  count as taken (one claimed-spot set). Spots in the leader's flight approach zone are refused.
- Test `CodexTactics.Squad.GiveWay.Behind`.
- The temporary `[GiveWayTmp]` refusal log was removed before the commit.
Commits: 8b3407e7 (rule + test), b5d6d5ff (runtime).

## Gate
- Build: `Scripts/build.ps1` local, one build, Succeeded. The first attempt hit the tool's 10-minute timeout during link and I restarted it
  in the background (same build, incremental). The only warning is the existing unity clash (ParseJson).
- The smokes and tests ran on the build that still had the temporary log. Removing it touched only that `UE_LOG` block and was not rebuilt.
- X6 solo: 17 / 18 (`Saved/Logs/gw3-after-solo.log`).
- smoke_parallel -Jobs 3: LevelGenRampSmoke, CoverSmoke, GroupCoverSmoke, FastRegroupSmoke, FormationMantleSmoke, LevelGenLaddersSmoke,
  MovementSmoke: 7 / 7 PASS, no re-run (`Saved/Logs/gw3-smokes.log`).
- `Scripts/test.ps1 -Filter CodexTactics.Squad`: 51 tests, 0 failed.
- `git diff --check` clean.
