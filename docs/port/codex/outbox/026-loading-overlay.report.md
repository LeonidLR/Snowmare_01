# 026 — Loading overlay with actual path progress

Status: implementation, build, focused unit and required four headless smokes passed. Parent reviewed and checkpointed implementation at `5d8f3e3b`. Rendered enabled/disabled acceptance evidence is still outstanding and handled separately by parent.

## Behavior

HUD-owned native Slate covers the entire viewport with an opaque dark background above Canvas, UMG (Z1–20), and the frontend layout (default Z100). It shows `Загрузка уровня… NN %` and a Russian phase label while Preparing, including while the world is paused. HitTestInvisible excludes the entire widget tree from input hit testing; no input mode, focus, clock, pause or gameplay gate is changed. Weak HUD captures protect attribute lifetime; EndPlay removes the exact owned widget from its weak viewport and clears ownership. Ready, Failed, Disabled and NotApplicable collapse it. The snapshot explicitly handles disabled preload before StartPreparation as well, so `-CodexPreload=0` cannot show the overlay during the WaitingForMode interval.

## Progress semantics

`UGameplayPreloadSubsystem::GetProgress` sums `FStreamableHandle::GetLoadedCount(Completed, Total)` over the already-retained handles. Verified local UE5.8 source: `Engine/Source/Runtime/Engine/Classes/Engine/StreamableManager.h:394` and `Private/StreamableManager.cpp:900`. The engine includes failed requests in completed counts; failure release remains unchanged. Counts represent requested paths per batch, not package bytes or elapsed time. Dependencies are discovered after initial loading: the measured 22-path initial request can become 646 total requests after the additional 624-path dependency request. Percentage may decrease at discovery (22/22=100%, then 22/646=3%). No fixed final denominator or monotonic/elapsed progress is invented. Paths can be 100% while final resource preparation/binding still runs; the phase label remains visible until release. Empty/invalid counts yield 0%, completed counts clamp to [0,total], and int64 accumulation/double ratio avoid integer multiplication overflow.

## Weather cover retained

`Source/CodexTactics/Private/Environment/WeatherPresenterActor.cpp:460` applies the startup manual fade; `:468` holds it while Preparing. Both are unchanged. Task025 established that this hold causes the black world while Preparing. The existing routine also waits for minimum startup ticks and snow warmup (:473-476), addressing weather startup settling. The hold is retained because this task explains the wait rather than changing weather readiness/release semantics. The new viewport overlay covers the same interval without exposing unfinished weather.

## Scope and checks

Only GameplayPreloadSubsystem.h/cpp, CodexTacticsHUD.h/cpp, the new GameplayPreloadProgressTest.cpp and this report are task changes. No assets, maps, Config, preload list, gameplay logic or protected combat code changed. Existing imported/untracked files remain untouched. `git diff --check`: PASS before build.

Focused test `CodexTactics.Core.GameplayPreload.Progress` covers actual 22/646 request examples, dependency discovery regression, zero/negative/excess/maximum counts, Preparing visibility at 100% paths, every terminal/disabled/not-applicable state, and null-world getter behavior.

Build result: `Result: Succeeded`; `Total execution time: 872.19 seconds`; wrapper exit 0. Full log: `Saved/Logs/Build.log`. After the build, parent-requested public API documentation comments were added to IsVisible/GetPercent; this is a comment-only source change with no behavior difference. Focused test: `Test Completed. Result={Success} Name={Progress} Path={CodexTactics.Core.GameplayPreload.Progress}`; `Automation Test Queue Empty 1 tests performed.`; engine exit 0. Log: `Saved/Logs/codex026-focused-multiprocess.log`.

The first standard `Scripts/test.ps1 -Filter CodexTactics.Core.GameplayPreload.Progress` launch stalled at TurnkeySupport startup and executed **zero tests**. Its owned PID50704 was verified against the agents project/focused filter, the log preserved at `Saved/Logs/codex026-focused-stalled.log`, and only that process tree terminated; wrapper exited 1 and released the shared lock normally. Parent authorized one retry using the existing `Scripts/smoke_parallel.ps1:31` workaround `-Multiprocess` (skips startup UBT ValidatePlatforms). Retry retained the original test wrapper engine flags, report export and TestExit arguments and used standard Enter-AgentLock/Exit-AgentLock. No scripts were edited and no lock bypass was used. The retry completed the one focused test successfully. Rendered observations and their limits are recorded below; headless tests alone were not used to establish appearance. Parent will run those captures; this smoke phase made no visual claims.

Routing: task026 Router2 confidence .55; parent-approved bounded implementation, no deeper preload profiling.

## Required smoke phase after parent checkpoint

Ran once each with the standard `Scripts/smoke_parallel.ps1` wrapper: `-Commands HitchProbeSmoke,MainMenuSmoke,MovementSmoke,SoloStartSmoke -NoRetry -Jobs 1 -ExtraArgs '-Multiprocess' -ResultJson Saved/Logs/codex026-smokes.json`. No gameplay flags, registry options, limits or configuration were changed. The documented Multiprocess override only avoids startup UBT platform validation. The wrapper retained its normal fixed-step defaults and ran configured-exclusive HitchProbeSmoke after shared smokes. All passed on first execution; wrapper exit 0.

Actual engine result lines:

```text
Smoke-HitchProbeSmoke.log:1722:[2026.11.16-14.22.29:392][399]LogCodexTactics: Display: Smoke RESULT: PASS
Smoke-MainMenuSmoke.log:1847:[2026.11.16-14.19.19:889][196]LogCodexTactics: Display: Smoke RESULT: PASS
Smoke-MovementSmoke.log:1646:[2026.11.16-14.20.10:552][927]LogCodexTactics: Display: Smoke RESULT: PASS
Smoke-SoloStartSmoke.log:1663:[2026.11.16-14.21.43:741][583]LogCodexTactics: Display: Smoke RESULT: PASS
[smoke_parallel] 4 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 266 s, sum of checks 263 s
```

Durations from wrapper: HitchProbeSmoke 46.1 s, MainMenuSmoke 74.6 s, MovementSmoke 54.7 s, SoloStartSmoke 87.7 s. Full per-smoke logs remain at `Saved/Logs/Smoke-<Name>.log`; wrapper at `Saved/Logs/codex026-smokes-wrapper.log`; structured results at `Saved/Logs/codex026-smokes.json`. No FAIL result line or residual failure requires deeper diagnosis. This phase changed only this report and saved evidence; no code, assets, scripts or configurations were edited. The public API comment additions after compilation are documentation only and do not alter the compiled behavior exercised by these tests.


## Rendered acceptance and screenshot

Parent ran the existing capture_start_flicker.ps1 on `/Game/Maps/Generated/L_Gen_Depot_1`, wall-clock, 1280x720 offscreen, fixed seed and 30 FPS cap via `-ForceDPCVars=t.MaxFPS=30`, with `-Multiprocess` for the same startup-only SDK workaround. Enabled capture: 1800 frames under `Saved/Screenshots/Environment/flicker/codex026_enabled/`; disabled control: 240 frames under `codex026_disabled/`, additionally `-CodexPreload=0`. No editor/assets saved. Capture readback/PNG work and machine load affect timings; this is appearance/lifetime verification, not a controlled startup-performance benchmark.

Visually inspected enabled frame 1: full viewport dark background covers the HUD, centered Russian `Загрузка уровня… 31 %` and `Основные ресурсы`. Frame 900: `8 %` and `Ресурсы уровня`, proving the attributes update while the game world is paused and reflecting real denominator discovery. Frame 1800: ordinary terrain/HUD, no loading overlay. The enabled log reports Ready after 100713.10 ms; game/capture exits both 0. This single loaded/instrumented timing is not a claimed speed improvement or comparison against025.

Disabled log explicitly reports `[GameplayPreload] disabled (Codex.Preload.Enabled 0)`. Visually inspected disabled frames 1 and 240: HUD visible with no loading message/cover; the first still has the separate legacy weather fade. The later frame is a sky view, so it is not used as proof of complete disabled-mode scene/gameplay correctness. This control verifies the requested overlay absence. Game/capture exits both 0.

Optional screenshot is the unmodified 320x180 first-frame thumbnail from the enabled capture: `docs/port/screenshots/026-loading-overlay.png`. Original recorder renders 1280x720 and exports these thumbnails. The source API never changes input mode or focus, the widget tree is HitTestInvisible, Ready/Failed collapse it, and Movement/SoloStart/MainMenu smokes pass; no manual click test is claimed.

Wrapper/meta evidence: `Saved/Logs/codex026-render-enabled.txt`, `codex026-render-disabled.txt`, and their `-meta.json` files. Per-capture engine logs, CSVs and thumbnails remain in the folders above. A final process inventory found zero task026 capture games; all task-started game/test processes are closed. Only the allowed screenshot and this report changed after the source checkpoint.

Final routing: `2 | gpt-6.1-sol | pass (implementation/unit/smokes)`, parent code and visual review complete. Engine API verification used the `ue-navigating-engine-source` skill against the actual C:/Program Files/Epic Games/UE_5.8 source, not its stale E: example path. No local model used. Progress can regress when new paths are discovered and can be100% paths during final preparation; this real path metric is deliberately not a time/ETA promise.
