# G024 — приказ окна со своей стороны

Статус: PASS. Код `e92eb4e63cd49d413f09d294e7845852272fc63a` на `grok/G024-window-side` (от `d4856826`). Не пушил.

Роутер: `python Scripts/Tools/model_router.py --classify "explicit window order own side in formation and follower queue"` → level 3, model null, route false, policy_blocked (`no subagents while Saved/Lead/ALLOW_SUBAGENTS is absent`). Работал на grok-4.7, без субагентов.

`3 | grok-4.7 | pass`

Явный приказ «залезть в это окно» идёт с той стороны, где стоит оперативник, и в соло, и в строю. Лидер подходит к своей стороне с закрытыми окнами и лезет в заказанное. Колонна ждёт, пока он окажется на дальней стороне, и встаёт в очередь за посадкой. Обычный клик по полу окно не открывает: открытый путь только у автоматической маршрутизации.

Чат #1184: баг дизайнера — «приказал окно, он обежал». `47385a98` как раз отпускал лидера строя на открытый путь. Это снято.

## Что сделано

`WindowOrderRules::ChooseSide` берёт тонкую ось подоконника и сторону оперативника. Подход — `approach_past_cm` 80 на его стороне, посадка — `landing_past_cm` 120 на дальней. Цифры в `Content/Data/Interactables/windows.json`.

`ArmWindowApproach` ставит файл окна. `OrderMoveTo` ведёт к своей стороне через `SteerOwnMoveVia` с закрытыми целыми окнами; цель приказа остаётся посадкой. Другой клик файл снимает. Автоматический приказ via не вооружает.

`QueueThroughWindow`: пока лидер не перешёл середину между подходом и посадкой, места нет (колонна не переходит раньше). Дальше место ранга — посадка плюс `column.spacing` × (ранг + 1) вдоль залезания. Слот последователя подменяется только тогда (`SquadSubsystemFormation.cpp`).

## Claim | Evidence

| Утверждение | Доказательство |
|---|---|
| Коммиты только по задаче, не пушил | `9d3de3ca` план. `14d0f74c` сторона и via. `769934b3` посадка остаётся целью. `47385a98` пропуск via у лидера строя (снят следующим). `e92eb4e6` файл и очередь. `git status` по Source/Scripts/docs/port/grok чистый. Junction паков не в коммитах |
| Сборка воркера, код `e92eb4e6` | `Scripts/build.ps1 -Remote worker` → `Result: Succeeded`, `Total execution time: 389.25 seconds`, exit 0. Лог `Saved/Logs/Worker/build-20261125-162641-78252/Build.log`. unity-clash 7 и `Feet` C4701 в `CodexTacticsPlayerControllerOrders.cpp` :974 и :1074 были до этой правки |
| Граница агента | До коммита `e92eb4e6`, 6 файлов в индексе: `Boundary Breach : PASS`, риск `2.0 / 4`, `6 files changed, 91 insertions(+), 10 deletions(-)`, exit 0 |
| Юнит-тесты | `Scripts/test.ps1 -Filter CodexTactics.Interactables.WindowOrder` → `6 tests, 0 failed`. `Saved/TestReport/index.json` `reportCreatedOn` `2026.11.25-14.44.54`, `succeeded` 6, `failed` 0. В том числе `SideIsTheOperatives` и `QueueFollowsTheLeaderThrough` |
| Смоуки slot2, код `e92eb4e6` | `worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot2 -ShipHead -ShareMachine -Jobs 1 -AllLogs -NoRetry -Commands WindowDetourSmoke,WindowCoverFireSmoke,CutawayClickSmoke,FormationWindowSmoke`, exit 0. Каталог `Saved/Logs/Worker/20261125-163944` |

Таблица воркера:

```
[smoke_parallel] 4 smokes, 1 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  WindowDetourSmoke          PASS      54.0s
  WindowCoverFireSmoke       PASS      48.4s
  CutawayClickSmoke          PASS      48.8s
  FormationWindowSmoke       PASS      47.1s
[smoke_parallel] 4 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 201 s, sum of checks 198 s
```

Соло, X6, `Saved/Logs/Worker/20261125-163944/Smoke-WindowDetourSmoke.log`:

```
Commander: move to (5565, -2711) (near side first, windows closed) -> accepted
Smoke ok  : the click is a window order
Smoke ok  : he crosses the ordered window (at 3.0 s)
Smoke ok  : he enters no other opening before it (0:)
Smoke ok  : he ends on the far side (side 1, start -1, dist 120)
Smoke RESULT: PASS
```

Строй, `Smoke-FormationWindowSmoke.log`. Обычный клик окно не берёт. Явный приказ — со своей стороны, все трое на дальней, никто раньше разбития:

```
Smoke ok  : P: the formation does not take the whole window on its own (broken 0, window order 0, 3 men just behind it)
Commander: move to (2765, -6475) (near side first, windows closed) -> accepted
[Window] Commander breaks the window StaticMeshActor_1128 climbing through: noise 15 m, 0 enemies heard it
Smoke ok  : O: the leader breaks the window climbing it (1.0 s)
Smoke ok  : O: the whole formation follows him through it (5.4 s) [Commander (leader) through at 1.6 s] [Engineer through at 3.5 s] [Medic-Sapper through at 5.4 s]
Smoke ok  : O: no follower goes through before the window is broken
Smoke RESULT: PASS
```

`WindowCoverFireSmoke` и `CutawayClickSmoke`: `Smoke RESULT: PASS`.

## Почему 47385a98 был неверен

На `14d0f74c` и `769934b3` соло уже проходило, а медик строя нет: слот колонны оставался снаружи. `47385a98` убрал via у лидера строя. На `Saved/Logs/Worker/20261125-154303` лидер шёл `move to (2765, -6475) -> accepted` без «near side first». Окно он разбил, медик за 40 с не перешёл. Открытый путь не лечит строй и как раз позволяет обежать здание. `e92eb4e6` возвращает via и ставит очередь за посадкой, когда лидер уже на дальней стороне.

## Не сделано

- Не пушил. `main`, карты, `.uasset`, `Config`, `Content/VFX`, `L_Gen_Depot_1.json` и props json не трогал.
- Плановый приказ via не вооружает (как и до G024).
- Мантию `LeftToRight` и цену нав-области окна не менял.
- Junction паков в коммитах нет.
- G022 не начинал: лид сказал остановиться после FINISH.
