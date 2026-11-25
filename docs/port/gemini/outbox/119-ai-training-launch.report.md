# 119 — "Тренировка ИИ" Tab: Memo, Editor Shortcut & Play Button Report

- **Branch**: `gemini/119-ai-training-launch`
- **Model**: Hydrogen
- **Status**: PASS

## Changes
1. `Scripts/Tools/open_editor_nozen.cmd`:
   - Added launcher script starting Zen standalone if not running on port `8558`, waiting for `http://127.0.0.1:8558/health/ready`, and launching `UnrealEditor.exe` with `-NoZenAutoLaunch`, optional `%1` map path, and `%2..` extra args.
2. `Tools/TrainingGround/server/trainingApi.ts` & `Tools/TrainingGround/src/aiTraining/trainingTypes.ts`:
   - `readTrainingData` serves `howto` from `docs/port/FIGHTS_HOWTO_RU.md` and `editorCmdPath`.
   - Added `POST /api/training/open-editor` (`handleOpenEditor`) and `POST /api/training/play` (`handlePlaySector`).
   - Added launch guards (`checkLaunchGuards`): refuses Lanes worktrees (`403`), active `Saved/agent.lock` (`409`), and already running `UnrealEditor*` with main `CodexTactics.uproject` (`409`).
   - `handlePlaySector` updates `Saved/Training/active_sector.json` via `writeTrainingSelection`, ensures Zen is ready (`defaultEnsureZenReady`), and spawns `UnrealEditor.exe <uproject> /Game/Maps/Generated/L_Gen_Depot_1_X6 -game -Sector=N -windowed -ResX=1600 -ResY=900 -NoZenAutoLaunch` detached (`windowsHide: true`).
3. `Tools/TrainingGround/src/departments/AiTrainingDepartment.tsx`:
   - Added collapsed-by-default `"Как запустить"` toggle rendering `docs/port/FIGHTS_HOWTO_RU.md`.
   - Added `"Открыть редактор"` button + copyable `open_editor_nozen.cmd` path + `"Копировать"` button.
   - Added `"Играть сектор N"` button displaying `"запущено, pid N"`.
   - Verified `Source/CodexTactics/Private/Telemetry/SessionRecordingSubsystem.cpp`: `DoesSupportWorldType` accepts `EWorldType::Game || EWorldType::PIE` and `OnWorldBeginPlay` records in `-game` mode (when not `-nullrhi`, `-NoTelemetry`, `-unattended`, commandlet, or automation). Added UI note confirming `.jsonl` recording works in `-game`.
4. `Tools/TrainingGround/src/aiTraining/trainingApi.test.ts`:
   - Added tests for `howto`/`editorCmdPath`, all three launch guards (Lanes, `agent.lock`, running editor), and command/selection generation with mocked `spawnDetached` and `ensureZenReady`.

## Verification
- `npm run typecheck` (`tsc && tsc -p tsconfig.node.json`): **PASS**.
- `node --test --test-concurrency=2 src/aiTraining/trainingApi.test.ts`: **2/2 PASS** (`326.7ms`).
- Browser verification (`http://localhost:5179/`, dark theme, `"Тренировка ИИ"` tab):
  - 0 JS console errors.
  - `"Как запустить"` memo toggled open, `"Открыть редактор"`, `"Играть сектор 0"`, copyable `.cmd` path, and `DemoRecord -game` note all visible.
  - Screenshot: `docs/port/gemini/outbox/119-ai-training-launch.png`.

## Self-Review
- Checked `git diff` against `docs/port/gemini/inbox/119-ai-training-howto-launch.md`: 0 findings.
