# Чат агентов CodexTactics (Claude ↔ Gemini ↔ Дизайнер)

Локальная система коммуникации и координации между агентами искусственного интеллекта (Claude Code и Gemini Antigravity) и ведущим геймдизайнером проекта.

## Архитектура системы

- **Хранилище данных:** `Saved/AgentChat/chat.jsonl` в основном каталоге проекта `CodexTactics`.
  - Формат строки: `{"id": int, "ts": "ISO-8601", "from": "claude|gemini|designer", "to": "claude|gemini|all", "topic": str, "text": str}`.
  - Конкурентная запись защищена межпроцессной блокировкой `Saved/AgentChat/chat.lock`.
  - Отметка прочитанного: `Saved/AgentChat/seen_<who>.json`.
  - Индикация присутствия: `Saved/AgentChat/presence_<who>.json`.
- **CLI-инструмент:** `Scripts/Tools/agent_chat.py` (стандартная библиотека Python 3).
- **HTTP Dev-сервер:** `Scripts/Tools/agent_chat_server.py` (слушает только `127.0.0.1:8765`).
- **Веб-интерфейс:** `Tools/AgentChat/index.html` (автономный HTML/JS/CSS без внешних CDN).

---

## Быстрый запуск

### 1. Запуск веб-сервера
В терминале:
```powershell
python "Scripts/Tools/agent_chat_server.py" --port 8765
```
Откройте в браузере: `http://127.0.0.1:8765`

### 2. Подключение Gemini (Antigravity)
Скопируйте текст из файла `Scripts/Tools/agent_chat_connect_gemini.md` в окно чата Antigravity.
Gemini перейдёт в режим блокирующего ожидания сообщений:
```powershell
$env:CODEX_AGENT = "gemini"; python "Scripts/Tools/agent_chat.py" wait --as gemini --timeout 1800
```

### 3. Подключение Claude Code
Скопируйте текст из файла `Scripts/Tools/agent_chat_connect_claude.md` в окно терминала Claude Code:
```powershell
python "Scripts/Tools/agent_chat.py" wait --as claude --timeout 1800
```

---

## Справочник команд CLI (`agent_chat.py`)

### Отправка сообщения (`post`)
```powershell
python Scripts/Tools/agent_chat.py post --as <claude|gemini|designer> --to <claude|gemini|all> [--topic <тема>] --text "<сообщение>"
```
Для длинных сообщений поддерживается `--file <путь>`:
```powershell
python Scripts/Tools/agent_chat.py post --as gemini --to claude --topic "architecture" --file "docs/plan.md"
```

### Чтение сообщений (`read`)
```powershell
# Читать последние 10 сообщений:
python Scripts/Tools/agent_chat.py read --last 10

# Читать все новые с момента последнего прочтения:
python Scripts/Tools/agent_chat.py read --as gemini

# Читать в формате JSON:
python Scripts/Tools/agent_chat.py read --since 15 --json
```

### Ожидание сообщений (`wait`)
Блокирует выполнение до появления нового сообщения, адресованного участнику или `all`:
```powershell
python Scripts/Tools/agent_chat.py wait --as claude --timeout 1800
```
- При поступлении сообщения: выводит текст в консоль и выходит с кодом `0`.
- По таймауту: выводит `NO_MESSAGES` и выходит с кодом `0` (готово к следующему вызову в цикле).
- Пока команда активна, создаётся `presence_<who>.json`, сигнализируя в веб-интерфейсе статус "слушает".

### Проверка присутствия (`presence`)
```powershell
python Scripts/Tools/agent_chat.py presence [--json]
```

### Запуск встроенных тестов (`selftest`)
```powershell
python Scripts/Tools/agent_chat.py selftest
```
Тест проверяет:
1. Корректность кодировки UTF-8 и кириллических символов.
2. Лимит сообщений по темам (Topic Limit) и сброс дизайнером.
3. Точечную доставку сообщений и широковещательный `all` через потоки `wait`.
4. Надежность блокировки при 50 одновременных параллельных записях.

---

## Ограничения и защита от зацикливания

- **Лимит темы (`--max-per-topic 10`):**
  Если Claude и Gemini обмениваются 10 сообщениями по одной теме подряд без участия человека, отправка 11-го сообщения блокируется с сообщением `TOPIC_LIMIT: ask the designer`. Это предотвращает бесконечные автономные циклы и галлюцинации без участия ведущего дизайнера. Ответ дизайнера в эту тему сбрасывает счётчик.
- **Безопасность среды:**
  Сервер слушает строго локальный адрес `127.0.0.1`.
- **Сохранность данных:**
  Запись ведётся в `Saved/AgentChat/chat.jsonl`, который игнорируется в git (находится в каталоге `Saved/`).
