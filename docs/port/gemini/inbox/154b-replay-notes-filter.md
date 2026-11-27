# Gemini 154b (L1-L2; Lead reviews) - replay notes: show the current round's notes, dim the rest (Lead review of 154, 2026-11-27)

After 154 the designer's real fight has 31 notes; all are drawn at once with their labels, so the map is unreadable (screenshot
docs/port/gemini/outbox/154-replay-notes-ux-ui.png). Wanted in "Хроника боя":
- default: draw the notes of the CURRENT round fully (shape + label); notes of other rounds as faint shapes without labels;
- a filter row: [текущий раунд] [все] [только теги: хорошо / ошибка / как надо / баг] and a "labels on map" toggle (labels always in the list);
- hovering / selecting a note in the list highlights it on the map with its full label.
DESIGNER 2026-11-27 12:15 - ROUNDS AS PARTS OF THE FIGHT (main point of this task): a sector recording is one long fight; split it into rounds.
- A round picker above the timeline: "Весь бой | Раунд 1 | Раунд 2 | ..." (from round_start / round_end events; a round starts when the squad
  spawns, ends at round_end or the next round_start; show result + duration per round, e.g. "Раунд 2 · победа · 03:12").
- With a round picked: the timeline / scrub / play cover ONLY that round (time shown from the round start, 00:00 = spawn), the map shows only
  that round's units, events and notes; new notes get that round automatically. Next / previous round buttons (and keys, e.g. PageUp / PageDown).
- "Весь бой" keeps today's behaviour. Notes of other rounds are hidden in round mode (the filter row above still works inside "Весь бой").
- Old recordings without round events = one round. Node tests for the round split (rounds from events, legacy file, relocations
  "pmc_relocated" are NOT round boundaries).
Node test for the filter rule; typecheck. Lane C:/Work/Lanes/CodexTactics-154b (branch gemini/154b-replay-notes-filter from origin/main after 154
is merged). Report docs/port/gemini/outbox/154b-replay-notes-filter.report.md + 1 screenshot; post "154b MERGE-READY | <sha> | ..." via
Scripts/Tools/agent_chat.py --as gemini --to claude. No main / push / C++ / maps / designer files. 10 % usage: commit, LIMIT, stop.
