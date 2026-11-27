# Codex 151 (L2-L3; Lead reviews) - per-round demo marks + Training Ground settings reach the Play build (designer 2026-11-27 morning)

Designer priority: recording + analysis of his fights. You run headless (full access) via Scripts/Tools/codex_task.ps1.
Lane: C:/Work/Lanes/CodexTactics-151, branch codex/151-round-marks-and-play-settings (from origin/main).

## A. Mark a ROUND, not the whole session
Today `Codex.Demo.Mark good|bad` sets ONE session field (USessionRecordingSubsystem::MarkSession -> footer "mark"); a sector session file holds
many rounds, the last call wins, and the designer, who marks after a round already ended and the next started, marks the wrong fight.
Wanted:
- Each `Codex.Demo.Mark <value>` writes a mark EVENT with time, round index and value (keep the footer field = last mark for old readers).
- Which round: the round in progress, EXCEPT when called between rounds or within the first 30 s of a new round before any contact
  (shot / damage / sighting) - then it marks the PREVIOUS round. Explicit forms: `Codex.Demo.Mark good prev`, `Codex.Demo.Mark good round 3`.
- On-screen confirmation via the existing floating text / message feed: "Раунд 3 помечен: good" (Russian, short).
- Consumers: demo_tactics.py and demo_dataset.py read per-round marks (fallback: footer mark = all rounds); the 148 replay shows the mark on
  its round in the event feed / timeline. Unit tests for the round-choice rule (C++ rules + Python).

## B. Training Ground settings must reach the build it launches
Training Ground edits Content/Data/LevelJson/*.json in the MAIN repo, but "Тренировка ИИ" launches the selected build root (main or
CodexTactics-play, Tools/TrainingGround/server/trainingApi.ts trainingBuildRoot). The designer set start_hour 12 in TG, launched Play and got night
(play copy still had start_hour 2). Fix: before launching a build whose root is not the main repo, sync the X6 level files TG edits
(L_Gen_Depot_1_X6.json + .sectors.json + .buildings_index.json) from main into that build root when they differ - back up the old file to
<build>/Saved/Backups/tg-sync-<timestamp>/ first, log what changed, show a short line in the TG launch result ("синхронизировано: start_hour 2 -> 12").
Never sync in the other direction; never touch other files. Node test for the sync rule (temp dirs). The Lead already copied the file once by hand.

## Rules
Build via Scripts/build.ps1 -Remote worker, Smart tests, mapped smokes (FULL names), Node tests one file / concurrency 2.
Report docs/port/codex/outbox/151-round-marks-and-play-settings.report.md; post "151 MERGE-READY | <sha> | ..." via
Scripts/Tools/agent_chat.py --as codex --to claude. Never edit/commit/checkout in main or CodexTactics-play (read-only for you; the TG code you
write may sync into play at runtime, but do not run that against the real play copy - tests use temp dirs). No push, no maps / .uasset /
Config / VFX, no edits to the designer's L_Gen_Depot_1_X6.json. Shared infra scripts only if needed and explained. 10 % usage: commit, post
"151 LIMIT", stop.
