# 025 - First-start black screen: bounded rendered measurement

Status: requested diagnosis complete; no gameplay/source/configuration changes. Branch `codex/025-first-start-black-screen-diag`, tested source `01bf30b5`. Fresh `Scripts/build.ps1` PASS (1.98 s UBT after waiting for the shared mutex). No temporary C++ instrumentation was needed.

## Finding and smallest proposal

The captured symptom is real: the first viewport frame shows HUD over a black world, then terrain appears when the startup cover is lifted. The direct visual hold is `WeatherPresenterActor.cpp:460` (manual camera fade), retained by `TickStartCover` at `:468` while `UGameplayPreloadSubsystem::IsPreparing()` is true. In these runs the Initial and Dependencies phases account for essentially all preload time. Disabling engine world pause alone does not lift this visual cover.

Smallest proposed correction, **not applied**: show an explicit preparing/loading overlay (with existing preload state, optionally phase/progress) while the gates are held, then remove it at Ready. Preserve the preload/pause semantics rather than exposing an apparently frozen playable HUD or letting level clocks run behind the cover. Reducing the 624-path dependency batch is a separate performance investigation; do not remove paths blindly and reintroduce first-use hitches.

## Method and controls

Map `/Game/Maps/Generated/L_Gen_Depot_1`, `-LevelJson=L_Gen_Depot_1.json`, existing `Scripts/Tools/capture_start_flicker.ps1`, 1800 frames per run, `-WallClock`, `-game -RenderOffscreen -ResX=1280 -ResY=720`. All four use `-ForceDPCVars=t.MaxFPS=30 -FixedSeed -Multiprocess`. The 30 FPS cap bounds capture throughput so it spans preparation; `-Multiprocess` skips unrelated startup platform-validation UBT contention. No cache was deleted. Cold means the **first game process after the build**, warm the immediately following game process, as the brief defines; these are not reboot-cold measurements or two PIE sessions in the same editor.

The existing recorder arms at PostLoadMapWithWorld before the first draw (`StartFlickerShotsCommand.cpp:6,150`). Its CSV `time` is game time (`StartFlickerProbe.cpp:114`), which freezes during engine pause; elapsed measurements below instead use UTC log timestamps relative to each actual Process.StartTime saved in metadata. Screenshot/readback log delivery is not GPU presentation/scanout timing; precision is limited by the capture frame and logging latency.

Both default runs have no ExecCmds. Run 3 adds the literal requested `-ExecCmds="Codex.Preload.WorldPause 0"`. Run 4 adds the already supported startup flag `-PreloadNoWorldPause` instead, because the requested late console command cannot establish an early no-pause control. No timeout or assertion was changed.

Capture command per tag:

```powershell
$env:CODEX_AGENT='codex'
$env:PYTHONIOENCODING='utf-8'
Scripts/Tools/capture_start_flicker.ps1 -Tag codex025_<case> -Frames 1800 -WallClock -Maps '/Game/Maps/Generated/L_Gen_Depot_1|-LevelJson=L_Gen_Depot_1.json' -ExtraArgs '<common arguments plus case override above>'
```

## Timing results (seconds)

T0 is the actual process start, not the start of the wrapper or time spent waiting for the build lock. First viewport is HUD+black; first visible world is the first captured frame with middle-band luma >5 and fade 0 (abrupt transition to middle-band luma 170-183). Cold frames 1 and 1577 were visually inspected: HUD-only black and visible terrain respectively.

| Case | First preload request from T0 | Ready/release from T0 | First viewport from T0 | First visible world from T0 (frame) | Captured black interval |
| --- | ---: | ---: | ---: | ---: | ---: |
| Cold/default | 44.028 | 119.536 | 47.504 | 120.087 (1577) | 72.583 |
| Warm/default | 37.665 | 79.775 | 40.967 | 80.373 (854) | 39.406 |
| Requested late ExecCmds pause=0 | 39.229 | 91.823 | 42.581 | 92.354 (951) | 49.773 |
| Early -PreloadNoWorldPause | 40.317 | 93.517 | 43.621 | 93.704 (908) | 50.083 |

`StartSeconds` is set before catalog gathering (`GameplayPreloadSubsystem.cpp:144`); the first observable request log is at `:175`. Inferring that earlier start from Ready timestamp minus the subsystem's reported elapsed duration gives 43.120 / 36.334 / 37.443 / 38.901 s from process start. These inferred starts are distinguished from the directly logged request times above. Ready is logged after `ReleaseStartupGates()` (`:411-413`). Visible-world readback follows that log by 0.551 / 0.598 / 0.531 / 0.187 s.

| Case | Initial phase, 22 paths | Dependencies phase, 624 paths | Reported preload total | Last captured pre-Ready game time | Shader grep lines |
| --- | ---: | ---: | ---: | ---: | ---: |
| Cold/default | 29.849 | 46.507 | 76.416 | 0.0000 | 9 |
| Warm/default | 16.562 | 26.842 | 43.441 | 0.0000 | 9 |
| Requested late ExecCmds pause=0 | 21.725 | 32.597 | 54.380 | 0.0000 | 9 |
| Early -PreloadNoWorldPause | 18.647 | 35.921 | 54.616 | 49.8021 | 9 |

All four report 11 skinned assets and zero optional misses. Initial+Dependencies account for 76.356/76.416 s cold and 43.404/43.441 s warm; the residual after those phase timers is below 0.061 s in every run. Phase names are the enum at `GameplayPreloadSubsystem.h:66`; phase logging and transition/gather are `GameplayPreloadSubsystem.cpp:306-334`. These timers localize the wait to the two asset preparation batches, but do not separate I/O, UObject post-load, and unlogged compilation inside an async load. No per-asset causal claim is made.

## Pause control and logged errors

The requested console command logs `Codex.Preload.WorldPause = "0"` 3.333 s after the first preload request. The game clock still stays at zero until Ready. Source ordering agrees: `AcquireGameplayPreloadPause` tests the CVar/startup flag and takes the pause at `CodexTacticsGameMode.cpp:234,237,250`; changing the CVar later does not call the release method. Thus run 3 is a literal requested test, **not proof of an actual no-pause startup**.

The early flag genuinely allows the game clock to advance to 49.8021 s before Ready, yet the world remains black until the Preparing cover gate clears. This is direct evidence that removing world pause alone does not remove this screen hold. Level clocks running behind the cover are a potential behavior risk; this diagnosis did not run assertions for every tutorial/wave/quest interaction and does not declare the no-pause mode gameplay-safe.

Grep `Compiling|ShaderCompile` (case-insensitive) matches 9 lines in every log. They include compiler setup, 8-worker selection, two shader-autogen checks that each say the file is unchanged and skip writing, and thread shutdown. The count is **matching log lines, not shaders/jobs compiled**. No positive shader-job count or a ~20 s shader compilation interval is established by this output; shader work cannot be ruled out merely from this grep.

Each log contains 40 Error/Fatal-matching lines, all the same normalized startup Python/toolset error set, including `AttributeError: module 'unreal' has no attribute 'ToolsetDefinition'` and `'AgentSkill'`. No new error signature appears in either pause control; there are no crashes and every game process exits 0. This is a log-error comparison, not full gameplay verification.

## Limits and handoff

The exact reported ~20 s first-PIE/normal-second-PIE timing was not reproduced by this separate-process, instrumented series: measured preload is 43-76 s. Screenshot/readback/PNG/logging overhead, a 30 FPS cap, existing cache state, and concurrent machine work are not isolated. Each case is n=1 in the order shown; the nonmonotonic 43/54/55 s warm/control results must not be treated as proof that pause-off speeds up or slows down loading. The reliable causal result is the observed Preparing-to-fade gate and the localization to Initial/Dependencies waits.

needs Claude: a per-asset/Insights breakdown within those two requests and an actual same-editor first/second PIE comparison if exact designer latency is required. This would require deeper profiling than this light-medium task. No preload list, shaders, camera logic, pause behavior, assets, maps or configs were edited.

## Evidence and cleanup

For each case cold/warm/exec_pause0/early_pause0: `Saved/Logs/codex025-<case>-meta.json` records actual process start/exit; `codex025-<case>-capture.txt` is wrapper output. Full engine log, CSV and 1800 thumbnails are under `Saved/Screenshots/Environment/flicker/codex025_<case>/`; plots are `strip_codex025_<case>.png` in its parent. Numeric extraction is `Saved/Logs/codex025-summary.json`.

All four process exits and capture-tool exits are 0. A final process inventory found zero running task025 games. No editor was launched; all task-created game processes closed through the existing recorder. Only this report is committed. Source diff remains empty; required TypeSafe boundary audit and whitespace check run before final commit.

Routing: `4 | main session | pass (bounded diagnosis)`; router confidence .56, route=false. No local model used.
