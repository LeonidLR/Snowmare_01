# 004 — Enemy first-use hitch: measurement checkpoint

Status: IN PROGRESS. No performance fix or successful HitchProbeSmoke run is claimed.
Branch/worktree: interim/2026-11-12, CodexTactics-interim only.

## Measurement part (2026-11-12)

- Added CodexTactics.HitchProbeSmoke on the existing Depot map, exclusive fresh-process execution with cpu/loadtime/frame tracing. It issues a real leader move, requires actual movement, spawns the active pressure/wave roster plus registered enemy classes, and reproduces military-zombie + three-PMC spawns in one frame. It records actual class/mesh paths and residency without preloading.
- Records top frame/spawn durations; fails if any measured post-setup game-thread frame, individual spawn, or same-frame spawn sum exceeds 250 ms. Startup/setup are logged separately and are not hidden as gameplay measurements.
- Added CPU trace scopes and elapsed logs around StartPlay, SpawnSquad, operative animation initialization, and ladder clip loading. Gameplay/load behavior is unchanged.
- Files: Scripts/test_map.json; HitchProbeSmokeCommand.cpp; OperativeAnimInstance.cpp; CodexTacticsGameMode.cpp; LadderClimbComponent.cpp.

Validation: test.ps1 -CheckMap PASS; git diff --check PASS. Build attempted but build.ps1 refused because the designer has the interim editor open. No editor was closed and no DLL-lock guard was bypassed. C++ compilation and baseline smoke remain UNVERIFIED at this checkpoint.

Routing: architecture 4 | gpt-6.1-terra | read-only analysis PASS; diagnostic implementation 3 | gpt-6.1-terra | static review PASS, build/runtime pending. Local model was not started alongside the designer's editor.

## Evidence and next steps

Designer logs play.log frame 614 (SKM_PmcOperator) and play2.log frame 835 (SK_Cutter_1slot) document synchronous first-use loads/mesh waits. The 13.9-second startup attribution remains a hypothesis until measured. Gemini's preload draft is not a runtime manifest: validation found folders/missing assets and incorrect ladder paths.

After the editor closes: build, run the cold probe, separate fixture failures from genuine >250 ms frames, and record trace/log timings. Then implement asynchronous full-roster preload/readiness, shared animation/ladder preparation and spawn scheduling in small commits, with cold/warm PIE versus -game evidence and touched-system regressions. No preload fix, introduction commit, packaged availability or hitch elimination is asserted yet.

Main, source packs, maps, Config and assets are untouched by this checkpoint.

## Source-history investigation (read-only; not a bisect result)

Terra traced the existing load paths through git history. These are introduction points for suspect code/assets, NOT proof of the hitch's causal commit:

| Path | Verified history | Implication |
|---|---|---|
| PMC body/gear synchronous load | 28650298 (2026-11-03), older SpawnOperative/body override e11e1a64 (2026-10-09); RPG additions 00eb12cc / 5299542d (2026-11-10) | The base SKM_PmcOperator cold-load path predates the reported regression window. |
| Wave class / military-zombie variant | c79d300b (2026-09-30), 4739359e (2026-11-07) | Existing sync loading can first surface when a new roster entry appears. |
| Cutter optimized mesh | bc84d81e / 478b09f7 (2026-10-21) | Binary addition/Blueprint history does not prove when editor DDC compilation first occurred. |
| Three ladder clips per component | 8d412c33 (2026-10-22) | Old synchronous path; a shared cache still needs measurement and lifecycle validation. |
| Animation initialization expansion | 7832f561 (2026-11-08), pistol c66dee37, launcher 66e7f543 / 9cbb2363 (11-09), prone 1875e6bf and injured 3023a233 / 632d61ec (11-10), stealth 90a6bf64 (11-11) | Growth agrees with the complaint interval but does not isolate the startup cost without a trace. |
| Concurrent 30-second schedules | Pressure b385755c (11-01), PMC 2f05a0e9 (11-03) | Schedule overlap predates the report; do not call it a newly introduced scheduling regression. |

Dates above follow commit-local timestamps; normalize against log timestamps before causal ordering. JSON clip-path leaf counts are not runtime-load counts (retarget/fallback/repeated references differ). No bisect, cold/warm timing, packaged test, or >250 ms gate has yet run for this checkpoint.
