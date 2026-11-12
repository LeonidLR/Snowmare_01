# 021 - Late-night queue 2026-11-12 (Claude) - start ONLY after 016, 018, 019, 017 and 020 are done; in order, no chat wait

Same rules as 016 / 017 (own branch per item from main, read-only reports / TS / Python only - the C++ exception was ONLY for 020).
Chat: "021 item N done <hash>" (to claude) or a real question (Russian, recommended default) - then continue with the default.

1. Implementation maps (read-only, same format as 016 item 3): #18 APC on Depot + the APC fight, #27+ the remaining PMC items
   (silhouettes, hearing, HP bars, autonomy / bot seeing PMC, loot, grenades, bot fights longer than one round), #46 Depot as a real
   level (immortality off, ammo runs out, cold, procedural mines / loot - what exists after props C4, what is missing), #41 the
   in-game QA sensors (stuck / off-navmesh, fire outside the look sector, T-pose, falling through the floor, "in cover but visible",
   order not executed) - one spec per sensor for Codex.
2. Training Ground combat lab: a door-breach scenario (#23, docs/design/DOORWAY_BREACH.md answers 2026-11-11) in the TS combat sim -
   stack at the door, the second-click entry, room clear order - with unit tests, so the C++ step has a reference behaviour.
3. Morning: the night trainer review (012 item 4) as planned.
4. **Grade the local-model trial (read-only, ~15 min).** Claude ran 7 simple tasks on the local model (qwen2.5-coder:14b via
   Scripts/Tools/local_task.py); answers in MAIN Saved/Orchestrator/local_trial/01..07-*.md (prompts in run_trial.sh). For each:
   correct / partly / wrong + why (check against the real file / run the one-liner and regex yourself, read-only). Report
   docs/port/gemini/outbox/021-local-model-grades.report.md with a verdict: which task kinds the local model can take. To claude.
