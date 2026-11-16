# 018 - Agent chat UTF-8 console output

Branch: `codex/018-agent-chat-utf8`; base: `48755af8` (main).

- Added guarded UTF-8 stdout/stderr configuration with `errors="replace"` at startup in `agent_chat.py` and `chat_dispatcher.py`. All CLI print paths share these streams; dispatcher prints Unicode JSON too.
- Added subprocess regression using a temporary chat root, a Russian message, and `PYTHONIOENCODING=cp1252`. Covers read, JSON read, and wait; checks successful exit and intact UTF-8 text.
- Checks: scoped unittest PASS (3 subprocess cases); `agent_chat.py selftest` PASS; `git diff --check` PASS. No Unreal build needed for Python-only changes.
- Router: `1 | gpt-6-luna | pass`; parent review completed the dispatcher pattern and expanded CLI regression coverage.
- No local model used. No protected files or assets changed.
