# HitchProbeSmoke red (2026-11-18) - report

Branch `claude-api/hitchprobe` (from main 80f7d55b). Fix commit `db0aceda`.

## Symptom

`CodexTactics.HitchProbeSmoke` failed twice on the worker on main: `first-use spawn windows (7 spawn frames, +10 frames each): 77 frames,
max 141.60 ms (frame #984)` / `max 149.15 ms (frame #990) <= 50 ms`. The frame is the first MILITARY_ZOMBIE spawn (class + mesh resident).

## Cause (evidence)

`Saved/Logs/HitchProbe-worker-fail.log` (L_Gen_Depot_1):

| log frame | event |
|---|---|
| [779] | `[GameplayPreload] ready` - world unpaused |
| [910] | `Minimap: floor scan` (setup 38.6 ms, first unpaused ticks + 1 s) |
| [924-957] | `[DropLinks] ... 331 drop links ... in 120 ms` (world timer poll) - the probe waits for this, so it was outside the window |
| **[984]** | `[HitchProbe] ... roster 7` -> `[Mantle] 1170 mantle links ... in 122 ms` -> `MILITARY_ZOMBIE class resident: yes` |
| | `[HitchProbe] frame #984 MILITARY_ZOMBIE + PMC 141.60 ms` |

The mantle-link generation (`UMantleSubsystem::PollReady`, a world timer that waits for the drop links and polls every 0.5 s) ran
**in the first spawn frame** of the measured window: 122 ms of 141.6 ms. It is a race against the probe start (the probe waits only for the drop
links), which is why it passed earlier this week and fails depending on timing - no enemy spawn / anim / shader cost involved (the spawn itself was
1.89 ms).

Temporary `[HitchTemp]` logs (frame + world-paused state in both generators; removed before the commit) confirmed the timing. After moving
the two link generators under the preload, the first run on the fix still failed with **159.24 ms at frame #1716**: the **minimap floor scan**
(`Minimap: ... setup 76.8 ms` at [716], under parallel load) had moved into the window instead (before, it ran while the probe still waited for
the drop links). So all three level-start one-shot jobs had to move.

## Fix

`UGameplayPreloadSubsystem::PrepareLevelStartWork` (Compiling phase, before `Finish(true)`, still under the world pause / loading cover),
one job per frame:

1. `UDropLinkSubsystem::TryGenerateForPreload` (new; `IsNavigationReady` shared with the poll),
2. `UMantleSubsystem::TryGenerateForPreload` (new; `IsReadyToGenerate` shared with the poll, still after the drop links),
3. `UMinimapSubsystem::TrySetupForPreload` (new; the squad already stands, only a navigation build would block).

Each clears its own poll timer when it generates. Fail-open, no waiting: a navmesh built at runtime (L_MovementTest) does not build while the
world is paused (navigation does not tick paused), so when it is not ready the step is skipped at once and the subsystems' old level-start paths do
the work as before (a first version waited up to 20 s and broke PauseMenuSmoke / SaveRoundTripSmoke on L_MovementTest - fixed before the commit).
Generated levels (prebuilt navmesh) are ready under the preload. A/B: `Codex.Preload.LevelStartWork 0` or `-PreloadNoLevelStartWork`.
Budget (50 ms) and the smoke untouched.

## Before / after (first-use window max, budget 50 ms)

| run | max | frame | drop / mantle links generated |
|---|---|---|---|
| worker, main (before) | 141.60 ms / 149.15 ms | #984 / #990 | gameplay frames, mantle **in** the window |
| local, links moved, minimap not yet | 159.24 ms (FAIL) | #1716 | paused; minimap scan in the window |
| local, fix, batch of 4 | **20.57 ms** | #756 | paused (93 / 127 ms under the cover), minimap 42.8 ms under the cover |
| local, fix, alone | **20.32 ms** | #780 | paused |
| local, fix + A/B switch, run B | **18.93 ms** | #786 | paused |
| local, A/B run A (`-PreloadNoLevelStartWork`, old flow) | 23.48 ms (pass) | #900 | gameplay frames (mantle at 868, just before the window) |

Run A shows the race: on this machine the old flow happened to finish the mantle links before the window; on the worker it did not.

## Tests

- Build: `Scripts/build.ps1` OK (pre-existing unity-clash warning `ParseJson` in CodexTacticsTests, unrelated).
- `smoke_parallel -Commands HitchProbeSmoke,EnemyArchetypeSmoke,MilitaryZombieSmoke,PmcArenaSmoke -Jobs 4`: 4/4 PASS. HitchProbeSmoke alone: PASS
  (2nd run). With the A/B switch build: HitchProbeSmoke PASS again (A/B run B), 4 passing runs in total. The last build only removed the temporary logs (compiled, not re-run).
- `Scripts/test.ps1 -Smart` (with the first, 20 s-wait version): unit 389/0; smokes 61 run, FAIL PmcRoofDefenceSmoke, SuppressionSmoke,
  PauseMenuSmoke, SaveRoundTripSmoke (timeout); PASS* CornerHoldSmoke (new flaky), PmcStalemateSmoke (known flaky). After the no-wait fix,
  rerun: PauseMenuSmoke PASS, SaveRoundTripSmoke PASS, HitchProbeSmoke PASS, PmcRoofDefenceSmoke PASS*, **SuppressionSmoke FAIL**.
- **SuppressionSmoke** (`F: the pack splits round it (6 left, 0 right)`, L_MovementTest): not caused by this change - on that map the navmesh is
  not ready under the preload, the new step is skipped at once (`navmesh not ready under the preload`) and the drop / mantle / minimap work runs at the
  same frames as on main. Separate red, needs its own look.
- **PmcRoofDefenceSmoke** (guard share > 0.6) is flaky on its own: A/B on the switch build - A (old flow, `-PreloadNoLevelStartWork`) **FAIL 0.47**,
  B (fix) PASS 0.87. Earlier runs with the fix: 0.58 / 0.51 FAIL, 0.42 FAIL (alone), 0.79 PASS (alone). Not in knownFlaky; the G008 merge reported 0.73-0.97 on 3 seeds.
  A separate issue (the guard share under load), not this fix.

## Notes

- The budget overran the planned "one build / one batch": the first fix run showed the minimap scan behind the mantle links, and the first full
  test run showed the 20 s wait on runtime-navmesh maps. Five builds, four smoke runs + one test.ps1 -Smart.
- The HitchProbeSmoke comment (`drop links + the minimap floor scan ... after the preload release when it runs`) is now only true for runtime-navmesh
  maps; left untouched (smoke not changed in this task).
