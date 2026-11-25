# Gemini 124 (Hydrogen) - agent chat "Задачи" tab: new column order and colours (designer 2026-11-25) - small, do right after 122

File: Tools/AgentChat/index.html only (board columns ~line 1149, column CSS, the "Реестр без задачи" block ~1252). Tools only.
Branch gemini/124-board-columns, worktree C:/Work/Lanes/CodexTactics-124.
Today, left -> right: В работе | Ждёт ответа / блокер | В очереди | Готово (ревью) | В main | Реестр без задачи.
Designer wants, left -> right:
1. "В очереди" (queued) - in the colour the LEFTMOST column has today (the current "В работе" colour), whole column.
2. "В работе" (in_progress) - ORANGE (e.g. #f97316 header / accent, readable in the dark theme). Items with status "waiting" (ждёт ответа /
   блокер) also go here, with a small red "блокер" chip on the card - the separate waiting column is removed.
3. "Готово (ревью)" (done) - LIGHT BLUE (e.g. #7dd3fc).
4. "В main" (in_main) - GREEN (e.g. #22c55e).
5. "Реестр без задачи" - collapsed by default behind a small toggle "Реестр (N)" at the bottom (state remembered in localStorage,
   try/catch), or removed if the toggle makes the code messy - say which in the report.
Keep the status filter <select> in the same new order. No server / data changes. Check: screenshot of the tab (dark theme) before / after
in the report; the page loads with no console errors (built-in browser at http://127.0.0.1:8765/ against your worktree copy of the file,
or open the file directly). Stage only index.html by path. Report docs/port/gemini/outbox/124-board-columns.report.md + "124 FINISH | ..." in chat. Never push.

ADDED (designer 2026-11-25): PROGRESS on every card in "В работе".
- Agents post progress lines in chat: "<id> PROGRESS <done>/<total> | <current step>" (e.g. "G017 PROGRESS 3/5 | A/B 300 seeds on the worker").
  Parse them in Scripts/Tools/task_board.py (match_chat_for_task already links chat lines to tasks): latest PROGRESS -> task fields
  progress_done / progress_total / progress_step; PLAN with numbered steps "1) ... 2) ... 3)" -> progress_total if no PROGRESS yet;
  START -> 0/total; FINISH -> full.
- A task with a PROGRESS / PLAN / START line in the last 2 h counts as in_progress even without a new commit (today Grok's G017 shows
  "queued" while it runs a 2-hour A/B on the worker).
- Card UI: a thin bar (orange fill) + "3/5 · A/B 300 seeds" + "N мин назад".
- pytest for the parser (PROGRESS / PLAN / START / FINISH, malformed lines ignored).
