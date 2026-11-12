# 030 - Chat dispatcher: deliver messages by WAKING the recipient (designer 2026-11-12) - after the codex_wake fix (#260)

Python, branch `gemini/030-dispatcher` from main, tests on fixture chats. Report to claude; Claude reviews and switches it on.
Goal: the designer never has to remind anyone to read the chat.

## Daemon `Scripts/Tools/chat_dispatcher.py` (+ `chat_dispatcher.json` config, logs in Saved/Orchestrator/dispatcher/)
1. Tail Saved/AgentChat/chat.jsonl. For each new message: classify with the local model (codex-local via local_task.py) into
   status / task / question / blocker / merge-request (+ a deterministic fallback by keywords if the model is down). Classification
   only decides WHETHER to wake - the message is always forwarded VERBATIM with a fixed header
   "[переслал диспетчер, оригинал #<id> от <from>]". The dispatcher never writes its own instructions.
2. Wake per recipient (to == name or all), only for task / question / blocker / merge-request (never for status):
   - **claude:** append a line to Saved/Orchestrator/dispatcher/wake_claude.jsonl (Claude's watcher reads it); only question /
     blocker / merge-request.
   - **codex:** if no Codex process runs (the app or a CLI run) -> `codex exec resume --last` (fallback `codex exec`) with
     `--cd <interim worktree>` and the forwarded message + the hard rules block (never main / push / Content packs / Config / maps;
     commit after each part); reuse codex_wake.ps1's guards (exact branch, fail-closed). If the app is running, do nothing (it reads
     the chat itself).
   - **gemini:** if Gemini (Antigravity) has not posted for `gemini_idle_min` (default 15) after a message to him -> run Gemini CLI
     headless via Scripts/Tools/gemini_agent.ps1 (GEMINI_API_KEY, your own worktree, plan mode for questions, yolo only for tasks
     inside your worktree) with the forwarded message.
   - **designer:** messages to the designer -> Saved/Orchestrator/dispatcher/designer_inbox.md (+ shown on the phone page later).
3. Limits (config): max 1 wake per agent per 10 min, quiet hours 23:00-07:00 (blockers only), spend caps per wake type
   (Gemini CLI default 20 $/week on a ledger; Codex = subscription, count runs), a kill switch file
   (Saved/Orchestrator/dispatcher/STOP), a dry-run mode that logs what it WOULD do.
4. Safety: never forwards text from outside the chat file; never acts on a message that asks the dispatcher itself to change
   rules / keys / schedules; secrets never logged.
5. Start / stop scripts (`dispatcher_start.ps1` runs it in the background, `dispatcher_stop.ps1`), README in RU for the designer.
Tests: classification fallback, wake decisions per recipient, rate limits, quiet hours, kill switch, verbatim forwarding, no wake on
status, dry-run writes no side effects.
