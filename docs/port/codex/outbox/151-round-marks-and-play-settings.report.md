# 151 — Per-round demo marks and Play settings

Branch: `codex/151-round-marks-and-play-settings`, based on `2ead232d`.
Implementation commit: `86892a291fd8185be3b0557ef70120fa70d6223b`.

## Delivered

- `Codex.Demo.Mark <value>` appends a `round_mark` event with `t`, `rt`, `round`, and `value`; footer `mark` remains the last accepted value.
- Default targeting uses the active round, except during intermission or the first 30 game seconds of a quiet new round, when it uses the last completed round. Exactly 30 seconds selects the current round. With no predecessor the first round selects itself. Contact uses recorded damage/hits, visible threats, enemy sight evidence, and live PMC contact / operative shot clocks (including misses).
- Explicit `Codex.Demo.Mark good prev` and `Codex.Demo.Mark good round 3` override the grace rule. Unknown round indices and malformed commands are ignored. Confirmation uses existing floating text: `Раунд 3 помечен: good`.
- `demo_tactics.py` exposes round marks in its report and JSON. `demo_dataset.py` adds a mark to each fight. Late events resolve by their explicit round index; repeated marks retain the latest value. Footer fallback labels all rounds only when the recording has no mark events.
- Replay displays the mark on its target round in the event feed and timeline. The model preserves the original call timestamp separately as `recorded_t`; recording rows remain unchanged. Legacy footer marks appear on every round.
- TG synchronizes only `L_Gen_Depot_1_X6.json`, `L_Gen_Depot_1_X6.sectors.json`, and `L_Gen_Depot_1_X6.buildings_index.json`, from the TG repository into the selected different build root, immediately before spawning the editor/game. Equal bytes are skipped; missing optional source companions do not delete destination files. All replaced files are backed up before any writes under `<build>/Saved/Backups/tg-sync-<timestamp>-<unique>/`. Errors abort the launch. Console logs identify changed files and backup location; launch status includes e.g. `синхронизировано: start_hour 2 -> 12`.

## Verification

- `Scripts/build.ps1 -Remote worker`: **PASS**, `Result: Succeeded`, compile 402.81 s. Existing warnings: seven unity-clash candidates and two C4701 warnings in `CodexTacticsPlayerControllerOrders.cpp`; none introduced here. Log: `Saved/Logs/Build.log`, remote detail under `Saved/Logs/Worker/build-20261127-090758-99616/`.
- `Scripts/test.ps1 -Smart`: **46 tests, 0 failures**, including `CodexTactics.Telemetry.SessionRecording.RoundMarks`. Unit filter: `CodexTactics.Bot+CodexTactics.Telemetry+CodexTactics.LevelJson+CodexTactics.Training.Sector`.
- Full mapped smoke commands: `CodexTactics.SectorBoundarySmoke` **PASS (44.7 s)**, `CodexTactics.SectorRoundsSmoke` **PASS (43.8 s)**, `CodexTactics.TutorialAmbushLakeSmoke` **PASS (47.1 s)**, `CodexTactics.TurnArenaLakeSmoke` **PASS (37.5 s)**. Worker rerun used `-Remote on -Jobs 2` and full console names: **4/4 first-pass successes, no retries**. Results: `Saved/Logs/151-worker-smokes.json`; archived logs: `Saved/Logs/Worker/farm-20261127-092700-96440/`.
- Smart's initial local smoke batch failed all four during content initialization with missing designer-pack skeletons followed by `EXCEPTION_ACCESS_VIOLATION` at address `0x90`; three serial retries showed the same failure. The remaining task-owned local retry was stopped after all four worker checks passed. No content files were altered to address the lane's missing packs.
- Python: `test_demo_marks.py` **2/2**, `test_demo_tactics.py` **20/20**, `test_demo_dataset.py` **9/9**.
- Node, one test file per invocation, `--test-concurrency=2`: `trainingSync.test.ts` **2/2**, `trainingApi.test.ts` **6/6**, `replay.test.ts` **8/8**. Tests cover exact file allowlist, byte comparison, backups, backup failure, no reverse writes, idempotence, launch ordering and result text, late replay marks, and legacy fallback. All sync writes use temporary directories.
- `npm run typecheck`: **PASS**. Lane dependencies installed with `npm ci --ignore-scripts --no-audit --no-fund`; package manifests unchanged.
- `git diff --check`: **PASS**.

## Scope and review notes

No map, `.uasset`, Config, VFX, designer level JSON, or shared infrastructure scripts changed. No source edits, commits, checkout operations, or runtime TG sync were performed against main or the real Play copy. No branch refs were pushed: worker transport bundled the unpushed implementation commit over SSH. Its existing `worker_sync.ps1` also invoked its automatic LFS-object push step for that commit; this task changed no LFS assets. The build and checks ran headlessly; the Russian floating confirmation was compile-tested, not visually inspected in a rendered fight. Round mark indices are scoped to their recording/sector.
