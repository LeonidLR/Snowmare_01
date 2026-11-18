# G008 отчёт — оборона PMC на крыше

Ветка `grok/G008-pmc-defence`. План `351392fb`. Правила `12f4a4b2`. Счёт Objective у защитника `d2a13073`. Посты в отряде `30185408`. Метрики и смоук `04e670c1`, сравнение координат `084ef812`. Удержание поста `d2ab7c2a`. Спринт на пост `0e17bc98`. Objective на посту равен нулю `9c0f35f0`. Пуш и main не трогались. Карты, `.uasset`, `L_Gen_Depot_1.json`, `pmc_commander.bounds.json` и `Config/` не менялись.

Захват точки не менялся. Защитник больше не обязан стоять в круге захвата и может отдать точку. Так принял дизайнер.

## Роутер

`model_router.py --classify` в этой сессии не вызывался. C++ ИИ по правилу маршрутизатора — уровень 3, модель grok-4.7. Печатных имён моделей Claude из классификатора нет.

- план: 3 | grok-4.7 | `351392fb`
- правила: 3 | grok-4.7 | `12f4a4b2`
- счёт Objective: 3 | grok-4.7 | `d2a13073`
- посты отряда: 3 | grok-4.7 | `30185408`
- метрики и смоук: 3 | grok-4.7 | `04e670c1`, затем `084ef812`
- удержание поста: 3 | grok-4.7 | `d2ab7c2a`
- спринт на пост: 3 | grok-4.7 | `0e17bc98`
- второе мнение: API | claude-opus-5-5, effort low, метка `grok-G008-review` | два замечания, переворот `IsWalkShort` отвергнут, фикс `9c0f35f0`
- ворота: 3 | grok-4.7 | сборка, тесты и смоуки ниже
- отчёт: 2 | grok-4.7 | этот файл

## До и после

Диагноз ночи 2026-11-18 (`docs/design/PMC_DEFENCE.md`): Objective 17–23% приказов, защитники сходятся в угол крыши, вход никто не держит. Пороги смоука те же: разнос ≥ 5 м, доля у входа > 0.6, общие пары укрытия 0 (`PmcArenaCommand.cpp:2512-2514`).

| Семя | Разнос, м | Доля у входа | Доля Objective | Пары |
| --- | --- | --- | --- | --- |
| 1 до | 8.4 | 0.41 | 0.51 | 0 |
| 1 после | 10.3 | 0.91 | 0.00 (0/62) | 0 |
| 2 до | 11.0 | 0.34 | 0.17 | 0 |
| 2 после | 9.6 | 0.77 | 0.00 (0/36) | 0 |
| 3 до | 11.0 | 0.08 | 0.21 | 0 |
| 3 после | 9.2 | 0.89 | 0.00 (0/58) | 0 |

«До» — сборка до `d2ab7c2a`, 2026.11.18-11.02. Семя 1 всё ещё в `Saved/Logs/Smoke-PmcRoofDefenceSmoke-seed1.log:1951` (`Smoke RESULT: FAIL` на `:1966`). Семена 2 и 3 в тех логах: `spread 11.0 m, guard 0.34, objective share 0.17` и `spread 11.0 m, guard 0.08, objective share 0.21` (11.02.55). Файлы семян 2 и 3 потом перезаписаны прогонами после фикса.

«После» — `9c0f35f0`. Семя 1: `Smoke-PmcRoofDefenceSmoke.log:1984` и `:2007`, PASS `:2022`, вход `team 1 access stairs at (-3640, 2480, 600) source=layout` (`:1601`). Отдельный прогон того же бинаря до перезаписи логом -Smart дал 10.4 м, 0.90, 0.00 (0/333). Семя 2: `Smoke-PmcRoofDefenceSmoke-seed2.log:1948` и `:1971`, PASS `:1986`, лестница `(3593, 1000, 900)`. Семя 3: `Smoke-PmcRoofDefenceSmoke-seed3.log:1909` и `:1932`, PASS `:1947`, лестница `(-2807, 4400, 900)`. Все три входа `source=layout`. Лестница галереи X4 в живой крыше Depot не выпала: у Depot_1 нет тактического файла. Галерея закрыта юнит-тестом.

Между этими столбцами, на `0e17bc98`, страж уже стоял у входа, но имя приказа оставалось Objective: лестница снаружи пузыря 12 м, счёт был 0.7. Замер того прогона (логи потом перезаписаны): семя 1 — 6.7 м, 0.96, Objective 0.60; семя 2 — 10.8 м, 0.86, 0.60; семя 3 — 9.5 м, 0.92, 0.46. Все три PASS по порогу. `9c0f35f0` обнуляет Objective на посту, доля приказов падает до 0. Самый узкий запас после фикса — семя 2, доля 0.77 при пороге 0.6. Порог не сдвигался.

## Утверждение | доказательство

| Утверждение | Доказательство |
| --- | --- |
| У защитника внутри зоны Objective равен 0, снаружи плоские 0.7. Ветка атакующего та же. | `d2a13073`. `PmcCommanderRules.cpp` ветка Defender в `ScoreOrder`. Тест `CodexTactics.AI.PmcCommander.Objective`. |
| Один страж входа, остальные — сектора дуги к спавну атакующих. Два живых: страж и один стрелок на всю дугу. Один живой: он страж. Нет входа: все стрелки, доля у входа — JSON null. | `12f4a4b2`, `30185408`. Схема `docs/port/grok/outbox/G002-arena-behaviour-schema.md:53-56`. |
| Пост стража на 1.5 м от входа к центру крыши. Чужое укрытие той же стены даёт 0, сосед ближе 5 м — множитель 0.25. | `Content/Data/AI/pmc_commander.json:67-74`: `guard_dist_m` 4, `guard_inset_m` 1.5, `min_spacing_m` 5, `spacing_penalty` 0.25, `area_factor` 2, `shift_after_s` 8, `sector_arc_deg` 180. `bounds` не менялся. |
| После контакта страж доходит до поста и не уходит FallBack вниз по лестнице. Сектор может Relocate. Атакующий `bHasDefencePost` не ставит. | `d2ab7c2a`. Ворота `PmcCommanderRules.cpp:110-125`. |
| Дорога на пост обороны всегда спринт. Тихий шаг на семени 2 не успевал: лестница в 25 м, атакующие кончились за 18 с, доля 0.47. | `0e17bc98`. `PmcSquadSubsystem.cpp:2025-2030` `Walk(State.DefencePost, true)`. |
| На посту Objective тоже 0, даже если пост снаружи пузыря 12 м. Юнит-тест ставит дистанцию 2500 см: без обнуления счёт был бы 0.35 при здоровье 0.2. | `9c0f35f0`. `PmcCommanderRules.cpp:119-120`. Тест `PmcArenaObjectiveRulesTest.cpp:171-174`. Прогон `Scripts/test.ps1 -Filter CodexTactics.AI.PmcCommander.Objective`: `Success`, `1 tests, 0 failed`, exit 0. |
| `bAtPost` — это пост обороны. Тактический спавн-пост больше не выглядит финишем, и человек идёт к входу. | `9c0f35f0`. `PmcSquadSubsystem.cpp:1401-1406`. |
| Условие шага не переворачивалось. `IsWalkShort` истинно, когда идти ещё надо. На посту защитник держит позицию и смотрит yaw обороны. | `PmcSquadSubsystem.cpp:2131-2146`: `if (!IsWalkShort) Hold`. |
| Второе мнение API одно. Переворот исполнителя отвергнут. Повторного платного вызова нет. | `Saved/Orchestrator/api_spend.jsonl`: `claude-opus-5-5`, `grok-G008-review`, in 85808, out 3110, usd 0.4054. Замечание про числовой 0 доли стража на стороне атаки оставлено: так написана схема. |
| Сборка редактора после `9c0f35f0`. | `Scripts/build.ps1`: `Result: Succeeded`, `Total execution time: 50.62 seconds`, exit 0. Unity clash `ParseJson` в двух старых тестах — прежний, не из G008. |
| Локальные именные смоуки на этом бинаре. | `smoke_parallel.ps1 -Remote off`: TacticalMap PASS 45.3 с, PmcStalemate PASS 50.6 с, PmcArenaTactical PASS 76.4 с, PmcRoofDefenceSmoke PASS 80.3 с, `0 failed`. Семена 2 и 3 тем же шагом 60 FPS, PASS 45 с и 45 с. Повтор внутри -Smart: крыша PASS, stalemate PASS, tactical PASS, TacticalMap PASS. |
| Юнит-фильтр -Smart. | `134 tests, 0 failed` за 29 с. Фильтр `CodexTactics.AI+CodexTactics.LevelJson+CodexTactics.Combat.Sight+CodexTactics.Combat.Cheats+CodexTactics.CodexTactics.AI+CodexTactics.LevelGen.TacticalMap+CodexTactics.Core.GameplayPreload`. |
| SaveLoadSmoke в -Smart умер по таймауту на старте движка, не в сохранении. | Лог оборван на `Launching UnrealBuildTool` `ValidatePlatforms` (`Smoke-SaveLoadSmoke.log` того прогона, ~300 с, FAIL TIMEOUT). Повтор один, UBT уже не держал замок: `SaveLoadSmoke PASS 41.3s`, exit 0. |
| SuppressionSmoke упал дважды на разъезде гончих. Порог не ослаблен, код страха не менялся. | `Smoke FAIL: F: the pack splits round it (6 left, 0 right)` — `Smoke-SuppressionSmoke.log:1752` и `Smoke-SuppressionSmoke-alone.log:1751`. Зона на месте (53 см от точки), ядро не пересекли (ближайший 357 см, ядро 210). Обход — `EnemyAIRules::FireDetourWaypoint`. Смоук сам гасит `Codex.Pmc.Encounters`. `UPmcSquadSubsystem::Tick` при пустых `Teams` сразу выходит (`PmcSquadSubsystem.cpp:587-590`). |

## Что не сделано

- D2 (мины и расстановка) и D3 (реакция атакующих на мины) не делались. Задача — диагноз, D1 и D4.
- Ветку атакующего в `ScoreOrder` не менял. Часы захвата не менял.
- Пороги 5 м, 0.6 и 0 пар не двигал. `SuppressionSmoke` не чинил: это не оборона PMC, и правка чужого обхода зоны страха вышла бы за задачу.
- `test.ps1 -Smart -Base main` при `CODEX_SMOKE_REMOTE=off`: 30 из 32 смоуков зелёные, exit 1 из-за SuppressionSmoke и таймаута SaveLoad. SaveLoad потом зелёный отдельно. Именной локальный набор (крыша ×3, stalemate, arena tactical, TacticalMap) зелёный.
- Лестница `B_gallery` на живой крыше Depot не встречалась. Её числа закрыты юнит-тестом правил.
- Не пушил и не мержил.
