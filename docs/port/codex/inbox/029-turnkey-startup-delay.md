# 029 - Why editor / commandlet start waits at "Turnkey SDK detection" (Claude, 2026-11-16) - LIGHT-MEDIUM, diagnosis only
Why: today an editor start and a levelgen build commandlet both stalled 10-13 min right after
"LogTurnkeySupport: Running Turnkey SDK detection" (RunUAT ... -ScriptsForProject=...), e.g. Saved/Logs/LevelGenBuildMap.log
(main checkout) jumps from 14:33:17 to 14:46:03 (UTC). Several "Build.bat -Mode=ValidatePlatforms" cmd processes were alive at once.
Branch codex/029-turnkey-diag (from main), worktree CodexTactics-agents. NO code / config edits - report only.
Do:
1. From the logs (main Saved/Logs/LevelGenBuildMap.log, CodexTactics.log, your worktree's logs; UE's UAT logs under
   %APPDATA%\Unreal Engine\AutomationTool\Logs) find what the start waits for: the UAT/UBT global mutex held by another worktree's
   build? the SDK detection itself? Quote timestamps.
2. Find the documented way to skip or cache the Turnkey SDK detection for editor / -run= commandlet starts in UE 5.8 (engine source
   under C:/Program Files/Epic Games/UE_5.8/Engine/Source - grep "Turnkey" / "SDK detection" / the command-line switch that disables
   it; quote file:line). Do not edit Config/*.ini or engine files.
3. Report: cause with timestamps, the switch / setting that avoids it, where we would add it (our scripts' editor command lines,
   Scripts/levelgen_build_map.ps1, smoke_parallel.ps1), and the risk. Claude applies it.
Report: docs/port/codex/outbox/029-turnkey-startup-delay.report.md. 10 % limit rule.
