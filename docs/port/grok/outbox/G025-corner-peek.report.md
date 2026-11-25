# G025 — угол: конус, плечо, CoverIndoor, торс, pitch строя

Статус: PASS по тому, что можно прогнать. Код `bab67531` на `grok/G025-corner-peek` (от `origin/main` `3c789a86`). Не пушил. MantleSmoke не шёл: карты нет.

Роутер: `python Scripts/Tools/model_router.py --classify "G025 corner look aim offset torso reset formation scan pitch"` → level 3, confidence 0.89, route false, `policy_blocked` (`no subagents while Saved/Lead/ALLOW_SUBAGENTS is absent`). Работал на grok-4.7, без субагентов.

`3 | grok-4.7 | pass`

Порядок лида, не порядок inbox. Ветка `grok/G025-corner-peek`, отчёт `G025-corner-peek.report.md` (очередь ловит `G025-*.report.md`; inbox звал `G025-look-polish`). Сборка локально: `Scripts/build.ps1` без `-Remote`. G026 не трогал.

## Что сделано

Конус. Пока есть ручной взгляд, центр peek-конуса смотрит на точку взгляда. Кламп AO в конус не подмешивается.

Плечо. Стойки 66/34/74/56 см. Снап угла остаётся 15 см: правое ребро на 80 см при стойке 66 даёт 14 см, снапа нет. Старые 57/28/65/49 не возвращал.

CoverIndoor. Shift-клик, пока уже идёт peek угла, не заказывает новый peek и не wrap. Поза переключается на набор `riffle`. Рука — знак взгляда относительно нормали стены, гистерезис `CornerLookSideHysteresisDeg` 12. Yaw и pitch клампятся осями блендспейса. Обычный клик по силуэту и выход из укрытия возвращают прежнюю позу. Тело не идёт на другое ребро: заказанный край остаётся peek-точкой.

ABP_Operative. Живой узел уже был связан: BlendSpace ← ActiveAimOffset, X ← AimYaw, Y ← AimPitch. Несвязанный узел не трогал. `bindings changed 0`, граф не пересобирался, ассет не сохранялся. На диске остаётся коммит дизайнера `74f1d128`. Изменений графа нет.

Торс. `ReleaseTorsoToMovement` обнуляет yaw меша, ствол и controller-desired rotation. В укрытии и в залезании вызов пустой. Pitch скана остаётся, если взгляд строя ещё задан. Выходы: снятие взгляда, выход из укрытия (в коммите AO, иначе линк не сходится), конец и отмена залезания, свой приказ хода.

Скан. `FormationSectorRules::LookPitch` качает pitch по периоду скана, роли сдвинуты по фазе. Над глазом на 150 см и дальше центр смещается на `scan.pitch_high_bias` 18°. На открытом размах — дважды `scan.pitch_amp` 12°. Читатель pitch в AnimInstance уехал в коммите AO; производитель — в коммите скана.

## Claim | Evidence

| Утверждение | Доказательство |
|---|---|
| Коммиты по шагам, не пушил | `96ab52cc` план. `e934a6d0` конус (лид принял). `4a2bb6e6` плечо. `b116647c` CoverIndoor. `e269da30` торс. `bab67531` скан и `test_map`. `git status` по Source/Scripts/docs/Content/Data чистый. Паки, `Config/DefaultScalability.ini` и скопированный kit umap не в коммитах |
| Сборка этого дерева, до трёх коммитов, код тот же | `Scripts/build.ps1` → `Result: Succeeded`, `Total execution time: 19.89 seconds`. Лог `Saved/Logs/Build.log`. Дальше правок не было: коммиты только разложили это дерево |
| Юнит-тесты на `bab67531` | `Scripts/test.ps1 -Filter` LookAo+Torso.Reset+ScanPitch+Tuning+CornerStandOffPerClipSide. Шесть Success, 0 failed. `Saved/TestReport/index.json` `reportCreatedOn` `2026.11.25-23.49.40`, succeeded 5, succeededWithWarnings 1, failed 0. Предупреждения у `ParseClampAndUnknownKeys` — сам тест (битый ключ и кламп) |
| Плечо, X6 | `Smoke-CornerSlotSmoke.log` `2026.11.25-21.25.10` `Smoke RESULT: PASS`. Плечо 14.9–20.3 см, проникновение 0.0 (лимит 25 и 2) |
| CoverIndoor, рендер | `Smoke-CoverIndoorSmoke.log` `2026.11.25-23.35.04` `Smoke RESULT: PASS`. Команда была `-RenderOffscreen`, не `-nullrhi`. 18 PNG 1280×720 в `Saved/CoverIndoor` (не в git) |
| Торс, X6 | `Smoke-TorsoDriftSmoke.log` `2026.11.25-23.40.50` `Smoke RESULT: PASS`. high_mantle 140 см, mesh versus velocity 2.1 deg after 0.5 s (gate 10), walked 202 cm |
| Скан, X6 | `Smoke-FormationScanPitchSmoke.log` `2026.11.25-23.12.43` `Smoke RESULT: PASS`. open ground aim pitch span 41.6 deg (gate 20), aim pitch toward the building 13.3 deg (gate +10) |
| Стена строя, озеро | `Smoke-FormationWallHugSmoke.log` `2026.11.25-23.52.26` `Smoke RESULT: PASS`, 88.8 с. Шесть сцен, худший зазор от лица +1 см к соло-стойке |
| Граница | `typesafe_triage.py --audit-diff --agent grok` после коммитов: `Clean workspace: No changes to audit`, exit 0. Файлы `origin/main...HEAD` не входят в запрет grok (Config/, Content/Maps/, CLAUDE.md, VFX, Shaders, два json депо) |

Плечо, оба хвата, стойка и присед (`Smoke-CornerSlotSmoke.log`):

```
Smoke ok  : in-cover high right stand: outer shoulder 14.9 cm from the edge (limit 25)
Smoke ok  : in-cover high left stand: outer shoulder 20.3 cm from the edge (limit 25)
Smoke ok  : in-cover low right crouch: outer shoulder 17.0 cm from the edge (limit 25)
Smoke ok  : in-cover low left crouch: outer shoulder 17.3 cm from the edge (limit 25)
Smoke ok  : in-cover low left crouch: penetration 0.0 cm (capsule, limit 2)
Smoke RESULT: PASS
```

CoverIndoor, kit, стойка и присед (`Smoke-CoverIndoorSmoke.log`):

```
Smoke ok  : shimmy back to the wall's exposed edge
Smoke ok  : standing look drives aim pitch up and down
Smoke ok  : the look crosses the corner normal onto the other hand
Smoke ok  : the look drives aim yaw off centre
Smoke ok  : clearing the look returns to the plain peek
Smoke ok  : crouched look drives aim pitch up and down
Smoke ok  : the look crosses the corner normal onto the other hand
Smoke ok  : the look drives aim yaw off centre
Smoke ok  : clearing the look returns to the plain peek
Smoke RESULT: PASS
```

Кадры взгляда: `05_stand_look_right_up.png`, `06_stand_look_right_down.png`, `07_stand_look_left_up.png`, `08_stand_look_left_down.png`, `11`–`14` то же в приседе, `09` и `15` прямой peek. Камера сзади за рюкзаком: винтовку в кадре не прочитать, доказательство yaw/pitch — строки смоука.

Торс (`Smoke-TorsoDriftSmoke.log`):

```
Smoke: climb high_mantle at V(X=513.00, Y=-17655.00, Z=10.00), 140 cm, walk V(Y=1.00)
Smoke ok  : mesh versus velocity 2.1 deg after 0.5 s (gate 10)
Smoke ok  : he actually walked (202 cm)
Smoke RESULT: PASS
```

До этого прямой коридор не требовался: путь сгибался, скорость уходила на 19.1° при нулевом лишнем yaw меша. Гейт 10° не ослаблял. Сектора строя в этом смоуке выключены.

Скан (`Smoke-FormationScanPitchSmoke.log`):

```
Smoke ok  : open ground aim pitch span 41.6 deg (gate 20)
Smoke ok  : aim pitch toward the building 13.3 deg (gate +10)
Smoke RESULT: PASS
```

Открытый размах больше 24°, потому что несколько бойцов и интерполяция. Юнит `Formation.ScanPitch` держит саму амплитуду и верхнее смещение. Повторно смоук не гонял: после зелёного прогона путь скана не менялся.

Юнит-тесты, консоль `test.ps1`:

```
Success  CodexTactics.Formation.Tuning.DefaultsMirrorFile
Success  CodexTactics.Formation.Tuning.ParseClampAndUnknownKeys
Success  CodexTactics.Formation.ScanPitch
Success  CodexTactics.Tactics.Cover.CornerStandOffPerClipSide
Success  CodexTactics.Tactics.Cover.LookAo
Success  CodexTactics.Tactics.Torso.Reset
6 tests, 0 failed. Report: C:\Work\Lanes\CodexTactics-grok\Saved\TestReport
```

## FormationWallHugSmoke

Озеро `/Game/Maps/L_Demo01_A_FrozenLake`, код `bab67531`. `powershell -File Scripts/smoke_parallel.ps1 -Commands FormationWallHugSmoke -Jobs 1 -NoRetry -Remote off -ExtraArgs "-Multiprocess"`, exit 0.

```
[smoke_parallel] 1 smokes, 1 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  FormationWallHugSmoke      PASS      88,8s
[smoke_parallel] 1 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 92 s, sum of checks 89 s
```

`Saved/Logs/Smoke-FormationWallHugSmoke.log`, разведка и бой в реальном времени, западная стена, проход 3.6 м и узкий проход 2 м. Все трое в приседе лицом наружу, никто за стеной, ближайшая пара 100 см:

```
Smoke ok  : plain wall (the building's west face), exploration: each man tight to the face - body centre <= the solo cover stand-off 45 + 10 cm (worst 45 cm off, nearest 45)
Smoke ok  : compound passage (east face, 3.6 m), exploration: each man tight to the face - body centre <= the solo cover stand-off 45 + 10 cm (worst 46 cm off, nearest 45)
Smoke ok  : narrow passage (south face, 2 m), exploration: each man tight to the face - body centre <= the solo cover stand-off 45 + 10 cm (worst 46 cm off, nearest 45)
Smoke ok  : plain wall (the building's west face), real-time fight: each man tight to the face - body centre <= the solo cover stand-off 45 + 10 cm (worst 45 cm off, nearest 45)
Smoke ok  : compound passage (east face, 3.6 m), real-time fight: each man tight to the face - body centre <= the solo cover stand-off 45 + 10 cm (worst 45 cm off, nearest 45)
Smoke ok  : narrow passage (south face, 2 m), real-time fight: each man tight to the face - body centre <= the solo cover stand-off 45 + 10 cm (worst 45 cm off, nearest 45)
Smoke ok  : over all scenes: the worst gap off the face +1 cm vs the solo cover stand-off
Smoke RESULT: PASS
```

## MantleSmoke

Не запускался. `Content/Maps/Generated/L_Gen_Depot_1.umap` в этом воркдереве нет (`DEPOT_NO`). Имя в `archivedMapSmokes` (`Scripts/test_map.json`). `smoke_parallel.ps1` на такое имя выходит 1 без `-ForceMap` (строки 79–86). `-ForceMap` не ставил и карту не подменял.

## Не сделано

- Не пушил и не мержил.
- MantleSmoke не шёл: карты нет, имя архивное.
- Кадр скана строя не снимал. Смоук шёл с `-nullrhi`. Числа 41.6° и 13.3° — доказательство гейта. Повторный рендер не запускал: код скана после PASS не менялся.
- ABP не менял: пины уже стояли, сохранять было нечего.
- `rendered:true` в `test_map.json` не коммитил. Kit umap скопирован, чтобы CoverIndoor нашёл карту, и не закоммичен.
- Паки Content и `Config/DefaultScalability.ini` не коммитил.
- G026 не трогал.
- Конусный юнит `PeekLookCone` в этом прогоне фильтра не повторял: шаг принят на `e934a6d0`.
