# Gemini 129 - "Тренировка ИИ" tab: how many fights the designer played per SECTOR (designer 2026-11-25) - small, right after 125

Tools only: Tools/TrainingGround/server/trainingApi.ts + src/departments/AiTrainingDepartment.tsx (+ test). No C++.
Source: Saved/Demos/*.jsonl in the MAIN checkout (human session recordings). Line 1 = header {"type":"header","map":..,"sector_id":N,...};
round_start / round_end events (round, result, kills) and the last line = footer {"type":"footer","result":..,"rounds":..,
"score_victory":..,"score_defeat":..,"score_draw":..,"duration_s":..}. Old files may lack sector_id (count them as "без сектора").
Show in the tab a small table per sector 1..6 for X6 (L_Gen_Depot_1_X6): sessions, completed rounds, wins / losses / draws, total minutes,
last played date; a row "всего"; highlight the sector with the FEWEST rounds ("сыграйте сюда"). Read the files on each tab open (cache by
file mtime); a truncated / bad line must not break the table. Only the footer + header + round_end lines are needed - do not parse snaps.
Test: node --test --test-concurrency=2 on the new test file only, with fixture jsonl files (complete, aborted, no-sector, truncated).
Dev server check on --port 5180+ and STOP it afterwards. Router first; Carbon self-review before FINISH.
Report docs/port/gemini/outbox/129-fights-per-sector.report.md + "129 FINISH | ...". Never push.
