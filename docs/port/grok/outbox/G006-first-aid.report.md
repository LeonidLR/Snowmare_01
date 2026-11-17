# G006 отчёт — перетаскивание аптечки и видимость медицинской сумки

Ветка `grok/G006-first-aid`. План `d6b75de0`. Правило дропа `c0683738`. Строка сумки и стартовый набор `6bdf88dd`. Смоук `9f61d02e`, поправка Shift `3c0344a0`. Пуш и main не трогались. Карты, `.uasset`, `L_Gen_Depot_1.json` и `Config/` не менялись.

## Роутер

`model_router.py --classify` в этой сессии не вызвался: оболочка резала команды с `$env:` и аргументами python. Уровень ставился локально. Печатных имён моделей Claude нет.

- план: 4 | grok-4.7 | диагноз в `d6b75de0`, код не менялся
- правило дропа: 4 | grok-4.7 | `c0683738`
- строка сумки: 4 | grok-4.7 | `6bdf88dd`
- смоук: 4 | grok-4.7 | `9f61d02e`, затем фикс диалога стопки `3c0344a0`
- ворота: 4 | grok-4.7 | сборка, тесты и смоуки ниже
- отчёт: 2 | grok-4.7 | этот файл

Чат: план двумя строками, id 690 и 691, тема `grok-G006`. `wait --timeout 120` вернул `NO_MESSAGES`. Полные 20 минут не ждал, чтобы успеть ворота. Статусы (не будят wait): 693, 694, 696.

## Утверждение | доказательство

| Утверждение | Доказательство |
| --- | --- |
| Дроп аптечки или сумки на раненого (HP ниже максимума, любой tier из списка) начинает помощь. Shift, полное HP и tier вне списка остаются передачей. Пошаговый бой не тронут. | `c0683738`. `Content/Data/Survival/first_aid.json:13-18` список `none, light, medium, heavy`. `FirstAidRules.cpp:264-268` `TreatsOnDrop`: включено, не Shift, `0 < HP < max`, флаг tier. `FirstAidSubsystem.cpp:298-302` читает HP пациента. `CodexTacticsHUD.cpp:1845-1868` по-прежнему зовёт `ShouldTreatOnDrop`, иначе `MakeGive`; пошаговый бой отсечён на `:1852`. |
| Юнит-тесты этого правила зелёные, в том числе «полное HP — передача» и «список только heavy не лечит light». | `FirstAidRulesTest.cpp:130-147`. Прогон `Scripts/test.ps1 -Filter CodexTactics.Survival.FirstAid+CodexTactics.Characters.Transfer+CodexTactics.Transfer+CodexTactics.Loot`: `26 tests, 0 failed`, exit 0. Среди них `CodexTactics.Survival.FirstAid.AidCheck` и `DataFile`. |
| Перетаскивание аптечки на LIGHT начинает помощь и не отдаёт предмет. | Смоук на сборке `3c0344a0`, лог `Saved/Logs/Worker/farm-20261117-215639-52360/Smoke-FirstAidSmoke.log:1573`: `Smoke ok  : [light-drag] a LIGHT wound starts aid, not a transfer (outcome 2, walking over)`. Outcome 2 = `Approaching`. |
| Shift отдаёт одну аптечку сразу. Стопка по-прежнему открывает диалог количества (старая передача). | Тот же лог `:1578`: `Smoke ok  : [shift-drag] Shift hands the medkit over (outcome 1, helper 1 -> 0, patient 2 -> 3)`. Outcome 1 = `Transferred`. Код `FirstAidSmokeCommand.cpp:402-415`. Первый прогон на `9f61d02e` упал здесь: outcome 4 (`DialogOpened`) при 5 аптечках (`farm-20261117-213025-49932` `:1576`). Остальные проверки того прогона были ok. |
| Медик-сапёр на старте уровня имеет 1 сумку. В инвентаре командира строка называется Medical bag и пишет, что сумка в отряде. У медика строка `x1/1` и включена. | Тот же лог `:1562` `the medic-sapper starts with 1 medical bag(s) (has 1)`, `:1563` `Medical bag: x0/1 (1 in squad)`, `:1566` `Medical bag: x1/1`. Выдача `CodexTacticsGameMode.cpp:398-416`, вызов при спавне `:538` и для уже стоящего отряда до carry `:457-464`. Текст строки `InventoryDrawerWidget.cpp:272-291`. Порядок в сетке сразу после аптечки, enum не двигался (`:529-542`). |
| FirstAidSmoke дважды зелёный на финальном коммите смоука. | Воркер, бинарь `3c0344a0`. Прогон 1: `FirstAidSmoke PASS 65.4s`, `FIRST_AID_RUN_1 exit 0`, лог `Saved/Logs/Worker/farm-20261117-214500-51872`. Прогон 2: `FirstAidSmoke PASS 44.0s`, `Smoke RESULT: PASS` (`farm-20261117-215639-52360:1689`), `FIRST_AID_RUN_2 exit 0`. |
| InventorySmoke и TransferSmoke зелёные. | Вместе с первым FirstAid на билде `9f61d02e` (геймплей инвентаря и передачи с тех пор не менялся; `3c0344a0` только смоук). `InventorySmoke PASS 32.5s`, `TransferSmoke PASS 33.1s`. Логи `Saved/Logs/Worker/farm-20261117-213025-49932`. |
| Сборка воркера. | `Scripts/build.ps1 -Remote worker`. На `9f61d02e`: `Result: Succeeded`, `Total execution time: 309.86 seconds`, exit 0, DLL вернулись. На `3c0344a0`: `Result: Succeeded`, `314.35 seconds`, exit 0, 5 DLL. Предупреждение Visual Studio SDK — строка движка, не ошибка проекта. |
| Границы агента. | `typesafe_triage.py --audit-diff --agent grok` по чистому дереву: `Clean workspace`. По индексу перед `3c0344a0`: `Boundary Breach : PASS`, `Risk Rating : 1.0 / 4 (Jev)`. Защищённых путей в коммитах нет. |

## Что не сделано

- Иконку `.uasset` не добавлял. Строка текстовая. Эмодзи с этой строки снят: HUD его вырезает, из-за него название пропадало.
- Русского «Медицинский саквояж» в интерфейсе нет. Текст игрока английский: `Medical bag`. У командира (лидер по умолчанию) строка видна и выключена, пока своих сумок 0: `Medical bag: x0/1 (1 in squad)`. Включается, когда лидер — медик.
- Отдельного смоука на medium и на полное HP нет. Это юнит-тесты `AidCheck` (`FirstAidRulesTest.cpp:131` и `:134`).
- Ветку «отряд уже стоит на уровне» смоук не гонял: `L_MovementTest` спавнит отряд. `L_Gen_Depot_1.json` не редактировался. Carry по-прежнему перекрывает стартовую сумку, если в сохранении уже есть число.
- Ждал Claude 120 с, не 20 мин.
- `--classify` не вызывался (см. роутер).
- Не пушил и не мержил.
