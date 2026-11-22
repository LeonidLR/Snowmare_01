# 098 - Дайджест walk-smoke: отчёт

Выполнил Claude (задачу принял у Gemini). Скрипт: `Scripts/Tools/walk_smoke_digest.py <log|json> [...] [--json]`, тесты: `Scripts/Tools/test_walk_smoke_digest.py`, фикстуры: `Scripts/Tools/fixtures/walk_smoke/` (короткие выдержки; kit_before.log частично синтетический - ради классов refused/other и кластера).

## Что значит каждый класс
- `ratio` - отряд шёл дольше, чем 2 x соло (`squad N s > 2 x solo`). Конвой тормозит в проёме/на повороте, но никто не застрял.
- `follower_stuck` - ведомый STUCK при активном приказе движения (нет прогресса 4 с). Типично: ведомый упёрся в меш/дверной проём/лидера.
- `leader_stopped` - лидер STOPPED, приказ движения уже не выполняется (чаще на лестницах и выходах на крышу: путь есть, приказ сброшен).
- `refused` - ножка отвергнута до запуска: нет полного navmesh-пути, старт/цель вне navmesh. В свежих after-логах 0, встречается в старых (B_y, B_b18).
- `other` - всё остальное: TIMED OUT (лидер у цели, ведомые не вошли), STOPPED у ведомого и т.п.

Кластеры: single-link, 3D-расстояние <= 150 см, печатаются при N >= 2. Счётчик "stuck meshes" - число строк `is stuck and failed to move` на Actor (включая `BP_Operative_*` - оперативники, мешающие друг другу). Дельта: если карта передана дважды (лог и/или json), первый файл - before, последний - after.

## Дайджест трёх реальных логов
```
== L_Gen_StoryShowcase_Kit [after-L_Gen_StoryShowcase_Kit-Walk.log]: buildings 0/6, walk legs 19/21, flat entry legs 102/120, max squad/solo 3.29, wall 657 s
failed legs: 20
  ratio           7  S01_open[f0] S03_locked_door[f0] S04_rubble_escape[f0,f1] S09_broken_back_windows[f0] S10_snow_drift[f0]
  follower_stuck 11  S01_open[f1] S03_locked_door[f1] S04_rubble_escape[f0,f2] S07_roof_outside[f0,f1,f2] S09_broken_back_windows[f1] S10_snow_drift[f0,f1]
  leader_stopped  2  S04_rubble_escape[f0] S07_roof_outside[st1]
stuck meshes (14 lines): StaticMeshActor_258 x4, StaticMeshActor_1024 x4, StaticMeshActor_1200 x3, BP_Operative_C_1 x2, BP_Operative_C_0 x1

== L_Gen_StoryShowcase [after-L_Gen_StoryShowcase-Walk.log]: buildings 0/6, walk legs 19/21, flat entry legs 99/120, max squad/solo 5.84, wall 668 s
failed legs: 23
  ratio           9  S01_open[f0] S03_locked_door[f0] S04_rubble_escape[f0,f1] S07_roof_outside[f2] S09_broken_back_windows[f0] S10_snow_drift[f0]
  follower_stuck 12  S01_open[f1] S03_locked_door[f1] S04_rubble_escape[f0,f2] S07_roof_outside[f0,f1,f2] S09_broken_back_windows[f1] S10_snow_drift[f0,f1]
  leader_stopped  2  S04_rubble_escape[f0] S07_roof_outside[st1]
stuck meshes (11 lines): BP_Operative_C_0 x4, StaticMeshActor_2853 x4, BP_Operative_C_1 x1, StaticMeshActor_2805 x1, StaticMeshActor_2951 x1

== L_Gen_BgenPlans [after-L_Gen_BgenPlans-Walk.log]: buildings 5/9, walk legs 32/32, flat entry legs 83/100, max squad/solo 5.15, wall 691 s
failed legs: 17
  ratio           6  B_x[f0,f1] B_y[f0] B_b18[f1] B_b20[f2]
  follower_stuck  8  B_x[f0,f1] B_y[f0,f1,f2] B_b18[f1]
  other           3  B_y[f1,f2] B_b20[f0]
stuck meshes (7 lines): StaticMeshActor_2778 x4, BP_Operative_C_1 x2, BP_Operative_C_0 x1
```
Наблюдения: в реальных логах кластеров <= 150 см нет (провалы на разных входах); блокирующих мешей мало и они повторяются (StaticMeshActor_258/1024/1200 в Kit, _2853 в Showcase, _2778 в BgenPlans) - первые кандидаты на проверку. Главный класс - follower_stuck (11/12/8), затем ratio.

## Тесты
`python -m pytest Scripts/Tools/test_walk_smoke_digest.py -q` -> `14 passed`. Покрыто: каждый класс, storey-теги, RESULT-заголовок, счёт нестыковок, кластерное правило 150 см (149 - да, 151 - нет, разные Z), json-отчёт, дельта, лимит 25 строк, `--json`, отсутствующий файл.

## Не сделано
- Гейт `typesafe_triage.py --audit-diff` смотрит только неструктурированные изменения: после коммитов он сообщил "0 files", поэтому реального аудита diff ветки нет; `git diff --check` чист.
- Дельта на реальных логах не показана (в наличии только after-версии одной и той же карты; before-пар в Logs не сопоставлялись).
- В `--json` не выводятся списки отдельных провальных ножек, только агрегаты.
- Рядом лежат десятки других *Walk*.log (eo*, final*), не прогонялись.
- Следовало бы решить, считать ли `follower STOPPED` отдельным классом (сейчас в `other`).
- Ничего не запушено.
