# 120 — укрытие у низкого забора: своя сторона, присед, без перелезания

Ветка `grok/120-fence-cover`. G017 не возобновлялся (код `bc41b3fc` не трогал). В main не вливал, не пушил.

Роутер: level 3 | grok-4.7 | pass (`model_router.py --classify`, confidence 0.73).

## Claim

Приказ «в укрытие» по силуэту приседа у низкого забора больше не перелезает через него. Боец остаётся на своей стороне, прижатый к забору, в приседе. Перелезание остаётся у клика по верху и у приказа перейти на ту сторону (путь через забор). Двойной клик по лицу глубокого верха (обломок 140 см, стойка наверху) по-прежнему лезет наверх.

Codex на `9f19a13b` уже закрыл дальний слот и авто-вольт на подходе. Дыра, которая оставалась: двойной клик по силуэту укрытия заказывался как climb (`OrderMantleTop`) до подтверждения укрытия. Для забора 135 см это `high_fence_over` (через, не наверх). `OrderMoveTo` внутри climb сбрасывал pending cover, и защита Codex не срабатывала.

## Что изменено

| Хеш | Что |
|---|---|
| `b7506da7` | fix(cover): клик по лицу низкого укрытия остаётся укрытием, если подъём не ставит на верх; слот не проецируется на верх забора; авто-mantle и авто-vault не срывают низкое укрытие |
| `5b17fda3` | test(smoke): групповой приказ — стена формации (присед у забора), соло по-прежнему входит в слот |

Правило: `CoverTraceRules::FaceDoubleClickStaysCover` — `Source/CodexTactics/Private/Tactics/CoverTraceRules.cpp:336` (объявление `CoverTraceRules.h:117`). Низкое укрытие и подъём не стоит наверху → укрытие. Иначе climb (обломок, высокий парапет).

Вызовы: `CodexTacticsPlayerControllerClickPreview.cpp:868` (путь дизайнера, `HandlePlayerClick`) и `CodexTacticsPlayerControllerOrders.cpp:569` (`HandleWorldHit`, им же пользуется смоук забора).

Пока игрок в низком укрытии или идёт в него, `UpdateMantleTrigger` глушит ссылку пути (`OperativeCharacterMantle.cpp:1255`), кроме явного `bMantleOrderPending`. `TryVault` отказывает и уже присевшему в низком укрытии (`OperativeCharacterVault.cpp:102`): `EnterCover` снимает pending, и старый запрет Codex после входа не держал. Прямой `TryVault` вне укрытия не затронут.

Проекция навмеша: если Z слота низкого укрытия выше `LowCoverSlotMaxAboveGroundFraction` (0.5) высоты стены, ноги возвращаются на землю (`CoverTraceRules.cpp:267`, поле `CoverTraceRules.h:43`).

Юнит: `CoverSystemTest.cpp:46-49`, фильтр `CodexTactics.Tactics.Cover.TraceDetectsHighWallVsLowBarricade`.

## Evidence

Сборка только на worker.

```
[build_remote] BUILT on codex-worker ... b7506da7
Result: Succeeded
Total execution time: 375.82 seconds
```

```
[build_remote] BUILT on codex-worker ... 5b17fda3
Result: Succeeded
Total execution time: 23.82 seconds
```

Юнит локально (`Scripts/test.ps1`, редактор дизайнера не держал DLL, UBT локально не собирал):

```
Success  CodexTactics.Tactics.Cover.TraceDetectsHighWallVsLowBarricade
1 tests, 0 failed.
```

`typesafe_triage.py --audit-diff --agent grok`: PASS на диффе `b7506da7` (8 файлов, boundary breach PASS). После коммитов рабочее дерево чистое.

Смоук X6, билд `5b17fda3`, `MantleFenceRouteSmoke -ForceMap -Map /Game/Maps/Generated/L_Gen_Depot_1_X6 -FenceCover120`:

```
MantleFenceRouteSmoke      PASS      46.6s
```

Лог `Saved/Logs/Worker/farm-20261125-130608-84468/Smoke-MantleFenceRouteSmoke.log`:

```
Smoke ok  : StaticMeshActor_355: the click beyond it (path link, walking) - mantled over it (high_fence_over), arrived in 2.6 s
Smoke ok  : StaticMeshActor_355: the click beyond it (path link, running) - mantled over it (high_fence_over), arrived in 2.4 s
Smoke ok  : StaticMeshActor_355: a double click on its face stays low cover on the near side, crouched, no climb (mantle none, in cover 1)
Smoke ok  : cover case 0: 1 monitored, no climb 1, held 1 (slot 1, crouched 1)
Smoke ok  : cover case 1: 1 monitored, no climb 1, held 1 (slot 1, crouched 1)
Smoke ok  : cover case 2: 3 monitored, no climb 1, held 1 (slot 0, crouched 1)
Smoke ok  : cover case 3: 3 monitored, no climb 1, held 1 (slot 0, crouched 1)
Smoke ok  : cover case 4: 1 monitored, no climb 1, held 1 (slot 1, crouched 1)
Smoke ok  : cover case 5: 1 monitored, no climb 1, held 1 (slot 1, crouched 1)
Smoke ok  : cover case 6: 3 monitored, no climb 1, held 1 (slot 0, crouched 1)
Smoke ok  : cover case 7: 3 monitored, no climb 1, held 1 (slot 0, crouched 1)
Smoke RESULT: PASS
```

Случаи 0–1: соло, обе стороны забора 135 см, вход в слот. 2–3: группа (3 человека), присед у стены, `bInCover` = 0 — это стена формации (`-SquadStart=formation`), не слот соло. 4–5: соло, джерси 95 см у (2568, -9520). 6–7: группа у джерси. Ноль вольтов и ноль mantle на приказах укрытия. Клик по земле за забором (шаг и бег) по-прежнему `high_fence_over`. Верх забора 10 см кликом не лезется (как и раньше, «nothing to measure»).

`LowCoverMoveLakeSmoke` PASS 43.8s (своя карта `L_Demo01_A_FrozenLake`, билд `b7506da7`).

`DepotVaultSmoke` PASS 36.7s на X6: прямой `TryVault` вне укрытия жив.

`MantleClickSmoke` на X6 (билд `b7506da7`, лог `farm-20261125-125601-84724`): двойной клик по лицу обломка 140 см всё ещё лезет наверх:

```
Smoke ok  : b: double click on the 140 cm wreck's face: he goes to it and climbs it (RunHighClimb after 0.5 s)
```

Там же FAIL только «c: 230 cm parapet: a test site on the map» и «d: 330 cm fence: a test site on the map». На X6 этих площадок нет.

## Что не сделано

`CoverSmoke` и `MantleSmoke` целиком зелёными на своих картах не прогнаны. `L_MovementTest` и `L_Gen_Depot_1` в этом дереве нет (они в `archivedMapSmokes`). Принудительный прогон на X6:

- `CoverSmoke` FAIL: луч зрения упёрся в `StaticMeshActor_62` карты X6 (`eye (-42, -16373, 112)`), не в логику укрытия.
- `MantleSmoke` FAIL: нет парапета 230 см и забора 330 см («a test site on the map»). Мешок песка вольтом прошёл (`a sandbag is no mantle`).

Записи дизайнера `Saved/Demos/2026-11-25_L_Gen_Depot_1_X6_*.jsonl` в редакторе не переигрывал: редактор не открывал, главный чекаут только для чтения. Доказательство — смоук на X6 выше.

Предупреждения C4701 `Feet` в `CodexTacticsPlayerControllerOrders.cpp:943` и `:1043` были до правки (hover лестницы и окна). Не трогал.
