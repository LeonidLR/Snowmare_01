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

## API follow-up

Branch `claude-api/g008-fixes` (on `grok/G008-pmc-defence` `8b79a5f2`). Review `docs/port/grok/outbox/G008-api-review.md`, findings 1-6. One commit per finding. Nothing pushed; main untouched; thresholds unchanged (spread >= 5 m, guard share > 0.6, shared cover pairs 0, stalemate re-decide <= 6).

| Finding | Commit | What changed |
| --- | --- | --- |
| 6 FindCoverNear cost | `3b466047` | The spacing key (`GetPathName` + `CoverObjectKey` Printf) is built only when `ClaimedPosts` is non-empty. With no claims `SpacingFactor` is 1, so behaviour is identical. |
| 5 refresh loop | `458a9706` | `PmcSquadLocal::LivingSquadIndices` (sorted, unique) over `GetTeamMembers` is used by both Decide's change test and RefreshDefence. |
| 4 IsWalkShort | `229c7771` | **Not inverted.** `IsWalkShort` is true while he is still *far*, so `!IsWalkShort -> Hold` held near men and walked far ones, like the attacker ring branch. Flipping it would have frozen far defenders. The real mismatch: that branch counted "there" at `ArriveCm` 250 cm, while `bAtPost` uses `PostArriveCm` 200 cm. Both now use `IsAtPost(PostArriveCm)`. |
| 2 sector shift | `f296f4c2` | Shift = `shift_after_s` on post (never at 0) AND (shot at in the last 2 s OR no target seen for `shift_no_target_s` 6) AND `shift_cooldown_s` 20 since his last shift. The wedge move happens only when the view allowed it. The cooldown is stamped on the attempt. The shift walk does not sprint while he sees a target. |
| 1 off-post fight | `468d633d` | Off post before contact: only the walk. In contact: fight orders keep their normal scores, the walk scores 1.05 (0.75 under fire), Advance / Flank 0. Sprints unless under fire. Unreachable post: after `defence_post_walks` 3 short / refused walks he becomes a no-wedge sector shooter on the nearest roof spot with a full path, then (second drop) where he stands. |
| 3 Retake / Occupy | `10d54c26` | `DefenderPointPull` restores the fix/pmc-arena-stalemate numbers. Retake is 0.2 + 0.8 x HoldFrac with attackers on the point, for any defender including the guard. Occupy is 0.8 on the push, or 0.9 on a late clock (from 60 %, full at 85 %); with posts only the occupier occupies (the shooter whose post is nearest the point, else the guard). `ResolveRound` still pays a time win only for defenders alone on the point, so that win is possible again. The executor walks onto the ring when the pull beats the area walk. |

Found while smoking finding 1, folded into `468d633d`:
- `HasOwnMoveOrder()` reads false mid-sprint.
- The tactical `bSettlePost` fired for defenders: their tactical spawn post never settles, so Ambush re-ran the defence walk **every decision**. That is the review's "re-issues Walk every decision", and it already existed on `8b79a5f2`. In PmcStalemateSmoke it produced 44 Ambush re-issues for one man and 3 false "short walks" per second. A defence post now disables it. The defence re-check also waits until the man stands still and the order is 2.5 s old.
- A walk that ended short counts only when he is not under fire. Pinned prone (`goes prone against the flash`) is not unreachable: the guard had dropped the stair post that way and settled 4.1 m from the access.
- A walk score of 0.5 in contact lost to a rocket (0.94) on the roof, where the street is visible at once. That run had guard share 0.00, hence 1.05 / 0.75.

### Numbers (final tree `10d54c26`, same binary)

| Seed | Spread, m | Guard share | Objective share | Pairs | Result | Log |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 10.6 | 0.92 | 0.00 | 0 | PASS | `Saved/Logs/Smoke-PmcRoofDefenceSmoke.log` |
| 2 | 6.7 | 0.71 | 0.14 (3 Retake orders, scores 0.20 / 0.27 / 0.38) | 0 | PASS | `Smoke-PmcRoofDefenceSmoke-seed2.log` |
| 3 | 9.5 | 0.95 | 0.00 | 0 | PASS | `Smoke-PmcRoofDefenceSmoke-seed3.log` |

Grok's `9c0f35f0` for comparison: 10.3 / 0.91 / 0.00, 9.6 / 0.77 / 0.00, 9.2 / 0.89 / 0.00.
- Seed 2 is the narrowest: spread 6.7 m against 5, guard share 0.71 against 0.6.
- The seed-2 Objective share is now non-zero because the defender Moreau retook the point while the attackers held it (finding 3 working). Before, it was 0 by construction.
- Sector posts at (-1655, 2259) and (-1672, 1954) on the seed-1 roof, and (-4308, 4004) on seed 3, are dropped as unreachable after 3 walks. The men hold where they stand, and no roof spot had a full path. Before this follow-up they stood short of the post forever with only Ambush scoring.

Gates:
- `Scripts/build.ps1` Succeeded.
- `Scripts/test.ps1 -Filter CodexTactics.AI`: 96 tests, 0 failed.
- `smoke_parallel.ps1 -Remote off -NoRetry`: PmcRoofDefenceSmoke PASS 65.6 s, PmcStalemateSmoke PASS 59.2 s (re-decide 6 <= 6, churn 17.07 <= 20, longest quiet 0.8 s), PmcArenaTacticalSmoke PASS 74.2 s, TacticalMapSmoke PASS 52.9 s, 0 failed.
- Seeds 2 and 3 were launched directly (the same `-UseFixedTimeStep -FPS=60 -FixedSeed -SquadStart=formation -LevelJson=L_PmcArenaDepot.json`, `ExecCmds=CodexTactics.PmcRoofDefenceSmoke seed=N`), because smoke_parallel cannot pass smoke arguments.

Budget: over plan.
- Builds: 5 instead of 1. The first smoke batch failed (roof guard 0.00 / 0.07, stalemate re-decide 11), and three fix rounds followed.
- Smoke batches: 4 runs of the 4-smoke batch plus the seed pair. The last batch is the clean one.

Watch:
- Stalemate re-decide sits exactly at its limit (6 <= 6).
- Seed 2 guard share has 0.11 of margin.
- The `HasOwnMoveOrder()`-false-while-sprinting behaviour probably also affects tactical-post settling for non-defenders. Not touched here.

## API follow-up 2

Branch `claude-api/g008-fixes`, on `3a5e113b`. Review `docs/port/grok/outbox/G008-api-review-2.md`, blocking findings A and B. Nothing pushed; main untouched; thresholds unchanged (spread >= 5 m, guard share > 0.6, shared cover pairs 0, stalemate same-order re-decide <= 6).

| Finding | Commit | What changed |
| --- | --- | --- |
| A post / point ping-pong | `06178ee5` | New `DefenderPointHold`: a posted defender off his post, standing on the point (level-aware distance <= radius), whose pull is still active there (the `DefenderPointPull` terms without the 0.7 x R off-point gate) scores Hold = weight x pull x max(0.5, health). Off post with any active pull (on or off the point), Ambush (the walk back) scores 0: Objective takes him to the point, Hold keeps him there. No TakeCover / plain Relocate off the point while he holds it. When the pull ends (attackers gone), the walk back (1.05) returns. Retake now beats the off-post walk at any clock: the walk is 0 while it is active. |
| B off post under fire | `fadbd2a5` | New pure rule `PmcDefenceRules::IsGoalOnPostLevel`. With a defence post: `FindCoverNear(PostLevelZ)` skips slots off the post's level (the view's cover, so `bCoverAvailable` / TakeCover / Relocate; the standoff band cover; the plain-ambush cover). FallBack / standoff goals and the tactical-map fall-back candidates are projected at the post's height and walked with the level kept. No spot on the level refuses the walk, and the order cools down. |
| Found smoking | `8005c9ee` | (1) The ambush spring turned every Ambush into Hold after contact. Since finding 1 the off-post walk is an Ambush in contact, so it was re-issued every 0.5 s. A defence post walk under way (a move order, or speed >= the still threshold) is now left alone; a stopped one is still re-issued. (2) The walk's "already at the goal" refusal was 250 cm, the view's `bAtPost` 200 cm. A man 200-250 cm off his post could neither settle nor walk, and three refusals dropped the post. Defence post walks now refuse only within 100 cm. |

Score sequences (unit test `CodexTactics.AI.PmcCommander.Objective`, default weights, radius 6 m):

| Case | On post | Walking (10 m) | Ring (5 m) | On point (2 m) | Pull ends |
| --- | --- | --- | --- | --- | --- |
| Late clock 0.9, no contact now (occupier) | Objective 0.90 (best) | Objective 0.90, Ambush 0 (was 1.05) | Hold 0.90, Ambush 0 | Hold 0.90 (best), Ambush 0 (was 1.05), Objective 0 | - (the clock does not end) |
| Same, quiet push | - | - | - | Hold 0.90 (best), Ambush 0 | - |
| Attackers on the point, HoldFrac 0.5 (guard, contact) | Objective 0.60 (best) | Objective 0.60 (best), Ambush 0; HoldFrac 1: 1.00 beats the walk | - | Hold 0.60 (best), TakeCover 0, Ambush 0 | Ambush 1.05 (best): back to his post |
| Not the occupier, late clock, on the point | - | - | - | Hold 0 | Ambush 1.05 (best) |

B unit test `CodexTactics.AI.PmcDefence.PostLevel`:
- Accepted: a roof goal (post 910, goal 930) and a goal at the tolerance (760).
- Refused: the street (0), one floor down (610) and a higher roof (1210).
- No post, or a planar tolerance: always accepted.
- Off post under fire, FallBack and TakeCover still score (> 0) and the walk scores 0.75. Only their goals are held to the level.

### Numbers (final tree `8005c9ee`, same binary)

| Seed | Spread, m | Guard share | Objective share | Pairs | Result | Log |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 12.0 | 0.97 | 0.00 (0 / 40) | 0 | PASS | `Saved/Logs/Smoke-PmcRoofDefenceSmoke.log:2119`, PASS `:2157`, access `:1603` |
| 2 | 8.4 | 0.81 | 0.09 (2 / 22) | 0 | PASS | `Smoke-PmcRoofDefenceSmoke-seed2.log:1956`, PASS `:1994` |
| 3 | 8.3 | 0.73 | 0.00 (0 / 17) | 0 | PASS | `Smoke-PmcRoofDefenceSmoke-seed3.log:1746`, PASS `:1785` |

Follow-up 1 (`10d54c26`) for comparison: 10.6 / 0.92, 6.7 / 0.71, 9.5 / 0.95. All three seeds keep their margins (seed 2: spread 8.4 against 5, guard share 0.81 against 0.6).

Gates:
- `Scripts/build.ps1` Succeeded.
- `Scripts/test.ps1 -Filter CodexTactics.AI`: 97 tests, 0 failed (+1: `PmcDefence.PostLevel`).
- `smoke_parallel.ps1 -Remote off -NoRetry -Commands PmcRoofDefenceSmoke PmcStalemateSmoke PmcArenaTacticalSmoke`: 3 run, 0 failed.
  - PmcRoofDefenceSmoke PASS 84.3 s.
  - PmcStalemateSmoke PASS 65.2 s: same-order re-decide 1 <= 6 (was 6 <= 6), churn 12.80 <= 20, longest quiet 4.9 s (`Smoke-PmcStalemateSmoke.log:1923-1935`).
  - PmcArenaTacticalSmoke PASS 99.4 s.
- Seeds 2 and 3 were launched directly as before (`-UseFixedTimeStep -FPS=60 -FixedSeed -SquadStart=formation -LevelJson=L_PmcArenaDepot.json`, `ExecCmds=CodexTactics.PmcRoofDefenceSmoke seed=N`): PASS, 110 s for both in parallel.

Budget: over plan.
- Builds: 3 completed instead of 1. The first attempt hit the 10-minute tool limit at compile step 5/58; it was not an error. The same build was then rerun detached.
- Smoke batches: 3 instead of 1.
  - Batch 1 (A + B only): roof PASS 59.0 s, tactical PASS 68.8 s, stalemate FAIL with same-order re-decide 13 > 6. 7 of the 13 were one defender's Ambush walk re-issued every 0.5 s by the spring.
  - Batch 2 (+ the 4 m projection fallback, and a spring skip for any defence post walk): stalemate FAIL 7 > 6, from three refused walks 222 cm off a post (the 200 / 250 cm mismatch). Roof FAIL: spread 4.2, guard share 0.52, time limit. A stalled walk was no longer re-issued.
  - Batch 3 (the spring skip limited to a walk on the move, and the 100 cm refusal): all PASS. It is the clean batch above.

Watch:
- With finding A the guard does leave the access to retake whenever the attackers stand on the point. That is the restored finding-3 rule, and it costs guard share in rounds where they reach it (batch 2's roof round went to the time limit). The three seeds above end by elimination before that matters.
- On post, Heal still walks the guard off the access (unchanged here). Batch 2 showed Heal -> walk back -> Heal cycles for the guard.
- A defender off his post, outnumbered, alternates FallBack (0.54) and Retake walks on the roof level (batch 2). Neither goes down the stair any more.
