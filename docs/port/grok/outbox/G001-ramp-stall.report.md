# G001 отчёт — LevelGenRampSmoke, стоп на STAIR_in_8

Ветка `grok/G001-ramp-stall`. План `b8a1afb5` (Claude одобрил, FileGapCm 170 не снижать). Трасса `8fd305a9`. Фикс `f739aaea`. Цели и таймауты смоука не менялись. `RampFileRules.cpp` / `.h` не трогались. Пуш и main не трогались.

## Роутер

- план: level 4 | grok-4.7 (основная сессия; confidence 0.49, route false) | диагноз в `b8a1afb5`, поведение не менялось
- трасса: level 3 | grok-4.7 (confidence 0.83, route true; печатное имя модели Claude проигнорировано) | поля добавлены, полный смоук с `trace` воспроизвёл тот же FAIL
- фикс: level 2 | grok-4.7 (основная сессия; confidence 0.56, route false) | два правила в `UpdateRampFile`, смоуки после сборки зелёные
- отчёт: level 2 | grok-4.7 (confidence 0.69, route true; печатное имя модели Claude проигнорировано; модель посередине вызова не переключалась) | этот файл

## Что изменилось

`8fd305a9` — только лог в `Source/CodexTacticsDev/Private/Debug/LevelGenRampSmokeCommand.cpp`, за уже существующим аргументом `trace`. Раз в 0.5 с: follow (Idle/Waiting/Paused/Moving), hold, traffic, own или form, destZ, dz, dir, onramp. Потеря своего приказа (true, затем false) пишется сразу, не раз в 0.5 с. Трасса не снята.

`f739aaea` — `Source/CodexTactics/Private/Characters/SquadSubsystemRampFile.cpp` и член `FRampHoldState::bBlockDescent` в `Source/CodexTactics/Public/Characters/SquadSubsystem.h:924-925`.

1. После `FindWaitBehind` холд не ставится, если посчитанное «впереди» не положительное (`SquadSubsystemRampFile.cpp:485-488`). Сам `FindWaitBehind` (`RampFileRules.cpp:64-66`, вне Allowed paths) по-прежнему считает бок о бок «впереди». Положительные 1–5 см по-прежнему держат.
2. Кандидат, чья цель на этаж ниже (`Dest.Z - Feet < -LevelChangeCm`, 150), ждёт, пока другой кандидат идёт вверх (`Direction > 0`), стоит ногами в пределах `LevelChangeCm` от Z этой цели и в 2D не дальше `MouthCm` (260) от неё (`SquadSubsystemRampFile.cpp:497-529`). Это traffic hold без своего приказа и пауза со своим. Флаг `bBlockDescent` ставится после `SetRampHold` (тот обнуляет структуру) и не даёт отпустить холд по потолку 6 с и по паузе повтора (`:538-558`). Почему: «X is still climbing to the floor he would walk down to».

Нового числа нет, поэтому JSON не добавлен. В `Content/Data/Movement/` по-прежнему только `critical_wound.json`, `falls.json`, `mantle.json`. Взяты уже лежащие в `FRampFileConfig` `LevelChangeCm` 150 (`RampFileRules.h:45`) и `MouthCm` 260 (`:36`). `FileGapCm` остаётся 170 (`RampFileRules.h:30`).

Сборка после фикса: `Scripts/build.ps1`, Result Succeeded, 813.62 с, exit 0.

## Трасса (до фикса): что доказано и что нет

Полный `LevelGenRampSmoke` с аргументом `trace` (не `only=`). Лог `Saved/Logs/Smoke-LevelGenRampSmoke-trace.log`. `only=STAIR_in_8 legs=group trace` эту ногу не воспроизводит: движок вышел 0, `Smoke RESULT: PASS` за несколько секунд, выбранный «задний» — медик, потому что порядок людей уже сдвинут прошлыми ногами. Доказательство ниже — полный прогон, нога 8.

Клик ноги 8 (`trace.log:2055-2059`): Commander sprint `(-2879, 1400)`, Engineer `(-2781, 1229)`, Medic-Sapper `(-2818, 1587)`.

(a) Командир не на паузе весь стоп. С 0.5 с до 3.9 с он `follow Moving hold 0`, ноги у устья, скорость 20–293. Пауза ramp file начинается около 4.4 с и держится до FAIL на 17.0 с, потому что Engineer впереди на 59–155 см (меньше FileGap 170) и ещё считается идущим. Два коротких отпуска (10.5 с и 12.5 с) с подножия его не уводят. С 13.5 с до 17.0 с пауза непрерывная.

```
Smoke trace 0.5 s: | Commander* (-2369, 1497) feet 2 v 293 follow Moving hold 0 traffic 0 own destZ 320 dz 318 dir 1 onramp 0 std | ...
Smoke trace 3.9 s: | Commander* (-2202, 1452) feet 2 v 23 follow Moving hold 0 traffic 0 own destZ 320 dz 318 dir 1 onramp 0 std | ...
Smoke trace 4.4 s: | Commander* (-2198, 1453) feet 2 v 0 follow Paused hold 1 traffic 0 own destZ 320 dz 318 dir 1 onramp 0 std | Engineer (-2257, 1353) feet 53 v 85 follow Moving hold 0 ...
[RampFile] Commander waits (his walk paused): Engineer is 73 cm ahead of him up the ramp
[RampFile] Commander walks on after 5.1 s: the way is free
[RampFile] Commander waits (his walk paused): Engineer is 155 cm ahead of him up the ramp
Smoke trace 17.0 s: | Commander* (-2192, 1368) feet 8 v 0 follow Paused hold 1 traffic 0 own destZ 320 dz 312 dir 1 onramp 1 std | Engineer (-2386, 1347) feet 143 v 15 ... | Medic-Sapper (-2508, 1355) feet 228 v 22 follow Moving hold 0 traffic 0 form destZ 98 dz -131 dir 0 onramp 1 std
```

Бок о бок, из-за которого добавлен отказ при Ahead не положительном (`trace.log:2070`):

```
[RampFile] Engineer waits (his walk paused): Commander is -2 cm ahead of him up the ramp
```

Там же дальше `-1`, `-0`, `-3` см. Это не дистанция впереди.

(b) Свой приказ медика пропадает на 1.90 с, не из-за холда и не из-за traffic. На 1.4 с он уже на крыше (feet 302), свой приказ жив, цель Z 320, скорость 666, точка около `(-2652, 1346)`. На 1.90 с он у групповой точки `(-2818, 1587)` (сам около `(-2807, 1566)`), приказ уже формация, цель Z 93, dz −209, dir −1, hold 0, traffic 0.

```
Smoke trace 1.4 s: | ... | Medic-Sapper (-2652, 1346) feet 302 v 666 follow Moving hold 0 traffic 0 own destZ 320 dz 18 dir 0 onramp 0 std
Smoke trace ORDER LOST 1.90 s: Medic-Sapper follow Moving hold 0 traffic 0 form dest (-2264, 1617, 93) feet 302 dz -209 dir -1
Smoke trace 1.9 s: | Commander* (-2192, 1442) feet 3 v 20 follow Moving hold 0 ... | ... | Medic-Sapper (-2807, 1566) feet 302 v 70 follow Moving hold 0 traffic 0 form destZ 93 dz -209 dir -1 onramp 0 std
[Formation] Medic-Sapper yields to the leader's walk: at (-2750, 1474), his slot (-2278, 1618)
```

Дальше он идёт вниз по STAIR_in_8 навстречу инженеру. На FAIL (`trace.log:2144`) координаты те же, что у прогона без трассы и у Codex 035/037:

```
Smoke FAIL: STAIR_in_8 group (all three box-selected, sprint): STUCK after 17 s (top level z 300): | Commander (leader) at X=-2191.981 Y=1368.172 feet 8, speed 0, still 4.1 s, order own -> X=-2879.000 Y=1400.000, path complete, 4 points, ends 0 cm from the goal at z 310 | Engineer at X=-2388.436 Y=1346.603 feet 145, speed 15, still 10.0 s, order own -> X=-2780.683 Y=1229.314, path complete, 3 points, ends 0 cm from the goal at z 310 | Medic-Sapper at X=-2511.048 Y=1355.094 feet 231, speed 22, still 6.4 s, order none/formation -> X=-2114.033 Y=1541.158, path complete, 3 points, ends 0 cm from the goal at z 310
Smoke RESULT: FAIL
```

Прогон без трассы до фикса: `LevelGenRampSmoke FAIL 110,6`, `[smoke_parallel] 1 run, 1 failed`, wall 112 s. Ног 12, 11 ok, 1 FAIL. Squad этой лестницы 10.0 с, solo 8.6 с. `WARN B_hall_n_stair_0` — навмеш, нога не идётся.

Снятие приказа — не ramp file. `traffic` на кадре потери равен 0, так что гипотеза «traffic hold, затем StopOperative» для этого кадра не подтвердилась. Обычное снятие по прибытии — `HandleMoveFinished` (`OperativeCharacterMovement.cpp:606`, `bOwnMoveOrder = false` на `:620`). Формация на другой этаж зовёт `FollowTo` (`SquadSubsystemFormation.cpp:1371-1375`). `FollowTo` сам обнуляет свой приказ (`OperativeCharacterMovement.cpp:276`). Все три места вне Allowed paths. Фикс их не меняет и приказ не сохраняет: он отказывается вести человека вниз, пока на том полу ещё есть поднимающийся.

## После фикса

Тот же клик, лог `Saved/Logs/Smoke-LevelGenRampSmoke.log` (последний из трёх зелёных прогонов). Отрицательных «впереди» у инженера нет: после 14 см он отпускается, командир ждёт только при 6 см (положительное). Медик получает formation hold и через 1.1 с отпускается, когда командир уже пошёл вверх. Нога проходит за 5.9 с, все трое на feet 302.

```
[RampFile] Engineer waits (his walk paused): Commander is 14 cm ahead of him up the ramp
[RampFile] Engineer walks on after 0.2 s: the way is free
[RampFile] Commander waits (his walk paused): Engineer is 6 cm ahead of him up the ramp
[RampFile] Medic-Sapper waits (formation hold): Commander is still climbing to the floor he would walk down to
[RampFile] Medic-Sapper walks on after 1.1 s: the way is free
Smoke ok  : STAIR_in_8 group (all three box-selected, sprint): all on the top level in 5.9 s: | Commander (leader) at X=-2625.397 Y=1357.944 feet 302 (top), speed 192, still 0.0 s, order own -> X=-2879.000 Y=1400.000, path complete, 2 points, ends 0 cm from the goal at z 310 | Engineer at X=-2716.781 Y=1275.853 feet 302 (top), speed 49, still 0.0 s, order none/formation -> X=-2552.912 Y=1643.835, path complete, 2 points, ends 0 cm from the goal at z 310 | Medic-Sapper at X=-2729.511 Y=1455.940 feet 302 (top), speed 27, still 0.1 s, order none/formation -> X=-2722.909 Y=1495.754, path complete, 2 points, ends 0 cm from the goal at z 310
Smoke RESULT: PASS
```

Предел правила: 2D считается до цели спуска, не вдоль лестницы. Чужой поднимающийся на том же Z пола и в пределах MouthCm от этой цели тоже удержит спуск, пока сам не уйдёт с пола. Это самое узкое правило, которое кадр 1.90 с поддерживает: командир на полу у устья попадает в MouthCm 260 от слота медика, инженер у подножия был дальше и как триггер не нужен.

## Claim | Evidence

| Claim | Evidence |
|---|---|
| До фикса нога 8 детерминированно FAIL, пути complete и кончаются в 0 см от цели на z 310 | `Saved/Logs/Smoke-LevelGenRampSmoke-trace.log:2144` и `:2351` `Smoke RESULT: FAIL`; прогон без трассы `LevelGenRampSmoke FAIL 110,6`, 1 run, 1 failed |
| Командир не Paused весь стоп: Moving hold 0 до 3.9 с, Paused hold 1 с 4.4 с до 17.0 с, потому что Engineer ближе 170 см | `trace.log` кадры 0.5 / 3.9 / 4.4 / 17.0 с и строки `Engineer is 73 cm ahead`, `walks on after 5.1 s`, `Engineer is 155 cm ahead` |
| Бок о бок: Engineer на паузе при «Commander is -2 cm ahead» | `trace.log:2070` (и −1 / −0 / −3 см следом) |
| Медик теряет свой приказ на 1.90 с уже на крыше; цель формации Z 93, dz −209, dir −1, hold 0, traffic 0 | `trace.log`: кадр 1.4 с `own destZ 320`, `ORDER LOST 1.90 s: Medic-Sapper ... form dest (-2264, 1617, 93) feet 302 dz -209 dir -1` |
| Снятие приказа вне Allowed paths; фикс приказ не сохраняет | `OperativeCharacterMovement.cpp:276` (`FollowTo`), `:606` и `:620` (`HandleMoveFinished`), вызов на другой этаж `SquadSubsystemFormation.cpp:1371-1375` |
| Холд не ставится при Ahead не положительном; спуск ждёт, пока поднимающийся на том полу | `f739aaea`, `SquadSubsystemRampFile.cpp:485-488` и `:497-558`, `SquadSubsystem.h:924-925`; `FileGapCm` 170 в `RampFileRules.h:30` не менялся |
| Нового числа нет, JSON не добавлялся | взяты `LevelChangeCm` 150 и `MouthCm` 260, `RampFileRules.h:45` и `:36`; каталог `Content/Data/Movement/` без нового файла |
| После фикса STAIR_in_8 group проходит, медик держится строкой «still climbing» и отпускается | `Smoke-LevelGenRampSmoke.log:1871-1875`, `:1879`, `:1890` (5.9 с, feet 302), `:2004` `Smoke RESULT: PASS` |
| LevelGenRampSmoke 3/3 при Jobs 1, без PASS* | три вызова `smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Jobs 1 -Commands LevelGenRampSmoke`: `PASS 134,2` wall 139 s; `PASS 121,3` wall 123 s; `PASS 84,9` wall 87 s. В каждом `1 run, 0 failed, 0 passed only on the re-run` |
| FallDrop, RoofCutawayFocus, SquadControl, Cover | один батч Jobs 1, exit 0, wall 215 s: `FallDropSmoke PASS 73,4`, `RoofCutawayFocusSmoke PASS 43,2`, `SquadControlSmoke PASS 49,2`, `CoverSmoke PASS 43,6`, `4 run, 0 failed` |
| Юниты Squad и Formation | `test.ps1 -Filter CodexTactics.Squad`: `41 tests, 0 failed`. Затем `-Filter CodexTactics.Formation`: `213 tests, 0 failed`. Exit 0 |
| typesafe `--audit-diff --agent grok` | exit 0. Печать: `Auditing unstaged changes for agent 'grok' (0 files)` и `Clean workspace: No changes to audit.` Инструмент смотрит только незакоммиченное (`typesafe_triage.py:254-261`); к моменту запуска C++ уже в `f739aaea` |

Запятая в секундах смоука — локаль оболочки (`134,2` = 134.2 с).

## Что не сделано

- Свой приказ медика не сохраняется. Места снятия вне Allowed paths (строки в таблице). Ramp file только не пускает его вниз сквозь поднимающихся.
- Юнит-тест двух новых правил не написан. Они живут в `UpdateRampFile`, а чистые тесты `CodexTactics.Formation.RampFile.SingleFile` по-прежнему описывают `FindWaitBehind`, включая бок о бок. Этот файл правил менять нельзя. Доказательство правил — смоук выше. Существующие 213 тестов Formation и 41 Squad зелёные, включая `RampFile.*`.
- `only=STAIR_in_8 legs=group` не является доказательством: на нём стоп не воспроизводится.
- Потолок 6 с для обычного холда файла не менялся. Для спуска его обходит только `bBlockDescent`, и только пока условие «поднимающийся на том полу» живо.
- Незакоммиченные паки дизайнера и `Config/DefaultScalability.ini` в коммиты не входили.
