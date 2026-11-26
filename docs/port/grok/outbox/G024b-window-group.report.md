# G024b — соло-бокс из трёх идёт в заказанное окно

Статус: PASS. Код `789940fd11891f65689c6ae99ce29e93f29e276f` на `grok/G024b-window-group` (от `bdd69437`, origin/main на старте задачи). Не пушил. Ветка на момент отчёта ahead 2, behind 10 относительно origin/main: rebase не делал.

Лид: G025 в воротах слияния, базу брать от origin/main, сборку гнать локально через `machine_dispatch.py build --lane`. Воркер был занят night-loop, диспетчер выбрал main. Воркдерево `C:\Work\Lanes\CodexTactics-G024b`. G025 (`C:\Work\Lanes\CodexTactics-grok`) не переключал.

Роутер: `python Scripts/Tools/model_router.py --classify "file a solo box selection through the ordered window"` → level 3, model null, route false, policy_blocked (`no subagents while Saved/Lead/ALLOW_SUBAGENTS is absent`). Работал на grok-4.7, без субагентов.

`3 | grok-4.7 | pass`

В SOLO бокс из трёх, которому приказали конкретное окно, больше не разъезжается в боковые слоты строя и не уходит в дверь. Каждый встаёт на ось залезания этого окна со своей стороны: ранг 0 на посадке, ранг N на `solo_group_queue_spacing_cm` × N дальше вдоль залезания. Первая нога — ближняя сторона, целые окна закрыты.

## Что сделано

`WindowOrderRules::FileSoloGroup` (`WindowOrderRules.cpp:135`). Подход и посадка G024. Если ступни уже за серединой между ними, подход и посадка отражаются, чтобы человек лез со своей стороны. Z копируется со ступней. Шаг очереди `solo_group_queue_spacing_cm` = 150 в `Content/Data/Interactables/windows.json` и в дефолте структуры.

`OrderGroupMove` (`CodexTacticsPlayerControllerOrders.cpp:181`): при клике в окно боковой разнос `ComputeGroupTargets` не считается. Каждый выбранный получает цель `FileSoloGroup` и `ArmWindowApproach`. Обычный клик по полу по-прежнему берёт старый разнос. Один человек — ранг 0, посадка, как в G024.

Пауза: `PlanGroupMove` / `StorePlannedMove` копируют подход (`SquadSubsystem.cpp:1489`, `bWindowFile`). `ExecutePlannedOrders` вооружает его перед `OrderMoveTo` и перед повтором недостижимого слота. Смоук кликает сразу, этот путь в логе не виден.

`WindowDetourSmoke`: после зелёного одиночного случая — бокс из трёх через `DisbandFormationToSoloBox`. Чужое отверстие до перехода заказанного окна — FAIL. Клик по одному подоконнику ставит всех в очередь в это окно, не в соседние окна стены: другое отверстие до перехода смоук считает провалом.

## Claim | Evidence

| Утверждение | Доказательство |
|---|---|
| Коммиты только по задаче, не пушил | `7afe097ddcfa63a3881a2ba4f1bdd2dc498fae25` план. `789940fd11891f65689c6ae99ce29e93f29e276f` код, 8 файлов, `316 insertions(+), 27 deletions(-)`. `git status -sb`: `## grok/G024b-window-group...origin/main [ahead 2, behind 10]`, Source/docs чистые. Неотслеживаемые junction паков и `Config/DefaultScalability.ini` в коммиты не входили |
| Сборка на main, код `789940fd` | `machine_dispatch.py build --lane C:\Work\Lanes\CodexTactics-G024b` → `DISPATCH build CodexTactics-G024b -> main (worker busy: night-loop (night loop job))`, `Result: Succeeded`, `Total execution time: 1620.13 seconds`, exit 0. Лог `Saved/Logs/Build.log`. unity-clash 7 и `Feet` C4701 в `CodexTacticsPlayerControllerOrders.cpp:1010` и `:1110` — те же предупреждения, что G024 называл старыми (тогда строки :974 и :1074) |
| Граница агента | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok` после коммита кода: `Auditing unstaged changes for agent 'grok' (0 files)...` / `Clean workspace: No changes to audit.`, exit 0. В коммите нет `Content/VFX`, `Config`, `Content/Maps`, `.uasset`, `CLAUDE.md`, `L_Gen_Depot_1.json`, props json |
| Юнит-тесты | `Scripts/test.ps1 -Filter CodexTactics.Interactables.WindowOrder` → `7 tests, 0 failed`. `Saved/TestReport/index.json` `reportCreatedOn` `2026.11.26-02.21.26`, `succeeded` 7, `failed` 0. Новое: `SoloGroupFilesThrough` state `Success`. Старые шесть, включая `FileMirrorsDefaults` и `SideIsTheOperatives`, тоже `Success` |
| Смоук на main, код `789940fd` | `machine_dispatch.py smoke --lane C:\Work\Lanes\CodexTactics-G024b --smokes WindowDetourSmoke` → `DISPATCH smoke WindowDetourSmoke -> main (worker busy: night-loop ...)`, exit 0. Карта `/Game/Maps/Generated/L_Gen_Depot_1_X6` |

Таблица:

```
[smoke_parallel] 1 smokes, 4 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  WindowDetourSmoke          PASS      63,2s
[smoke_parallel] 1 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 67 s, sum of checks 63 s
```

Одиночный, `Saved/Logs/Smoke-WindowDetourSmoke.log`:

```
Commander: move to (5565, -2711) (near side first, windows closed) -> accepted
Smoke ok  : the click is a window order
Smoke ok  : he crosses the ordered window (at 3.0 s)
Smoke ok  : he enters no other opening before it (0:)
Smoke ok  : he ends on the far side (side 1, start -1, dist 120)
Smoke info: single operative passed, next is the box selection of 3
```

Бокс, тот же лог. Сообщение дизайнера, затем трое в одну линию с шагом 150 см, все «near side first, windows closed», все на дальней стороне, чужих проёмов 0:

```
[SQUAD] [SOLO MODE: ON] Box selection disbanded formation into Solo mode (3 operatives selected).
[SQUAD] Group (3 operatives) moving into position!
Commander: move to (5835, -2322) (near side first, windows closed) -> accepted
Engineer: move to (5985, -2322) (near side first, windows closed) -> accepted
Medic-Sapper: move to (6135, -2322) (near side first, windows closed) -> accepted
Smoke ok  : the group click is a window order
Smoke ok  : Solo box selection of 3 is the group that was ordered
Commander vaults over StaticMeshActor_394 (95 cm)
Engineer vaults over StaticMeshActor_394 (95 cm)
Medic-Sapper vaults over StaticMeshActor_394 (95 cm)
Smoke ok  : BP_Operative_C_0 crosses the ordered window (at 2.9 s)
Smoke ok  : BP_Operative_C_0 ends on the far side (side -1, start 1, dist 172)
Smoke ok  : BP_Operative_C_1 crosses the ordered window (at 4.4 s)
Smoke ok  : BP_Operative_C_1 ends on the far side (side -1, start 1, dist 220)
Smoke ok  : BP_Operative_C_2 crosses the ordered window (at 5.8 s)
Smoke ok  : BP_Operative_C_2 ends on the far side (side -1, start 1, dist 411)
Smoke ok  : nobody enters another opening before the ordered window (0:)
Smoke RESULT: PASS
```

После перехода GiveWay сдвинул командира с пути следующих, поэтому конечные dist 172 / 220 / 411 — это не слоты посадки. Все трое перелезли один и тот же подоконник `StaticMeshActor_394`.

## Не сделано

- Не пушил. `main`, карты, `.uasset`, `Config`, `Content/VFX`, `L_Gen_Depot_1.json` и props json не трогал. Junction паков контента в коммитах нет.
- Клик по одному подоконнику не раздаёт людей по соседним окнам той же стены. Очередь одна, в заказанное окно. Иначе `WindowDetourSmoke` падает на чужом проёме.
- Мантию `LeftToRight` и цену нав-области окна не менял.
- Пауза вооружает подход в коде; смоук отпускает приказ сразу, так что отпуск паузы в логе не проверен.
- `WindowCoverFireSmoke`, `CutawayClickSmoke`, `FormationWindowSmoke` в этом задании не гонял. Одиночная половина `WindowDetourSmoke` в том же прогоне зелёная.
- G025 не начинал и не трогал.
