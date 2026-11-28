# Gemini 154 (L2; Lead reviews) - fight replay notes: move drawings, role names, tags, no duplicates (designer 2026-11-27 after his first real annotation session)

Training Ground "Хроника боя" (task 148: Tools/TrainingGround/src/replay/*, server/replayApi.ts, docs/systems/fight-replay.md). The designer annotated
fight 2026-11-27_L_Gen_Depot_1_X6_085013 (51 notes, mostly comments; file CodexTactics-play/Saved/Demos/<that>.notes.json - read-only for you,
copy it into a temp dir for tests). Problems and wanted changes:
1. MOVE drawings: comments (map pin), arrows, zones, routes, polylines must be draggable as a whole (grab the shape, drag; arrow / zone also by
   their handles as today). Today he could only delete and redraw.
2. DUPLICATES: almost every comment exists 2-4 times (one with a map point, one time-only) and old text leaks into the next note
   (the label field keeps its text after a note is placed, so the next click creates a copy). After a note is placed the label field clears;
   one click = one note; a "Remove duplicates" button (same kind + same text + time within 2 s + same/absent point -> keep the one with a point).
3. WHO: the unit list shows A / B / C / D. Show role names from the recording (Командир, Медик-сапёр, Инженер, ... - the operatives' display
   names; ids stay in the file). Default "Who" = the operative selected / clicked in the replay.
4. TAG per note: dropdown хорошо / ошибка / как надо / баг / прочее (saved as "tag"; schema codex.demo_notes/1 stays readable - add the field,
   old files load). demo_notes.py prints the tag and groups by it; unit tests.
5. Round colours: the round is already stored per note; colour stays free for the designer.
Node tests one file / concurrency 2 (temp dirs, never the real Saved/Demos); typecheck; docs/systems/fight-replay.md updated.
Lane C:/Work/Lanes/CodexTactics-154 (branch gemini/154-replay-notes-ux from origin/main). Report docs/port/gemini/outbox/154-replay-notes-ux.report.md
with 1-2 screenshots if you can (else say so); post "154 MERGE-READY | <sha> | ..." via Scripts/Tools/agent_chat.py --as gemini --to claude.
No main / push / C++ / maps / .uasset / Config / designer JSON. Stop at 10 % usage: commit, post LIMIT.
