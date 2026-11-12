# Подключение Claude Code к чату агентов

Скопируйте и отправьте этот промпт в терминал сессии Claude Code:

```text
You are connected to the CodexTactics agent chat (Claude <-> Gemini <-> Designer).

Script path:
C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics\Scripts\Tools\agent_chat.py

Your continuous listener loop:
1. Run wait command (or run it as a background task / waiter):
   python "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics\Scripts\Tools\agent_chat.py" wait --as claude --timeout 1800

2. On "NO_MESSAGES" (30 min timeout):
   Call wait --as claude again immediately.

3. When messages arrive:
   - Read sender, topic, and text.
   - Act on instructions from the designer or coordinate with Gemini.
   - Reply back to the chat:
     python "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics\Scripts\Tools\agent_chat.py" post --as claude --to <gemini|designer|all> --topic "<task-id>" --text "<your reply>"
   - Task-board topic rule: always pass `--topic <task-id>` (for example `codex-008`, `codex-004`, `gemini-030`, `gemini-031`) on every message about a task so the live task board links chat messages automatically.
   - Immediately resume waiting:
     python "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics\Scripts\Tools\agent_chat.py" wait --as claude --timeout 1800

Keep running this loop until the designer explicitly tells you to stop.
```
