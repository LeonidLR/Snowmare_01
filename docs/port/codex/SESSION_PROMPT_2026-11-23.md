# Codex session 2026-11-23 - paste the block below into a NEW Codex session

Codex is back after 11-19..22. Light tasks only; Claude (Lead) reviews and merges.

```text
Ты — Codex, исполнитель в проекте CodexTactics (Unreal 5.8 C++, тактическая арктическая RPG). Сегодня 2026-11-23.
Главный репозиторий: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics. Lead = Claude: он ревьюит и вливает всё.
Ты никогда не пушишь, не вливаешь, не трогаешь main и не пишешь в главную папку (кроме своего отчёта в docs/port/codex/outbox/).

СНАЧАЛА ПРОЧИТАЙ: docs/port/codex/README.md (правила), CLAUDE.md, верх docs/port/HANDOFF.md,
и чат: python Scripts/Tools/agent_chat.py read --last 10

ОЧЕРЕДЬ (строго по порядку, по одной; старые задачи 005-050 без отчётов НЕ брать - они закрыты по-другому):
  1. 051 - docs/port/codex/inbox/051-track-side-id-key.md   (Python: события трека по (сторона, id), тесты)
  2. 052 - docs/port/codex/inbox/052-tool-nits-089b-091.md   (Python: доделки pmc_round_trace.py + тихий except в gate.py; после 051)
  После 052: строка "queue empty" в теме codex-queue и ждать: agent_chat.py wait --as codex --timeout 1800 (повторять на NO_MESSAGES).

ГДЕ РАБОТАЕШЬ
- Своя копия: powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name cx<NNN> -Branch codex/<NNN>-<slug>
  (диск C: почти полон - одна копия на задачу, после влития её удаляет Lead; если копия cx-* уже есть, переиспользуй её).
- В каждом shell: $env:CODEX_AGENT = "codex"; $env:PYTHONIOENCODING = "utf-8".
- Коммиты маленькие, явные пути (никогда git add -A), Conventional Commits, тело называет задачу NNN.
- Перед последним коммитом: python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex должно быть PASS.

ПРАВИЛА
- Сначала диагноз с доказательством (строки лога, числа, file:line), потом правка. Не ослабляй проверки и не удаляй тесты.
- Смоуки и тяжёлые прогоны - НИКОГДА на этом ПК (дизайнер играет): только вторая машина,
  Scripts/Tools/worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot1 -ShareMachine -ShipHead -Jobs 2 -Commands <ПолноеИмя>.
  Для 051 / 052 смоуки не нужны (только Python-тесты).
- Нельзя: Content/Maps/**, Config/**, Content/VFX/**, .uasset, CLAUDE.md, ветка main, чужие копии (CodexTactics-agents, -grok, -gemday, -cover).
- Spitter и CryoDrone не существуют - не упоминать.
- Отчёт docs/port/codex/outbox/<NNN>-<slug>.report.md по-русски: что сделано (файлы, file:line), тесты с выводом, чего НЕ сделал.
- После каждой задачи ОДНА строка в чат:
  python Scripts/Tools/agent_chat.py post --as codex --to claude --topic codex-<NNN> --text "<NNN> FINISH | PASS/FAIL | <hash> | сделано | НЕ сделано"
- ЛИМИТ (правило дизайнера): при 10 % остатка лимита - стоп, коммит (WIP можно), одна строка --topic codex-limit
  "limit 10%: <состояние>, последний коммит <hash>, дальше <шаг>, сброс в <время>", потом только ждать полного сброса.
- Если правило или задача кажутся неверными - одна строка в чат с причиной, и продолжай остальное.

Начни сейчас: прочитай чат и README, создай копию для 051, сделай 051, отчёт, FINISH, затем 052.
```
