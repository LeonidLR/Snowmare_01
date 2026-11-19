# G008b report - his own rim, and the hold stays shut

Branch `grok/G008b-overwatch-hold`. Code `78fade83`. Nothing pushed. `main` not touched. Maps, `Content/VFX`, `Config/DefaultScalability.ini`, and the designer's untracked `Content/` were not staged.

## Handover

Done:

- Lead review items 1 and 2 are in `78fade83`. The 60 m, four-step roof probe is gone. `lip_cm` is 200 in `fire_gate.json` and in `FRoofLipConfig`. One step, and only when the blocked hit is the floor under his feet. The real muzzle line is traced to the rim, then the segment from the rim to the retry point ignores only the rim actor.
- The overwatch hold latches shut for that posting on a hit, suppression, his own wound, or standing too close. The in-range clock does not restart when the attacker steps out. Stepping off the roof clears both. A mate dying does not latch it. `07c27808` and `089c480e` stay. `ShouldHoldFire` was not retimed.
- Seed 11, Operator Lind, side A: still on `B_hall_n` for 20.6 s, Ambush 2.39, then Objective 0.70 at 20 s. Seed 33, Operator Haas, side A: on the post 26.2 s, FallBack 0.94 at 24 s (the allowed too-close exit).

Left. This is not a clean pass:

- After `lip_cm` 200, seed 11 fires 0 shots from the post and the side metric `overwatch_kills` is 0. Seed 33 fires 0 of 33 shots from the post and `overwatch_kills` is 0. The 2 m cap cannot clear a first hit several metres out along the shot. `lip_cm` was not put back to 6000.
- `PmcRoofDefenceSmoke` failed the parallel run and the solo re-run. Both misses are defender spread (4.6 m, then 4.2 m) against `>= 5 m`. Guard share on the solo run is 0.90. The check was not weakened.

## Review defect

`8a2cc2d2` let a blocked kneeling shot walk a horizontal probe up to four times, out to `lip_cm` 6000. That skips crates, sandbags, and the next parapet, for both PMC sides and the player squad. The review refused the merge.

## What changed

`78fade83`

Roof lip, `OperativeCharacterCombat.cpp:181-285`. The blocked muzzle hit is the only candidate (`Hit.Distance` from 1 cm to `lip_cm`, not a start inside geometry, not a corner shot). A downward trace from the feet names the floor actor and the floor component. The rim must be that actor or that component. One column reads the rim top for `MayClearRoofLip` (`SquadFireRules.cpp:108-127`, logic unchanged). The shot is then traced from the muzzle to 5 cm short of the impact. The segment from the impact to `RoofLipRetryOrigin` (`past_cm` 80, muzzle height) ignores only the rim actor. A start-penetrating gap, or any other actor in the gap, stays blocked. A gap that reaches the enemy is a clear line and does not continue past him. A clear gap retries from the retry point to the aim point with the original query, which still sees the rim. A tall wall and a man on the ground stay blocked.

| Key | cm |
| --- | --- |
| lip_cm | 200 |
| past_cm | 80 |
| min_drop_cm | 180 |
| band_above_cm | 120 |
| band_below_cm | 40 |

`FRoofLipConfig::LipCm` is 200 (`FireGateRules.h:30`). `CodexTactics.Combat.FireGate.TuningFileMirrorsDefaults` stays green. `CodexTactics.Combat.SquadFire.RoofLip` expects a retry at 160 cm and refuses 2700 cm.

Hold. `ShouldLatchOverwatchHold` (`PmcCommanderRules.cpp:1044-1052`) is true only for a posted overwatch defender who is hit, suppressed, wounded, or too close. The 20 s window is not a latch. `IsOverwatchHoldOpen` (`:1055`) returns false when `bOverwatchHoldClosed` is set, and still reads the live personal reasons. The squad copies the latch onto the view after the running order exists (`PmcSquadSubsystem.cpp:1737-1749`). While he stays on the roof, `PostRangeContactAt` is stamped once and kept (`:1471-1477`). Stepping off clears the clock and the latch (`:1488-1489`).

## Before to after

Side-aware. First track index of that display name on that side. Radius 300 cm, the same disk as `IsAtOverwatchSpot`. The wide-lip rows are the rejected probe. The review rows are this binary.

| Run | Defender | On post | First leave | Shots from post | Overwatch kills | First orders |
| --- | --- | --- | --- | --- | --- | --- |
| wide after3-11 Lind A | A | 20.6 s | 20.6 s | 3/3 | A 1 / B 0 | Ambush 2.39, Objective 0.70 at 20 s |
| wide after3-33 Haas A | A | 26.2 s | 24.6 s | 8/38 | A 0 / B 0 | Ambush 2.36, FallBack 0.94 at 24 s |
| review-11 Lind A | A | 20.6 s | 20.6 s | 0/0 | A 0 / B 0 | Ambush 2.39, Objective 0.70 at 20 s |
| review-33 Haas A | A | 26.2 s | 24.6 s | 0/33 | A 0 / B 0 | Ambush 2.36, FallBack 0.94 at 24 s |

Review logs: `Saved/Logs/G008b-review-11.log:2141` and `Saved/Logs/G008b-review-33.log:2069`. Both arenas exited 0. Lind's three wide-lip shots do not return once the probe cannot walk 27 m to the southwest rim. Haas's eight shots from the post were the same probe. His 33 later shots are after the FallBack.

## Gate

Build: `Scripts/build.ps1` local, one compile after the edit. `Saved/Logs/Build.log`: `Result: Succeeded`, `Total execution time: 279.74 seconds`. The pre-existing `ParseJson` unity clash is a warning.

Arena: X4 tactical seeds 11 and 33, local, fixed step, exit 0. Shots from post 0 and `overwatch_kills` 0 on both. `lip_cm` left at 200.

Smokes, local (clock 06:48, before 08:00): `smoke_parallel.ps1 -Jobs 3 -Remote off -ExtraArgs -Multiprocess`, `CODEX_SMOKE_REMOTE=off`. Exit 1. Wall 314 s.

| Smoke | Result | Seconds |
| --- | --- | --- |
| PmcRoofDefenceSmoke | FAIL | 120.4 (parallel 65.3 s spread 4.6 m; solo 55.1 s spread 4.2 m, guard 0.90, winner B, survivors A 1 / B 3) |
| PmcStalemateSmoke | PASS | 66.1 |
| PmcArenaTacticalSmoke | PASS | 88.4 |
| PmcArenaSmoke | PASS | 66.2 |
| CoverSmoke | PASS | 62.8 |
| CornerHoldSmoke | PASS* | 153.6 (parallel FAIL at 84.9 s: fire stance at 12.0 s, and 52 of 134 barrel shots outside the cone; solo PASS 68.7 s) |
| PeekMinesSmoke | PASS | 54.2 |

`CornerHoldSmoke` is not in `knownFlaky`. The parallel miss is the corner barrel check. The lip block does not run for a corner shot (`!bCornerShot`). Solo passed. The roof-defence thresholds and the fixture were not changed. Logs: `Saved/Logs/Smoke-PmcRoofDefenceSmoke.log:1983`, `Saved/Logs/Smoke-PmcRoofDefenceSmoke-alone.log:2120` and `:2145`.

`Scripts/test.ps1 -Filter CodexTactics.Combat+CodexTactics.AI`: 289 tests, 0 failed, exit 0. That includes `SquadFire.RoofLip`, `FireGate.TuningFileMirrorsDefaults`, and `PmcCommander.OverwatchHold`.

`git diff --check` exit 0. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok` exit 0, risk 2.0 / 4, boundary pass.
