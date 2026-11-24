# G015 - PMC mines and tripwires (enemy AI)

**PARKED 2026-11-24 (designer + Lead): not now - a cost with little value for the open-field / 3v3 sector training. Do not start.**

From: enemy-AI session (Claude), 2026-11-23. Designer: "Дайте им всё оружие ... мины, растяжки ... не бесконечное".
Lead OK: Grok implements, AFTER G014 (grenades change open-field fights more; mines are prepared defence).

## Goal
Before contact a defending PMC squad lays a finite number of mines / tripwires (start: 1 mine + 1 tripwire per squad) on the
attackers' likely approach to its position, out of its own walking paths. During the fight nothing new is laid.

## Where
- Placement exists - reuse it: Source/CodexTactics/Private/LevelGen/PropsPlacementC4.cpp + PropsPlacementInternal.h (generator
  mines / C4), Source/CodexTactics/Private/Interactables/RelocationSubsystem.cpp (deploy at runtime), TripwireActor /
  TripwireRules (Interactables). The PMC already avoids its own charges (PmcEncounterNav filter, PmcOwnedChargesSmoke).
- Where to lay: a pure rule `PmcDefenceRules::PickTrapSpots(...)` - on the navmesh between the attackers' approach (the arena's
  defence approach / SetDefenceApproach) and the defence posts, 8-25 m out, in a chokepoint when the tactical file has one,
  never on a mate's path to his post.
- Counts in Content/Data/AI/pmc_operators.json (e.g. "traps": {"mine": 1, "tripwire": 1}); arena key `Traps=0|1`.
- Laid by a defender walking there (an order step before Ambush), so it costs time - no teleporting traps.

## Tests / checks
- Unit: PickTrapSpots (approach side, distance band, not on mates' paths, count limit).
- Worker build + CodexTactics.AI tests + the 19 Pmc smokes + PmcOwnedChargesSmoke: no new FAIL.
- Paired A/B on X6 (seeds 1060-1359, same machine, Traps=0 vs 1) vs the base of that day. Accept: wins >= base, captures <= base.

## Report (docs/port/grok/outbox/G015-...report.md)
Wins / captures / kills vs the base + skill card: traps laid per round, attackers killed / wounded by traps, PMC hurt by own traps
(must be 0), time from round start to traps laid, defenders' time in cover.
