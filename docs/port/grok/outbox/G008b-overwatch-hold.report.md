# G008b report - overwatch roof hold, and the round that leaves it

Branch `grok/G008b-overwatch-hold`. Merged with `origin/main` at `08b64ee6`. Nothing pushed. `main` not touched. Maps, `Content/VFX`, `Config/DefaultScalability.ini`, and the designer's untracked `Content/` were not staged.

## Handover

Done:

- The posted defender stays on the overwatch roof for the post's engagement window (`07c27808`, `089c480e`). A mate dying does not close the hold. Those commits stay.
- Seed 11, team-1 Operator Lind (sniper, clip 5): the first fight `[FireTrace]` is `no target`. The crouch muzzle hits the roof rim. The sight line already starts past it, so the team sees a man at 80-85 m and no round leaves.
- A low rim is now stepped past (`fire_gate.json` `roof_lip`). Seed 11 after that: 3 shots from the post, 3 overwatch hits, side metric `overwatch_kills` 1, then he leaves at 20.6 s on Objective 0.70 (the 20 s gene).

Left:

- Acceptance asks `overwatch_kills > 0` and shots-from-post `> 0` on 2-3 X4 tactical seeds. Seed 11 meets both. Seed 33 (Haas) fires 8 shots from the post and the side kill metric stays 0. Seed 22 (Roux) leaves at 14 s on the allowed too-close FallBack and fires 0 from the post.
- `Scripts/test.ps1 -Smart` exited 1. Units 622/0. The only smoke that failed its solo re-run is `PmcRoofDefenceSmoke` (roofs seed 1). The named batch of that smoke, with `-Multiprocess`, passed on this same binary. The check was not weakened.

Next exact step, if someone continues:

- One quiet local `PmcRoofDefenceSmoke` with the named-batch flags (`-Multiprocess -UseFixedTimeStep -FPS=60 -FixedSeed`) and read `spread_m` / `guard_share`. The two Smart failures of roofs seed 1 disagree with each other (guard 0.35, then spread 0.7 m). The rounds end by elimination from the ground (RPG-7, a ground Lind). `roof_lip` retries only when the shooter stands at least `min_drop_cm` (180) above the target, so an upward shot is unchanged.

## Cause

`Saved/Logs/G008b-trace-11.log`, X4 tactical seed 11, `Codex.Debug.FireTrace 1`, binary before the lip retry.

Team-1 Lind is the sniper (clip 5) on `B_hall_n`, post `V(5600, -750, 910)`, range 11000 cm (`:1636`-`:1638`). Team-2 Lind is the m16 (clip 30). The hold opens at `:1667`-`:1670`: contact 8544 cm, "opens fire" (the posture order, attention off 1.00), Ambush 2.39, hp 100%.

The first `[FireTrace]` for the sniper after that order:

- `:1686` `exploring: no opening shot` (stance 1, clip 5). The fight has not started, so `RunCombatShooting` returns before `FindShootTarget` (`OperativeCharacterCombat.cpp:836`).
- `:1709` `no target` (stance 1, clip 5). That is the first fight reason. `FindShootTarget` returned empty (`OperativeCharacterCombat.cpp:904-907`).

The detail on `:1709` lists the three nearest Enemy-tagged actors: himself at 0 cm and two teammates, each `not visible to the team`. Both teams carry the tag, so that list is not the shoot set. The man the commander sees at 8544 cm is inside the post range and inside the sniper's 120 m. `EvaluateShotLine` (`OperativeCharacterCombat.cpp:121`) refuses an unseen man before the muzzle trace. A seen man who fails the muzzle trace is dropped with no line of his own, and the reason collapses to `no target`.

The muzzle is the crouch point (85 cm). On this roof the shot toward the team-2 spawn runs southwest. The rim in that direction is about 27 m out, inside `lip_cm` (60 m). A parapet near 100 cm, and the roof slab on a descending line, stop the trace. The sight sample starts past that rim, which is why the team already knows the man. `ShouldHoldFire` was left alone (attention off 1.00). The kneel is the sniper gate (`AS_Knee_Aim_Start` / `AS_Knee_Aim_Idle`), and standing fire is unused. The dead zone (`max_horizontal_cm` 240) does not cover an 80 m shot from a 9 m roof. Weapon range is not the cap: engagement is `min(11000, 12000)`.

## What changed

`fix(combat): a kneeling shot from a roof steps past the parapet`

When the body line is blocked, the shot is not a corner shot, and his feet are at least `min_drop_cm` above the target, the line walks a horizontal probe out to `lip_cm` (`OperativeCharacterCombat.cpp:181-261`). A hit whose top sits in the band around his feet is his own rim (`SquadFireRules::MayClearRoofLip`, `SquadFireRules.cpp:108-127`). The shot is retried from `past_cm` beyond that face, at muzzle height (`RoofLipRetryOrigin`, `:129-133`). A retry that starts inside geometry stays blocked. A tall wall, a hit past `lip_cm`, and a man on the ground (drop under 180 cm, so prone fire through a wall) do not retry. The dead zone still applies after the line. `HasUsefulFireLine` is unchanged.

Numbers, `Content/Data/Combat/fire_gate.json` `roof_lip`, mirrored by `FRoofLipConfig` (`FireGateRules.h:27-38`) and loaded in `FireGateRules.cpp:51-55`:

| Key | cm |
| --- | --- |
| lip_cm | 6000 |
| past_cm | 80 |
| min_drop_cm | 180 |
| band_above_cm | 120 |
| band_below_cm | 40 |

`past_cm` 80 clears the fixture parapet (20 cm thick) and stays inside a 4 m wall. Unit test `CodexTactics.Combat.SquadFire.RoofLip` (`SquadFireRulesTest.cpp:154`). `FireGateRulesTest.cpp:239` lists `roof_lip.lip_cm` and `roof_lip.past_cm` as known keys, so the file must match the struct defaults.

The hold is unchanged. `IsOverwatchHoldOpen` still closes on hit, suppression, a personal wound, too close, or `ambush_max_hold_s` (20) after in-range contact. Objective / TakeCover / Advance / Flank stay 0 while it is open.

## Before to after

Side-aware. The posted fighter is the first track index of that display name. Radius 300 cm, the same disk as `IsAtOverwatchSpot`. Shots from post are that fighter's shot events while his feet are inside the disk.

| Run | Defender | On post | First leave | Shots from post | Overwatch kills | First orders |
| --- | --- | --- | --- | --- | --- | --- |
| before-11 Lind A | A | 0.6 s | 0.6 s | 0/5 | 0 | Ambush 1.00, then the sector walk |
| before-22 Roux B | B | 0.8 s | 0.8 s | 0/44 | 0 | Ambush 1.00 at 0 s |
| before-33 Haas A | A | 0.6 s | 0.6 s | 0/30 | 0 | Ambush 1.00 at 0 s |
| after2-11 Lind A | A | 20.6 s | 20.6 s | 0/0 | 0 | Ambush 2.39, Objective 0.70 at 20 s |
| after2-22 Roux B | B | 14.8 s | 14.8 s | 0/0 | 0 | Ambush 1.58, FallBack 0.92 at 14 s |
| after2-33 Haas A | A | 26.2 s | 24.6 s | 0/17 | 0 | Ambush 2.36, FallBack 0.94 at 24 s |
| after3-11 Lind A | A | 20.6 s | 20.6 s | 3/3 | 1 | Ambush 2.39, Objective 0.70 at 20 s |
| after3-22 Roux B | B | 14.8 s | 14.8 s | 0/49 | 0 | Ambush 1.58, FallBack 0.92 at 14 s |
| after3-33 Haas A | A | 26.2 s | 24.6 s | 8/38 | 0 | Ambush 2.36, FallBack 0.94 at 24 s |

after3 logs: `Saved/Logs/G008b-after3-{11,22,33}.log`. Seed 11 behaviour line `:2141` `overwatch kills A 1 / B 0`. The 3 Lind shots are also `overwatch` hits. Haas's 8 shots include 3 overwatch hits; the side kill metric stays 0 (no killing blow inside the 3 s / on-spot window). Roux's 49 shots are after the allowed FallBack. His weapon caps the roof at 60 m, and the threat is already inside the too-close band. Median time on post: before 0.6 s, after3 20.6 s.

## Gate

Build: `Scripts/build.ps1` local, after the lip edit. `Saved/Logs/Build.log`: `Result: Succeeded`, `Total execution time: 24.84 seconds`. The first compile of the edit failed on `C2440` (`EvaluateShotLine` is const; the penetrating retry must not assign `this`). That assignment was removed. The pre-existing `ParseJson` unity clash is a warning.

Arena: seeds 11, 22, 33, scenario tactical, `L_Gen_Depot_X4`, local, fixed step. after3 exited 0.

`git diff --check` on the eight combat files: exit 0. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok`: exit 0, risk 1.7 / 4, boundary pass. Untracked designer `Content/` and `Config/DefaultScalability.ini` were outside that diff.

Named smokes, `smoke_parallel.ps1 -Jobs 3 -Remote off -ExtraArgs -Multiprocess`, `CODEX_SMOKE_REMOTE=off`. Exit 0. Wall 194 s.

| Smoke | Result | Seconds |
| --- | --- | --- |
| PmcRoofDefenceSmoke | PASS | 70.1 |
| PmcStalemateSmoke | PASS* | 137.0 (first try FAIL 67.2 s, solo re-run PASS 69.8 s) |
| PmcArenaTacticalSmoke | PASS | 75.3 |
| PmcArenaSmoke | PASS | 51.5 |

Stalemate is the known flake (quiet spell / attackers short of the point; `smoke_parallel` printed the 2026-11-10 / 2026-11-11 note). The PASS log for roof defence in this batch was overwritten by the Smart run below.

`Scripts/test.ps1 -Smart -Jobs 3`, `CODEX_SMOKE_REMOTE=off`, base HEAD while the lip edit was uncommitted. Units: `622 tests, 0 failed` in 101 s (the filter includes `CodexTactics.Combat.SquadFire.RoofLip`). Smokes: 71 mapped, wall 2280 s, exit 1.

- `PmcRoofDefenceSmoke` FAIL. Parallel (`Saved/Logs/Smoke-PmcRoofDefenceSmoke.log:2471-2474`): roofs seed 1, spread 5.2 m (passes `>= 5`), guard share 0.35 (fails `> 0.6`), winner B by elimination. Solo re-run (`Smoke-PmcRoofDefenceSmoke-alone.log:1989-2004`): same seed, spread 0.7 m (fails), guard 0.96 (passes). Defenders die on the roof to RPG-7 and to Operator Lind, who spawned on the ground (`:1612`, `:1664`, `:1714`).
- `GroupSelectSmoke`, `GroupCoverSmoke`, `SquadControlSmoke`: TIMEOUT at 300 s under the 3-wide load, then PASS alone (PASS*). SaveLoadSmoke PASS 43.6 s.
- `PmcStalemateSmoke`, `PmcArenaTacticalSmoke`, `PmcArenaSmoke` PASS inside this Smart batch as well (68.1 s, 79.6 s, 64.5 s).

The roof-defence check, its 5 m / 0.6 thresholds, and its fixture were not changed.
