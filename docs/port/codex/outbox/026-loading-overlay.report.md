# 026 — Loading overlay (review checkpoint)

Status: implementation complete; build and focused unit passed. Required smokes intentionally not started pending parent code review. No commit yet.

## Behavior

HUD-owned native Slate covers the entire viewport with an opaque dark background above Canvas, UMG (Z1–20), and the frontend layout (default Z100). It shows `Загрузка уровня… NN %` and a Russian phase label while Preparing, including while the world is paused. HitTestInvisible excludes the entire widget tree from input hit testing; no input mode, focus, clock, pause or gameplay gate is changed. Weak HUD captures protect attribute lifetime; EndPlay removes the exact owned widget from its weak viewport and clears ownership. Ready, Failed, Disabled and NotApplicable collapse it. The snapshot explicitly handles disabled preload before StartPreparation as well, so `-CodexPreload=0` cannot show the overlay during the WaitingForMode interval.

## Progress semantics

`UGameplayPreloadSubsystem::GetProgress` sums `FStreamableHandle::GetLoadedCount(Completed, Total)` over the already-retained handles. Verified local UE5.8 source: `Engine/Source/Runtime/Engine/Classes/Engine/StreamableManager.h:394` and `Private/StreamableManager.cpp:900`. The engine includes failed requests in completed counts; failure release remains unchanged. Counts represent requested paths per batch, not package bytes or elapsed time. Dependencies are discovered after initial loading: the measured 22-path initial request can become 646 total requests after the additional 624-path dependency request. Percentage may decrease at discovery (22/22=100%, then 22/646=3%). No fixed final denominator or monotonic/elapsed progress is invented. Paths can be 100% while final resource preparation/binding still runs; the phase label remains visible until release. Empty/invalid counts yield 0%, completed counts clamp to [0,total], and int64 accumulation/double ratio avoid integer multiplication overflow.

## Weather cover retained

`Source/CodexTactics/Private/Environment/WeatherPresenterActor.cpp:460` applies the startup manual fade; `:468` holds it while Preparing. Both are unchanged. Task025 established this is necessary startup cover while the world/resources are not ready. The new viewport overlay explains this wait without exposing unfinished weather or changing release timing.

## Scope and checks

Only GameplayPreloadSubsystem.h/cpp, CodexTacticsHUD.h/cpp, the new GameplayPreloadProgressTest.cpp and this report are task changes. No assets, maps, Config, preload list, gameplay logic or protected combat code changed. Existing imported/untracked files remain untouched. `git diff --check`: PASS before build.

Focused test `CodexTactics.Core.GameplayPreload.Progress` covers actual 22/646 request examples, dependency discovery regression, zero/negative/excess/maximum counts, Preparing visibility at 100% paths, every terminal/disabled/not-applicable state, and null-world getter behavior.

Build result: `Result: Succeeded`; `Total execution time: 872.19 seconds`; wrapper exit 0. Full log: `Saved/Logs/Build.log`. After the build, parent-requested public API documentation comments were added to IsVisible/GetPercent; this is a comment-only source change with no behavior difference. Focused test: `Test Completed. Result={Success} Name={Progress} Path={CodexTactics.Core.GameplayPreload.Progress}`; `Automation Test Queue Empty 1 tests performed.`; engine exit 0. Log: `Saved/Logs/codex026-focused-multiprocess.log`.

The first standard `Scripts/test.ps1 -Filter CodexTactics.Core.GameplayPreload.Progress` launch stalled at TurnkeySupport startup and executed **zero tests**. Its owned PID50704 was verified against the agents project/focused filter, the log preserved at `Saved/Logs/codex026-focused-stalled.log`, and only that process tree terminated; wrapper exited 1 and released the shared lock normally. Parent authorized one retry using the existing `Scripts/smoke_parallel.ps1:31` workaround `-Multiprocess` (skips startup UBT ValidatePlatforms). Retry retained the original test wrapper engine flags, report export and TestExit arguments and used standard Enter-AgentLock/Exit-AgentLock. No scripts were edited and no lock bypass was used. The retry completed the one focused test successfully. Required later validation: HitchProbeSmoke, MainMenuSmoke, and two existing level smokes. Rendered enabled/disabled observation is still outstanding; headless tests alone do not establish visual appearance.

Routing: task026 Router2 confidence .55; parent-approved bounded implementation, no deeper preload profiling.
