# G007 отчёт — бой на пандусе STAIR_ext_7

Ветка `grok/G007-ramp-fight`. План `1d10ac33`. Фикс `802f396e`. Пуш и main не трогались. `L_Gen_Depot_1.json`, карты, `.uasset` и `Config/` не менялись. `OperativeCharacterAim.cpp`, `FireGateRules.*`, `ThreatFacingRules.*`, `OperativeCharacterFireGate.cpp` не редактировались.

## Роутер

Классификация плана (до этого коммита): level 3 | grok-4.7 | confidence 0.58, route false. На реализацию роутер повторно не вызывался. Имена моделей Claude из JSON роутера не использовались.

## Утверждение | доказательство

| Утверждение | Доказательство |
| --- | --- |
| На общем склоне чистая трасса — не мёртвая зона. Зона остаётся, когда трасса из дула/глаз заблокирована или между стрелком и целью есть обрыв ровного пола. | `802f396e`. `SquadFireRules.cpp:105` `AppliesDeadZone`: геометрия и (`bTraceBlocked` или `bLevelEdge`). `OperativeCharacterCombat.cpp:241` и `:332` вызывают это после трассы. Обрыв: `IsLevelEdgeBetween` (`SquadFireRules.cpp:110`), проба пола `DeadZoneLevelEdge` (`OperativeCharacterCombat.cpp:126`). |
| Пороги 180 / 240 / 45 / +100 лежат в `fire_gate.json`, не во втором литерале. `params` не менялся. | `fire_gate.json:15-21`: `chest_offset_cm` 100, `min_delta_z_cm` 180, `max_horizontal_cm` 240, `min_angle_deg` 45. Чтение корня `dead_zone`: `SquadFireRules.cpp` (`TryGetObjectField` `dead_zone`). |
| «Прямо под платформой» по-прежнему геометрия зоны. Чистый склон зону не включает. Обрыв платформы включает. | `SquadFireRulesTest.cpp:30-38`. `TestTrue` «Right under the platform» на `IsInDeadZone` сохранён. Новые: `AppliesDeadZone` clear = false, blocked = true, level edge = true. Склон `{500,465,430,395}` и ступени `{500,480,460,440}` — не обрыв при пороге 180. Платформа `{500,500,200}` — обрыв. |
| Высота мили — ноги к ногам против тех же 120 см. `AttackRange` 180 не поднят. Уступ 1.1 м вне досягаемости. На общем склоне дистанция тоже ноги к ногам; на ровном полу и на уступе точка удара — тело (+100), иначе плоский укус и уступ 1.1 м меняют старый смысл. | `EnemyAIRules.cpp:235-252`. `CanMelee` по-прежнему `Distance <= AttackRange && Height <= 120`. `CanMeleeFeet`: высота ног, уступ если высота больше горизонтали, общий склон при угле ≥ 15° и не уступ. Тесты `EnemyAIRulesTest.cpp:43-48`: 1.1 м прямо и на 40 см в сторону — false; склон (124, 83) — true; ровно 160 см — false. Подход к цели (acceptance) не менялся. |
| ИИ персонажа зовёт тот же `CanMeleeFeet`. Смоук считает `canMelee` так же. Поза удара сохраняется до толчка. | `EnemyCharacterBehavior.cpp:318-321` и `:348-354` (`LastMeleeAttackerFeet` до `AttackTarget`). Геттер `EnemyCharacter.h:410`. Колонка смоука `RampFightSmokeCommand.cpp:267` и `:297-301`. |
| Юнит-тесты фильтра зелёные. | `Scripts/test.ps1 -Filter CodexTactics.Combat.SquadFire+CodexTactics.Characters.EnemyAI+CodexTactics.Combat.Sight+CodexTactics.Combat.FireGate`: `38 tests, 0 failed`, exit 0. В том числе `SquadFire.Rules`, `EnemyAI.Rules`, `FireGate.TuningFileMirrorsDefaults`. |
| RampFightSmoke 3/3 на финальном бинаре. Лимит 1.5 с не ослаблен. | `smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands RampFightSmoke`. `RampFightSmoke PASS 36.8s`, exit 0. Лог `Saved/Logs/Smoke-RampFightSmoke.log:1619` `[Melee] BP_Enemy_Frostbitten_C_0 hits Commander for 11.6`. `:1646-1649`: видит на 0.1 с; урон через 0.80 с (hp 60 -> 5); мили на 1.5 с inside normal reach; `Smoke RESULT: PASS`. |
| Остальные смоуки ворот зелёные на том же геймплее. | Один прогон пяти команд, exit 1 только из-за тогда ещё красного RampFight (смерть врага закрывала смоук как незавершённый, три проверки уже были ok). `FireGateSmoke PASS 54.1s`, `CoverSmoke PASS 58.7s`, `LevelGenRampSmoke PASS 78.4s`, `EnemyArchetypeSmoke PASS 41.9s`. После этого менялся только `RampFightSmokeCommand.cpp`. |
| Сборки. | Геймплей: `Scripts/build.ps1`, `Result: Succeeded`, `Total execution time: 245.79 seconds`, exit 0. Дальше только смоук: 24.20 с и 16.68 с, оба `Succeeded`, exit 0. Старое предупреждение unity-clash `ParseJson` в тестах не из этой задачи. |
| Границы агента. | `typesafe_triage.py --audit-diff --agent grok` по индексу перед `802f396e`: `Boundary Breach : PASS`, `Risk Rating : 2.0 / 4 (Jev)`, 13 files, exit 0. |

## Что не сделано

- Не пушил, не мержил, main не трогал.
- `GetElevationAdvantage` по-прежнему на литералах +100 и 150. Это не мёртвая зона выстрела.
- 15° и телесные +100 см в `CanMeleeFeet` — именованные константы рядом с правилом, не ключи `dead_zone`. Четыре порога зоны в JSON. 120 см и 180 см не поднимались.
- Четыре соседних смоука не перезапускались после последней правки только смоука (удар с уроном 0 от уклонения тоже считается попаданием в досягаемости). Игровой модуль эта правка не пересобирала.
- Роутер на подзадачи реализации повторно не гонялся.
