# Диспетчер командного чата CodexTactics

Автоматический демон [chat_dispatcher.py](../../Scripts/Tools/chat_dispatcher.py), который следит за `Saved/AgentChat/chat.jsonl` и **будит получателя**, когда в чате появляется новая задача, вопрос, блокер или запрос на ревью/мерж.

---

## Что именно делает диспетчер
1. **Классифицирует новые сообщения** через локальную модель (`codex-local` через Ollama `local_task.py`) в один из 5 типов:
   - `status` — никого **не будит** (подтверждения, отчёты «готово», прогресс).
   - `task` — новая задача.
   - `question` — вопрос или запрос решения.
   - `blocker` — критическая ошибка сборки / падение / блокер.
   - `merge-request` — запрос на ревью и влитие ветки.
   Если Ollama недоступна, включается **детерминированный fallback по ключевым словам**.
2. **Пересылает текст дословно (verbatim)** — диспетчер никогда не придумывает свои поручения:
   ```text
   [переслал диспетчер, оригинал #<id> от <from>]
   <точный текст сообщения>
   ```
3. **Будит каждого агента по его механизму:**
   - **claude** (`question` / `blocker` / `merge-request` — не `task`): дописывает строку в `Saved/Orchestrator/dispatcher/wake_claude.jsonl`.
   - **codex** (только если не запущено приложение/CLI `codex`): проверяет worktree и точную ветку `interim/2026-11-12` (логика `codex_wake.ps1` fail-closed), запускает `codex exec resume --last --cd <interim>` (или `codex exec`) с дословным сообщением и жёстким блоком правил (запрет на `main`, push, паки `Content/*`, `Config`, карты).
   - **gemini**: ждёт `gemini_idle_min` (по умолчанию **15 минут**), если Gemini в Antigravity не ответил в чат — запускает headless CLI через `Scripts/Tools/gemini_agent.ps1` в его worktree (`-Mode plan` для вопросов, `-Mode yolo` только для задач/блокеров/мержей).
   - **designer**: сохраняет сообщения для дизайнера в `Saved/Orchestrator/dispatcher/designer_inbox.md`.

---

## Безопасность, лимиты и управление
- **Конфиг:** `Scripts/Tools/chat_dispatcher.json`
  - **Rate limit:** максимум **1 пробуждение на агента раз в 10 минут**.
  - **Тихие часы:** `23:00–07:00` — срабатывают только сообщения типа `blocker`.
  - **Лимит расходов Gemini CLI:** по умолчанию **$20 / неделя** в `Saved/Orchestrator/dispatcher/gemini_spend.jsonl`.
  - **Защита самого диспетчера:** сообщения, пытающиеся приказать диспетчеру изменить правила, ключи API, лимиты или расписание, игнорируются; секреты из `env` вырезаются из логов.
- **Пробный режим (без побочных эффектов):**
  ```powershell
  python Scripts/Tools/chat_dispatcher.py --once --dry-run
  ```
- **Запуск в фоне:**
  ```powershell
  powershell -ExecutionPolicy Bypass -File Scripts/Tools/dispatcher_start.ps1
  ```
- **Экстренная остановка (Kill Switch):**
  ```powershell
  powershell -ExecutionPolicy Bypass -File Scripts/Tools/dispatcher_stop.ps1
  ```
  или вручную создайте пустой файл `Saved/Orchestrator/dispatcher/STOP`. Чтобы снова разрешить запуск, удалите файл `STOP` или передайте `-ClearStop`.
