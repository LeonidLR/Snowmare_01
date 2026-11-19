# Smart Enemies - audit of the code and the night training against the GDD

> **Status**: v1, 2026-11-19, Claude Fable (design architect session). Read-only audit: no code, smoke, map, trainer or data
> was changed and nothing was run.
> **GDD**: [smart-enemies.md](smart-enemies.md). **Plan**: [smart-enemies-roadmap.md](smart-enemies-roadmap.md).
> **Base**: main `fd2d13e3` (code = merge `80f7d55b` + later docs commits).

## 0. Method and limits

**Checked in the code (verified)**: the headers and the key functions named below, by reading them. Line numbers are of
main `fd2d13e3`. Short names: `SQ` = `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp`, `CR` =
`Source/CodexTactics/Private/AI/PmcCommanderRules.cpp`, `CRh` = `Public/AI/PmcCommanderRules.h`, `CAT` =
`Public/AI/SquadOrderCatalog.h`.

**Taken from reports (not re-measured)**: all run numbers - `docs/port/reports/worker-night-2026-11-18.md`,
`worker-night-2026-11-19.md`, `worker-overwatch-kills.md`, `docs/port/gemini/outbox/083-qd-coverage.report.md`,
`docs/port/grok/outbox/G008-pmc-defence.report.md`, `docs/design/PMC_TRAINING_ANALYSIS.md`, `docs/systems/pmc-squad-ai.md`.

**Not done (limits)**: no round was replayed; the Crossroads clip's own track / log was not read (its job id is not
identified yet), so section 4.1 separates what the code guarantees from hypotheses about that clip; the operative body
(`OperativeCharacter*.cpp`, 3500+ header lines) was audited by its API and by keyword search, not line by line; the monsters
were audited from `docs/systems/enemy-ai-and-perception.md`, the rule headers and the smoke list, not from the behaviour code;
the building generator branch was read as documents, not as code.

Status words: **EXISTS** (in main, with a test), **PARTIAL**, **MISSING**, **BROKEN** (in main, known not to work),
**UNVERIFIED** (probably inherited, no test proves it for enemies).

## 1. Summary

| GDD part | Exists | Partial | Missing / broken |
|---|---|---|---|
| Layer 0 terrain knowledge | tactical map file + queries | positions are geometric, ground-only, no building ids | validated firing positions; building graph at runtime; retreat options |
| Layer 1 body | cover, corner peek, stances, mantle, ladders, drops, suppress, heal, RPG | enemy use of mantle / window / drop links unproven | doors at runtime, glass, hearing of anything but gunfire |
| Layer 2 order catalog | 13 orders | - | corner peek, mantle, drop, door, grenade orders for the AI |
| Layer 3 fighter brain | 14-order utility with commitment, bands, attention | flank, fall back, ambush | cover-to-cover movement |
| Layer 4 squad plays | - | roof defence roles (G008), ambush posts at spawn | the layer itself; 12 of 15 plays |
| Perception / fairness | honest sight, stealth, shared belief | reaction model (attention only) | hearing, tells, reaction-time rule |
| Buildings | windows as spawn posts | stairs (flaky), roofs | every interior behaviour (PL-06..PL-09) |
| Monsters | pack, flank detour, scent, fire fear, suppression split, horde, drop links, ambush v3 | jumpers on tactical drop spots | doors rule, noise attraction to firefights as a designed situation |
| Encounters / Director | level JSON squads, 3 postures | generator posts (TS only) | spawn at the generated post, charge ownership, Director |
| Training | pipeline runs 14 h without crashes; QD archive; clips | metrics | fitness and descriptors that measure the designer's criteria; evaluation with enough rounds; any gate before a run |

Of the 21 dangerous situations of GDD section 8 (table in section 3): **2 work today** (DS-16 pack hunt, DS-18 RPG - both
older than the trainer), **7 partly** (DS-01, DS-04, DS-09, DS-10, DS-12, DS-14, DS-15), **12 cannot happen** with today's
code. None of the human-squad situations the designer listed on 2026-11-19 works. None of his three acceptance examples
(DS-00A Crossroads, DS-00B Alley, DS-00C Bridge) can happen: there is no kill-zone recognition, no crossfire selection, no cut-off (static
or by a run round a building) and no pre-contact "occupy positions" (0 matches for crossfire / anvil / cut-off in `Source/`).

## 2. Behaviour by behaviour

### 2.1 Layer 0 - terrain knowledge

| GDD item | Status | Evidence | Test |
|---|---|---|---|
| Tactical map file + queries (chokepoints, windows, roofs, drop spots, hiding spots) | EXISTS | `LevelGen/TacticalMapSubsystem.*`, `TacticalMapRules.*`; TACTICAL_MAP.md 6 | `TacticalMapSmoke`, `CodexTactics.LevelGen.TacticalMap.*` |
| Positions have a checked line of fire | MISSING | TACTICAL_MAP.md 6 "No runtime line traces", "line-of-sight checks for posts (geometric only)" | none. Consequence measured: 0 shots from 203 overwatch posts (`worker-overwatch-kills.md` 2); G008b rounds 2-4 could not make the roof man fire over the parapet (branch `grok/G008b-overwatch-hold`, head `d38a1289` "UNFINISHED ... overwatch_kills still 0") |
| Upper storeys / roofs as squad positions | PARTIAL | windows carry `floor`, roofs carry spots; "floors / roofs are not analysed as squad positions (the analysis is ground only)" (TACTICAL_MAP.md 6) | - |
| Positions know their building | MISSING | building-stories GDD 1: "`tactical/*.json` places carry no building id" | - |
| Cover objects as firing positions with a field of fire | PARTIAL | cover slots vs the nearest threat only: `FindCoverNear` `SQ:2376`, quality = wall between him and the threat (`CoverQualityAgainst`, `CRh:667`); corner bonus when the peek reaches (`SQ:2410`, `:2480`) - no "which area does this slot cover" | `PmcCommanderSmoke` |
| Building graph at runtime (rooms, stair heads, exits, opening states) | MISSING | generator-side only, on branch `claude/building-generator` (32 commits ahead of main, not merged); runtime R1-R4 "separate tasks" not started (BUILDINGS_TECH_SPEC 4, 7 on that branch) | `LevelGenBuildingsSmoke` (Codex 048) state not checked here |
| Predicted retreat / retreat options | MISSING | no code | - |
| Ground defence site (positions round a ground objective) | MISSING | `ResolveDefenceSite` resolves a site only on a roof: tactical overwatch roof (`SQ:3469-3482`) or a layout roof (`SQ:3483-3495`); PMC_DEFENCE D1 "on the ground: the guard covers the doorway" is not in `PmcDefenceRules.h` | `guard_share` = None in 1776 / 1776 X4 rounds (night 2026-11-19) |

### 2.2 Layers 1-2 - body verbs and the order catalog

The catalog has 13 orders (`CAT:19-48`): Move, TakeCover, LeaveCover, Stance, FirePosture, PriorityTarget, Suppress,
CeaseSuppress, Heal, Reload, SwitchWeapon, Hold, LauncherStrike.

| Verb | Body | AI can order it | Evidence / test |
|---|---|---|---|
| Walk / sprint | EXISTS | EXISTS (Move) | - |
| Take cover on a slot | EXISTS | EXISTS (TakeCover) | `PmcCommanderSmoke` |
| Corner peek / lean | EXISTS (`OrderCornerPeek`, `OrderRunCornerPeek`, `OperativeCharacter.h:1663-1686`) | MISSING as an order; PARTIAL in effect - a man in a corner slot leans out by his own fire (`SQ:1908`), corner slots get a bonus | no enemy peek test |
| Stances incl. prone at band distance | EXISTS | EXISTS (`SQ:2341-2344`) | `PmcStandoffSmoke` |
| Mantle / vault / window sill | EXISTS (`TryStartMantle`, `OrderMantleTop`, mantle nav links `Interactables/MantleNavigation.h:108`) | MISSING as an order; UNVERIFIED by pathing (0 mentions of mantle / vault in `SQ`, `CR`) | `MantleFenceRouteSmoke` is for the player's squad |
| Ladders | EXISTS | by pathing only: `SQ:121`, `:1637-1639` (no new order mid-climb) | `LadderEnemySmoke` (monster family), roof rounds |
| Drop links / jump down | EXISTS (`Falls/DropLink*`, `LedgeFallComponent`) | by pathing only; no "jump out of the window" decision | `DropLinkReachSmoke` (not PMC) |
| Doors: open / kick / breach | MISSING at runtime (opening-states GDD 5: every state degrades to open / blocked until R1); `DoorwayBreachRules` + `DoorwaySubsystem` give positions only; the player's hookup is not done either (TASK_BOARD R17-5) | MISSING (0 mentions of door / breach in `SQ`, `CR`, `SquadOrderCatalog.cpp`) | `DoorBreachSmoke` covers the rules |
| Suppress | EXISTS | EXISTS | `SuppressionSmoke`, `PmcUnderFireSmoke` |
| Hold fire / posture | EXISTS | EXISTS (`ShouldHoldFire`, `CRh:802`) | `PmcArenaObjectiveSmoke` |
| Heal | EXISTS | EXISTS | - ; known side effect: Heal walks the guard off his access (G008 report "Watch") |
| Grenade | EXISTS for operatives (`TryAIGrenadeThrow`, aimed at monster clusters, `OperativeCharacterWeapons.cpp:384`) | MISSING for squads: PMC carry 0 (`Content/Data/AI/pmc_operators.json:16`), "team-aware grenades are a follow-up" (pmc-squad-ai.md:44) | - |
| RPG | EXISTS | EXISTS (RpgStrike) | `RpgPmcSmoke` |
| Charges | generator places PMC charges (087, TS) | C++ does not read `owner: pmc`; PMC are not immune to / aware of their own charges (087 report "Left") | - |
| Attackers vs mines (spot, defuse, detour) | MISSING for the AI (PMC_DEFENCE D3 not started) | - | - |

### 2.3 Layer 3 - the fighter brain

| GDD item | Status | Evidence | Test |
|---|---|---|---|
| Per-fighter utility over 14 orders with commitment and hysteresis | EXISTS | `CRh:17-57`; `ScoreOrder` `CR:232-350`; the decision loop scores and picks per fighter: `SQ:1681`, `:1694` | `CodexTactics.AI.PmcCommander.*` (97 AI tests), `PmcStalemateSmoke` |
| Engagement bands, standoff, attention ("strike when they look away") | EXISTS | `CRh:183-191`, `:347-356` | `PmcStandoffSmoke` |
| **Cover-to-cover movement (PL-01)** | **MISSING** | Advance = one walk to a point on the fighter -> threat line: `SQ:1958-1972` (`Walk(AdvanceGoal, ...)`), the comment at `CR:251` calls it "bounding overwatch" but only limits it to one advancing mate at a time (`CR:259`); the Objective walk and FallBack are single walks too | none measures exposure. Symptom: "the fighters run for a long time" (designer, Crossroads clip); attackers' roof walks of 35-80 m replaced within 2-5 s under fire (pmc-squad-ai.md:682) |
| Flank with a safe route | BROKEN by design | the goal is a geometric point 90 deg off the threat (`FlankPoint`, `CR:575-586`), walked directly (`SQ:1974-2007`); only the attention of seen enemies picks the side (`SQ:1996-1999`); no exposure check | 3,114 flanks attempted, 48 succeeded = 1.54 % (PMC_TRAINING_ANALYSIS 4.1); `flanks_completed` mean 0.10-0.13 per side-round (083 report 1.1) |
| Nobody rests in the open (P1) | **MISSING** | standing in the open is a legal resting state: Hold scores `W.Hold * 0.3 * 0.5` in the open without contact (`CR:237`); TakeCover needs a real (not stand-in) threat and a found slot (`CR:246`); Crouch needs contact (`CR:311`); Ambush is the only pre-contact position order and see the next line | none |
| Pre-contact waiting on a position | BROKEN | Ambush = `W.Ambush * Step(!S.bEverContact) * ...` (`CR:301`); `bEverContact` is set by any known enemy at any distance (`SQ:386`, `:799`) and is forced true for an assaulting team (`SQ:1561`). On X4 the posted defenders knew a threat 120-155 m away at t = 0, Ambush scored 0, and they left the post after a median 17 s (`worker-overwatch-kills.md` 2-3) | G008b commit `07c27808` "hold overwatch defenders until the post's range" is on the parked branch, not in main |
| Fall back | PARTIAL | one walk away from the threat, tactical-map aware (`chokepoint_avoid`); "only 10.8 % of retreats were survived" (PMC_TRAINING_ANALYSIS 3.3); no layers, no covering mate | `tactical_fall_backs` stat |
| Relocate after being spotted | EXISTS | `CR:287-296` | - |
| Stable facing | BROKEN (open bug) | TASK_BOARD B17-5: PMC marksmen "shake" left-right on seeing the squad | - |

### 2.4 Layer 4 - squad plays

There is **no squad layer**. The "commander" is a per-fighter scorer that sees four counters of what mates do
(`MatesSuppressing / Flanking / Advancing / Healing`, `CRh:531-535`). The only role logic is G008's roof defence.

| Play | Status | Evidence |
|---|---|---|
| PL-01 Bound | MISSING | 2.3 |
| PL-02 Occupy positions | MISSING | 2.3 "nobody rests in the open"; posts are given only at spawn (`SpawnSquadTactical` `SQ:3151`, `AssignTacticalPosts` `SQ:3232`) |
| PL-03 Hammer and anvil | MISSING | no crossfire, no retreat prediction anywhere |
| PL-04 Ambush at the exit | PARTIAL | spawn on `BestAmbushSpots` (`GatherPostCandidates` `SQ:2940`), hold fire by `ShouldHoldFire`; broken by the contact rule above; `ambush_success` mean 0.14 per side-round (night 2026-11-18 4.1) |
| PL-05 Hold a roof / point | PARTIAL | G008 D1: one access guard + sector shooters + spacing (`PmcDefenceRules.*`, `SQ:3526`); passes `PmcRoofDefenceSmoke` on the Depot_1 roof (3 seeds); on X4 tactical no guard in any round and spread 19-50 m "with no trend" (night 2026-11-19) |
| PL-06 Garrison / window fire | PARTIAL (spawn only) | a window post from the tactical map, standing (`SQ:2227`); no duck / shift, no choice of windows after spawn, no door / stair guard; `window_positions_used` mean 0.89 |
| PL-07 Fall back upstairs | MISSING | no storey in the belief, no hearing, no stair-head position |
| PL-08 Escape (window, fire escape) | MISSING | - |
| PL-09 Enter and clear | MISSING | no door code in the AI; no room concept |
| PL-10 Take the height | PARTIAL | cover slots up to 3 levels above get the `high_ground` bonus (`SQ:2503-2504`, `CRh:279`); no check of field of fire / access / escape; roof posts cannot fire (2.1) |
| PL-11 Suppress and move / flank | PARTIAL | Suppress scores higher while a mate moves (`CR:272`); `suppress_then_move` mean 1.38; flank broken (2.3) |
| PL-12 Fall back by layers | MISSING | - |
| PL-13 Hunt | PARTIAL | posture Hunt with a "radio intel" search point, "a deliberate cheat (no hearing model yet)" (pmc-squad-ai.md:426); `PmcEncounterHuntSmoke` |
| PL-14 Bait | MISSING (parked by the designer) | - |
| PL-15 Hold a chokepoint | PARTIAL | `terrain_danger`, `choke_leave_danger` terms; `TacticalPmcSmoke` |

### 2.5 Perception, fairness, readability

| GDD item | Status | Evidence |
|---|---|---|
| Honest sight, shared team belief with memory, stealth both ways | EXISTS | `UpdateSight` `SQ:680-900` (stealth `:740-751`, honest fire `:754-776`); `PmcFireLineSmoke` |
| Hearing | PARTIAL | gunfire only, 60 m (`SQ:66`); no steps, doors, glass ("no hearing of PMC steps", pmc-squad-ai.md:424) |
| Reaction time before the first shot | MISSING as a rule | attention factor only; no `t_react` |
| Tells (glint, tracks to positions, voices) | MISSING | KILL_ZONE_AMBUSH.md is a draft |
| No spawn in view / no teleport | EXISTS | encounter spot "out of the squad's view" (`PmcEncounterRules.h:55-60`); hybrid relocation for monsters' ambush |
| The player's stand-in sees human enemies | BROKEN | "`USquadAutonomySubsystem` and the playtest bot still do not perceive hostile operatives as threats" (pmc-squad-ai.md:424-425); bot rounds on X4: closest approach 38-68 m, 195-209 s of silence (night 2026-11-18 4.4) |
| Turn-based combat | MISSING | "turn-based combat ignores them" (same lines) |

### 2.6 Buildings

| GDD item | Status | Evidence |
|---|---|---|
| Walk every stair flight | PARTIAL | `LevelGenRampSmoke` FAIL on the 2026-11-18 sweep (stuck at the top of `B_warehouse_stair_0`); test X6: 17 of 18 flights after give-way v3 (TASK_BOARD 2026-11-19); open: a roof objective got a 2-point straight path although the navmesh had a route (pmc-squad-ai.md:630) |
| Interiors with rooms, cores, opening states | on the generator branch only | `claude/building-generator` not merged; S2 rooms not started |
| Doors, glass, breach at runtime (R1-R3), AI costs (R4) | MISSING | BUILDINGS_TECH_SPEC 4 on the branch |
| Enemy decisions inside a building (R-B1..R-B8) | MISSING | 0 building / room / door logic in the AI files |

### 2.7 Monsters

| Instinct | Status | Evidence / test |
|---|---|---|
| Pack roles (direct / flank detour / fall back to the pack) | EXISTS | `Characters/EnemyTacticsRules.h`; `CodexTactics.Characters.EnemyTactics.*` |
| Scent tracking | EXISTS | `AI/ScentRules.h`; `HoundScentSmoke` |
| Fear of fire; pack splits round a suppression zone | EXISTS | `EnemyAIRules::FleeDirection`, `FireDetourWaypoint`; `SuppressionSmoke` (green again after Codex 047) |
| Horde pressure | EXISTS | `Combat/HordeRules.h`; `HordeSmoke`, `HordeFirefightsSmoke` |
| Ambush (coordinated, watch-aware, hybrid relocation) | EXISTS | `Combat/Ambush*Rules.h`; `CombatLabAmbushSmoke`, `CombatLabAmbushRelocateSmoke` |
| Drop links per archetype (never / free / jumper) | EXISTS | `falls.json enemy_drops`, enemy-ai-and-perception.md:176 |
| Jumpers placed on the tactical map's drop spots | MISSING | TACTICAL_MAP.md 7 "PMC jumpers (no archetype allowed)"; placement of monsters by the analysis is F-4b "later" |
| Ladders: mutants never, zombified soldiers / marksman yes | EXISTS | `LadderEnemySmoke` |
| Monsters vs PMC | EXISTS | `pmc_operators.json` `factions` |
| Doors and monsters | decided 2026-11-19 (OQ-14): closed doors stop mutants, a horde breaks one in T s; not built yet | OQ-14 |

### 2.8 Encounters and the Director

| GDD item | Status | Evidence |
|---|---|---|
| Level squads with Defend / Hunt / Patrol | EXISTS | `PmcEncounterRules.h:13-41`; `PmcEncounterSmoke`, `PmcEncounterHuntSmoke` |
| Defend squads on tactical posts along the player's way | EXISTS | `PmcEncounterSubsystem.cpp:326-329` |
| Generator-placed squads and charges in the game | PARTIAL | TS + builder done (merge `cd7af575`); `ParseSquad` does not read `post`; no accepted layout has a `pmc` block; only `L_Gen_Depot_1.json` has `pmc_squads` |
| Garrisons by building role | MISSING | roles exist on the generator branch only |
| AI Director | MISSING | AI_DIRECTOR.md is a draft; no Director class in `Source/` |

## 3. Dangerous situations (GDD section 8)

| Id | Can it happen today? | Why not |
|---|---|---|
| DS-00A Crossroads | no | no PL-02, no crossfire, no retreat prediction, no fire-hold that survives distant contact |
| DS-00B Alley | no | no kill-zone recognition; nothing sends a man onto the taller roof (height is only a bonus on cover slots near him, `SQ:2503-2504`; roof posts come from the tactical map at spawn and cannot fire, 2.1); a "flank" is a point `flank_distance_cm` 10 m from a SEEN threat (`CR:267`, `CR:575-586`), not a run round a building to a rear exit before contact; a ground goal pulls the squad together (4.1) |
| DS-00C Bridge | no | as DS-00A, plus - option 2: the AI cannot order a climb (no mantle entry among the 13 catalog orders, `CAT:19-48`; 0 mentions of mantle in `SQ` / `CR`) and obstacle tops are not positions (the body can do it: `OrderMantleTop`, `OperativeCharacter.h:526`); option 1: no interior behaviour, no window-to-window shift, no window exit (2.4 PL-06 / PL-08, 2.6). The victims' drop off the bridge exists (drop links) |
| DS-01 Window fire | by spawn luck only | window post at spawn; no duck / shift; sectors unvalidated |
| DS-02 Roof overwatch | no | 0 shots from roof posts |
| DS-03 Upstairs | no | no hearing, no storeys, no stair head |
| DS-04 Chokepoint exit ambush | partly | broken by the contact rule; works at short range |
| DS-05 Crossfire from two buildings | no | no two-team commander, no garrison |
| DS-06 Bait | no | parked |
| DS-07 Flank through a building | no | flank is a geometric point |
| DS-08 Layers | no | - |
| DS-09 Mined approach | data only | charges generated, not owned / used by the AI; attackers ignore mines |
| DS-10 Roof defence with a guard | on the Depot_1 smoke roof | not on generated maps |
| DS-11 Window escape and return | no | - |
| DS-12 Hunt squad | partly | hunts by a cheat, moves in straight walks |
| DS-13 They storm your building | no | - |
| DS-14 Dinner bell | partly | monsters attack PMC; no designed attraction to firefights |
| DS-15 Drop ambush | partly | drop links yes, placement on drop spots no |
| DS-16 Pack hunt | yes | - |
| DS-17 Pinned with the horde | no | needs DS-02 |
| DS-18 RPG on cover | yes | - |

## 4. Why the night training produced bad behaviour

### 4.1 The designer's three clips (Crossroads, Alley, Bridge)

What the designer saw. Clip 1: the attackers ran a long way in the open; the defenders stood in the middle of the crossroads
without cover and let them through. Clip 2: the attackers ran down a lane between two buildings; the defenders stood bunched
between the buildings next to the goal marker ("GOAL 2" is visible in his screenshot), nobody on the taller roof, nobody at
the corner, nobody behind the attackers. Clip 3: the attackers crossed a bridge over a cut unhindered; nobody held its
ends, nobody used the building beside it or the obstacle at its far end.

**The common pattern (his words and the code agree):** the failure is position selection - kill-zone recognition, role
assignment, pre-occupation - not shooting. RC1, RC3 and RC4 below are that failure seen from three sides.

**Guaranteed by the code (verified):**
1. Nothing can produce the expected ambush: no code chooses two crossing positions, predicts a retreat or places a cut-off.
2. A defender without a post has no order that takes him to cover before he *sees* an enemy: TakeCover needs a real threat
   (`CR:246`), Ambush without a post only crouches where cover was found near him and is switched off by the first distant
   contact (`CR:301`, `SQ:386`).
3. Standing in the open is a positive-scoring resting state (Hold, `CR:237`), and inside the defended area the Objective
   order is 0 (G008, `CRh:113`), so on a ground objective nothing pulls him anywhere.
4. A ground objective has no defence site (2.1), so G008's guard and sector spots do not exist there.
5. The attackers' approach is a straight walk (2.3).

**Hypotheses about that round (to check on its track + log; roadmap task T-01):**
- H1 the round was a ground-objective scenario (street / tactical route end), so cases 3 and 4 applied to all three defenders.
  Clip 2 supports it by eye: the defenders stand at the goal ring on the ground; with a posted defence only the occupier is
  pulled onto the point (`DefenderPointPull`, `CRh:621`), but a ground goal has no site, so nobody is "posted" and everyone
  may be pulled - to confirm in the round's order log;
- H2 the defenders had tactical posts along a long route and left them when `bEverContact` turned true at long range;
- H3 no cover slot was found near them (`bCoverAvailable` false): the crates of that arena may not be cover objects, or lie
  outside the search radius;
- H4 the "let them run through" part is `ShouldHoldFire` or the attention rule keeping them passive, or the two sides not
  being in each other's weapon band.
Each hypothesis has one observable in the round's JSON (scenario, `tactical.posts`, order sequence per fighter, first-shot
times); none needs a new run.

### 4.2 Root causes, ranked

| # | Root cause | Evidence | Kind |
|---|---|---|---|
| RC1 | **The expected behaviours are not in the game, and training cannot add them.** The brain is a per-fighter score over 14 orders; the trainer moves ~40 numbers of it. Roles, crossfire, retreat prediction, occupying positions before contact, building use are absent as code. | section 2.4; 0 matches for crossfire / cut-off in `Source/`; genes = numeric leaves of `pmc_commander.json` (PMC_TRAINING_ANALYSIS 3.1) | missing behaviours |
| RC2 | **Movement is a straight walk.** Advance, Objective, Flank and FallBack are single navmesh walks to a geometric point; nothing prices exposure. | `SQ:1958-1972`, `:1974-2007`; flank success 1.54 %; retreats survived 10.8 %; "run for a long time" | missing foundation |
| RC3 | **Pre-contact behaviour collapses and the open is a legal place to stand.** The only waiting order dies on the first distant contact; nothing sends an unposted man to cover; Hold in the open scores. | `CR:301`, `SQ:386 / :799 / :1561`, `CR:237`, `CR:246`; posts left after a median 17 s with the threat 120-155 m out | scoring / missing play |
| RC4 | **Positions are not validated and half of them do not exist.** Posts are geometric: roof men cannot fire over the parapet (0 shots from 203 posts; G008b unfinished after 4 rounds); a ground objective has no site (guard None in 1776 / 1776); cover slots do not know what they cover; positions have no building. | 2.1 | broken foundation |
| RC5 | **The trainer measures the wrong thing, with too little signal, against the wrong opponent.** Fitness = win, kills, damage, survivors in a PMC-vs-PMC mirror (`Scripts/Trainer/trainer_config.json` `fitness`); none of the designer's criteria (idle in the open, positions before contact, crossfire, guard, spread) is in it. QD axes are contact time, move count and ambush hits - not tactics - and were mis-scaled twice (2 / 48 cells, then mobility bins 2-3 unreachable). 6 rounds per candidate (`evaluation`: 2 jobs x 3 rounds), `promotion_margin` 0: the champion changed 25 times in 37 generations; in pmc-v5 fresh fitness of new champions fell 18.5 -> 9.1. The bot leg is blind (2.5). | reports of 2026-11-18 / 11-19; 083 report; memory note 2026-11-12 (pmc-v4: champion 33 % win vs baseline 75 %) | metrics / fitness |
| RC6 | **Buildings cannot be used.** No interior behaviour, no runtime doors, stairs still flaky, building data not merged. | 2.6 | missing dependency |
| RC7 | **No gate before a run.** A 14.5 h run (1776 rounds) started on code whose two new metrics were dead on that map (`guard_share` None, `overwatch_kills` 0 - the second known since the morning before). | night reports | process |
| RC8 | **Old known findings were never closed.** Flank route safety, advance / fall-back yo-yo, self-play ceiling were written on 2026-11-04 (PMC_TRAINING_ANALYSIS 5-6); the designer disabled the trainer on 2026-11-12 with "fix evaluation before any training"; runs resumed on 2026-11-17 while the committed config still has 6 rounds per candidate and promotion margin 0 (whether other evaluation fixes landed was not checked here). | those documents; `trainer_config.json` `evaluation`, `evolution.promotion_margin` | process |

RC1-RC4 mean that even a perfect trainer would have nothing good to find; RC5 means that if good behaviour existed the
trainer would not prefer it. The night result was therefore expected, not bad luck.

### 4.3 What worked (keep)

The arena, seeds and replay; 0 crashes in 1776 rounds; the 11 behaviour metrics arrive; the QD archive mechanics; the clip
camera after `5915bd8a`; honest sight; commitment (re-decide 1 <= 6 in `PmcStalemateSmoke`); G008's roles on a roof; the
tactical map export; the shared body. The roadmap builds on all of them.

## 5. Verified versus assumed

| Statement | Basis |
|---|---|
| No squad layer, no crossfire / retreat logic, no door logic in the AI | verified (read + search) |
| Advance / Flank are straight walks to geometric points | verified (code) |
| Ambush gated by `bEverContact`, set at any distance | verified (code) + report numbers |
| Ground objectives get no defence site | verified (code); matches `guard_share` None |
| Roof posts cannot fire because of the parapet | from the G008b branch log and reports; not re-traced |
| Enemies can use mantle links / window sills by pathing | assumed from the shared body; no test |
| The Crossroads round's exact cause | hypotheses H1-H4, not verified |
| Monster rows | from documents and smoke names, not code reading |
| Building generator state | from the branch documents |
