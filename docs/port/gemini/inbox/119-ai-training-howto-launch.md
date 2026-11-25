# Gemini 119 (Hydrogen) - "Тренировка ИИ" tab: how-to + editor shortcut + launch button (designer 2026-11-25) - right AFTER 105b

Branch gemini/119-ai-training-launch from origin/main, worktree C:/Work/Lanes/CodexTactics-119. Tools only (Tools/TrainingGround + Scripts/Tools);
no C++, no maps, no .uasset. UI text in Russian (the designer reads it), code / comments in English.
Files: Tools/TrainingGround/src/departments/AiTrainingDepartment.tsx (the tab), Tools/TrainingGround/server/trainingApi.ts (its API,
already writes Saved/Training/active_sector.json), wired in vite.config.ts. Read them first; reuse the existing API style and tests
(src/aiTraining/trainingApi.test.ts).

1. MEMO (collapsed by default): a small "Как запустить" toggle / mini-tab inside the tab; when opened shows the step list. Content = the
   steps of docs/port/FIGHTS_HOWTO_RU.md (pick sector -> "Сделать активным" -> open X6 -> Play, Codex.Sector N / 0, recording check line
   "[DemoRecord] recording", Saved/Demos, round / ammo rules, settings file). Render it from that .md (served by the API) so one source stays true.
2. EDITOR SHORTCUT: move the Lead's launcher into the repo as Scripts/Tools/open_editor_nozen.cmd (content below; add an optional %1 = map
   path, %2.. = extra args passed to UnrealEditor.exe). It starts Zen standalone if not running, waits for http://127.0.0.1:8558/health/ready,
   then opens the editor with -NoZenAutoLaunch (fix of 11-25: editor hung on a hidden "Wait for ZenServer?" box). In the tab: button
   "Открыть редактор" -> POST /api/training/open-editor -> runs it detached (no console window) with the X6 map
   /Game/Maps/Generated/L_Gen_Depot_1_X6. Show the path of the .cmd as copyable text too.
3. LAUNCH BUTTON: "Играть сектор N" -> POST /api/training/play {sector} -> first makes the sector active (same code path as "Сделать
   активным"), then starts the game standalone, detached: UnrealEditor.exe <uproject> /Game/Maps/Generated/L_Gen_Depot_1_X6 -game
   -Sector=N -windowed -ResX=1600 -ResY=900 -NoZenAutoLaunch (Zen started first exactly like the .cmd). Show "запущено, pid N".
   GUARDS (must have, tested): refuse with a clear Russian message if a CodexTactics editor or game is already running in main
   (use Scripts/Tools/editor_status.py logic or its /api data: editor command line with CodexTactics.uproject, see memory: name alone is
   not enough), or if Saved/agent.lock is held by a build. Only the main checkout path, never a Lanes worktree.
   Before coding, check in Source/ that DemoRecord also records in -game (search "DemoRecord"); if it does not, say so in the UI note and the report.
4. Tests: vitest for the new API routes (guard refused / ok, command line built correctly - mock the spawn). npm test in Tools/TrainingGround PASS.
   Manual check: screenshot of the tab with the memo open (Saved/Screens or the report), NOT a real game launch while the designer plays.
Checklist #1104. BUILD NOTHING in C++. Report docs/port/gemini/outbox/119-ai-training-launch.report.md + "119 FINISH | ..." in chat. Never push.

Launcher content today (Saved/Lead/Open_Editor_NoZenAutoLaunch.cmd):
```
@echo off
rem Lead 2026-11-25: editor hung at 0-5 %% ("Wait for ZenServer?" hidden behind the splash; Zen readiness event never arrives).
rem Starts Zen on its own (if not running), then the editor that connects to it.
set ZEN=%LOCALAPPDATA%\UnrealEngine\Common\Zen
"%ZEN%\Install\zen.exe" status | find "8558" >nul || start "" /min "%ZEN%\Install\zenserver.exe" --port 8558 --data-dir "%ZEN%\Data" --http asio --quiet
:wait
curl -s -m 2 http://127.0.0.1:8558/health/ready >nul 2>&1 || (timeout /t 2 >nul & goto wait)
start "" "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "%USERPROFILE%\Documents\Unreal Projects\CodexTactics\CodexTactics.uproject" -NoZenAutoLaunch
```
