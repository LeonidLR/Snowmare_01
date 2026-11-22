# 089 report - Say exactly what each fighter decided in the designer's two clips (smart enemies T-01) - REWORK FOLLOW-UP (a-h)

Branch `gemini/089-pmc-round-trace` (base `origin/main` newest). Python standard library only, re-using `nighttrainer/track_stats.py`, no C++, no Content/Data modified or committed.

## Handover (read first)

- **Статус**: REWORK FOLLOW-UP завершён. Все замечания ревью от 2026-11-22 (пункты a-h) полностью устранены, проверены на реальных клипах и покрыты unit-тестами.
- **Выполненные исправления**:
  1. **Пункт a (`defence.open_idle_s` и коллизия имен)**:
     - В `compute_trace_metrics` массив `open_idle_s` индексируется по `fighter_index` и кортежу `(side, id)`. Бойцы атакующей стороны с одинаковыми именами (например, Roux / Moreau / Lind на стороне B и стороне A) больше не перезаписывают показатели защитников.
     - Аналогично спавны и посты в `parse_log_for_round` индексируются по `(team, name)` и `(team, fid)`, а поиск постов защитников учитывает команду бойца.
     - В секции 4 таблицы маркдауна добавлен столбец `Source` (`arena.defence.open_idle_s` или `track states`).
  2. **Пункт b (`evaluate_h2` при отсутствии лога)**:
     - Если лог отсутствует (`not log_data.get('found')` или `not log_data.get('round_found')`), вердикт H2 возвращает `"CANNOT TELL"` ("No execution log available; defender tactical post assignments are unknown."), а не ошибочный `"DOES NOT HOLD"`.
  3. **Пункт c (`evaluate_h4` и удаление инвертированного `attention_off < 0.9`)**:
     - Ветвь `attention_off < 0.9` полностью удалена.
     - Доказательством H4 теперь служат строго строки `holds his fire` (прямой вызов `ShouldHoldFire`) и задержка первого выстрела защитников относительно момента контакта (`gap_s > 3.0s`).
  4. **Пункт d (Метки времени в Section 3)**:
     - Для событий стрельбы лога относительное время `t` рассчитывается от заголовка старта раунда `[PmcArena] round N:`. Если временных меток в логе нет, в хронологии явно выводится `"t n/a"`, исключая подмену на время контакта `first_contact_s`.
  5. **Пункт e (Оружие бойцов в Section 2)**:
     - Устранена генерация фиктивного оружия `'m16'`. Оружие извлекается из группы `look` строки спавна в логе либо из поля `weapon` в треке; при отсутствии данных выводится `"n/a"`.
  6. **Пункт f (`evaluate_h1` порог Objective)**:
     - Одиночный приказ Objective больше не приводит к вердикту `HOLDS`. Оценка базируется строго на доле приказов (`obj_order_ratio > 0.2` / 20%) либо наличии приказов Objective до первого контакта (`pre_contact_obj_orders`).
  7. **Пункт g (Прямая поддержка директорий `.arena` и `track.json`, таблица 4 клипов)**:
     - `pmc_round_trace.py` теперь принимает напрямую путь к `.arena` директории или к файлу `round_*.track.json`.
     - При отсутствии поля `role` в `sides` роли стороны защитника и атакующего автоматически определяются по `track.objective.attacker` или `rnd["attacker"]` (если attacker B, то def A / atk B; если attacker A, то def B / atk A).
     - Все 4 реальные папки из `Saved/Reports/worker-2026-11-19/` успешно обработаны, результаты сведены в таблицу ниже.
  8. **Пункт h (Фикстуры и строгие ассерты)**:
     - В `trace_fixture_round_1.arena.json` внедрена C++ структура `open_idle_s` (`fighter_index`, `side`, `id`, `total_s`, `before_contact_s`).
     - Имя `Operator Lind` продублировано на атакующей стороне A (`total_s: 99.0`) и обороняющейся стороне B (`total_s: 12.4`). Тест строго утверждает, что защитник получает свои 12.4s, а не 99.0s атакующего.
     - В `trace_fixture_round_1.log` добавлена строка поста с `range 3200 cm`, тест утверждает `range_cm == 3200.0`.
     - Реализован и протестирован отказ на `--round 0` без явного флага `--zero-based`.
- **Что НЕ сделано и почему**:
  - Никаких нерешённых вопросов по 089 не осталось. Все замечания закрыты на 100%.

---

## Сводная таблица реальных клипов (Saved/Reports/worker-2026-11-19)

| Clip | Round | Scenario | Def / Atk | Winner | Duration | First Contact | H1 (Ground Obj) | H2 (Posts) | H3 (Cover) | H4 (Hold Fire) |
|---|---|---|---|---|---|---|---|---|---|---|
| `g0036_c04_fresh_archive_0.arena` | r2 (round 3) | tactical | Def A / Atk B | Side A (elimination) | 71.4 s | 17.3 s | DOES NOT HOLD (5.6% <= 20%) | CANNOT TELL (no log) | HOLDS (52.2s in open) | HOLDS (delay 10.9s) |
| `g0022_c01_eval_archive_0.arena` | r0 (round 1) | tactical | Def A / Atk B | Side A (elimination) | 136.9 s | 19.2 s | DOES NOT HOLD (1.5% <= 20%) | CANNOT TELL (no log) | HOLDS (33.6s in open) | DOES NOT HOLD (prompt 0.0s) |
| `g0002_c06_eval_archive_0.arena` | r2 (round 3) | tactical | Def A / Atk B | Side A (elimination) | 189.4 s | 5.9 s | DOES NOT HOLD (3.3% <= 20%) | CANNOT TELL (no log) | HOLDS (18.0s in open) | HOLDS (delay 35.9s) |
| `g0025_c01_eval_archive_0.arena` | r1 (round 2) | tactical | Def B / Atk A | Side B (elimination) | 240.0 s | 25.7 s | DOES NOT HOLD (4.3% <= 20%) | CANNOT TELL (no log) | HOLDS (74.2s in open) | DOES NOT HOLD (prompt 0.0s) |

---

## Таблица доказательств (Claim | Evidence)

| Утверждение | Доказательство (файл:строка / вывод / коммит) |
|---|---|
| Ключ `open_idle_s` по `fighter_index` и `(side, id)`, защита от перезаписи однофамильцев | `Scripts/Tools/pmc_round_trace.py:605-650`, тест `Scripts/Trainer/tests/test_round_trace.py:137-143` |
| Пост `range_cm` парсится и сохраняется в `posts_assigned` | `Scripts/Tools/pmc_round_trace.py:375-385`, тест `Scripts/Trainer/tests/test_round_trace.py:144-147` |
| `evaluate_h2` возвращает `CANNOT TELL` при отсутствии лога | `Scripts/Tools/pmc_round_trace.py:834-840`, запуск на клипах worker-2026-11-19 |
| `evaluate_h4` без `attention_off < 0.9`, только `holds his fire` и `gap > 3.0s` | `Scripts/Tools/pmc_round_trace.py:924-966`, тест `Scripts/Trainer/tests/test_round_trace.py:175-177` |
| Время выстрелов Section 3 относительно старта раунда либо `t n/a` | `Scripts/Tools/pmc_round_trace.py:300-315`, `420-435`, `1020-1040` |
| Оружие в Section 2 из спавна / трека либо `n/a`, без фиктивного `m16` | `Scripts/Tools/pmc_round_trace.py:340-350`, `995-1015`, вывод клипов |
| `evaluate_h1` использует порог 20% или приказы до контакта | `Scripts/Tools/pmc_round_trace.py:783-832`, тест `Scripts/Trainer/tests/test_round_trace.py:165-170` |
| Прямая поддержка `.arena` папок и `track.json` с авто-ролями по `attacker` | `Scripts/Tools/pmc_round_trace.py:458-490`, `1115-1175`, запуск на 4 клипах |
| C++ форма `open_idle_s` в фикстуре раунда 1 | `Scripts/Trainer/fixtures/trace_fixture_round_1.arena.json:37-46` |
| Запрет `--round 0` без `--zero-based` | `Scripts/Tools/pmc_round_trace.py:75-80`, тест `Scripts/Trainer/tests/test_round_trace.py:46-52`, `222-228` |
| Вывод источника стояния (`Source`) в секции 4 маркдауна | `Scripts/Tools/pmc_round_trace.py:1045-1060`, тест `Scripts/Trainer/tests/test_round_trace.py:192-195` |
| Все тесты набора `test_round_trace.py` проходят успешно | `Ran 6 tests in 0.359s - OK` |
| Все 91 тестов модуля Trainer проходят без регрессий | `Ran 91 tests in 19.230s - OK` |
| `git diff --check` чист | 0 whitespace ошибок |
