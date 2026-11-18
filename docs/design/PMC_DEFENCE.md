# PMC defence - roles, setup phase, mines (from the designer's clip notes 2026-11-18)

Source: `docs/port/reports/designer-tactics-notes-2026-11-18.md` (notes 1, 1b, 2, 3). Code: `PmcCommanderRules` (utility commander,
weights in `pmc_commander.json`), `SquadOrderCatalog` (shared operative orders), `TacticalMapSubsystem` (TACTICAL_MAP.md), the player's
deploy / defuse code (`RelocationSubsystem` deploy tasks, `ADeployableActor` defuse). Designer decisions (2026-11-17 / 11-18) are binding.

## Diagnosis (night data, worker report 2026-11-18)
- Objective is 17-23 % of all PMC orders: every defender walks INTO the capture circle -> the whole squad bunches in one corner.
- No role split for defenders: no sector per fighter, no "guard the roof access" post, no spacing between fighters' covers.
- No placement orders: PMC cannot lay a mine / tripwire; attackers cannot react to a mine (defuse / detour / accept the risk).
- The X4 QD clip cannot be read: the camera frames the whole map (already the worker session's task - clip camera on X4).

## D1 Defender roles (roof and ground)
When a PMC squad defends a point (arena objective rounds; later campaign posts), the commander assigns ROLES once at round start and
re-assigns only when a fighter dies:
- **Access guard** (1 fighter): the roof access nearest the point (`overwatch_roofs[].access[]`: stair / hatch / ladder).
  - Inside access (stair / hatch): stand facing the hatch / stair head within `guard_dist_m` (JSON, ~4 m), weapon aimed at it.
  - Outside ladder: stand at the roof edge above the ladder top so the ladder AND the adjoining street are in the fire sector
    (spot = ladder top offset inward, look = the street direction from the analysis).
  - On the ground: the guard covers the doorway the attackers are most likely to use (D3 route).
- **Sector shooters** (the rest): the approach arc toward the attackers' general direction (defenders know it) split into N equal
  sectors; each shooter takes the best edge / cover spot inside his sector (roof edge spot from `overwatch_roofs[].spots`, else a cover
  slot), fires DOWN on approaches, and shifts along the edge after `shift_after_s` or when suppressed (the existing Relocate order with a
  sector constraint).
- The Objective order for a defender becomes "stay inside the defended AREA (roof / point radius x `area_factor`)", not "stand on the
  point": its score is 0 while he is inside the area, so it never pulls everyone to one spot.
- **Spacing:** two defenders never share one cover object and keep >= `min_spacing_m` (JSON, ~5 m); the cover score of a slot within
  `min_spacing_m` of a teammate's claimed slot is multiplied by `spacing_penalty`.

## D2 Setup phase (defenders before contact)
From round start until the first sighting (or `setup_max_s`), defenders with charges run a setup plan, then take their D1 posts:
- **Mines on the approaches:** points on the straight line attacker start -> point (route lines / chokepoint exits from the tactical map),
  `mine_spacing_m` apart, never on the defenders' own retreat line.
- **Doorway mine:** the doorway the attack route enters (tactical map doors on the route).
- **Roof access trap:** inside access -> mine or tripwire on the hatch / stair head; outside ladder -> none (the guard covers it).
- **Tripwire on the flank** of the defenders' own position (the approach with the shortest covered path to them).
- Mines do not forbid a straight run: they make it costly. Charges come from the squad's supply (see Q1).

## D3 Attackers vs mines
- An attacker spots a mine with the same reveal rule as the player's squad (3 m, revealed state shared within his squad).
- On a spotted mine on his path: either **defuse** (reuse the player's defuse task; time + success chance from the same JSON) or
  **detour** (path cost around the mine radius) - utility choice with weights; low time left / under fire -> detour or accept.
- An unspotted mine: the attacker's route scoring has a `mine_risk` term from the tactical map danger (chokepoint / doorway on the
  defenders' side = likely mined) so a careful attacker slows / checks before a doorway; weights decide (aggressive personalities rush).

## D4 Metrics (arena result JSON -> trainer, QD axes later)
`defender_spread_m` (mean pairwise distance of living defenders, sampled 1 Hz), `guard_near_access_share` (share of time a defender is
within `guard_dist_m` of the access), `setup_charges_placed`, `mine_kills`, `mines_defused`, `mine_detours`, `attackers_reached_roof`.

## Phases and lanes
| Phase | What | Who |
|---|---|---|
| P1 | D1 roles + sectors + access guard + Objective-as-area + spacing; D4 spread / guard metrics; smoke `PmcRoofDefenceSmoke` (Depot_1 roofs scenario: spread >= 5 m, guard near access > 60 %) | Grok (grok-4.7, L3) or API Opus patch -> Claude review |
| P1b (G008b) | Overwatch hold: the commander keeps a defender on an overwatch post until contact or `ambush_max_hold_s` (worker evidence 2026-11-18: X4 overwatch_kills = 0 because 203 posted defenders were pulled off after a median ~17 s while the threat was 120-155 m out, range 110; docs/port/reports/worker-overwatch-kills.md) | Grok after G008 |
| P2 | Deploy orders: `ESquadOrder::PlaceCharge` (mine / tripwire at location, reuses RelocationSubsystem deploy task) + `Defuse`; PMC supply; D2 setup plan; smoke `PmcSetupMinesSmoke` | Claude agent (cross-system: catalog + relocation + commander) |
| P3 | D3 attacker mine reaction + mine_risk term + metrics | Grok / API |
| P4 | Trainer: new metrics in the report, weights for the new terms in pmc_commander.json gene list | Antigravity (Python) |

## Designer decisions 2026-11-18 (all recommendations accepted)
- Q1 Charges: the PMC sapper role carries 2 mines + 1 tripwire, others 0 (mirrors the player's medic-sapper; arena JSON can override).
- Q2 Setup time: until first contact, capped at `setup_max_s` = 45 s (JSON).
- Q3 Lanes: P1 = Grok (grok-4.7) after G007, instead of G005 short orders (G005 moves after P1); Claude reviews + merges.
</content>
</invoke>
