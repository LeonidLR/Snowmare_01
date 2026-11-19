# 048 — проходимость зданий LevelGen на построенной карте

## Handover

Ветка `codex/048-buildings-smoke` от `3d265680`. Реализованы smoke зданий и навигационные пробы пролётов. BgenTest X6 показал 13 FAIL среди зданий и 16 FAIL в Solo. Следующий шаг для генератора — проверить центры комнат `*_r_f2_core` относительно `*_gap_stair_f2`, затем повторить строгий smoke на пересобранной карте. Для диагностики оставшегося блока и подъёмов используйте `only=B_x2_23_t8_block` и `only=B_hall_n_stair_2 legs=solo trace` соответственно.

## Проверка зданий

`LevelGenBuildingsSmokeCommand.cpp:219` читает постановку проверок здания из принятой разметки карты и выполняет `FindPathSync` по навигационным данным оперативника. Для каждой наружной двери проверяются центры комнат, площадки этажей и ходовая крыша. Поворот здания учитывается при определении наружной стороны двери и центров комнат. Проекции ограничены высотой и границами цели. Для полого здания промежуточный этаж проверяется, когда он задан в `floors_detail` или указан как уровень прибытия лестницы.

`LevelGenBuildingsSmokeCommand.cpp:302` записывает `Saved/LevelGen/buildings_smoke_<map>.json` с количеством проверок и первой недостижимой целью для каждого здания. `only=<id>` ограничивает прогон одним зданием. `maxfail=<n>` включает отчётный режим и ограничивает список ID в строке результата. Для старого Depot X6 реестр `Scripts/test_map.json:152` включает отчётный режим с лимитом 10 ID. Отдельного файла `docs/port/SMOKE_REGISTRY` в ветке нет.

| Строгий прогон | Зданий всего | Входных зданий | PASS | FAIL | Сравнение |
| --- | ---: | ---: | ---: | ---: | --- |
| `L_Gen_BgenTest_X6` | 52 | 41 | 28 | 13 | TS-валидатор генератора сообщил 0 FAIL |
| `L_Gen_Depot_1_X6` | 70 | 57 | 50 | 7 | Исходный аудит в постановке 048 указал 24 из 70 |

У BgenTest X6 первые недоступные цели у 12 зданий — центры комнат `*_r_f2_core`. Координаты всех 12 лежат внутри вырезов `*_gap_stair_f2` или на их границе по accepted JSON. Это `B_hall_n`, `B_warehouse`, `B_steelworks`, `B_x1_4_t7_hall`, `B_x1_10_t7_store`, `B_x1_14_t7_store`, `B_x1_25_t7_store`, `B_x2_4_t8_hall`, `B_x2_16_t8_hall`, `B_x2_21_t8_hall`, `B_x2_27_t8_hall`, `B_x2_28_t8_store`. У `B_x2_23_t8_block` первым не проецируется центр `B_x2_23_t8_block_r_f1_0`. Проверенный центр лежит вне размеченных вырезов этажа.

На старом X6 navmesh содержит проекции центров уровня 1 семи блоков, однако к ним отсутствует полный путь от наружной двери. Это `B_x1_16_t7_block`, `B_x2_12_t8_block`, `B_x2_13_t8_block`, `B_x2_23_t8_block`, `B_x1_6_4_t1003_block`, `B_x1_7_1_t1003_block`, `B_x1_1_1_t1004_block`. Аудит из постановки и движковый smoke используют разные критерии. Движковый smoke проверяет полный путь и допускает навигационные ссылки, доступные оперативнику.

Логи строгих прогонов — `Saved/Logs/048-buildings-bgen-x6.log` и `Saved/Logs/048-buildings-depot-x6.log`.

## Проверка пролётов

| Метрика BgenTest X6 Solo | До | После |
| --- | ---: | ---: |
| Полный путь для верхних пролётов | 12/29 | 29/29 |
| Пролёты с пробой `---` | 1 | 0 |
| Запланировано Solo-прогонов | 18 | 48 |
| Solo PASS | 6 | 32 |
| Solo FAIL | 12 | 16 |
| Верхние Solo PASS | 0 из 0 | 14 из 29 |

Исходный прогон использовал DLL, уже имевшиеся в рабочей копии. Итоговый использовал локальную сборку ветки 048. У `B_x2_4_t8_hall_stair_0` верхняя метровая проба перешла с `---` на Z=237 см. У `B_hall_n_stair_1` вместо `no navmesh path` найден полный путь с концом на Z=610 см. `LevelGenRampSmokeCommand.cpp:276` ограничивает расширенное окно проб высотой пролёта и проецирует точку прибытия на верхний уровень, сокращая отступ на короткой площадке. `LevelGenRampSmokeCommand.cpp:388` планирует проверку верхних пролётов. Solo требует, чтобы выбранный боец поднялся, а остальные остались на исходном этаже. Из 16 итоговых сбоев 12 — `STUCK`, 4 — другой боец покинул исходный этаж. Логи — `Saved/Logs/048-ramp-x6-before.log` и `Saved/Logs/048-ramp-x6-after.log`.

## Gate

- UE 5.8, локальная сборка — PASS, `Saved/Logs/Build.log`. Автоматическая проверка отклонила `Scripts/build.ps1 -Remote worker` из-за передачи приватного кода и локальных файлов.
- `LevelGenBuildingsSmoke` в строгом режиме на обеих X6-картах и `LevelGenRampSmoke legs=solo` на BgenTest X6 — выполнены, результаты выше.
- `Scripts/test.ps1 -CheckMap` и проверка JSON — PASS.
- `Scripts/test.ps1 -Smart` локально — PASS, 48/48 unit, 26/26 smokes, без повторных запусков. Лог `Saved/Logs/048-test-smart.log`.
- `LevelGenRampSmoke` по умолчанию на `L_Gen_Depot_1` — PASS 12/12, `Saved/Logs/Smoke-LevelGenRampSmoke.log`. `LevelGenLaddersSmoke` — PASS 13/13, `Saved/Logs/Smoke-LevelGenLaddersSmoke.log`.
- Отчётный запуск на Depot X6 — PASS с теми же 7/57 FAIL в JSON, `Saved/Logs/Smoke-LevelGenBuildingsSmoke.log`. Запуск `only=B_x2_23_t8_block maxfail=1` на BgenTest X6 — PASS, в JSON один проверенный и один недостижимый объект, `Saved/Logs/048-buildings-only.log`. После него восстановлен полный отчёт BgenTest X6.
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex` — PASS для всех пяти файлов. `git diff --check` — PASS.
- Три файла BgenTest X6 остаются локальными fixtures.
