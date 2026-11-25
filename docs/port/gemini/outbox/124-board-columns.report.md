# 124 — Board Columns, Colours & Progress Bar Report

- **Branch**: `gemini/124-board-columns`
- **Model**: Hydrogen
- **Status**: PASS

## Changes
1. `Tools/AgentChat/index.html`:
   - Reordered board columns left-to-right: `queued` ("В очереди") -> `in_progress` ("В работе", orange `#f97316` / `#fb923c`, includes `waiting` cards with red `"блокер"` chip) -> `done` ("Готово (ревью)", light blue `#7dd3fc`) -> `in_main` ("В main", green `#22c55e` / `#4ade80`).
   - Updated `<select id="filter-state">` options to match the new column order.
   - Collapsed `"Реестр без задачи"` at the bottom behind `"Реестр (N)"` (`.register-footer` / `.register-toggle`), remembered in `localStorage` (`agent_chat_register_open`) with `try/catch`.
   - Added thin orange progress bar + `"done/total · step · N мин назад"` line on `"В работе"` cards when `progress_total` is present.
2. `Scripts/Tools/task_board.py`:
   - Added `parse_progress_from_messages()` parsing `PROGRESS <done>/<total> | <step>`, `PLAN` numbered steps, `START`, and `FINISH` from matched chat messages, ignoring malformed `PROGRESS` lines.
   - Wired `blocked`, `progress_done`, `progress_total`, `progress_step`, `progress_ts` into `board_tasks`.
   - Tasks with `START`/`PLAN`/`PROGRESS` in the last 2 hours count as `in_progress` even without git commits.
3. `Scripts/Tools/tests/test_task_board.py`:
   - Added `test_progress_parsing_and_chat_in_progress_window`.

## Verification
- `python -m pytest Scripts/Tools/tests/test_task_board.py`: **4 passed** (`1.46s`).
- Browser verification (`http://127.0.0.1:18765/`, dark theme, viewport 1440x900):
  - 0 JS console errors, 0 HTTP >= 400 errors.
  - 4 columns verified left-to-right (`В очереди`, `В работе`, `Готово (ревью)`, `В main`).
  - `"Реестр (66)"` collapsed at bottom by default, expands/collapses on click.
  - Screenshot: `docs/port/gemini/outbox/124-board-columns.png`.

## Self-Review
- Checked `git diff` against `docs/port/gemini/inbox/124-board-columns.md`: 0 findings.
