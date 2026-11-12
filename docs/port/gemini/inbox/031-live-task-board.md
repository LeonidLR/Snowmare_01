# 031 - Live task board inside the agent chat page (designer 2026-11-12): every task, who does it, its state - right now

Python + HTML (Scripts/Tools/agent_chat_server.py + Tools/AgentChat/index.html), branch `gemini/031-task-board` from main, after 030.
Report to claude.

## Data (no new manual bookkeeping - derive it)
- `Scripts/Tools/task_board.py` builds `Saved/AgentChat/task_board.json` every minute (and on demand) from:
  1. task files: docs/port/{codex,gemini}/inbox/*.md (id = file prefix like codex-008 / gemini-030, title = first heading,
     owner = folder, plus moves recorded in INTERIM_LEAD.md / RESOURCES.md, e.g. 006 -> gemini 020);
  2. reports: docs/port/{codex,gemini}/outbox/*report* -> "done / waiting for review";
  3. git: branches named after the task (codex/*, gemini/0NN-*), last commit time + hash, merged into interim / main ->
     "merged into interim" / "in main";
  4. chat: messages whose topic contains the task id or slug -> last activity time + text, "asked a question" if the last one is a
     question to the designer;
  5. the mechanics register (docs/port/mechanics_register.json): link a task to its register item(s) when ids / slugs match; show the
     register items that have NO task as "not started".
- State per task: queued -> in progress (commits or chat in the last 2 h) -> waiting (question / blocked) -> done (report) ->
  in interim -> in main. Owner shown with the agent colour; "idle for N min" like the asleep banner.

## Page
- A "Задачи" tab next to the chat: columns by state (or a table with filters: owner, state, area), each card = id, title (RU where
  the register has a RU name), owner, branch + last commit, last chat line, links to the task file / report.
- A "Сейчас" strip on top: what each agent is doing right now (its newest in-progress task + minutes since its last activity).
- Phone layout like the chat (works through the 022 Tailscale access when enabled).
- Read-only: the page changes nothing in git or the files.

## Convention for agents (add to agent_chat_connect_*.md and RESOURCES.md)
Every chat post about a task uses `--topic <task id>` (e.g. codex-008, gemini-030) - that is how the board links messages.
Tests: parsing fixtures (task files, reports, branches via a fake git, chat), the state machine, register matching.
