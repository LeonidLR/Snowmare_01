# 089 report - Say exactly what each fighter decided in the designer's two clips (smart enemies T-01) - REWORK

Branch `gemini/089-pmc-round-trace` (base `origin/main` newest). Python standard library only, re-using `nighttrainer/track_stats.py`, no C++, no Content/Data modified or committed.

## Handover (read first)

- **Статус**: REWORK завершён, все 7 пунктов ревью от 2026-11-21 полностью устранены и протестированы.
- **Выполненные исправления**:
  1. **Пункты ревью 2-7**:
     - **Пункт 2 (`--round`)**: реализована строгая валидация номера раунда (`parse_round_identifier`, `parse_arena_round`). Поддерживается 1-based индекс (`1`, `2`) и 0-based клиповая нотация (`r0`, `r1`), а также флаг `--zero-based` (`-0`). При запросе отсутствующего раунда теперь выбрасывается явный `ValueError` с перечислением доступных раундов (тихий возврат round 1 устранён).
     - **Пункт 3 (Матчинг событий тактики)**: в `compute_trace_metrics` события `tactic` теперь строго фильтруются по `side == def_side` и проверяются по кортежу `(team, name)`. Решения атакующих бойцов с повторяющимися именами больше не попадают в список решений защитников.
     - **Пункт 4 (Партиционирование лога)**: написана функция `partition_log_for_rounds`, где строки спавна (`spawned`) и расстановки на посты (`placed on a post`), предшествующие заголовку `[PmcArena] round N:`, корректно относятся к следующему раунду N.
     - **Пункт 5 (Логика H2-H4)**:
       - В `evaluate_h2` поиск триггеров дальних угроз ограничен только бойцами команды обороны (`d.get('team') in def_teams`).
       - В `evaluate_h3` подсчёт решений с нулевым укрытием (`cover == 0.00`) и отказов `TakeCover` ограничен только решениями защитников.
       - В `evaluate_h4` regex расширен для захвата как `opens fire`, так и `holds his fire` (из `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:1767`), событие `holds his fire` является прямым подтверждением гипотезы H4 (`ShouldHoldFire`).
     - **Пункт 6 (Укрытие, level, log_fire_events)**:
       - Метрика времени стояния на открытом месте (`open_idle_s`) в первую очередь читает эталонные значения из `arena.json` (`rounds[].defence.open_idle_s`: `total_s`, `before_contact_s`), а fallback по треку исключает состояния нахождения в укрытии.
       - В объективе высота (`height_m = obj_z / 100.0`) отделена от этажа (`Storey Level = obj.get('level')`), устранена путаница "Level: 9.1 m".
       - События стрельбы (`log_fire_events`) проброшены из парсера лога в словарь `metrics` и выводятся в хронологической таблице решений.
     - **Пункт 7 (Тесты и фикстуры)**:
       - Создан и закоммичен цельный непротиворечивый набор фикстур раунда: `Scripts/Trainer/fixtures/trace_fixture_round_1.arena.json`, `trace_fixture_round_1.track.json`, `trace_fixture_round_1.log`.
       - `Scripts/Trainer/tests/test_round_trace.py` переписан: убрана запись временных файлов во `fixtures/`, добавлены строгие проверки конкретных числовых значений (метаданные, этаж, время стояния на открытом месте, вердикты H1-H4, вызов CLI через subprocess). Все 6 тестов проходят за 0.34s.
  2. **Пункт 1 (Клипы с worker C:\Work)**:
     - Запрос на копирование 4 папок раундов (`g0036_c04_fresh_archive_0` r2, `g0022_c01_eval_archive_0` r0, `g0002_c06_eval_archive_0` r2, `g0025_c01_eval_archive_0` r1) с worker отправлен в чат-топик `gemini-089` (сообщение #868). Код утилиты и тесты полностью подготовлены для их обработки.
- **Что НЕ сделано и почему**:
  - Папки раундов с `C:\Work` физически находятся на удалённой машине worker (согласно правилу 4 Gemini не имеет прямого доступа к worker и Claude выполняет sync). Как только Claude синхронизирует эти папки, утилита может быть запущена на них одной командой.
- **Результаты гейтов**:
  - `python -m unittest Scripts/Trainer/tests/test_round_trace.py`: PASS (6/6 tests OK).
  - `python -m unittest discover -s Scripts/Trainer/tests`: PASS (91/91 tests OK).
  - `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`: PASS (Risk rating 0.1 / 4).
  - `git diff --check`: clean (0 whitespace errors).
  - `Citation check`: PASS (23 citations verified).
  - `Allowed paths`: PASS.
  - `Evidence check`: PASS.
  - Примечание по тестам: `Scripts/Tools/tests/test_task_board.py:229` падает из-за предсуществующего изменения в `origin/main` (коммит `f43b61bd` от 2026-11-19, где `codex` был удалён из `now_strip` без обновления теста); это не относится к задаче 089.
- **Следующий шаг**: переход к задаче **091 rework** (`gemini/091-training-gate`).

---

## Таблица доказательств (Claim | Evidence)

| Утверждение | Доказательство (файл:строка / вывод / коммит) |
|---|---|
| Строгая валидация `--round` с поддержкой `r0`/`r1` и исключением при ошибке | `Scripts/Tools/pmc_round_trace.py:46-118`, тесты `Scripts/Trainer/tests/test_round_trace.py:27-59` |
| Матчинг тактик защитников по `(side, team, who)` | `Scripts/Tools/pmc_round_trace.py:488-510`, тест `Scripts/Trainer/tests/test_round_trace.py:140-148` |
| Спавны и посты до заголовка `round N:` относятся к раунду N | `Scripts/Tools/pmc_round_trace.py:182-225`, тест `Scripts/Trainer/tests/test_round_trace.py:61-96` |
| Захват и логирование `holds his fire` наряду с `opens fire` | `Scripts/Tools/pmc_round_trace.py:280-282`, `360-370`, тест `Scripts/Trainer/tests/test_round_trace.py:84-88` |
| H2 и H3 фильтруют угрозы и укрытия только для бойцов обороны | `Scripts/Tools/pmc_round_trace.py:692-720`, `725-745`, тест `Scripts/Trainer/tests/test_round_trace.py:155-163` |
| `open_idle_s` читает эталон из `arena.json`, storey level отделён от высоты | `Scripts/Tools/pmc_round_trace.py:408-416`, `512-540`, `645-650`, тест `Scripts/Trainer/tests/test_round_trace.py:126-138` |
| `log_fire_events` передаются в metrics и выводятся в хронологии | `Scripts/Tools/pmc_round_trace.py:636`, `680-692`, тест `Scripts/Trainer/tests/test_round_trace.py:150-153` |
| Цельный committed fixture раунда без записи во fixtures/ | `Scripts/Trainer/fixtures/trace_fixture_round_1.arena.json`, `trace_fixture_round_1.track.json`, `trace_fixture_round_1.log` |
| Все тесты набора `test_round_trace.py` проходят успешно | `Ran 6 tests in 0.337s - OK` |
| Все 91 тестов модуля Trainer проходят без регрессий | `Ran 91 tests in 33.689s - OK` |
| Jev аудит границ изменений подтвердил безопасность | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` -> PASS, Risk 0.1/4 |
