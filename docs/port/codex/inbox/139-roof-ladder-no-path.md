# Codex 139 (Lead reviews) - order onto a roof with a ladder: the squad does not climb (designer playtest 2026-11-26 ~07:47 UTC, X6, main editor)

Designer at a building with a ladder clicks the roof: nobody climbs. Log (excerpt next to this file: 139-roof-ladder-excerpt.log, main
Saved/Logs/CodexTactics.log lines 7600-7746):
- `Engineer: move to (11510, -7621) -> accepted` ... 7 roof clicks between (11446..11700, -7557..-7667), all `[Formation] bypass: none - the destination
  (x, y, 300) is on another level (feet at 2)`;
- `[GiveWay] Engineer: his walk to (11446, -7658) was ended as blocked 402 cm from it - asked again (1 / 4)`;
- NO `[Ladder] ... reached LadderActor_N's link going up` line after 07:31 (earlier the same session, LadderActor_18 worked via the path at 07:28).
So the path to the roof point is partial: it never takes a ladder link. Find out which: (a) this building's ladder nav link is not connected at the
top or bottom (roof navmesh island, link end off the mesh, link on a different roof part), (b) no ladder reaches that roof section, (c) the
order code drops the ladder link for formation orders to another level. Log the ladder actor nearest (11500, -7640) with its link ends + navmesh
projections. Fix the root cause; if it is map data (X6 placement), report it with the exact ladder / roof and a proposed data fix - do NOT edit the
designer's uncommitted L_Gen_Depot_1_X6.json in main.
Also: a click on a roof that is unreachable must say so ("no way up") instead of silently walking to the wall.
- Smoke: X6 roof-via-ladder step for that building (order to the roof point -> leader reaches z>=290 within the limit).
- Branch codex/139-roof-ladder-no-path from origin/main; build via machine_dispatch.py / build.ps1 -Remote worker; report docs/port/codex/outbox/139-roof-ladder-no-path.report.md;
  FINISH `139 FINISH | PASS/FAIL | <hash> | ...`. Queue: right after 134 (blocks high ground), then 136 -> 137 -> 138 -> G022.
