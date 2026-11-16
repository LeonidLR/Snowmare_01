# 029 - Startup delay attributed to Turnkey

2026-11-16. Branch `codex/029-turnkey-diag`, base `0ed28902dc4efa365d428c7a17f601d570dc1ee7`.
Router: level 4 (confidence 0.94) | primary Codex model | pass (bounded read-only diagnosis). Applied existing ue-navigating-engine-source workflow against installed UE 5.8 source. Report only; no script, engine, Config, map, asset or main-checkout edits, and no new editor/build/test launches.

## Finding

The captured 12m45s commandlet stall is the synchronous **UBT AutoSDK validation** wait, not a long Turnkey VerifySdk operation. The GUI log shows the distinction directly: Turnkey SDK detection completed in 2.696s, but startup then waited another 55.357s for UBT AutoSDK. A competing Build.bat invocation is a plausible cause of that wait: the installed batch script serializes every invocation through an engine-path-derived file lock, shared across worktrees. The retained logs do not identify the historical lock owner or divide elapsed time between lock acquisition and validation work; do not claim a proven holder PID/worktree or UAT mutex contention.

## Log evidence (bracketed engine timestamps UTC)

Main logs were read only under `C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics/`:

| Evidence | Lines / timestamps | Meaning |
|---|---|---|
| `Saved/Logs/LevelGenBuildMap-backup-2026.11.16-14.47.08.log` | 85: launches Build.bat `-Mode=ValidatePlatforms -OutputSDKs -AllPlatforms`; 476: `-run=pythonscript -unattended`, no Multiprocess | Startup subprocess exists even for this unattended commandlet. |
| Same backup | 897: `14:33:17.177` loads TurnkeySupport; 898: `14:46:02.158` **UBT AutoSDK ReturnCode: 0** | Adjacent log entries bracket 764.981s. There is no `Running Turnkey SDK detection` in this commandlet log. |
| Main `Saved/Logs/CodexTactics.log` | 76: launches the same ValidatePlatforms; 1108: `14:45:05.609` Running Turnkey SDK detection; 1110: `14:45:08.305` Completed SDK detection, exit 0; 1111: `14:46:03.662` UBT AutoSDK ReturnCode: 0 | UAT SDK detection is quick; remaining 55.357s ends at the separate UBT completion. GUI and commandlet UBT completions within 1.504s are consistent with contention, not proof of its owner. |
| Worktree `Saved/Logs/LevelGenBuildMap.log` | 897: `13:31:30.032` TurnkeySupport load; 898: `13:31:30.035` UBT AutoSDK return 0 | Same startup seam can complete in 0.003s if the earlier-launched validation is already done. |
| Worktree `Saved/Logs/task028/PmcStalemateSmoke-1-engine.log` | 465: command line includes `-unattended -Multiprocess`; no Launching UnrealBuildTool / UBT AutoSDK / Turnkey SDK detection lines | Existing successful smoke provides a real local example of the recommended switch, not a new controlled benchmark. |
| Worktree `Saved/Logs/task028/Build.log` | 1: `Build.bat is already running, waiting for existing script to terminate...` | Direct local evidence the shared batch lock actually causes waiting; this later run does not establish the earlier stall's owner. |

The requested main `LevelGenBuildMap.log` had rotated: its current instance was from 16:19 UTC. The named backup above retains the 14:33 incident.

UAT logs: `%APPDATA%/Unreal Engine/AutomationTool/Logs/C+Program+Files+Epic+Games+UE_5.8/Log.txt` starts `2026-11-16T14:46:47Z`, records main-project **device** VerifySdk (`TurnkeyReport_1.log`), and ends `AutomationTool executed for 0h 0m 1s`, exit 0. Its own startup deletes old Log.txt/Log.json in that shared engine folder. It therefore cannot retrospectively prove a mutex holder at 14:33. Main `Intermediate/TurnkeyLog_0.log` no longer exists. Current main AutoSDKInfo.txt reports total execution 0.34s but has been overwritten; it is not historical timing evidence for the stalled invocation.

## Installed UE 5.8 source: supported controls and blocking point

Paths below are relative to `C:/Program Files/Epic Games/UE_5.8/Engine/` (all read only):

- `Source/Developer/TargetPlatform/Private/TargetPlatformManagerModule.cpp:368` creates ValidatePlatforms/OutputSDKs arguments; **390** guards subprocess launch with `!FParse::Param(..., "Multiprocess") && NeedsToRunSetupPlatformsUBT()`. `408` starts it early during FileSystemReady. **498** synchronously calls `FPlatformProcess::WaitForProc(SDKSetupUBTProc)`; **553** emits the observed UBT AutoSDK ReturnCode. Thus **`-Multiprocess`** avoids this startup subprocess and wait for both GUI and commandlet launches.
- Same source **54** names the targeted environment alternative **`UE_SKIP_UBT_SDK_SETUP`**; **223-227** returns false when it is `1`. Scope it to the child launch and restore the parent's old value in finally if Claude chooses this alternative. It skips the same AutoSDK subprocess; it is not a Turnkey VerifySdk disable flag.
- Same source **229-238** explicitly says UBT is normally always invoked when AutoSDK is disabled. **249-251** reads `Intermediate/CachedAutoSdkInfo.ini` only after that early path, checks root/version/setup manifests, and **547-550** writes the cache after successful AutoSDK-enabled execution. Current agent-shell UE_SDKS_ROOT is unset; no claim about the historical GUI environment. Copying/pre-seeding this cache is not a general fix for manually installed SDKs. Do not enable AutoSDK just to obtain caching.
- `Source/Editor/TurnkeySupport/Private/TurnkeySupportModule.cpp:3510-3513` explicitly skips UAT SDK querying for **commandlets or unattended** processes. **3522** logs Running Turnkey SDK detection; **3537** constructs FSerializedUATProcess; **3548** logs completion. For GUI SDK querying, `-unattended` takes this guard but changes interactive behavior. No dedicated NoTurnkey/SkipTurnkey switch was found in this targeted source; do not invent one. `-Multiprocess` alone does not suppress interactive GUI Turnkey querying.
- `Build/BatchFiles/Build.bat:18-20` derives a temp lock filename from the full batch-file path; **26-30** acquires it via file redirection or loops with the exact observed already-running message. This is distinct from UAT's mutex. `Source/Programs/AutomationTool/Program.cs:465-468` passes WaitForUATMutex to ProcessSingleton; `Source/Programs/Shared/EpicGames.Build/Automation/ProcessSingleton.cs:45-59` keys its mutex by the AutomationTool assembly path and logs explicit UAT contention. No such contention line survives in the retained UAT log.

## Where Claude can apply the change

Minimal proposal: add **`-Multiprocess` to unattended editor/commandlet argument lists**, retaining all existing repository locks and designer-priority checks. Do not add it to Build.bat arguments: it belongs to UnrealEditor/UnrealEditor-Cmd.

- `Scripts/levelgen_build_map.ps1:58`: append to `$CmdArgs` before Start-HiddenProcess at 61. Existing `-run=pythonscript -unattended` already suppresses Turnkey's UAT SDK query; missing Multiprocess leaves UBT validation active.
- `Scripts/smoke_parallel.ps1:31-33` already exposes `-ExtraArgs '-Multiprocess'` and documents precisely this Build.bat contention (including a prior 25min incident). Make it a deliberate standard engine argument in `$gameArgs` construction before launch at 86, or standardize callers passing ExtraArgs; avoid duplicate implementations.
- `Scripts/test.ps1:76-81`: add to its native UnrealEditor-Cmd invocation, which is already unattended. This is also where task026's focused retry needed the existing direct Multiprocess workaround.
- An automated GUI launch can use `-Multiprocess` to skip UBT validation while retaining GUI Turnkey SDK checks. Do not make a designer's interactive editor unattended just to suppress those checks.

Risk: skipping startup SDK validation can leave SDK availability/status or AutoSDK environment changes stale/unrefreshed; suitable for existing built Win64 project tests and known SDK setup, not a substitute for SDK verification during packaging/platform changes. Keep normal validation for SDK installation, platform bring-up and packaging. Multiprocess is broader than the targeted environment variable; this report verifies its relevant branch, not all engine-wide effects. No startup switch removes waits caused by project locks, builds themselves, asset preload, shader work or Zen. Claude should apply and gate the chosen script change; no blanket machine-wide environment override or mutex bypass proposed.

Validation: source/log cross-check only, no runtime A/B or performance guarantee. No deep engine investigation needed for the blocking point. **needs Claude: historical lock-holder attribution would require preserved process/lock telemetry from the incident; retained logs cannot recover it.**
