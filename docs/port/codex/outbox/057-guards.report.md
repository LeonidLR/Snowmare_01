# 057 — ограничения маршрутизатора и вызова Agent

Проверено в `codex/057-guards` от `origin/main` (`d495ac83`).

- Диагноз. Старый `model_router.py:134–139` при L4 с уверенностью 99% возвращал `route=true`, `ue-l4-architect` и Fable. Локальный `PreToolUse.Agent` уже запускал `agent_gate.py`. Новый хук добавлен вторым в эту группу.
- `Scripts/Tools/model_router.py:91,97,182,258`. Классификатор сохраняет уровень L4 для сопоставления моделей Codex. Рекомендация L4 — `do it yourself (Lead) or Codex`. При отсутствии `Saved/Lead/ALLOW_SUBAGENTS` выводится отдельная строка причины. Даже конфигурация L1–L3 с Fable/архитектором не выдаёт рекомендацию Agent.
- `Scripts/Tools/agent_guard.py:28,45,69`. Хук блокирует `Agent` кодом 2 и причиной в stderr без `ALLOW_SUBAGENTS`. Для Fable и `ue-l4-architect` дополнительно нужен `ALLOW_FABLE`. На другие инструменты новый хук не распространяется. [Контракт PreToolUse](https://code.claude.com/docs/en/hooks)
- Регистрация проверена в локальной `.claude/settings.local.json` копии 057. Семантическое сравнение JSON показало единственное отличие: добавленный guard рядом с существующим Agent-хуком. Личные разрешения и `UserPromptSubmit` сохранены. Файл игнорируется Git. Для переноса в активную копию Lead после слияния запускает из её корня `python Scripts/Tools/agent_guard.py --install-local`. Повторный запуск не создаёт дубль.

## Проверки

- `python -m unittest discover -s Scripts/Tools/tests -p test_agent_guard.py` → 8/8 PASS, включая вызовы stdin, общий флаг, исключение Fable, две строки хука и регистрацию.
- `python -m pytest -q -p no:cacheprovider Scripts/Tools/test_model_router.py Scripts/Tools/tests/test_agent_guard.py` → 15 PASS, 2 FAIL, 3 успешно пройденных подслучая. Те же 2 FAIL воспроизведены до правки в исходном `test_model_router.py:46,143`. Тесты ожидают `local_first_for_level_1=false`, а `Scripts/Tools/model_routes.json:18` уже хранит `true`. Конфигурация Codex и обе проверки сохранены.
- `git diff --cached --check` и `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex` → PASS.

## Чего не сделал

Активный локальный JSON Lead не редактировал. Живой запуск Claude Code или subagent не делал. Смоуков, пуша и слияния не было.
