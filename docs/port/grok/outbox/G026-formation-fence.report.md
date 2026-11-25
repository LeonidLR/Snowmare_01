# G026 — забор в формации: переход, удар на перелезе, присед по прибытии

Ветка `grok/G026-formation-fence` от `origin/main` `0b6efa44`. В main не вливал, не пушил. G022 и G025 не начинал.

Роутер (план): level 3 | grok-4.7 | pass, `policy_blocked` (без субагентов). `Scripts/Tools/machine_dispatch.py` в ветке нет. Сборки локальные, `Scripts/build.ps1` без `-Remote` (лок агента на этой машине). Воркер занят гейтом лида, дизайнер сегодня не у машины.

## Хеш «до 120»

В задаче «до 120» назван `ade54f85^1`. Это не родитель без 120.

| Коммит | Что |
|---|---|
| `d4856826` | первый родитель `2843fb3d`. Фильтра `FindPathForMoveRequest` ещё нет |
| `2843fb3d` | `Merge grok/120-fence-cover` (2026-11-25 13:18:14), родители `d4856826` и `eda7c4cc`, 14 файлов. Здесь фильтр навмеша и правила 120 |
| `ade54f85^1` | это уже `2843fb3d`: 120 влит. Gemini 127 назвал 120 хешем `ade54f85` — на один коммит позже |
| `0b6efa44` | `origin/main` на старте ветки |

Дерево `d4856826` не пересобирал. Чистый repro части 1 на текущем main (после 120) проходит при pending cover 0, так что фильтр 120 в этом бою простаивал. Отдельный прогон «до» не отделяет причину, которой нет.

## Часть 1 — быстрый переход

Не воспроизводится на текущем main. Правило 120 не трогал: приказ «в укрытие» по-прежнему не перелезает.

`FormationFenceCrossSmoke` на X6 (`L_Gen_Depot_1_X6`, `-LevelJson=L_Gen_Depot_1_X6.json`), прогон 2026-11-25 19:14 UTC, `Saved/Logs/Smoke-FormationFenceCrossSmoke.log`:

- Commander: 25 см от живого слота, `high_fence_over`, путь 991 см ≤ 1.5× 1005, pending-at-click 0
- Engineer: 6 см, `high_fence_over`, путь 1273 ≤ 1.5× 967 (живой слот после смены ролей 843, прямая судьи — большая из двух)
- Medic-Sapper: 9 см, `high_fence_over`, путь 1291 ≤ 1.5× 1044, pending 0
- очередь мантии к 20 с не висит

Тот же исход был на поведении origin/main до правки (прогон ~16:43 UTC; тот лог позже перезаписан). Гейт 120 отдельно: `MantleFenceRouteSmoke` с `-FenceCover120` на том же X6 — двойной клик по земле за забором по-прежнему `high_fence_over`, двойной клик по лицу и восемь cover-кейсов остаются на ближней стороне, без подъёма.

## Часть 2 — удар на перелезе

Было: `CancelMantle` для `Knockdown` / `HeavyHit` / `Interrupted` телепортировал на `MantleBaseFeet` (дизайнер Q5, 2026-11-15). Тяжёлый удар в `HandleDamaged` отменял подъём. На поведении main первый удар на 32 % клипа снимал мантию: retreat 149 см, «dropped back to the base», удары на 60 % и 90 % не доходили.

Стало: начатый mantle или vault доигрывается до дальней стороны. Урон пишется как раньше. Нокдаун копится и играет после посадки. Смерть и `EndPlay` отпускают на месте. Приказ во время перелеза не подменяет цель продолжения точкой дальше чем на 20 см ближе к старту, чем посадка.

| Место | Правило |
|---|---|
| `MantleRules.cpp:855` | `TraversalCompletes`: удар, нокдаун, оборванный монтаж — доиграть; смерть и `EndPlay` — нет |
| `MantleRules.cpp:870` | `ResumeKeepsTheLanding`, допуск 20 см |
| `OperativeCharacterHealth.cpp:58` | `HandleDamaged` больше не зовёт `CancelMantle` |
| `KnockdownComponent.cpp:248` | во время mantle/vault нокдаун ждёт дальней стороны |
| `OperativeCharacterMantle.cpp:627` | после посадки `FlushDeferredKnockdown`, и если он уже лежит — без `RequestMove` |
| `OperativeCharacterVault.cpp:274` | то же после волта; резюме `NoOrder`, если лежит |
| `OperativeCharacterMantle.cpp:722` | запасной `CancelMantle` для удара / нокдауна / обрыва ставит на дальнюю сторону, не на базу |
| `MantleRulesTest.cpp:313` | `CodexTactics.Characters.Mantle.LinksAndHits`: доигрывание и отказ от короткой цели |

Прогон после правки (19:14 UTC, тот же лог): удары 80 HP на 32 / 60 / 91 %, retreat 0 см, дальняя сторона 159 см за лицом, нокдаун спрошен 1, cancel 0. В логе: «the knockdown waits until the far side», падение уже на дальней стороне.

Юнит: `Scripts/test.ps1 -Filter CodexTactics.Characters.Mantle` — 12 тестов, 0 падений, включая `LinksAndHits`. Порог `IsHeavyHit` (35 HP) не ослаблял.

## Часть 3 — присед по своему прибытии

Было: присед ждал совпадения рысканья со слотом (`WallSettleYawDeg`, 30°). Пока подход не кончен, первый у слота смотрит вдоль стены, поэтому стоял, пока не встанут остальные. На прогоне до правки командир дошёл за 0.85 с (yaw подхода) и присел только в 2.85 с, когда кончился covered approach. Взгляд подхода — контракт v2.5, его не менял.

Стало: присед по дистанции и остановке, без порога рысканья. Ведомый `WallSettleCm` 60 (`SquadSubsystemFormation.cpp:1251`), лидер `WallLeaderSettleCm` 120 (`:1669`). Финальный подвод к слоту остался.

Второй заход того же бага: первый присевший вставал обратно, потому что лидер ещё бежал, а присед был от отряда (`SquadSubsystem.cpp:1865`, «stands up to sprint with the leader»). Поза стены оставалась, повторный присед не срабатывал. `FormationWallHugSmoke` на восточном проходе 3.6 м: инженер в позе стены, но стоя. Теперь в позе стены он не встаёт под спринт лидера. Проход 2 м и на первом прогоне был зелёный.

Прогон 19:14 UTC, после ожидания нокдауна части 2 (смоук не отдаёт стену, пока лидер лежит — иначе приказ умирает как «the leader's next order»):

- Commander: присед 0.65 с, отметка прибытия 0.85 с
- Medic-Sapper: 1.50 / 1.50
- Engineer: 3.20 / 3.20

Последний пришёл через 3.2 с; двое уже сидели. Лимит 0.5 с от своего прибытия.

## Коммиты

| Хеш | Что |
|---|---|
| `30c54dbc` | план |
| `d2051f61` | смоук X6, три части, без смены поведения |
| `10103a5c` | судья по живому слоту (после смены ролей) и шеренга на навмеше |
| `9381f383` | доигрывание перелеза; присед без порога рысканья; смоук ждёт, пока лидер встанет |
| `b515add6` | в позе стены не вставать под спринт лидера |

## Гейт

Сборки локальные: поведение (до смоука), затем `FormationFenceCrossSmokeCommand.cpp` 41 с, затем `SquadSubsystem.cpp` 105 с. `Result: Succeeded`. TypeSafe `--audit-diff --agent grok` на диффе поведения: Boundary Breach PASS.

| Проверка | Результат |
|---|---|
| `FormationFenceCrossSmoke` | PASS, 65 с (19:14 UTC) |
| `CodexTactics.Characters.Mantle` | 12 / 12 PASS |
| `LowCoverMoveLakeSmoke` | PASS, 69 с |
| `FormationWallHugSmoke` | PASS, 83 с (после `b515add6`; 2 м и 3.6 м, exploration и бой) |
| `MantleFenceRouteSmoke -ForceMap -Map /Game/Maps/Generated/L_Gen_Depot_1_X6 -FenceCover120 -LevelJson=L_Gen_Depot_1_X6.json` | PASS, 81 с. Клик за забор — перелез. Лицо и cover case 0–7 — ближняя сторона, no climb |

Логи: `Saved/Logs/Smoke-FormationFenceCrossSmoke.log`, `Saved/Logs/Smoke-FormationWallHugSmoke.log`, `Saved/Logs/Smoke-LowCoverMoveLakeSmoke.log`, `Saved/Logs/Smoke-MantleFenceRouteSmoke.log`, `Saved/Logs/AutomationTests.log`.
