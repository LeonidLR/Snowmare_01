# 001 - Agent chat (Claude <-> Gemini, watched by the designer)

Tooling task: Python + one HTML page. NO game code, NO C++, nothing under `Source/` or `Content/`.

## Where to work
- Create a worktree first, from the main checkout folder `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics`:
  `powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name gemchat -Branch gemini/agent-chat`
  then work ONLY in `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-gemchat`. Follow the rules in `docs/port/gemini/README.md`
  (`$env:CODEX_AGENT = "gemini"`, explicit-path commits, no push / merge, never touch main's files except your report in
  `docs/port/gemini/outbox/`).

## Goal
A local chat where Claude (Claude Code session) and Gemini (an Antigravity agent session) talk to each other all day while the designer
watches and can type. Neither agent can "listen"; each runs a blocking `wait` command that returns as soon as a message for it arrives.

## Build
1. `Scripts/Tools/agent_chat.py` (Python 3, standard library only):
   - Storage: `Saved/AgentChat/chat.jsonl` in the MAIN checkout (`C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics\Saved\AgentChat\`,
     resolve it from the script location so it works from any worktree via an absolute path constant + `--root` override), one JSON per
     line: `{"id": n, "ts": ISO time, "from": "claude|gemini|designer", "to": "claude|gemini|all", "topic": str, "text": str}`.
     Append with a lock file (both agents may write at once); UTF-8 (Russian text).
   - `post --as <who> --to <who|all> [--topic T] --text "..."` (or `--file path` for long text) -> prints the new id.
   - `read [--as <who>] [--since ID] [--last N]` -> prints messages (human-readable, or `--json`).
   - `wait --as <who> [--timeout 1800]` -> blocks until a message addressed to `<who>` or `all` (not from `<who>`) arrives after that
     participant's last-seen id (kept in `Saved/AgentChat/seen_<who>.json`), prints those messages and exits 0; on timeout prints
     `NO_MESSAGES` and exits 0 (so a loop can simply call it again). Poll the file every 2 s (no busy loop).
   - `presence`: each `wait` writes `Saved/AgentChat/presence_<who>.json` (`{"waiting_since": ts}`) and clears it on exit, so the page can
     show who is listening.
   - Limit: `--max-per-topic` (default 10) - `post` refuses a message when a topic already has 10 messages between claude and gemini
     since the designer's last message in it, printing `TOPIC_LIMIT: ask the designer`.
2. `Scripts/Tools/agent_chat_server.py`: `python Scripts/Tools/agent_chat_server.py [--port 8765]` serves on 127.0.0.1 only:
   - `GET /` -> `Tools/AgentChat/index.html`; `GET /api/messages?since=ID` -> JSON; `POST /api/post` (`{"to","topic","text"}`, from =
     designer); `GET /api/presence`.
3. `Tools/AgentChat/index.html` (one file, vanilla JS, no CDN): message list like a messenger (colour per sender: Claude / Gemini /
   designer), auto-refresh every 2 s, a text box + "to: Claude / Gemini / all" + topic field to post as the designer, presence dots
   ("Claude listening", "Gemini listening"), Russian UI labels, dark theme, works in a narrow window.
4. `Scripts/Tools/agent_chat_connect_gemini.md`: the exact text the designer pastes into Antigravity to connect, telling the agent to loop:
   `python <abs>\Scripts\Tools\agent_chat.py wait --as gemini --timeout 1800` -> if messages: do what they ask within the rules
   (read-only code questions, reviews, plans, Training Ground TypeScript; never C++; never touch main) -> answer with `post --as gemini`
   -> call `wait` again; on `NO_MESSAGES` call `wait` again; keep going until the designer says stop. Also `agent_chat_connect_claude.md`
   with the equivalent for Claude (Claude will adapt it to its background-waiter).
5. `docs/tools/agent-chat.md`: how to start (server, connect both), files, limits.

## Checks before reporting
- A Python self-test `python Scripts/Tools/agent_chat.py selftest` (temp root): two threads waiting as claude / gemini, a post to each wakes
  only the right one, `all` wakes both, the lock survives 50 concurrent posts, the topic limit triggers, Cyrillic round-trips.
- Start the server, open the page (or fetch `/api/messages`) and confirm a designer post appears for `wait --as claude`.

## Report
`docs/port/gemini/outbox/001-agent-chat.report.md` (Russian): commit hash(es), how to start it, self-test output, anything not done.
