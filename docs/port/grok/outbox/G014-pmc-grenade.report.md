# G014 — приказ PMC «граната»

Статус: PASS. Код `7b9e4b375823190739d57d4c335f48a3dcc70657` на `grok/G014-pmc-grenade` (от `origin/ai/x6-arena` `ef80cef3`). Не пушил.

Роутер: `python Scripts/Tools/model_router.py --classify "PMC commander grenade order C++ utility AI"` → level 3, policy_blocked (нет `Saved/Lead/ALLOW_SUBAGENTS`). Работал на grok-4.7, без субагентов.

`3 | grok-4.7 | pass`

Граната — только приказ командира. `PmcPolicyRules` на этой ветке нет. В набор из 9 приказов Learning Agents ничего не добавлял.

## Что сделано

Боец бросает гранату через уже существующий `UGrenadeSubsystem::ThrowAt`, когда это окупается: видимый чужой оперативник, до которого винтовка не добивает, или двое и больше в диске взрыва. Дальность броска с учётом стойки, не ближе 5 м, свой в 6 м от точки удара запрещает бросок. Один бросающий за раз. Монстры по-прежнему на автономном `TryAIGrenadeThrow`.

Вес `grenade` в коде 0: файл без ключа не бросает. В `pmc_commander.json` стоит 0.9. Пороги безопасности в `pmc_commander.bounds.json` заморожены, вес `grenade` — ген тренера (0.45..1.35).

Арена: `Grenades=0|1`, по умолчанию 1. В раунд пишется `grenades` и `grenades_thrown`.

## Claim | Evidence

| Утверждение | Доказательство |
|---|---|
| Коммит только по задаче, 9 файлов, не пушил | `7b9e4b375823190739d57d4c335f48a3dcc70657` `feat(ai): PMC commander grenade order (G014)`. `git status -sb`: `ahead 1`, чужие junction и `Config/DefaultScalability.ini` не в коммите |
| Сборка редактора | `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1` → `Result: Succeeded`, `Total execution time: 478.54 seconds`, exit 0. Предупреждения unity-clash и `Feet` в `CodexTacticsPlayerControllerOrders.cpp` были до этой правки |
| Юнит-тесты приказа | `Scripts/test.ps1 -Smart -SkipSmokes` → `158 tests, 0 failed`, `unit 0/158 failed (30 s); smokes skipped`. В `Saved/Logs/AutomationTests.log`: `GrenadeOrder` Result=Success, `GrenadeTarget` Result=Success |
| Граница агента | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok` → `Boundary Breach : PASS`, риск `2.0 / 4`, `9 files changed, 334 insertions(+), 8 deletions(-)` |
| Смоки на воркере, не на этом ПК | `worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot2 -ShipHead -ShareMachine -Commands <19 имён>`, exit 0. Синк `7651bae6 -> 7b9e4b37`. `[smoke_parallel] 19 run, 0 failed, 0 passed only on the re-run (PASS*), 3 known-fail; wall 1,714 s`. Каталог `Saved/Logs/Worker/20261124-155701` |
| `-Smart` не гонял движок здесь | У `-Smart` в карте есть смоки. Запустил с `-SkipSmokes`. Движковые смоки на этой машине не стартовали |

Таблица воркера (jobs 2):

```
TacticalPmcSmoke           PASS     120.6
PmcArenaTacticalSmoke      PASS     142.1
PmcTeamSmoke               PASS      62.9
PmcCommanderSmoke          PASS      53.3
PmcTraversalSmoke          KNOWN-FAIL 1408.5
PmcArenaSmoke              PASS      59.8
PmcArenaObjectiveSmoke     PASS      54.0
PmcStalemateSmoke          PASS      56.1
PmcRoofDefenceSmoke        PASS      86.5
PmcCrossroadsSmoke         KNOWN-FAIL 118.3
PmcAlleySmoke              KNOWN-FAIL 115.7
PmcEncounterSmoke          PASS      50.5
PmcEncounterHuntSmoke      PASS      54.7
PmcPostsSmoke              PASS      46.6
PmcOwnedChargesSmoke       PASS      45.2
PmcStandoffSmoke           PASS      46.8
PmcDeathPoseSmoke          PASS      46.0
PmcUnderFireSmoke          PASS      44.0
PmcFireLineSmoke           PASS      48.5
```

`PmcCrossroadsSmoke` и `PmcAlleySmoke` — `knownFail` в `Scripts/test_map.json` (T-04b, красные нарочно). `PmcTraversalSmoke` — `knownFail` там же, строка реестра: лестницы и сбросы ниже 95 % (T-12), не из-за этой задачи. Новый FAIL не появился. Текст лога Traversal с воркера не забирал (`-AllLogs` не ставил).

`-Smart` насчитал 4254 «изменённых» файла: неотслеживаемые junction паков. На фильтр юнитов это не повлияло: `CodexTactics.AI.PmcCommander.Grenade*` вошли и прошли.

## Файлы

- `Source/CodexTactics/Public/AI/PmcCommanderRules.h`, `Private/AI/PmcCommanderRules.cpp` — `EPmcOrder::Grenade`, `ScoreGrenadeTarget`, вес и пороги, чтение JSON.
- `Content/Data/AI/pmc_commander.json`, `pmc_commander.bounds.json`.
- `Source/CodexTactics/Public/AI/PmcSquadSubsystem.h`, `Private/AI/PmcSquadSubsystem.cpp` — выбор цели, `ThrowAt`, `GrenadesThrown`, один бросающий.
- `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp` — `Grenades=0|1`.
- `Source/CodexTacticsTests/Private/AI/PmcCommanderRulesTest.cpp` — `CodexTactics.AI.PmcCommander.GrenadeTarget`, `GrenadeOrder`.
- `docs/systems/pmc-squad-ai.md` — строка таблицы.

## Не проверено

- Парный прогон X6 на 300 семян (1060–1359), `Grenades=0` против `1`, сравнение с `det/x6base-r1`. Не запускал.
- Карточка навыка кроме `grenades_thrown`: убийства гранатой, выход из укрытия за 3 с, тимкиллы, время в укрытии, отход в укрытие. Счётчика тимкилла гранаты нет. Свой может войти в радиус за время запала.
- Обучение Learning Agents и редакторский PIE.
- Локальные смоки карты `-Smart` (их список шире 19 имён PMC). На этой машине их нет.
