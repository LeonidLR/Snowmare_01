# 035 - Task board: done tasks in green + correct "done" detection (designer 2026-11-12) - AFTER 034 stealth v2

Branch gemini/035-board-green from main (031 is merged in main: Scripts/Tools/task_board.py, Tools/AgentChat/index.html).
1. Every task whose state is done / in interim / in main is shown GREEN (card border + a check mark; in interim = green outline,
   in main = solid green), in every view (columns, table, the "Сейчас" strip never shows them as active).
2. Fix done detection: today 027 / 028 (and others) show as "в очереди" although finished - the report name does not match the task
   file name. Match reports by task id prefix (e.g. "027" / "028") in docs/port/{gemini,codex}/outbox/*.md, by a "NNN done <hash>"
   chat line from the owner, or by a merged branch gemini/NNN-*. Tests for each rule on fixtures.
3. Show the evidence on the card (which report / chat line / branch made it done).
