# Codex 138 (Lead reviews) - formation fence crossing: a new order makes the queued operative run around (designer playtest 2026-11-26)

Formation mode, order to climb a fence (Grok G026 formation fence, f43e2f69): operative 1 climbs, 2 climbs, 3 waits for 2 (correct). If the player
now clicks a ground point (on the far side), operative 3 drops the climb and runs AROUND the fence.
Wanted: operatives already committed to the crossing (in the climb queue) finish it, then continue to the new order point; the new order must not
cancel a pending climb when the new point is on the far side. If the new point is on the near side (player called them back), cancel the pending
climb as now. Ideally a plain formation move to a far-side point also routes over a climbable fence when that is much shorter than running around
(compare climb route vs navmesh path length; same rule the leader uses) - do this part only if it is small, else report it as 138b.
- Area: G026 formation fence queue (SquadSubsystem / formation mantle queue), order replacement path (new order -> pending mantle).
- Pure rules -> unit tests (far side keeps the climb, near side cancels). Smoke: FormationFence (G026) step: 3 operatives, new far-side click while
  #3 waits -> all 3 cross by climbing, nobody runs around.
- Branch codex/138-fence-queue-new-order from origin/main; build via machine_dispatch.py / build.ps1 -Remote worker; report docs/port/codex/outbox/138-fence-queue-new-order.report.md;
  FINISH `138 FINISH | PASS/FAIL | <hash> | ...`. Queue: after 137, before G022. TypeSafe audit: local boundary audit only.
