# Gemini 154b (L1-L2; Lead reviews) - replay notes: show the current round's notes, dim the rest (Lead review of 154, 2026-11-27)

After 154 the designer's real fight has 31 notes; all are drawn at once with their labels, so the map is unreadable (screenshot
docs/port/gemini/outbox/154-replay-notes-ux-ui.png). Wanted in "Хроника боя":
- default: draw the notes of the CURRENT round fully (shape + label); notes of other rounds as faint shapes without labels;
- a filter row: [текущий раунд] [все] [только теги: хорошо / ошибка / как надо / баг] and a "labels on map" toggle (labels always in the list);
- hovering / selecting a note in the list highlights it on the map with its full label.
Node test for the filter rule; typecheck. Lane C:/Work/Lanes/CodexTactics-154b (branch gemini/154b-replay-notes-filter from origin/main after 154
is merged). Report docs/port/gemini/outbox/154b-replay-notes-filter.report.md + 1 screenshot; post "154b MERGE-READY | <sha> | ..." via
Scripts/Tools/agent_chat.py --as gemini --to claude. No main / push / C++ / maps / designer files. 10 % usage: commit, LIMIT, stop.
