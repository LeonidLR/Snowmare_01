# Отчёт 031: Живая доска задач в AgentChat (Live Task Board)

## Что сделано

1. **`Scripts/Tools/task_board.py`** (только стандартная библиотека Python):
   - Собирает `Saved/AgentChat/task_board.json` атомарной записью (`save_board_atomic`) исключительно в режиме чтения из:
     - `docs/port/{codex,gemini}/inbox/*.md` с учётом переназначений в `docs/port/codex/INTERIM_LEAD.md` и `docs/port/RESOURCES.md` (`parse_lead_moves`);
     - `docs/port/{codex,gemini}/outbox/*report*` (`scan_outbox_reports`, `find_task_report`);
     - локальных и remote-tracking git-веток `codex/*`, `gemini/0NN-*` и их статуса слияния в `interim` / `main` (`match_git_for_task`);
     - `Saved/AgentChat/chat.jsonl` по `--topic <task-id>` и ссылкам на задачи (`match_chat_for_task`);
     - `docs/port/mechanics_register.json` (`load_mechanics_register`, `match_register_for_task`): сопоставляет русские названия (`title_ru`) и области (`area`), а несвязанные позиции реестра выводит в `not_started_register` со статусом `not_started`.
   - Автомат состояний (`compute_task_state`): `queued` → `in_progress` (коммит или сообщение в чате за последние 2 часа) → `waiting` (вопрос `designer`/`claude`/`all` или блокер) → `done` (найден report) → `in_interim` → `in_main`.
   - Полоса `now_strip` для `codex`, `gemini`, `claude`: текущая активная задача и `idle_min`.

2. **`Scripts/Tools/agent_chat_server.py`**:
   - В `run_server` добавлен фоновый поток `_board_refresh_loop`, обновляющий `task_board.json` каждые 60 секунд.
   - `GET /api/task-board` (поддерживает `?refresh=1`, иначе отдаёт кэш до 60с).
   - `GET /api/port-doc?path=...` (read-only просмотр markdown-файлов в `docs/port/` с проверкой `_PORT_DOC_RE` и `same_origin_only()`).

3. **`Tools/AgentChat/index.html`**:
   - Вкладка `«Задачи»` рядом с `«Чат»` (без действий записи в git/файлы — только чтение).
   - Верхняя полоса `«Сейчас»` по агентам (`codex`, `gemini`, `claude`) с текущей задачей и временем простоя.
   - Фильтры (`owner`, `state`, `area`, поиск по тексту/ID/ветке), колонки состояний + блок неначатых пунктов из `mechanics_register.json`.
   - Модальное окно просмотра markdown-документа (`task_file` / `report_file` / `doc`) через `/api/port-doc` через `textContent`.
   - Адаптация под экран телефона (`@media (max-width: 720px)`).

4. **Конвенция `--topic <task-id>`**:
   - Добавлено обязательное правило указывать `--topic <task-id>` (`codex-008`, `gemini-030`, `gemini-031` и т.д.) при отправке сообщений по задаче в:
     - `Scripts/Tools/agent_chat_connect_claude.md`
     - `Scripts/Tools/agent_chat_connect_codex.md`
     - `Scripts/Tools/agent_chat_connect_gemini.md`
     - `docs/port/RESOURCES.md`

## Проверки (после финальных правок)

- `python -m unittest Scripts/Tools/tests/test_task_board.py -v` — **OK (2/2 tests, 0.264s, без предупреждений)**:
  - `test_state_machine_handover_register_and_now_strip`: проверены handover-перенос `codex-006 -> gemini-020`, все переходы состояний (`queued`, `in_progress`, `waiting`, `done`, `in_interim`, `in_main`), привязка `mechanics_register.json`, `not_started_register` и расчёт `now_strip` (`idle_min`).
  - `test_http_endpoints_and_security_guards`: проверены `GET /api/task-board?refresh=1` (200), `GET /api/port-doc` (200 для разрешённого `.md`, 400 для traversal `../Source/Secret.cpp`), 403 при чужом `Origin`.
- `python Scripts/Tools/task_board.py --repo-root . --storage-dir "$env:TEMP\tg_board_smoke"` — **exit 0**: разобрано `34 tasks, 145 register items`.
- `node --check` по `<script>` блокам `Tools/AgentChat/index.html` — **exit 0** (синтаксис JS валиден).
