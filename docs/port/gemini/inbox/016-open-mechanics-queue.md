# 016 - Open mechanics queue (Claude, designer 2026-11-11: "move on through the unimplemented mechanics") - in order, no chat wait

Source of truth for "open": your board-reconcile.report.md + TASK_BOARD.draft.md. Mortars are POSTPONED (designer) - skip.
Rules as always: one branch per item from `main` (`gemini/016-<n>-<slug>`), TS / Python tooling or read-only reports only; never C++,
Source, Config, Content assets, maps, .uasset, main, push. Chat: only "016 item N done <hash>" or a real question (Russian, with a
recommended default). Design questions go to the designer and you CONTINUE with the default.

1. **T11 / #26 Depot x2 - the generator's "extend" mode (TypeScript, Training Ground).** Board T11 + register pl-depot-x2: the level
   generator takes an existing level JSON (the designer's L_Gen_Depot_1.json is READ-ONLY - work on a copy fixture) and builds the
   doubled area around it, keeping every hand-placed object; candidate layouts listed with a score. npm test + typecheck green, report.
2. **#41 Auto-QA, part 1 (Python tooling).** docs/design/AUTO_QA.md: the morning report generator only - reads existing smoke / bot /
   trainer outputs (Saved/Automation, Saved/Orchestrator, Saved/Trainer; via typesafe_triage.py --telemetry for big jsonl), merges
   duplicates, writes Saved/Orchestrator/qa/morning-YYYY-MM-DD.md with new "QA" rows proposed for the board. Unit tests on fixtures.
   No game sensors (that is C++ - list them as follow-ups for Codex).
3. **Implementation maps (read-only), one report each, for Codex / Claude next week:**
   a. #4 wall-to-wall cover dash (group order),
   b. #25 + #35 blizzard: accuracy and formation break-up (docs/design/WEATHER_COLD_COMBAT.md),
   c. #16 ragdoll (fatal hits / falls),
   d. #45 session recording (Codex.Demo.Record).
   Each: existing classes / functions to extend (file:line), data keys, insertion points, smokes, new vs reuse, open design questions
   with a recommended default.
4. Tomorrow morning: the night trainer review (012 item 4).
