# 038 — HANDOFF + TASK_BOARD

Готово на ветке codex/038-board-handoff от main b840758c (2026-11-17).

- TASK_BOARD.md: добавлен раздел «Сделано (ночь 2026-11-16 → 17 и день 11-17)», ровно14 строк для всех actual first-parent merge-коммитов диапазона4d069017..b840758c. Прямые commits не выдаются за merges; отчёты/проверки не выдаются за исправления gameplay.
- HANDOFF.md: заменён только верхний устаревший snapshot. Указаны ThreatFacingRules, FormationLeaderCornerRules, F-3, F-4a,037, открытые B17-1..4 (Claude), B17-5/080 и B17-6/081 (Gemini), очередь077→080→081→078→079 и три unresolved smoke у Claude. Исторические нижние срезы явно отделены от актуального.
- Остальная доска, включая B17-таблицу, побайтно сохранена: удаление только вставленного блока точно восстанавливает исходный файл. В HANDOFF замена нового префикса старым snapshot точно восстанавливает исходный файл. Все3 Markdown-файла UTF-8 без BOM.
- Валидация parent: точное совпадение множества14 merge-хешей и числа строк; отсутствие BOM, UTF-8 decode; побайтные проверки сохранности выше; git diff --check. Сборка/smoke не требуются для docs-only. Перед commit — staged TypeSafe audit.
- Router: level2/confidence0.52. Model: gpt-6.1-sol cheap sub-run. Attempt1 blocked: nested CLI filesystem tools require an installed Core app; no file changes. Attempt2 tool-free text drafting PASS; parent reviewed hashes/claims, applied byte-preserving insertion, уточнил порядок открытой очереди. No local model/local_task.py.
- Только3 разрешённых docs-пути изменены; без правок B17, engine/config/assets/main checkout, без push/merge в main.
