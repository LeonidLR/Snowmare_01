# 060 — merge gate для Lead

Ветка `codex/060-merge-gate`. Gate объединяет ручные шаги из задания #971. Запуск ведёт Lead из основной копии после ревью веток. Пример предварительного просмотра:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tools/merge_gate.ps1 -Branches 'codex/062-indoor-cover-fixes,codex/063-r1-fixes' -DryRun
```

- `merge_gate.ps1:21`, `:125` проверяют GUI-процесс для точного пути своего `.uproject`. Процессы с `-game` или `-run=` пропускаются.
- `merge_gate.ps1:9`, `:75`, `:156` читают porcelain с NUL-разделителями, копируют каждый ` M` файл в `Saved/Backups/designer-wip-<timestamp>` и сверяют SHA-256 оригинала и копии. При ошибке или изменении источника gate останавливается.
- `merge_gate.ps1:165` отклоняет staged/unmerged состояние. `:179` выполняет последовательные `git merge --no-ff` по закреплённым коммитам. Конфликт останавливает gate на текущей ветке с сохранением состояния для Lead.
- `merge_gate.ps1:197` собирает при изменении `Source/` или `.uproject`. `:207` запускает `test.ps1 -Smart -SkipSmokes -Base <old HEAD>`, с `-FullTests` берёт весь набор CodexTactics.
- `merge_gate.ps1:29` распознаёт два известных X6 WIP-падения только при совпадении с соответствующим ` M` файлом. Отдельно выводит их имена. Падение с чистым файлом, обрыв журнала или неожиданный результат останавливают gate.
- `merge_gate.ps1:224` отправляет на слот 1 только смоуки из impact map для влитых путей, сверяет имена и статусы. `:252` выводит `ready to push` после успешных этапов. Gate не выполняет push.

## Проверка

- `python -B -m unittest discover -s Scripts/Tools/tests -p test_merge_gate.py -v` — 8 тестов PASS. Проверены точный путь UE, Git-статусы, SHA-256 и копирование файла с кириллицей и скобками, два известных X6 падения, неполный лог, критерии сборки. Три dry-run в настоящих временных Git-репозиториях сохранили HEAD и содержимое WIP и не создали backups.
- Общий Python suite — `Ran 150 tests; FAILED (failures=1, errors=5)`. Все 8 тестов gate прошли. Пять ошибок возникли в `test_chat_dispatcher.py` на `git init` с exit 128. Один FAIL в `test_task_board.py:229` на пустом `title_ru`. Эти файлы и инструменты не менялись.
- TypeSafe boundary audit и `git diff --check` — PASS.

## Чего не сделал

Реальный merge, UE-сборку, automation и worker smokes через этот gate не запускал. Так проверка 060 не трогала checkout дизайнера, реальные ветки для вливания и слот worker.
