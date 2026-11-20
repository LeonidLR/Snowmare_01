# G011 — мёртвые счётчики арены (overwatch_kills и post_moves)

Статус: подсчёт в коде, локальная сборка зелёная, `CodexTactics.AI.Pmc` зелёный. Смоуки не запускались: их гоняет Claude на воркере.

Код: `8739ee15` (`wip(G011): count overwatch kills and defence-post walks`), поверх `origin/main` `42578b69`. Ветка `grok/G011-arena-dead-metrics`.

## Причина

Диагноз по уже записанному `PmcArenaTacticalSmoke` от 2026-11-19 (`Saved/Logs/Smoke-PmcArenaTacticalSmoke.log`, JSON `Saved/Arena/20261119-133629.json`). Свежий прогон на этом коммите не делался.

`post_moves` был 0 у обеих сторон (JSON, строки 742 и 767), хотя ходьба состоялась. В логе три принятых выхода на пост обороны, строка «walks to his … post» (ветка обороны, не «walks onto»):

- строка 1672: Operator Roux, team 1, `sector_shooter`
- строка 1712: Roux ещё раз
- строка 1732: Operator Vance, team 1, `sector_shooter`

Эта ветка делала `break` до `Team.Stats.PostMoves += bTaken ? 1 : 0` (старая строка `PmcSquadSubsystem.cpp:2306`). Сброс статистики тут ни при чём: `ResetTeam` в начале раунда, в JSON того же раунда живы orders, objective_moves, ambushes, retreats. Раунд закончился без убийств (`kills A 0 / B 0`, строка лога 1801), поэтому `overwatch kills A 0 / B 0` (строка 1798) для него честно.

Геометрическая ветка убийства с крыши не мёртвая: `Saved/Logs/G008b-after3-11.log:2141` даёт `overwatch kills A 1 / B 0`. Дыра счёта другая: смерть смотрела только `IsAtOverwatchSpot`. Выстрел с тактического поста Overwatch, если ноги не попали в пятно (сдвиг навмеша относительно точки файла), в счёт не входил.

Поведение ИИ не менялось. FallBack по-прежнему считается во время удержания.

## Правка

Чистое правило в `PmcArenaBehaviourRules`. Окно прежнее: возраст `<= 3` с, возраст 3.0 входит, 3.01 нет, отрицательная разница времени по-прежнему входит.

- `Source/CodexTactics/Public/AI/PmcArenaBehaviourRules.h:120` — `FPmcOverwatchKillNote` (сторона, вид поста, на посту, на пятне крыши, время попадания).
- `Source/CodexTactics/Public/AI/PmcArenaBehaviourRules.h:211` — `CountsAsOverwatchKill`. Сторона 0 или 1, окно 3 с, и либо пятно крыши, либо пост вида Overwatch и боец на нём. Window, Ambush, LadderRoof, None не считаются.
- `Source/CodexTactics/Public/AI/PmcArenaBehaviourRules.h:214` — `PostMoveIncrement`: 1, если шаг принят, иначе 0.
- `Source/CodexTactics/Private/AI/PmcArenaBehaviourRules.cpp:329` и `:347` — тела этих функций.
- `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:2262` — принятый шаг на пост обороны прибавляет `PostMoves` до `break`. Удержание на посту и отказ шага не прибавляют.
- `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:2308` — прежний шаг на тактический пост идёт через ту же функцию.
- `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp:365` — в заметку попадания добавлены вид поста и флаг «на посту».
- `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp:2017` — на выстреле пишутся `GetFighterPost` и `IsAtPost` (`PostArriveCm` 200, допуск этажа стороны).
- `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp:2067` — смерть зовёт `CountsAsOverwatchKill` и пишет строку `overwatch kill check`.
- `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp:3692` — отдельная строка `post moves A / B`. Старый формат строки behaviour не тронут.
- `Source/CodexTacticsTests/Private/AI/PmcArenaBehaviourRulesTest.cpp:142` — проверки правила внутри `CodexTactics.AI.PmcArena.BehaviourJson`. Старые проверки этого теста не менялись. Отдельного имени `KillAndPostMove` нет.

## Гейт

| шаг | результат |
|---|---|
| `Scripts/build.ps1` (локально) | Код 0. `Result: Succeeded`, компиляция 141.50 с. Перед ней ожидание дизайнера истекло (`levelgen build running`, `max_wait_minutes` 30), сборка пошла дальше. Лог `Saved/Logs/Build.log`. |
| `Scripts/test.ps1 -Filter CodexTactics.AI.Pmc` | Код 0. 70 тестов, 0 упало. То же ожидание дизайнера, затем прогон. `Success` у `CodexTactics.AI.PmcArena.BehaviourJson` (в нём новые проверки счёта). Отчёт `Saved/TestReport`, лог `Saved/Logs/AutomationTests.log`. |
| смоуки | Не запускались. Claude: `PmcArenaTacticalSmoke`, `TacticalPmcSmoke`, `PmcArenaSmoke`, `PmcArenaObjectiveSmoke`. Десять сидов и ненулевые `overwatch_kills` / `post_moves` в JSON тоже у Claude. |

Ошибок компиляции не было, повторной правки после сборки нет.
