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

## Executed baseline and first catalog part (2026-11-12)

Current diagnostic C++ build PASS (46.89 s, UE 5.8.3 CL58210709). Two fresh-process -game runs reproduced the issue; these are NOT cold/warm PIE measurements:

| Measurement | First process | Second process, explicit CPU trace |
|---|---:|---:|
| Process elapsed | 92.4 s | 33.8 s |
| Maximum measured gameplay frame | 3641.83 ms | 1755.95 ms |
| PMC squad spawn (3) | 2366.08 ms | 1320.89 ms |
| Military zombie spawn, same frame | 1266.16 ms | 431.07 ms |
| Same-frame spawn sum | 3632.23 ms | 1751.96 ms |

Leader actually moved 575.9 cm; configured class and seven-entry roster checks passed. Both performance gates FAIL as expected before a fix. First-run startup: SpawnSquad 7434.34 ms, first operative AnimInitialize 3460.69 ms. Second-run startup: StartPlay 4049.62 ms, SpawnSquad 3989.28 ms, first AnimInitialize 1802.11 ms. Process/DDC warm-up differs; do not equate the second fresh process with a second PIE in the same editor.

Evidence in interim Saved: Logs/HitchProbe-cold-baseline-2026-11-12.json; Logs/HitchProbe-traced-baseline-2026-11-12.json; Orchestrator/hitch-baseline-traced-2026-11-12.log; Orchestrator/hitch-baseline-2026-11-12.utrace; Orchestrator/hitch-baseline-events.csv. The first harness log was overwritten by its normal second-run log naming; first metrics above were captured before overwrite. Explicit -tracefile was needed: the first run had only an in-memory trace. Insights relative export initially landed in its executable directory; that single generated CSV was moved to interim, future exports must use absolute paths. No engine source/assets were changed.

CPU trace proof (GameThread, second run): FEngineLoop::Tick at trace t28.538724 = 1.756003 s; Tick_Core = 1.752362 s; SKM_PmcOperator / ABP_Operative_C -> Codex_OperativeAnimInitialize = 1.251966 s; military-zombie LoadObject -> StaticLoadObjectInternal -> LoadPackageInternal -> FlushAsyncLoading = 0.407535 s. These are measured synchronous load/initialization stalls, not speculative perception/nav cost. Original 13.9 s PIE gap still has no direct trace attribution. Engine toolset Python startup AttributeError messages are separate pre-measurement engine-plugin errors, not attributed to the spawn hitch.

First implementation part: new Core/GameplayPreloadCatalog gathers configured enemy/variant classes, squad/PMC bodies, loadout/PMC weapons/looks, ladder CDO clips and process-cached weapon-animation JSON paths, retaining the runtime retarget helper and excluding folder metadata. It performs no loading and does not alter gameplay yet. Native M4/ProneDive/interaction/blizzard and Blueprint/SCS overrides remain explicit gaps. Catalog build PASS 9.07 s; two CodexTactics.Core.GameplayPreload tests PASS; impact-map check PASS. Build's Find unity-clash heuristic warning concerns distinct named test namespaces, no compiler/link failure.

Routing: 4 | gpt-6-astra | readiness/lifecycle design PASS (review only); 3 (Jev .89) | gpt-6.1-terra | initial catalog PASS after build/tests. Async coordinator, complete catalog, deferred binding, shared ladder preparation, readiness/spawn gates and post-fix proof remain IN PROGRESS; task004 is not done.
