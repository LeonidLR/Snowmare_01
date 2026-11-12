# 035 - Task board: done in green, correct done detection, plain-Russian descriptions (designer 2026-11-12) - AFTER 034 stealth v2

Branch gemini/035-board-green from main (031 is merged in main: Scripts/Tools/task_board.py, Tools/AgentChat/index.html).
1. Every task whose state is done / in interim / in main is shown GREEN (card border + a check mark; in interim = green outline,
   in main = solid green), in every view (columns, table, the "Сейчас" strip never shows them as active).
2. Fix done detection: today 027 / 028 (and others) show as "в очереди" although finished - the report name does not match the task
   file name. Match reports by task id prefix (e.g. "027" / "028") in docs/port/{gemini,codex}/outbox/*.md, by a "NNN done <hash>"
   chat line from the owner, or by a merged branch gemini/NNN-*. Tests for each rule on fixtures.
3. Show the evidence on the card (which report / chat line / branch made it done).
4. **Plain-Russian description on every card (designer 2026-11-12):** titles are too short / cryptic ("034 Свист" - the designer
   cannot tell what it is). Each card shows 1-2 lines in simple Russian: what the feature does for the player / the designer.
   Source: a `Суть:` line right under the first heading of each task file (docs/port/{gemini,codex}/inbox/*.md). Backfill it for
   ALL existing task files (read each, write one honest sentence, no jargon; commit the task-file edits in your branch), and from
   now on every new task file starts with `Суть:`. Fallback if missing: the register item's RU "what" text.
5. **Wrong register match:** 034 (stealth v2) is shown as "Свист" (whistle) - the register matching picks wrong items. Match only by
   explicit ids (a `Реестр: <id>` line in the task file, backfilled like `Суть:`), never by fuzzy words; show no register name rather
   than a wrong one. Test: 034 -> pl-stealth-mode.
