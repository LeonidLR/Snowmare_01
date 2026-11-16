# 075 - CoverSmoke: стенд-офф у торца стены (Claude, 2026-11-16)

Ветка `fix/075-cover-standoff` от main `4d069017` (main на момент создания ветки; независима от 074), worktree `CodexTactics-gemcli`. Исполнитель: Claude.

## Причина (доказана логами)

Временный пробник в `EnterCover` (результат свип-перемещения капсулы на слот: откуда, куда, чем заблокирован) + `-PreCmds "Codex.Debug.MoveTrace 1"`,
CoverSmoke x5 до правки (`-NoRetry -Jobs 1 -ExtraArgs -Multiprocess`): 3 PASS, 2 FAIL. Логи `Saved/Logs/claude075-diag{1..5}.log`.

FAIL (diag3, стенд-офф 72 см вместо 28):

```text
[MoveTrace] Engineer: path to (-380, -6261, z 92): result 2, ... length 91          <- его точка "corner watch" строя
[Cover] Commander: turns along the wall to the left (threat known)
[MoveTrace] Commander: path to (-391, -6259, z 10): result 2, 2 points, length 81    <- слот у угла, стенд-офф 28 см
[Cover] Commander: to the left corner (81 cm)
[MoveTrace] Commander: move completed: Success[Success], 81 cm from the goal, velocity 0
[075probe] Commander: snap from (-311.8, -6273.0) to slot (-391.3, -6258.9) -> (-348.7, -6266.5) blocked 1 by BP_Operative_C_1 / CollisionCylinder ... time 0.464
Smoke FAIL: ... standing the _R stand-off from the real wall end (72 cm, stand-off 28; ...)
```

PASS (diag1) - то же самое, но Engineer ещё в пути: `snap ... -> (-391.3, -6258.9) blocked 0`, 29 см.

Цепочка:
1. Строй (formation) ставит Engineer на точку «corner watch» у левого угла: `(-380, -6261)` - в 11 см от слота стенд-оффа лидера `(-391, -6259)`.
2. Лидер получает угрозу слева и автоматически идёт к углу (`TrySnapToCoverCorner`, сдвиг 81 см).
3. Прибытие (`EnterCover`, `OperativeCharacterCover.cpp`) ставит капсулу на слот **свип-перемещением** (`SetActorLocation(..., bSweep=true)`),
   меш доезжает «глайдом». Если Engineer уже стоит у угла, свип упирается в его капсулу (`time 0.464` / `0.175` / `0.017`) и лидер
   остаётся на 49 / 53 / 54 / 72 см от торца.
4. `CoverSlot` при этом уже «угловой» (край 28 см) - `ShouldSnapToCorner` считает, что он на месте, повторного подхода нет; Engineer
   уходит сразу после (строй перестраивается: `Engineer: path to (-226, -6293)`), но лидер так и стоит (Codex 019: 6+ с неподвижно).

То же самое - второй флейк CoverSmoke `unknown threat, moving to his right ... cvr_std_idle_L` (diag5): вход в укрытие у середины стены
`snap from (-44.7, -6321.3) to slot (-124.5, -6305.4) -> (-50.6, -6320.2) blocked 1 by BP_Operative_C_2` (Medic-Sapper), он стоит в
74 см от слота, поэтому следующий заказанный смоуком шиммі - 45 см вместо 120 и кончается раньше проверки клипа на 0.6 с.

Почему «зависит от кадров / нагрузки»: смоук идёт тикером, строй перестраивает точки по своему таймеру - успел ли напарник дойти до
своей точки к моменту прибытия лидера, решает порядок событий, а не геометрия. Шаг шиммі, радиус прибытия, nav-проекция и таймер не виноваты:
путь 81 см, `ends 0 cm from the goal`, слот тот же `(-391, -6259)` во всех прогонах.

Дополнительно найдено (не правил): для коротких приказов (< 120 см до конца пути) Detour crowd завершает движение «moved too far» сразу,
0 см пройдено (`move completed: Success, 81 cm from the goal, velocity 0`), если отталкивание от напарника развернуло направление агента -
тогда лидер не идёт, а «прыгает» на слот свипом за один кадр + глайд меша. Пробовал условие «нужен прогресс 20 см по пути»
(`UOperativeCrowdFollowingComponent`): лидер стал идти, но упирался в того же Engineer (49 / 53 см, 2 FAIL из 5), а у ведомых строя
появились сотни подавленных финишей (до 304 за прогон) - откатил, это вне задачи и задевает строй.

## Правка

- `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp:200-216` (`EnterCover`): если свип на слот остановил **напарник
  своей команды, который сам не в укрытии**, и недолёт > `pass_squadmate_min_gap_cm`, свип повторяется с игнором этого напарника
  (`IgnoreActorWhenMoving`); лог `[Cover] X: onto the slot past Y (the sweep stopped N cm short, now M cm off)`. Стены / препятствия,
  враги и напарник в укрытии (групповое укрытие плечом к плечу) по-прежнему останавливают свип - как раньше.
- Чистое правило `CoverArrivalRules::ShouldPassSquadmate` (`Source/CodexTactics/Public/Tactics/CoverArrivalRules.h`,
  `Private/Tactics/CoverArrivalRules.cpp`), константа в `Content/Data/Tactics/cover_arrival.json` (`pass_squadmate_min_gap_cm: 2`).
- Тесты `CodexTactics.Tactics.Cover.ArrivalPassesASquadmateOnTheSlot`, `...ArrivalTuningFileMirrorsDefaults`
  (`Source/CodexTacticsTests/Private/Tactics/CoverArrivalRulesTest.cpp`).
- Допуски смоука (28 / 15 см) не трогал, knownFail не ставил.

Коммиты: `9a3acaf0` (правка + тесты + JSON), следующий - этот отчёт.

## Проверки

| Проверка | Результат |
|---|---|
| `Scripts/build.ps1` | Succeeded |
| `test.ps1 -Filter CodexTactics.Characters` | 85 tests, 0 failed |
| `test.ps1 -Filter CodexTactics.Tactics` | 128 tests, 0 failed (в т. ч. 2 новых Arrival) |
| CoverSmoke x6 | 6/6 PASS, стенд-офф 29 см во всех; правило сработало в 4 из 6 (`past Engineer ... 35 cm short`, `past Medic-Sapper ... 56 / 57 / 64 cm short`, везде `now 0 cm off`); логи `Saved/Logs/claude075-gate{1..6}.log` |
| CornerHoldSmoke, CornerEntrySmoke, CoverCrouchSmoke, FireGateSmoke | PASS |
| GroupCoverSmoke (доп., напарники в укрытии рядом) | PASS |
| `typesafe_triage.py --audit-diff --agent claude` | PASS (0.0 / 4) |

## Решения для дизайнера

1. Принято в коде: слот укрытия принадлежит тому, кто в него входит; идущий мимо напарник не останавливает вход - капсулы на мгновение
   перекрываются, напарник сдвигается своим следующим шагом. Если такое «проскальзывание» сквозь напарника заметно глазу - скажите,
   альтернатива: лидер ждёт и доходит, когда напарник уйдёт (дольше и снова зависит от таймингов).
2. Корень конфликта - в строе (не трогал, вне моих путей): точка «corner watch» ведомого ставится на угол, к которому сам лидер идёт
   стенд-оффом. Предлагаю отдельную задачу владельцу строя: не ставить corner watch ближе ~70 см к слоту укрытия лидера (или к его
   ожидаемому угловому слоту).
3. Отдельная находка для движения: crowd «moved too far» завершает короткие приказы (< 120 см) без шага при отталкивании от напарника
   (см. выше). Не правил - задевает строй; отдельная задача с прогоном строевых смоуков.

## Не сделано

- Ничего не пушилось и не сливалось. Строй и crowd-следование не менялись (эксперимент откатан).
