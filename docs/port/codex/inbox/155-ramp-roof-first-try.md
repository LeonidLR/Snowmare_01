# Codex 155 (L2-L3; Lead reviews) - an order onto a roof via a RAMP fails on the first try, works on the second (designer playtest 2026-11-27)

Fight 2026-11-27_L_Gen_Depot_1_X6_085013 (play copy Saved/Demos), round 4, game time ~15:19 (real ~09:05:30 local = 07:05:30 UTC in the play log):
the Commander was ordered onto a building roof; he did NOT go up the ramp on the first order, only on the second. NOT a ladder (that is 139).
Commander log lines 07:05:30-07:06:05 are next to this file (155-ramp-roof-first-try-excerpt.log); the full play log is
CodexTactics-play/Saved/Logs/CodexTactics.log (read-only). Note the repeated sprint orders to (-2597, -11215) / (-2661, -11215) / (-2514, -11215).
Find why the first roof order via the ramp does not take (partial path, NoWayUp from 139 not merged yet, ramp nav link, formation / cover
state, GiveWay, the order being replaced ...), fix the root cause, and make a refused roof order say why on screen (never silent).
Replay the same situation in a smoke on X6 (full smoke name; reuse a roof / ramp smoke if one exists) - first order -> on the roof.
Lane C:/Work/Lanes/CodexTactics-155, branch codex/155-ramp-roof-first-try from origin/main. Build via Scripts/build.ps1 -Remote worker, Smart tests,
mapped smokes. Report docs/port/codex/outbox/155-ramp-roof-first-try.report.md; post "155 MERGE-READY | <sha> | ..." via Scripts/Tools/agent_chat.py
--as codex --to claude. No main / push / maps / .uasset / Config / VFX / designer JSON. 10 %: commit, LIMIT, stop.
