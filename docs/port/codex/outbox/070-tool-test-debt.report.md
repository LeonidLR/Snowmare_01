# 070 · Исправление Python-тестов инструментов

- `Scripts/Tools/tests/test_chat_dispatcher.py:24` изолирует настройки Git временной копии от окружения запуска. Фикстура по-прежнему создаёт настоящий репозиторий и worktree. Очистка регистрируется до создания репозитория, stderr включён в диагностику сбоев (`:72`).
- `Scripts/Tools/task_board.py:439` и `:524` учитывают точную ссылку реестра на файл задачи как сильное совпадение. До исправления `008-throwables.md` давал пустой `title_ru`, потому что slug содержит только `throwables`. Совпадение по одному токену по-прежнему даёт раздел, а заголовок остаётся исходным. Оба случая проверяет `Scripts/Tools/tests/test_task_board.py:235`. Проверка полосы «Сейчас» соответствует правилу из `af37e9e2`, время простоя Codex проверяется в карточке задачи (`:246`).

## Проверки

- `python -m unittest discover -s Scripts/Tools/tests -p test_chat_dispatcher.py -v` с `GIT_CONFIG_COUNT=invalid`. До исправления `Ran 5 tests; FAILED (errors=5)`, все на `git init`, код 128. После исправления `Ran 5 tests; OK`. Обычный запуск до правки также давал 5/5, первопричину исторического сбоя отдельно не подтвердил.
- `python -m unittest discover -s Scripts/Tools/tests -p test_task_board.py -v`. До исправления `Ran 3 tests; FAILED (failures=1)`; сравнение показало пустой `title_ru`. После исправления `Ran 3 tests; OK`.
- `git diff --cached --check` и TypeSafe audit-diff — PASS.

Проверены два набора Python-тестов из задачи. Полный набор инструментов, UE-сборка и смоуки не запускались.
