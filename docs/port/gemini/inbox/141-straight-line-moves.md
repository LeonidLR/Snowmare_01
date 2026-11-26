# Gemini 141 (L3, Hydrogen + Neon review; Lead reviews) - move orders: go straight, climb on the way (designer 2026-11-26, lost fights over it)

Problem (designer playtest): operatives take the easiest navmesh route; to avoid a fence / drop / obstacle they run long detours, and in combat
that walks them into bad places (a tripwire, open ground). "If I order a move straight ahead, he must go straight, whatever is in the way."
Designer decision: implement 1 + 2, and 3 only in the tactical pause.
1. DEFAULT = prefer the straight line. When the navmesh route is much longer than the straight line (>= 1.3x, tunable in DA_GameBalanceConfig),
   build the straight route with traversal on the way: fence / low-obstacle vault (mantle), drop down, ladder, window (combat rule), and take it.
   If something on the line is impassable (tall wall, unjumpable gap) -> the shortest local detour around THAT obstacle, back onto the line,
   never a big loop. Applies to Solo and formation orders (formation: fence queue G026 / Codex 138).
2. STRICT STRAIGHT order (modifier + click on the ground): the operative walks exactly along the line and climbs / drops over everything climbable;
   in front of an impassable obstacle he stops and says so ("Can't get through here"). Modifiers already used on clicks: Ctrl = attack,
   Shift = suppress / corner look, Alt = squad-wide (cover / corner). Find a FREE binding (e.g. Alt+click on open ground if that does nothing now,
   else propose one) and post it to the Lead BEFORE coding the input part.
3. WAYPOINTS, tactical pause only: while paused, the player can lay a route of points for the selected operative / group (shown as a line + dots),
   executed on unpause; each leg uses rule 1. Binding: free one, same confirmation step as in 2. Real time stays click = one destination.
Also: KNOWN (revealed) tripwires / mines are never on a chosen route - rules 1 / 2 / 3 stop in front of one (or step around it locally) and warn.
Check whether path costs already avoid revealed traps today and report.
- Pure rules (straight-vs-detour choice, local detour bound, strict-mode stop, waypoint queue) -> unit tests. Smokes on X6: fence across the line
  (default climbs, not around), tall wall (local detour), strict mode stop, revealed tripwire on the line (stop + warn), pause waypoints (3 legs).
- Branch gemini/141-straight-line-moves from origin/main; build via machine_dispatch.py; report docs/port/gemini/outbox/141-straight-line-moves.report.md;
  FINISH `141 FINISH | PASS/FAIL | <hash> | ...`. Queue: after 135, before 121.
