# 018 - agent_chat.py crashes on Russian text in a cp1252 console (Claude, 2026-11-16) - LIGHT, Python only
Why: `python Scripts/Tools/agent_chat.py wait --as claude` died with
`UnicodeEncodeError: 'charmap' codec can't encode characters` at agent_chat.py:361 (`print(format_message(msg))`) when a Russian
message arrived - the listener stops and the message is lost for the waiter. Workaround today: PYTHONIOENCODING=utf-8.
Branch codex/018-agent-chat-utf8 (from main).

Do:
1. At startup of agent_chat.py (main()), make stdout/stderr UTF-8 safe: `sys.stdout.reconfigure(encoding="utf-8", errors="replace")`
   (same for stderr), guarded with hasattr for non-reconfigurable streams. Check the other print paths (read, presence) use the same.
2. Check Scripts/Tools/chat_dispatcher.py and any other script that prints chat text for the same problem (grep "print(" + format_message);
   fix only the same one-line pattern there.
3. Test in Scripts/Tools/tests/ (unittest style, like test_agent_chat_idle.py): run the script in a subprocess with
   env PYTHONIOENCODING=cp1252 and a temp --root holding one Russian message -> exit 0 and the text printed.
Allowed paths: Scripts/Tools/agent_chat.py, Scripts/Tools/chat_dispatcher.py (only the print fix), Scripts/Tools/tests/test_agent_chat_utf8.py,
report docs/port/codex/outbox/018-agent-chat-utf8.report.md.
Checks: `python -m unittest Scripts/Tools/tests/test_agent_chat_utf8.py`, `python Scripts/Tools/agent_chat.py selftest`.
Router level expected 1-2 -> cheaper model.
