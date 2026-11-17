# G004 диагноз — цена замороженного трупа выше бюджета 0.5 ms

Ветка `grok/G004-corpse-cost`. Статус: диагноз, бюджет не взят. В main не вливал и не пушил. Бюджет 0.5 ms не поднимал. Дым `CorpseCostSmoke` не ослаблял. Вид смерти (ragdoll, оседание, заморозка) не менял.

Код `CorpseSubsystem` возвращён к main коммитом `a13406a1`. В дереве от main остаются план `eeabcbcc` и этот отчёт. Правки физики, снимка позы и повторной остановки тика измерены и сняты: ни одна не вывела абсолютное game-время 150 замороженных тел из полосы ~7.4–9.5 ms.

Замеры CorpseCost — локально, `-Jobs 1`, `-UseFixedTimeStep -FPS=60 -FixedSeed`, `CODEX_SMOKE_REMOTE=off`, карта `/Game/Maps/Generated/L_Gen_Depot_1`, offscreen 1280x720. Дельта дыма: `Frozen150.GameMs - Frozen50.GameMs`. Логи: `Saved/Logs/G004/`.

Роутер этого завершения (`python Scripts/Tools/model_router.py --classify "G004 wrap-up: revert corpse experiments and write the diagnosis report"`):

```
{"level": null, "model": null, "agent": null, "confidence": 0.0, "route": false, "reason": "short follow-up (context of the dialogue): main session", "followup": true}
```

Сессия осталась на grok-4.7.

| часть | level | model | result |
|---|---|---|---|
| план и базовые три прогона | 3 | grok-4.7 | pass |
| сброс физики после заморозки (`a53eaa35`) | 3 | grok-4.7 | redo — дельта не упала, код снят `a13406a1` |
| снимок позы ragdoll (`a2990cdf`) | 3 | grok-4.7 | redo — абсолют 150 в той же полосе, код снят |
| тихий повторный freeze (`daee6b0a`) | 3 | grok-4.7 | redo — mesh tick 0, дельта 1.62, код снят |
| отчёт-диагноз | 3 | grok-4.7 | pass |

## Claim

Сто лишних замороженных тел добавляют около 1.8 ms игрового потока на чистом main. Это повторяется в одиночном прогоне и под `-Jobs 4`. `UCorpseSubsystem::Tick` в это время занимает 0.03–0.11 ms. Счётчики CostTrace (физика, tick меша, pose) не содержат оставшуюся миллисекунду: после того как physics state и mesh tick стали 0, дельта дыма осталась 1.62 ms, а game 150 frozen — 8.34 ms.

## Evidence

### База, дерево плана `eeabcbcc` (код = main)

Три прогона подряд, один процесс.

```
[CorpseCost] no bodies    game 4.30   draws 144
[CorpseCost] 50 frozen    game 6.37   draws 339
[CorpseCost] 150 frozen   game 7.44   draws 582
[CorpseCost] 150 NOT frozen game 18.77
Smoke FAIL: 100 more frozen bodies (50 -> 150) add 1.07 ms
Smoke ok  : freezing saves 11.33 ms
Saved/Logs/G004/baseline-1.log
```

```
game 4.66 / 6.28 / 8.39 / 21.38   delta 2.12   saves 12.98   baseline-2.log
game 4.80 / 6.61 / 8.78 / 21.01   delta 2.17   saves 12.23   baseline-3.log
```

Средняя дельта трёх прогонов: (1.07 + 2.12 + 2.17) / 3 = 1.79 ms. Абсолют 150 frozen: 7.44, 8.39, 8.78. Заморозка экономит 11–13 ms против тикающих 150 (порог 3 ms проходит). Разброс выборки 50 тел около 2 ms, поэтому один знак дельты не доказательство.

Нагрузка `-Jobs 4`, тот же код, `load-jobs4.log`: game 5.87 / 6.82 / 8.63 / 25.13, дельта 1.81, saves 16.51. Та же полоса, что у одиночных прогонов.

### Сброс физики `a53eaa35` — эффекта нет, снят `a13406a1`

Три прогона: дельты 1.43, 1.00, 1.35. Абсолют 150: 9.27, 7.47, 8.64 (`fix-1.log`, `fix-2.log`, `fix-3.log`).

Контроль `Codex.Corpse.ReleaseFrozenPhysics 0` на той же сборке, `control-keep-physics.log`: game 5.92 / 7.52 / 9.00 / 21.64, дельта 1.47. Контроль и правка в одной полосе.

### Снимок позы `a2990cdf` — эффекта нет, снят

Только ragdoll (около 8 снимков на 150 тел). Три прогона:

```
pose-1.log  game 6.55 / 8.22 / 7.70 / 20.61   delta -0.51 PASS
pose-2.log  game 5.31 / 6.86 / 8.59 / 19.58   delta  1.73 FAIL
pose-3.log  game          7.09 / 8.23         delta  1.14 FAIL
```

PASS в pose-1 — горячая выборка 50 (game 8.22) при абсолюте 150 = 7.70, внутри базы 7.44–8.78. Средняя дельта трёх прогонов ( -0.51 + 1.73 + 1.14 ) / 3 = 0.79 ms, выше 0.5.

### VSM, только cvar, в код не входил

`r.Shadow.Virtual.Cache.DeformableMeshesInvalidate 0` подтверждён в логе (`= "0"`). Сборка снимка позы. `vsm-off.log`:

```
game 4.98 / 6.90 / 8.83 / 19.56   draws 144 / 343 / 589 / 587
Smoke FAIL: add 1.92 ms
```

Абсолют 150 = 8.83. Кэш теней деформируемых мешей эту дельту не держит.

### Showflag скелетов, только cvar

`showflag.SkeletalMeshes 0` подтверждён (`ShowFlag.SkeletalMeshes = "0"`). `showflag-skel-off.log`:

```
game 5.00 / 7.75 / 9.51 / 18.62   draws 136 / 247 / 383 / 365
Smoke FAIL: add 1.76 ms
```

Дровы на 150 упали с ~589 до 383. Game 150 вырос до 9.51. Сдача скелетных дровов эту миллисекунду не держит.

### CostTrace: куда деваются ~1.8 ms

Счётчик пишет только `UCorpseSubsystem::Tick` (settling, cap, fades) и число тел с физикой, tick меша и pose. Это не полный игровой поток.

На снимке позы, `trace-pose.log`, окно 150 frozen:

```
[CorpseCostTrace] tick 0.032 ms (settling 0.012, cap 0.002, fades 0.002); frozen 150, simulating 0, blend 0, physics state 11, mesh tick 11, pose 8
[CorpseCost] 50 frozen    game 8.19
[CorpseCost] 150 frozen   game 8.47
Smoke ok: add 0.28 ms
```

0.28 ms — снова горячие 50 (8.19) при абсолюте 150 = 8.47. Тик подсистемы ~0.03 ms. Одиннадцать мешей ещё тикали; их tick в счётчик подсистемы не входит.

После повторной остановки, `daee6b0a`, `quiet-1.log`. Во время обеих замороженных выборок: physics state 0, mesh tick 0, simulating 0, blend 0, pose 4 на 50 и pose 8 на 150. Тик подсистемы ~0.035 ms на 50 и ~0.10 ms на 150 (settling ~0.08). Одиннадцать тел один раз написали `FreezeCorpse declined`, затем затихли. Спама по кадру нет.

```
[CorpseCostTrace] tick 0.035 ms (settling 0.029); frozen 50, simulating 0, blend 0, physics state 0, mesh tick 0, pose 4
[CorpseCost] 50 frozen    game 6.72   draws 343
[CorpseCostTrace] tick 0.109 ms (settling 0.084); frozen 150, simulating 0, blend 0, physics state 0, mesh tick 0, pose 8
[CorpseCost] 150 frozen   game 8.34   draws 589
Smoke FAIL: add 1.62 ms
```

Итог CostTrace: из ~1.6–1.8 ms дельты подсистема объясняет ~0.07 ms (0.10 − 0.03). Остальные ~1.5–1.7 ms — игровой поток вне `UCorpseSubsystem::Tick`, при выключенном tick меша и при уничтоженной физике. На базе дровы растут 339 → 582 (~2.4 на тело), render в baseline-1 при этом 6.68 → 5.66. Устойчивый сигнал — game, и он вне счётчиков трупа.

Поток текстур (`r.TextureStreaming 0`, `stream-off.log`) на той же сборке: game 6.41 / 6.72 / 7.49 / 26.69, дельта 0.76, дровы по-прежнему 343 → 589. Абсолют 150 = 7.49 совпадает с базовым 7.44. В конфиг это не записывал.

### Что оставлено в ветке

`git checkout main --` двух файлов, коммит `a13406a16fc154925235fee6535e578b80b709db`:

- `Source/CodexTactics/Private/Characters/CorpseSubsystem.cpp`
- `Source/CodexTactics/Public/Characters/CorpseSubsystem.h`

`git diff main` по этим путям пустой. Cvar-прогоны (VSM, showflag, streaming) в коммиты не входили.

## Следующие две гипотезы

Обе проверяются срезом CPU-trace игрового потока по окну 600 кадров «50 frozen» и окну «150 frozen». Скрипт trace подготовлен, прогон не снят. Пока таймер не назван, файлы вне `Corpse*` не трогать.

1. Обновление примитивов сцены на игровом потоке для лишних ~100 подвижных skeletal mesh (end-of-frame update, стриминг скелетного меша). Showflag убирает сдачу дровов и не трогает этот проход: дровы упали, game 150 остался 9.51 ms. CostTrace при mesh tick 0 по-прежнему видит дельту 1.62 ms.
2. Инкрементальный обход UObject / GC, которого нет в CostTrace. Память 50 → 150 в quiet-1 почти плоская (4123 → 4133 MB), визит объектов на кадр всё ещё может дать порядка 1 ms. Искомые таймеры: `IncrementalPurgeGarbage`, `CollectGarbage`. Если верхний таймер вне `Corpse*`, правка — только после ответа Claude (EnemyCharacter, HUD, камера, `Build.cs`, дым — закрыты).

## Что не сделано

- Бюджет 0.5 ms не взят. Трактовать pose-1 (−0.51) и trace-pose (0.28) как успех нельзя: абсолют 150 frozen остался в базе.
- Полный гейт на бинарнике, который проходит бюджет: CorpseCost x3, DeathRagdoll, Knockdown, MilitaryZombie, OptimizedCharacter, `typesafe_triage.py --audit-diff --agent grok`. Не запускался, потому что дельта не упала.
- CorpsePersist x3 PASS снят на сборке `a53eaa35` до отката. Это не повод оставлять откатанный код.
- Оконный экспорт Unreal Insights не сделан. Без него имя таймера вне подсистемы — гипотеза, не факт.
