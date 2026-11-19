# G010 - roof defence flake

Status: fix committed. The 10x worker run is the lead's (command below). One local `PmcRoofDefenceSmoke` on this build PASSed (spread 12.1 m, guard 0.96).

Fix commit: `4f4105a42e07b7614e9cd0fe40770f5e1b5f352a` (`4f4105a4`).
Branch: `grok/G010-roof-defence-flake`, on top of `origin/main` `16b7b6ba` (the RESOURCES line). This report does not change the build.

## Cause

Two independent causes in the lead's batch `Saved/Logs/SeedBatch/20261119-124936` (RoofDefence10, same fixed seed, `-Multiprocess`). Official results are the task table (4 FAIL / 6 PASS). Tracks are `runNN/round_1.track.json`.

The access is the Depot_1 stair at (-3640, 2480, 600). The guard post is 1.5 m inset, about (-3507, 2411, 610). The access guard is always Operator Kessler. Vance and Roux are sector shooters and are never within 4 m of the stair (0 near-samples in all 10 runs). Only Kessler's presence creates the guard share. Dead men are already excluded from the snapshot, so the share is not dead time.

### Guard share is a real walk off the stair (runs 01, 07, 10)

Depot_1 has no tactical file, so the G009 ranged hold never opens. After the first sighting Ambush on the post scores 0 and Hold scores about 0.10. Heal scores 0.20-1.01 when a mate is wounded and not currently seen (`pmc_commander.json` heal 1.1, heal_below 0.45). The executor then walks to the patient when he is farther than 230 cm. Off the post, Ambush scores 1.05 and pulls him back, and Heal fires again.

Away-from-stair samples while the man is alive:

| run | smoke | what the track shows |
|---|---|---|
| 01 | FAIL, guard 0.55, spread 4.7, A elimination | Kessler near 79/140. Away orders include Heal 12, Ambush 23, Objective 10, FallBack 10. Vance dies at 37.5 s, Roux at 70.8 s, Kessler lives. Heal starts at t=10 while he is already on the post (1.7 m) and repeats until about t=76. |
| 07 | FAIL, guard 0.41, spread 7.9, A elimination | All three live the whole 41 s (0 solo samples). Kessler near 18/42. Away: Heal 12, Ambush 9. This run fails on Heal alone. |
| 10 | FAIL, guard 0.55, spread 10.1, A elimination | Roux dies at 1 s. Kessler near 37/66, Vance 0/66. Away: Heal 10, Ambush 17. Heal from about t=25 to t=58. |
| 05 | FAIL on spread only, guard 0.64 | He stays until a real retake (Objective 0.80 at t=71), returns, then a late Objective/FallBack. Not the Heal yo-yo. |

Dropping only the Heal leaves from the recorded tracks (a projection, not a re-run) puts run 01 near 0.83, run 07 near 0.98, run 10 near 0.97. The late Objective/FallBack in run 01 is a real retake and is left alone. FallBack and Objective were not zeroed.

### Spread is a measurement artefact (runs 01 and 05)

`MeanSpreadM` of one living defender returns 0, and `SampleDefence` added that 0 into the 1 Hz mean. Formed seconds (2+ living) were already above the bar:

| run | coded mean | formed mean (solo seconds skipped) | solo samples |
|---|---|---|---|
| 01 | 4.7 m | 9.3 m | 69 of 140 |
| 05 | 3.6 m | 13.6 m | 116 of 157 (Roux dies at 40.6 s, Kessler at 155.8 s) |

Runs 02 and 04 also have a long solo tail, and their formed spread is high enough that the diluted mean stays >= 5 m. Guard sampling is unchanged. The check stays `spread >= 5` and `guard share > 0.6`.

Not the cause: G009's hold (no tactical post on this roof), attacker scoring, shared cover (0 in all 10), the guard dying and dead time being counted.

## Fix

A defender standing on his post scores Heal 0 when the wounded mate is farther than `heal_reach_m`. Within that reach he heals in place. Off the post, Heal keeps its score and the 1.05 Ambush walk back still wins. Attackers and unposted men are unchanged. The same reach is what the heal walk already used (the local 230 cm constant now reads the weight, so the two cannot drift).

- `Source/CodexTactics/Private/AI/PmcCommanderRules.cpp:291` Heal case. Standing post = `bAtPost` and (`bHasDefencePost` or `bOnRangedPost` or `bOnOverwatchRoof`). Zero when `WoundedMateDistanceCm > HealReachCm`. Distance -1 (no patient) does not trip the comparison; the old formula still zeroes it.
- `Source/CodexTactics/Private/AI/PmcCommanderRules.cpp:762` loads `heal_reach_m` as centimetres.
- `Source/CodexTactics/Public/AI/PmcCommanderRules.h:347` `HealReachCm = 230`.
- `Content/Data/AI/pmc_commander.json:78` `"heal_reach_m": 2.3`, and the `_defence_comment` sentence. Frozen, not a trainer gene. `pmc_commander.bounds.json` is not extended.
- `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:1216` and `:2200` use `Team.Weights.HealReachCm` (was a file-local 230 cm).

A 1 Hz spread sample with fewer than two living defenders is not a sample. `MeanSpreadM` of one man still returns 0, so the existing assertion stays.

- `Source/CodexTactics/Private/AI/PmcDefenceRules.cpp:953` `TrySpreadSample` returns false and leaves the out-value untouched.
- `Source/CodexTactics/Public/AI/PmcDefenceRules.h:367` declaration.
- `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp:2278` `SampleDefence` adds the sample only when `TrySpreadSample` returns true. An all-solo round still reports 0 (no samples).
- `docs/port/grok/outbox/G002-arena-behaviour-schema.md:53` the spread sentence now says a one-man sample is left out of the mean.

## Tests

- `CodexTactics.AI.PmcCommander.PostHealStays`: on a defence post, a patient at 800 cm scores Heal 0 and Crouch wins; at 200 cm and at exactly `HealReachCm` Heal wins; one centimetre past is 0 again; off the post Heal scores and Ambush still wins; an unposted medic still picks Heal; a window post and an overwatch roof score Heal 0 for a far patient.
- `CodexTactics.AI.PmcCommander.WeightsJson`: shipped file loads `HealReachCm == 230`; a missing key keeps 230; `heal_reach_m` 4.5 loads as 450 cm.
- `CodexTactics.AI.PmcDefence.SpreadSample`: one man and nobody are not samples and do not write 0; two men 10 m apart are a sample; two formed seconds plus one solo second average 10 m. `MeanSpreadM` of one man stays 0.

`Scripts/test.ps1 -Smart`: 148 tests, 0 failed (31 s). Log `Saved/Logs/AutomationTests.log`. Both new tests `Result={Success}`.

## Gate

| step | result |
|---|---|
| `Scripts/build.ps1 -Remote worker` | Not completed on the worker. The worker lock was held by `worker_sync.ps1 -Ref origin/main -Build -Jobs 5 -Commands all` (local pid 53360, since 13:01). That full sweep would outlast the 30 minute wait, so the editor was built locally. |
| `Scripts/build.ps1` (local) | Exit 0. `Result: Succeeded`, 383 s. `Saved/Logs/Build.log`. |
| `Scripts/test.ps1 -Smart` units | 148 tests, 0 failed. |
| `Scripts/test.ps1 -Smart` smokes | 36 run, 0 failed, wall 875 s. Local, because the tree was dirty (`smoke_remote` exit 99). |
| named smokes | `PmcArenaTacticalSmoke` PASS 75 s, `PmcStalemateSmoke` PASS 71 s, `TacticalPmcSmoke` PASS 64 s, `PmcArenaSmoke` PASS 66 s. |
| `PmcRoofDefenceSmoke` (one run, not the 10x) | PASS 71 s. spread 12.1 m, guard 0.96, shared cover pairs 0, objective share 0.00. `Saved/Logs/Smoke-PmcRoofDefenceSmoke.log`. |
| `PmcCrossroadsSmoke`, `PmcAlleySmoke` | KNOWN-FAIL (T-04b until T-08). Unchanged. |
| `CoverSmoke` | PASS* . The parallel batch hit the documented cover-entry timing (`anim_M4_cvr_std_idle_L` while moving right). The solo re-run PASSed. The script printed it as new flaky because it is not in `knownFlaky`. Not this diff. |
| `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok` | Exit 0. Boundary PASS. Risk 2.0/4. |
| `git diff --check` | Clean on the ten files before the commit. |

Before -> after, for the four failing runs. The "after" column for the batch is the track projection above, except the one measured local run (spread 12.1 m, guard 0.96), which is a single fixed-seed pass and not a claim of 10/10.

| run | before | projected after both fixes |
|---|---|---|
| 01 | spread 4.7, guard 0.55 | spread 9.3, guard ~0.83 |
| 05 | spread 3.6, guard 0.64 | spread 13.6, guard unchanged (already over 0.6) |
| 07 | spread 7.9, guard 0.41 | spread unchanged, guard ~0.98 |
| 10 | spread 10.1, guard 0.55 | spread unchanged, guard ~0.97 |

Thresholds were not lowered. Attacker scoring was not changed. `ambush_max_hold_s` was not changed.

## Lead's 10x

NOT done here. The worker was busy with the full sweep above, and the task leaves the 10 repeats to the lead.

From a checkout of this branch whose `Binaries/Win64` is the editor build of the fix (this worktree is), after the branch is on origin:

```
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -Ref origin/grok/G010-roof-defence-flake -SkipSmoke
ssh codex-worker "powershell -NoProfile -ExecutionPolicy Bypass -File C:\Work\CodexTactics\Scripts\Tools\worker_seed_batch.ps1 -Preset RoofDefence10"
```

Code ref, if you pin a SHA instead of the branch: `4f4105a42e07b7614e9cd0fe40770f5e1b5f352a`. The report commit on top of it does not change code. `worker_sync -Ref` copies `Binaries/Win64` from the checkout that runs it; the worker does not compile.
