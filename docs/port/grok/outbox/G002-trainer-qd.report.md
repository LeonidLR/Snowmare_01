# G002 отчёт — метрики поведения и архив MAP-Elites

Ветка `grok/G002-trainer-qd`. После правок ревью перебазирована на main `c169137e` (TACTICAL_MAP P2+P3 уже в main). C++ и `Content/` не трогал. Ночной pmc-v5 на второй машине не трогал. В main не вливал и не пушил.

Роутер (имена моделей Claude в выводе игнорировал; обе сессии — grok-4.7, отдельной дешёвой сессии не было):

| часть | level | model | result |
|---|---|---|---|
| план | 4 | grok-4.7 | pass |
| разбор `behaviour` в адаптере | 2 | grok-4.7 | pass |
| MAP-Elites | 3 | grok-4.7 | pass |
| режим qd, лига, утренний отчёт | 3 | grok-4.7 | pass |
| форма ToJson | 2 | grok-4.7 | pass |
| секция отчёта | 2 | grok-4.7 | pass |
| правки ревью (снимок архива, NaN, сетка, default ga, элитизм) | 3 | grok-4.7 | pass |

## Claim

Тренер читает объект `behaviour` раунда (11 метрик §4 плюс `moves`). Нет поля, или значение не конечное (NaN / ±Infinity) — нуль, старый результат арены жив. Скаляр фитнеса тот же. `search.mode` по умолчанию `ga` (скалярный турнир). `qd` — переключатель в конфиге: MAP-Elites, родители из заполненных клеток, в начале популяции верхние `evolution.elitism` гены без мутации. Соперники в `qd` — один набор из снимка архива на входе в поколение (пока снимок пуст — текущий чемпион) и бот плейтест-отряда. Утренний `report.html` пишет занятые клетки, лучшего, значения поведения и подпись клетки. Форма `PmcArenaBehaviourRules::ToJson` (`schema`, `side`, `tactical_map`, `metrics.*.value`) читается: объект раунда побеждает объект стороны.

## Evidence

Хеши после `git rebase` на `c169137e`. Старые хеши до перебазирования больше не указывают на эти коммиты.

| утверждение | коммит | доказательство |
|---|---|---|
| План до кода | `4dc0e553` | `docs/port/grok/outbox/G002-trainer-qd.plan.md` |
| Каталог и нули, если поля нет | `eb6d104e` | `adapter.py` `BEHAVIOUR_METRICS`, `parse_behaviour`. Схема `docs/port/grok/outbox/G002-arena-behaviour-schema.md` |
| Архив: вставка, замена только если скаляр строго выше, выбор родителей | `a1672339` | `nighttrainer/qd.py` `Archive.consider`, `next_population` |
| Режим, лига, секция отчёта, мок пишет `behaviour` | `1e195c86` | `trainer.py` `search_mode` / `_league_plan` / `_update_archive`; `report.py` `_archive_section` |
| Тесты архива, нулей, ga против qd | `1e18d54b` | `Scripts/Trainer/tests/test_qd.py` |
| Форма арены P3 | `576e90bf` | `test_cpp_tojson_shape`: `side`, `tactical_map`, 11 метрик, `value` 42 / 3.3 / 1, `overwatch_kills` 0 |
| Правки ревью | `a820d186` | снимок `gen_NNNN/qd_archive.in.json`, конечные метрики, чужая сетка сбрасывается, default `ga`, элитизм, конфиг без BOM |

Адаптер берёт объект раунда, иначе сторону кандидата. Лишние `side` и `tactical_map` не метрики.

## Правки ревью (`a820d186`)

1. Снимок на первом входе в поколение: `Trainer._snapshot_archive` пишет `gen_NNNN/qd_archive.in.json` один раз. `_league_plan` читает снимок, не живой `qd_archive.json`. Повторный вход файл не перезаписывает. Живой архив по-прежнему обновляется в конце поколения, до `generation_done`.
2. `_metric_value` возвращает None, если `math.isfinite` ложно. В поведение это приходит как 0.
3. `Archive.load`: файл не объект, или оси / число корзин / диапазон не совпали с конфигом — пустой архив и строка `[qd] ... starting an empty archive`.
4. Один default: `qd.search_mode_of`. Его зовут `Trainer.search_mode` и `report._archive_section`. Докстринг `night_trainer.py` говорит то же. В `trainer_config.json` `"mode": "ga"`. `qd` остаётся переключателем.
5. `qd.next_population` ставит впереди лучшие `evolution.elitism` гены (по умолчанию 2) без мутации, остальное — мутированные дети.
6. `genome.write_json` уже атомный (временный файл + `os.replace`). Снимок идёт через `Archive.save` → `write_json`. Новых записей мимо него нет.
7. Блоб `trainer_config.json` в индексе: первый байт `{` (123), BOM нет, перевод строки LF, как у main.
8. Ветка перебазирована на `c169137e`. Конфликтов не было: эти коммиты main `Scripts/Trainer` и `docs/port/grok` не трогали.

Тесты на это: `test_league_reads_the_entry_snapshot_not_the_live_archive`, `test_non_finite_metric_is_missing`, `test_load_drops_a_foreign_or_non_object_file`, `test_elitism_is_prepended_unmutated`, отсутствие снимка в ga-прогоне.

## Гейт правок

Юниты после перебазирования:

```
python -m unittest discover -s Scripts/Trainer/tests
Ran 75 tests in 16.625s
OK
```

`test_core.py:295` — прежний ResourceWarning на незакрытый `sample_arena_result.json`. Не из этой правки.

Мок ga, конфиг как в дереве (`search.mode` ga), exit 0:

```
python Scripts/Trainer/night_trainer.py --mock --run-id g002-fix-ga --max-generations 3 --parallel 2 --poll-s 0.01
[2026-11-17T21:16:32] run g002-fix-ga (mock): resume after generation -1, max 3, parallel 2
[2026-11-17T21:16:37] gen 0: best 5.599 (g0000_c00) mean 4.248 champion baseline (4.8 s)
[2026-11-17T21:16:42] gen 1: NEW CHAMPION g0001_c02 fresh 9.994 > baseline 9.948
[2026-11-17T21:16:42] gen 1: best 11.975 (g0001_c02) mean 10.665 champion g0001_c02 (5.0 s)
[2026-11-17T21:16:46] gen 2: NEW CHAMPION g0002_c06 fresh 5.769 > g0001_c02 5.573
[2026-11-17T21:16:46] gen 2: best 10.760 (g0002_c06) mean 8.425 champion g0002_c06 (4.8 s)
[2026-11-17T21:16:47] run g002-fix-ga finished (max generations) after generation 2
```

`qd_archive.json` и `gen_0000/qd_archive.in.json` нет. В `report.html` есть `MAP-Elites is off` и `Search mode: ga`.

Мок qd — тот же конфиг, в копии только `"mode": "qd"`. Exit 0. `mock.crash_rate` 0.03 дал четыре джоба с exit 3, повтор не спас, прогон дошёл до конца:

```
python Scripts/Trainer/night_trainer.py --mock --config <temp qd> --run-id g002-fix-qd --max-generations 3 --parallel 2 --poll-s 0.01
[2026-11-17T21:16:57] run g002-fix-qd (mock): resume after generation -1, max 3, parallel 2
[2026-11-17T21:17:02] gen 0: best 5.941 (g0000_c00) mean 4.538 champion baseline (5.4 s) archive 3/48
[2026-11-17T21:17:08] gen 1: best 12.263 (g0001_c00) mean 9.564 champion baseline (5.3 s) archive 4/48
[2026-11-17T21:17:12] gen 2: best 10.666 (g0002_c00) mean 7.598 champion baseline (4.7 s) archive 4/48
[2026-11-17T21:17:13] run g002-fix-qd finished (max generations) after generation 2
```

Снимки `Saved/Trainer/g002-fix-qd`:

| поколение | снимок filled | поколения клеток в снимке | соперники eval |
|---|---|---|---|
| 0 | 0 | — | champion, bot |
| 1 | 3 | 0 | archive, bot |
| 2 | 4 | 0, 1 | archive, bot |

Живой архив в конце: `filled` 4, `capacity` 48, схема `codex.trainer_qd_archive/1`. Поколение не играет против клеток, которые само же ещё только запишет. В отчёте есть `Cells filled`.

Typesafe по индексу правки (до этого абзаца отчёта):

```
python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok
[Jev:System1] Auditing staged (the commit) changes for agent 'grok' (7 files)...
 • Boundary Breach : PASS
 • Risk Rating     : 0.1 / 4 (Jev)
 7 files changed, 209 insertions(+), 24 deletions(-)
```

Exit 0.

Typesafe по этому отчёту (индекс, `--agent grok`): exit 0, Boundary Breach PASS, risk 0.0/4.

Первый гейт до ревью (71 OK, мок `g002-qd-gate`, архив 4/48) остаётся в истории ветки. Его хеши сгорели при перебазировании. Числа ниже — тот прогон, не этот.

```
python -m unittest discover -s Scripts/Trainer/tests
Ran 71 tests in 28.367s
OK
```

```
[2026-11-17T20:56:28] gen 0: best 5.877 (g0000_c00) mean 4.736 champion baseline (9.2 s) archive 4/48
[2026-11-17T20:56:36] gen 1: best 12.133 (g0001_c05) mean 9.958 champion baseline (8.2 s) archive 4/48
[2026-11-17T20:56:45] gen 2: best 13.583 (g0002_c00) mean 7.794 champion g0002_c00 (8.3 s) archive 4/48
```

## Что не сделано

- В main не вливал и не пушил. Это делает Claude.
- Реальную арену не запускал. P3 уже в main `c169137e`, ночной прогон всё равно остаётся на `ga`, пока в конфиге не включат `qd`.
- Имена тактик aggressive flanker / patient ambusher / suppress-and-push за три поколения мока не выпали. `highlight_for` на месте.
- Критик утра и сценарии карт в задачу не входили.
- `pmc_commander.json` не менял. Скаляр фитнеса не менял.
- `typesafe --audit-diff` смотрит индекс, не всю ветку против main. Код правки прогнан по индексу до коммита `a820d186`.
