# G002 отчёт — метрики поведения и архив MAP-Elites

Ветка `grok/G002-trainer-qd` от main `7705adf6`. Новее main (`04016715`) не перебазировал. C++ и `Content/` не трогал. Ночной pmc-v5 на второй машине не трогал.

Роутер (имена моделей Claude в выводе игнорировал; этот вызов — одна сессия grok-4.7, отдельной дешёвой сессии не было, переделок не было):

| часть | level | model | result |
|---|---|---|---|
| план | 4 | grok-4.7 | pass |
| разбор `behaviour` в адаптере | 2 | grok-4.7 | pass |
| MAP-Elites | 3 | grok-4.7 | pass |
| режим qd, лига, утренний отчёт | 3 | grok-4.7 | pass |
| форма ToJson с ветки `claude/tactical-p2` | 2 | grok-4.7 | pass |
| секция отчёта | 2 | grok-4.7 | pass |

## Claim

Тренер читает объект `behaviour` раунда (11 метрик §4 плюс `moves`). Нет поля — нули, старый результат арены жив. Скаляр фитнеса тот же. `search.mode`: `qd` (по умолчанию для нового прогона) или `ga` (старый турнир). В `qd` клетка архива хранит лучшие веса, родители следующей популяции берутся из заполненных клеток, соперники — один элитный набор из архива (пока архив пуст — текущий чемпион) и бот плейтест-отряда. Утренний `report.html` пишет занятые клетки, лучшего, значения поведения и подпись клетки. Форма, которую пишет `PmcArenaBehaviourRules::ToJson` на `claude/tactical-p2` (`schema`, `side`, `tactical_map`, `metrics.*.value`), уже читается: объект раунда побеждает объект стороны.

## Evidence

| утверждение | коммит | доказательство |
|---|---|---|
| План до кода | `03d7169e` | `docs/port/grok/outbox/G002-trainer-qd.plan.md` |
| Каталог и нули, если поля нет | `94995a9e` | `adapter.py` `BEHAVIOUR_METRICS`, `parse_behaviour`. Схема `docs/port/grok/outbox/G002-arena-behaviour-schema.md` |
| Архив: вставка, замена только если скаляр строго выше, выбор родителей | `b4b41a54` | `nighttrainer/qd.py` `Archive.consider`, `next_population` |
| Режим, лига, секция отчёта, мок пишет `behaviour` | `d7037574` | `trainer.py` `search_mode` / `_league_plan` / `_update_archive`; `trainer_config.json` `search.mode` = `qd`; `report.py` `_archive_section` |
| Тесты архива, нулей, ga против qd | `ec814fb3` | `Scripts/Trainer/tests/test_qd.py` |
| Форма арены P3 | `c7e1c701` | `test_cpp_tojson_shape`: `side`, `tactical_map`, 11 метрик, `value` 42 / 3.3 / 1, `overwatch_kills` 0 |

`claude/tactical-p2` `Source/CodexTactics/Private/AI/PmcArenaBehaviourRules.cpp` `ToJson` кладёт те же 11 ключей и пишет `rounds[].behaviour` = сторона A, плюс `sides.A.behaviour` / `sides.B.behaviour` (`PmcArenaCommand.cpp` около строк 2272 и 2277). Адаптер берёт объект раунда, иначе сторону кандидата (`adapter.py`, комментарий G002 в `parse_doc`). Лишние `side` и `tactical_map` не метрики. Менять адаптер не пришлось.

Юниты, эта сессия:

```
python -m unittest discover -s Scripts/Trainer/tests
Ran 71 tests in 28.367s
OK
```

Три поколения мока из `trainer_config.json` (`search.mode` qd, `mock.crash_rate` 0.03, клипы включены):

```
python Scripts/Trainer/night_trainer.py --mock --max-generations 3 --run-id g002-qd-gate
[2026-11-17T20:56:19] run g002-qd-gate (mock): resume after generation -1, max 3, parallel 2
[2026-11-17T20:56:28] gen 0: best 5.877 (g0000_c00) mean 4.736 champion baseline (9.2 s) archive 4/48
[2026-11-17T20:56:36] gen 1: best 12.133 (g0001_c05) mean 9.958 champion baseline (8.2 s) archive 4/48
[2026-11-17T20:56:37]   job g0002_c01_eval_archive_0 attempt 1/2 failed: RuntimeError: exit code 3, no result JSON
[2026-11-17T20:56:38]   job g0002_c01_eval_archive_0 attempt 2/2 failed: RuntimeError: exit code 3, no result JSON
[2026-11-17T20:56:44] gen 2: NEW CHAMPION g0002_c00 fresh 6.042 > baseline 6.037
[2026-11-17T20:56:45] gen 2: best 13.583 (g0002_c00) mean 7.794 champion g0002_c00 (8.3 s) archive 4/48
[2026-11-17T20:56:46] run g002-qd-gate finished (max generations) after generation 2
```

Exit 0. Поколение 1 уже играет `archive` (джоб `g0002_c01_eval_archive_0`). Exit 3 — заложенный `crash_rate` 0.03 мока, повтор не спас, прогон дошёл до конца. Клипы четырёх хайлайтов: `ok`.

`Saved/Trainer/g002-qd-gate/qd_archive.json`: `filled` 4, `capacity` 48. Клетки (агрессия, подвижность, терпение):

| клетка | лучший | фитнес | подпись |
|---|---|---|---|
| [1, 1, 1] | g0002_c00 | 13.583 | mixed: mid aggression 68.57, mid mobility 5.33, mid patience 1.00 |
| [1, 2, 1] | g0001_c05 | 12.133 | mixed: mid aggression 79.24, mid mobility 6.03, mid patience 1.17 |
| [2, 1, 1] | g0001_c02 | 12.132 | mixed: mid aggression 93.25, mid mobility 5.65, mid patience 1.17 |
| [2, 2, 1] | g0002_c06 | 8.693 | mixed: mid aggression 99.22, mid mobility 6.21, mid patience 1.00 |

`report.html` строка секции: `Cells filled: <b>4</b> / 48.` Плюс таблица Best / Fitness / Highlight / Behaviour.

`git diff --stat 7705adf6` — только `Scripts/Trainer/**` и `docs/port/grok/outbox/G002-*`.

## Что не сделано

- Реальную арену не гонял. Блок пишет ветка `claude/tactical-p2`, её в этот worktree не вливал. Пока в результате нет `behaviour`, метрики 0 и такой раунд садится в нулевую клетку.
- Имена тактик aggressive flanker / patient ambusher / suppress-and-push в этом моке не выпали: три поколения заполнили четыре средние клетки, подпись `mixed`. Функция подписи есть (`qd.py` `highlight_for`); крайние корзины мок за три поколения не достал.
- Критик утра (неделя A6) и сценарии карт (A4) в задачу не входили.
- `pmc_commander.json` не менял. Скаляр фитнеса не менял.
- `typesafe_triage.py --audit-diff` смотрит только индекс или незакоммиченное, не всю ветку против main. Обзора уже закоммиченных `Scripts/Trainer/**` он не делает.

## Гейт

Юниты: 71 OK, вывод выше. Мок 3 поколения: exit 0, архив 4/48, отчёт с секцией, вывод выше.

Typesafe по поставленному отчёту (до абзаца с этим выводом; путь тот же):

```
python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok

[Jev:System1] Auditing staged (the commit) changes for agent 'grok' (1 files)...
 • Agent Audited   : GROK — staged (the commit)
 • Boundary Breach : PASS (code check of protected paths)
 • Risk Rating     : 0.0 / 4 (Jev)
 docs/port/grok/outbox/G002-trainer-qd.report.md | 80 +++++++++++++++++++++++++
 1 file changed, 80 insertions(+)
```

Exit 0. Защищённые пути grok не задеты.
