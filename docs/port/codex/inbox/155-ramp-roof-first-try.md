# Codex 155 (L2-L3; Lead reviews) - an order onto a roof via a RAMP fails on the first try, works on the second (designer playtest 2026-11-27)

Fight 2026-11-27_L_Gen_Depot_1_X6_085013 (play copy Saved/Demos), round 4, game time ~15:19 (real ~09:05:30 local = 07:05:30 UTC in the play log):
the Commander was ordered onto a building roof; he did NOT go up the ramp on the first order, only on the second. NOT a ladder (that is 139).
Commander log lines 07:05:30-07:06:05 are next to this file (155-ramp-roof-first-try-excerpt.log); the full play log is
CodexTactics-play/Saved/Logs/CodexTactics.log (read-only). Note the repeated sprint orders to (-2597, -11215) / (-2661, -11215) / (-2514, -11215).
Find why the first roof order via the ramp does not take (partial path, NoWayUp from 139 not merged yet, ramp nav link, formation / cover
state, GiveWay, the order being replaced ...), fix the root cause, and make a refused roof order say why on screen (never silent).
Replay the same situation in a smoke on X6 (full smoke name; reuse a roof / ramp smoke if one exists) - first order -> on the roof.
CASE 2 (designer 2026-11-27 ~11:00, it cost him the whole squad): a building around X -7600..-7200, Y 15700..16900 (screenshot
155-case2-building.webp: a room with an inner ramp on the left and a ladder on the right; the ladder is visibly SHORT of the roof). Neither the
ramp nor the ladder got anyone up: the roof orders at 09:00:41 and 09:01:17 UTC became "move to (-7215, ...) (near side first, windows closed)"
ground orders - silently. Log in 155-ramp-roof-case2-excerpt.log.
=> Also write a MAP AUDIT (python or a commandlet / smoke, your choice): for every ladder and ramp on L_Gen_Depot_1_X6 check that its top
reaches the roof / upper floor it claims (ladder top vs roof height, landing on navmesh, nav link connected both ends; ramp top joins the roof
navmesh) and list every broken one with building number (148 buildings_index.json), actor, heights and the proposed data fix. Do NOT edit the
map / LevelJson / designer files - the Lead and the designer apply data fixes (same rule as 139's LadderActor_22). Fix code-side causes yourself.

Lane C:/Work/Lanes/CodexTactics-155, branch codex/155-ramp-roof-first-try from origin/main. Build via Scripts/build.ps1 -Remote worker, Smart tests,
mapped smokes. Report docs/port/codex/outbox/155-ramp-roof-first-try.report.md; post "155 MERGE-READY | <sha> | ..." via Scripts/Tools/agent_chat.py
--as codex --to claude. No main / push / maps / .uasset / Config / VFX / designer JSON. 10 %: commit, LIMIT, stop.

## Codex Lead resume review (2026-11-27)

Preserve WIP 4b725e7d; merge current origin/main before final validation (153/135b/152 are now merged; 156 may follow).
Read the latest AgentChat Lead notes. The saved branch currently changes Scripts/Tools/worker_sync.ps1: remove that unrelated shared-infrastructure
change from the final diff; the previous Lead explicitly reverted the same unsolicited LFS change in task 121. Do not replace it with another
infrastructure edit. Use the existing permitted worker transport, report actual transport effects, and keep branch refs/main unpushed.
Review the 152 traversal-gait overlap while preserving the independent own-order restoration. Show real first-order roof arrival and refusal
feedback in mapped smokes; keep the map audit read-only and report every broken ladder/ramp by building/actor. Do not apply designer data fixes.