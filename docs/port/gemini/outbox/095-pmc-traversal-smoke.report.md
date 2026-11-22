# 095 PmcTraversalSmoke - отчёт после доработки (Claude, 2026-11-22 / 23)

Ветка `gemini/095-pmc-traversal-smoke`, worktree `CodexTactics-gemday`. Gemini-версия dc7e7e34 переписана (ревью Fable: REWORK, 7 пунктов),
затем правки по повторному ревью (MERGE WITH FIXES). Коммиты: `12b50b01` (переработка), `f8ed050c` (спуск mantle down),
`9c8ce53e` (повторное ревью). Сборка Succeeded; тесты `CodexTactics.AI.PmcTraversal.*` 4/4.
Прогон на воркере (slot1, -ShareMachine, fixed step 60 fps), карта `L_Gen_Depot_X4`, 242 ноги на проход
(лестницы 19/19, трапы >= 7 ступеней 23/23, mantle 120 из 3107, drop 80 из 409), 4 прохода в одном запуске, 957 с.
Лог: `Saved/Logs/Worker/20261123-001643/Smoke-PmcTraversalSmoke.log` (старт уровня formation, `-SquadStart=formation`, коммит 9c8ce53e).

## Результаты (засчитано только с доказательством прохода самой связи)

| Вид | PMC solo | PMC formation | T0 solo (контроль) | T0 formation (контроль) |
|---|---|---|---|---|
| Лестницы | 13/19 (68 %) | 13/19 (68 %) | 13/19 (68 %) | 13/19 (68 %) |
| Трапы | 22/23 (96 %) | 22/23 (96 %) | 22/23 (96 %) | 22/23 (96 %) |
| Mantle вверх/через | 66/79 (84 %) | **0/79 (0 %)** | 67/79 (85 %) | 67/79 (85 %) |
| Mantle вниз | 33/41 (80 %) | 32/41 (78 %) | 34/41 (83 %) | 34/41 (83 %) |
| Спрыгивания (drop) | 62/80 (78 %) | 64/80 (80 %) | 62/80 (78 %) | 64/80 (80 %) |
| `IsMantlePathAllowed` после приказа | 242/242 | **0/242** | 242/242 | 242/242 |

RESULT: FAIL (knownFail). Ни один вид у PMC не дотягивает до 95 %, кроме трапов. Планка теперь судит mantle вверх и вниз отдельно.

Drops упали на 2-4 против прогона f8ed050c (66/80 -> 62/80 у PMC solo): drop через свою связь теперь не засчитывается при промахе
мимо нижней поверхности (3 у PMC solo, `missed the lower surface`) и при rescue-телепорте с не-navmesh (1, `rescued off navmesh`).
Прошлый прогон со стартом уровня в Solo (`-SquadStart=solo`, `20261122-233854`, f8ed050c) дал те же числа по остальным видам
и `IsMantlePathAllowed` 242 / 0 / 242 / 242: переключение режима в коде = режим старта уровня.

## Главные группы отказов (печатает код; PMC solo 46, PMC formation 111, T0 solo 44, T0 formation 42)

1. **PMC в formation не лезет вообще**: разрешение mantle у бойца team 1 берётся из Solo-режима ОТРЯДА ИГРОКА
   (`OperativeCharacter::OrderMoveTo`, `bMantlePathAllowed`): 0 из 242 приказов, mantle вверх 0/79
   (35 `no climb (path avoided link)`, 16 `stuck (at link end)`, 14 `stuck (at link start)`, 9 `stuck (walking)`, 5 timeout).
   Mantle вниз от этого не зависит (спуск идёт сам). Баг игры для T-07, не смоука.
2. **Drop: путь обходит связь** - 10 в каждом проходе PMC (`no drop (path avoided link)`), 3-4 `took another drop link`,
   у PMC solo ещё 3 `missed the lower surface` + 1 `rescued off navmesh`. У T0 то же самое: не специфично для PMC.
3. **Mantle: путь обходит связь** - 9 вверх + 6 вниз в solo (у T0 столько же), плюс 2-4 `climbed another obstacle`.
4. **Лестницы: одни и те же 6 во всех 4 проходах, марш ни разу не замечен** (`no march seen`: 3 `stuck (walking)`,
   2 `timed out (walking)`, 1 `stuck (at link start)`) - боец до марша не доходит, значит раскладка / navmesh, а не PMC:
   `B_x1_8_t7_block_stair_0`, `B_x1_16_t7_block_stair_0`, `B_x2_25_t8_block_stair_0`, `B_hall_n_stair_0`, `STAIR_in_8`,
   `B_x2_4_t8_hall_stair_0`.
5. **Трапы**: единственный отказ `LAD_B_x1_9_t7_gallery` - `stuck (at link start)` (по 3D ближе к низу), во всех проходах.
   Таймауты трапов-люков 874 см ушли: они проходят за 24-29 с (бюджет 15 + путь/150 + высота/30 + 8 с).

Ранений при приземлении (heavy / lethal по правилам падения): 0 во всех проходах (нокдаун и смерть в проверке выключены).

## Правки по повторному ревью (9c8ce53e)

1. Drop через свою связь засчитывается только при `GetDropCount()` > начального, `DidLastDropLandOnTarget()` и без rescue.
2. `PhaseNote`: ближний конец связи по 3D от ног (прежние 37 `stuck (at link end)` в formation теперь 16 end + 14 start).
3. Ключи групп: mantle с up/over / down, без секунд бюджета (`PmcTraversalRules::FailureGroupKey`, тест).
4. Планка: mantle вверх и вниз по отдельности; с `-TraversalOnly` судится то, что сэмплировано (пустой план = FAIL).
5. Конфиг в `Content/Data/AI/pmc_traversal.json` перенести нельзя: pre-push guard разрешает в gemini/* только
   `Content/Data/AI/horde.json`. Остался в `docs/port/gemini/pmc_traversal_config.json`, Lead переносит при мерже (загрузчик готов).
6. Отказ лестницы пишет `march seen` / `no march seen`.

## Что исправлено по 7 пунктам ревью

1. Mantle засчитывается только если подъём начался (`GetMantleCount`/`IsMantling`) у основания ЭТОЙ связи (`GetMantleBaseFeet`),
   вниз - если начался спуск (`GetMantleDescentCount`/`IsMantleDescending`) с посадкой у конца связи или hop у её верха.
   Вверх/через и вниз считаются отдельно. Плюс диагностика: был ли линк в пути (`FindLinksOnPath`).
2. Четыре прохода в одном запуске: режим отряда переключается в коде (`EnterSoloMode`/`ExitSoloMode`), `IsMantlePathAllowed`
   читается ПОСЛЕ `OrderMoveTo` и суммируется по проходу; старт уровня и `-SquadStart=` пишутся в лог.
3. Таймаут ноги считается от длины пути и высоты трапа (`PmcTraversalRules::LegTimeoutSeconds`).
4. Контроль team 0 - настоящий: те же 242 ноги, оператор игрока (лидер formation), таблица рядом.
5. `Scripts/test_map.json`: глоб `Source/**/Debug/Pmc*` в правиле pmc-squad-ai на месте (как в main), `LevelGenRampSmoke`
   в правиле pmc-traversal восстановлен; добавлены глобы правил / теста / конфига и `CodexTactics.AI.PmcTraversal`; timeout 2700.
6. Прибытие: на земле, |Vz| < 30, не в mantle / спуске / трапе / drop; после телепорта скорость обнуляется и боец сначала
   встаёт на землю (settle 0.25-3 с). Drop засчитывается только если `ULedgeFallComponent::GetActiveLink()` был этой связью.
7. Группы отказов и сводная таблица печатаются кодом (`PmcTraversalRules::GroupCounts`).

Также: строки отказов содержат координаты связи, nav-цель и ноги бойца; ошибка загрузки layout пишется в лог;
пустой план (опечатка в фильтре) = FAIL; потеря бойца = abort, а не "no path" на всех остальных ногах.
Чистая логика вынесена в `Source/CodexTactics/{Public,Private}/Characters/PmcTraversalRules.*` (папка AI вне allowlist ветки).

## Не сделано / не проверено

- Конфиг остался в `docs/port/gemini/pmc_traversal_config.json`: pre-push guard не пускает `Content/Data/AI/` в ветку gemini/*.
  Загрузчик уже предпочитает `Content/Data/AI/pmc_traversal.json` - перенести при мерже.
- Отказы `path avoided link` - это выбор навигации (есть другой путь), а не неспособность пройти; строгая проверка "через связь"
  их честно считает провалом. Принудить путь через конкретную связь смоук не умеет.
- Причину 6 лестниц и `LAD_B_x1_9_t7_gallery` не разбирал (одинаково у T0 - вероятно, раскладка / navmesh X4, отдельная задача).
- Здоровье / нокдаун в проверке по-прежнему выключены (меряется движение); ранения при посадке только считаются.
