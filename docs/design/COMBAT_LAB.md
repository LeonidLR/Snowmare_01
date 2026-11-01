# Combat lab - the squad moving while it fights (L_FormationLabCombat)

Game designer decisions 2026-11-01 (branch `formation/combat-lab`). A COMBAT copy of the formation lab to test the squad's movement while it
fights - the new kind of fight after the tutorial's horde: (1) running into an enemy PATROL (a meeting engagement), (2) being AMBUSHED.
UE-only, no Godot reference. The wider "smart enemies" work (lures, bait-and-flank, enemy cover AI) is POSTPONED by the designer: only
patrols + ambushers here.

## 1. The map

| What | Where |
|---|---|
| Map | `/Game/Maps/L_FormationLabCombat` (not a campaign map, not cooked; `L_FormationLab` and its smokes are unchanged) |
| Layout | `Content/Data/FormationLab/Combat/formation_lab_combat_01.json` = an OVERLAY (`codex.formation_layout_overlay/1`): `"extends": "../formation_lab_01.json"` (the lab's walls / zones / routes / spawn stay the ONE source of truth) + `towers`. In a subfolder so the Training Ground simulator does not list it as a layout. |
| Builder | `Scripts/Editor/build_formation_lab.py` (the lab builder; overlays + towers added, the 01 plan unchanged byte for byte): `CODEX_LAB_LAYOUT=<abs>/Content/Data/FormationLab/Combat/formation_lab_combat_01.json CODEX_LAB_TARGET=/Game/Maps/L_FormationLabCombat`, then `Scripts/bake_navmesh.ps1 -Map L_FormationLabCombat` (tiles Default 148). Report `Saved/Logs/FormationLabCombatLayout.txt`. |
| Level JSON | `Content/Data/LevelJson/L_FormationLabCombat.json` (found by the map name): `combat_start: ambush`, `ambush_fight: skirmish`, `susanin_wave: 0`, `horde_enabled: false`, `cold_rate_mult: 0`, `intro_dialogue: false`, `cheats`, `pressure`; one dummy wave (the file must hold a valid wave; never played). |
| Towers | 4 solid blocks 3 x 3 x 4.0 m (11 rungs x 36.4 cm, a walkable roof), each with an `ALadderActor` 10 cm off one face (3 wall brackets, nav link): `T_SW` (-5200, -2200) ladder +x, `T_NW` (-5200, 3800) +x, `T_NE` (2200, 5450) -y, `T_SE` (5300, -200) -x - spread over the lab, >= 8 m off the lab lanes. Mutants never take a ladder (LADDERS.md: only the Marksman / PMC Fighter / Military Zombie climb), so a squad on a roof is safe from them. |

## 2. Enemy pressure (endless)

`UCombatPressureSubsystem` (world subsystem, active when the level JSON has a `"pressure"` block). Pure rules `CombatPressureRules`
(`CodexTactics.Combat.Pressure.*`) and `AmbushWatchRules` (`CodexTactics.AI.AmbushWatch.*`); parsing `LevelJsonRules::ParsePressureSettings`
(`CodexTactics.LevelJson.Pressure`).

- **Schedule.** The first event at `first_event_sec` (**30**; designer answer 6, 2026-11-01 - was 15), then one every `interval_sec` (**120**). A group of `group_min`..`group_max`
  (**1-3**) enemies; at most `max_alive` (**4**) pressure enemies alive - a due event WAITS while the cap is full and fires as soon as there is
  room. Turn-based combat stops the clock. No valid spot (the squad in a tight corner): retried in 10 s.
- **Types.** Hound, Frostbitten, Spitter, Cutter, Marksman by the `types` weights (equal by default). NEVER the Brute or the Cryo Drone (refused by
  the parser, dropped by the rules).
- **Kind.** `patrol_share` (0.6) patrols, the rest ambushes; seeded (`seed`), so a run is reproducible.
- **Spawn spot (both kinds).** Never inside or near a wall: at least `wall_clearance_cm` (250) from every wall / tower box (the level's blocking
  static meshes >= 50 cm tall, read as oriented boxes), on the navmesh at ground level (never a roof), inside the navmesh bounds, reachable on
  foot, not on top of the squad (patrols >= `min_squad_distance_cm` 25 m, ambushers >= 14 m), mostly ahead of the squad's way. Scored
  (`CombatPressureRules::ScoreSpot`): patrols prefer out of the squad's sight and looks; ambushers MUST be concealed.
- **Patrols.** The group's leader walks a loop of `patrol.points` (4) waypoints `patrol.radius_cm` (12 m) round the spawn (a runtime
  `APatrolRouteActor`; waypoints near walls dropped), pausing `wait_sec`; the others escort him (`AEnemyCharacter` escort). Their own
  perception (enemy_perception.json) finds the squad -> Patrol -> Engage -> the fight starts (or the squad sees them first and fires).
- **Ambushers.** A HIDING SPOT (section 2.1) 14-30 m from the squad, ahead of its way, that NO operative has a line of sight to; the group stands
  there (a guard post, its own senses MUTED - only the watch rule decides). Each 0.1 s: the squad beyond `strike_range_cm` (18 m) -> wait; in
  range and someone looks its way -> HOLD; in range and nobody looks its way -> STRIKE (Patrol -> Engage at the nearest operative, its own
  senses back). "Looks its way" (`AmbushWatchRules`): each living member's body yaw AND his formation sector look (FORMATION_SECTORS.md) count;
  a look covers +-`watch_half_angle_deg` (45) within `watch_range_cm` (50 m). So the squad turning its back to it (or a rear guard looking the
  other way) is what lets it strike; an all-round watch never gets ambushed. Shot at / spotted first, the whole group fights at once.
  Since v2 (answer 4) a member's AIM counts as one more look: an operative aiming at / fighting ANOTHER enemy still watches - an ambush inside
  his aim cone (+-45 deg of the bearing to the enemy he aims at, `AOperativeCharacter::GetAimTargetPoint`) holds (`AmbushWatchRules::MemberLooks`:
  body, aim, formation sector look; looks within 5 deg of one another are one).
- **The fight.** Pressure enemies are level enemies spawned at runtime (`UWaveSubsystem::SpawnLevelEnemy` - not part of a fight until they
  engage; `ULevelEncounterSubsystem::MarkLevelEnemy`), so the tutorial's exploration -> combat start is reused: the first contact starts the
  real-time SKIRMISH (`NotifyHostileContact`), every later contact joins it, the last kill ends it straight in exploration (no panel). The squad
  keeps its formation in the fight (`Codex.Squad.CombatFormation`) and takes move orders as usual.
- **Recycling.** An unengaged pressure enemy (still patrolling / hidden) farther than `recycle.distance_cm` (60 m) from every operative,
  unseen, for `recycle.after_sec` (60 s) is removed - the endless map never clogs with enemies left behind.

### 2.1 Ambushes v2 (game designer answers 2026-11-01, branch `formation/combat-lab-v2`)

Pure rules `AmbushSpotRules` (`CodexTactics.Combat.AmbushSpots.*`), `AmbushPlanRules` (`.Combat.AmbushPlan.*`), `AmbushRelocateRules`
(`.Combat.AmbushRelocate.*`), the aim look `AmbushWatchRules::MemberLooks` (`.AI.AmbushWatch.AimCounts`); parameters in the level JSON
`"pressure"."ambush"` (`LevelJsonRules::ParsePressureSettings`, `CodexTactics.LevelJson.Pressure`); smoke `CombatLabAmbushSmoke`.

- **Hiding spots (answer 1).** An ambush hides only at a candidate spot, read once from the level's own walls (the same obstacle boxes as the
  clearance rule) plus hand-placed points:
  - **wall ends** - per FREE end of a wall box >= `spots.min_wall_length_cm` (150; posts / ladder rails are no walls), one spot on each face,
    `spots.end_inset_cm` (100) inside the end and `spots.wall_standoff_cm` (260) off the face. An end that runs into another box within
    `spots.joined_gap_cm` (30) - a corner, touching pieces - is no end;
  - **doorways / gaps** - two free ends of parallel walls facing each other `spots.doorway_min_cm`..`doorway_max_cm` (80..600) apart: their
    jamb spots are doorway spots (the lab: lane C's 1 / 2 / 3 m gaps, the V2 run's 4.5 / 6 m gaps; the 24 m "big gap" is open ground);
  - **authored points** - the level JSON `"ambush"."points": [[x, y], ...]` (world cm) and any actor in the map tagged `CodexAmbushPoint`
    (none placed yet: the map is not rebuilt). The lab has 8, behind walls off the lanes (W_A_1 east, W_B_3 north, W_C_4 south, W_D_11 south,
    W_E_13 south, W_V2_17 south, W_G_20 east, W_F_15 east).
  Spots within `spots.merge_cm` (100) are one (authored > doorway > wall end). The SELECTOR is unchanged: among the candidates 14-30 m from the
  squad that are concealed, on the navmesh at ground level, >= the clearance from every wall and reachable on foot, the best by
  `CombatPressureRules::ScoreSpot` (ahead of the squad's heading strongly, the middle of the band) + the kind's bonus (`spots.authored_bonus`
  0.5, `spots.doorway_bonus` 0.25). The hiding clearance is min(`wall_clearance_cm`, standoff - 10) = 250 cm, so the 2.5 m rule of every
  spawn still holds. No candidate in reach: a patrol instead (as before).
- **Coordinated ambushes (answer 2).** `ambush.coordinated.share` (0.35) of the ambush events are a PLAN of `bodies_min`..`bodies_max` (4-6):
  four PARTIES - flank left, flank right, front, rear (bodies 0-3, the extras to the front, then the rear) - each at its own concealed
  hiding spot in its SECTOR round the plan's anchor (`anchor_ahead_cm` 5 m ahead of the leader; front +-45 deg of the heading, the flanks
  45-135 deg, the rear beyond), the spot nearest its slot (`flank_offset_cm` 15 m to the side, `front_offset_cm` 16 m, `rear_offset_cm` 15 m),
  10-30 m from the squad (`min_squad_distance_cm` / `max_squad_distance_cm`), the parties >= `min_separation_cm` (6 m) apart. No complete
  layout (four sides) or no room: a single ambush instead. At least `min_ranged` (1) ranged member (Spitter / Marksman by the type weights; a
  melee front, then rear, body is swapped). Phases (`AmbushPlanRules::NextPhase`):
  1. **Waiting** while the nearest operative is beyond `trigger_range_cm` (18 m) of the anchor; **Holding** in range while both flank sides
     are watched (the watch rule per flank party);
  2. **Flank block** as soon as a flank side is unwatched (or any party is spotted / shot at): both flank parties strike - they close off the
     squad's sides - and EVERY ranged member of the plan strikes with them and SUPPRESSES: `USuppressionSubsystem::SetSuppressIntent` on its
     current target (refreshed every 0.1 s), so its rounds count deliberate (x `weight.deliberate_multiplier` 1.5, SUPPRESSION.md section 6)
     - the squad is pinned down while the others flank;
  3. **Assault**: the rear and the front strike once `assault_min_sec` (1.5 s) passed and every living flank member is within
     `block_distance_cm` (9 m) of an operative, at the latest at `assault_max_sec` (4 s).
  A single-spot ambush's ranged members suppress the same way when they strike. **Alive cap (chosen):** the plain events keep `max_alive` 4;
  a plan has its OWN cap `coordinated.max_alive` (**6**): it spawns only while the pressure enemies alive + its bodies stay within 6 (else a
  single ambush), and while more than 4 are alive the plain timer events wait as before.
- **Relocation (answer 3).** A hidden single group is BYPASSED while the squad is beyond its strike range and it lies behind the squad's way
  (> `relocate.behind_angle_deg` 100 deg off the heading) or farther than `relocate.far_cm` (35 m). Bypassed for `relocate.after_sec` (15 s)
  and no operative has a line of sight to any member, it MOVES to the best concealed hiding spot ahead of the squad (cos >= `min_ahead_cos`
  0.2, >= 6 m from where it hid): the group is put on the new spot at once (an unseen hop - nobody can see it go: the squad's sight hides
  every enemy no operative sees) and its guard post moves along (`AEnemyCharacter::MoveGuardPost`). Seen, it waits until unseen; no spot ahead,
  it retries after another 15 s (the 60 m / 60 s recycling stays the last resort). A waiting coordinated plan that is bypassed is laid out
  afresh round the squad and each party moves there; with no layout its parties go on as single groups. `relocate.enabled` false = the first
  version (wait until recycled).
- **The watch (answer 4)** stays +-45 deg; aiming counts (above). **Immortality (answer 5)** keeps the knockdown of a heavy hit. **Save / load
  (answer 7)** restarting the pressure is fine.

## 3. Options (cheats)

`CombatCheats` (`CodexTactics.Combat.Cheats.Resolve`): **immortality** of the squad (an operative loses no health at all - hits, blasts, bleeding,
burning, falls; hits still knock down) and **infinite ammo** (no shot empties the clip; no reloads). The level JSON `"cheats": {"immortal": true,
"infinite_ammo": true}` turns them on (L_FormationLabCombat: both); console `Codex.Cheat.Immortal` / `Codex.Cheat.InfiniteAmmo` = -1 (the level
decides, default) / 0 off / 1 on. Development / editor builds only: in Shipping the console variables do not exist and every cheat is off.

## 4. Console (dev builds)

| Command | What |
|---|---|
| `Codex.Pressure.Spawn [patrol\|ambush\|coordinated] [count]` | a spawn event now (the seeded pick without arguments; ignores the caps; `coordinated` = a plan of 4-6 round the squad, `count` its bodies) |
| `Codex.Pressure.Clear` | removes every pressure enemy |
| `Codex.Pressure.Status` | the state in the log (alive, next event, each group with its hiding spot / plan party, the ambushers' states, every plan's phase, every relocation, the cheats) |
| `Codex.Pressure.Enabled` | -1 = the level decides, 0 off, 1 on |
| `Codex.Pressure.IntervalSec` | seconds between events; <= 0 = the level's 120 |

## 5. Smoke

`CodexTactics.FormationLabCombatSmoke` (`Scripts/test_map.json`, map L_FormationLabCombat, `-LevelJson=L_FormationLabCombat.json`): 8 seeded
events (24 enemies) - each spawn's clearance (its record + the smoke's own 36 rays at two heights and a ground trace), navmesh, distance,
ambushers concealed; the timer at 3 s never exceeds max_alive and an event waits; an ambusher placed where the leader looks HOLDS >= 3 s with no
fight, moved to the squad's widest unwatched arc it SPRINGS (< 0.1 s) and starts the skirmish; a 3-enemy patrol 30 m ahead - the contact starts
the skirmish while the squad walks, a move order in the fight is carried out in formation (followers <= 10 m); no health lost, no clip dropped;
the leader climbs the nearest tower. `shot` (rendered) saves `Saved/Screenshots/CombatLab/01_overview_towers.png`, `02_patrol.png`,
`03_ambush_springs.png`.

`CodexTactics.CombatLabAmbushSmoke` (same map / level JSON): the level's own timer - the first group at 30 s; the hiding-spot inventory (wall
ends, doorways, the 8 authored points) and three squad placements whose seeded single ambush picks an authored point / a doorway / a wall
end (concealed, clear of walls by its record + its own rays); a coordinated plan of 6 (hound / frostbitten flanks, cutter + marksman front,
hound + spitter rear) - each party in its own sector, 6 alive above `max_alive` 4 within the plans' cap 6, all concealed; watched all round it
holds; the flanks unwatched: both flanks + the two ranged strike (suppress intent on an operative) while the melee front / rear wait; the
assault 1.5-4 s later; deliberate rounds raise an operative's suppression level; a single ambush left 52 m behind relocates after 15 s,
unseen, to a concealed spot ahead (the same enemy, still hidden); the leader firing at a frozen decoy keeps an ambusher in his aim cone
holding. `shot` saves `Saved/Screenshots/CombatLabV2/01_coordinated_plan.png`, `02_flank_block.png`, `03_suppressor.png` (every enemy revealed).

## 6. Open questions for the designer

Round 1 (answered 2026-11-01, implemented in section 2.1): 1 hiding spots = wall ends / doorways / authored points with the old selector;
2 coordinated multi-direction ambushes, ranged ones suppress; 3 a bypassed ambush relocates ahead unseen; 4 the watch stays +-45 deg, aiming at
another enemy still counts; 5 immortality keeps the knockdown; 6 the first group at 30 s; 7 save / load restarting the pressure is fine.

Round 2 (v2, open):

1. **Relocation is an unseen hop.** The group is put on its new spot at once, only while no operative sees any member and the new spot is out
   of sight (the player cannot see it go either). A real walk round the squad would need a hidden route (walking the shortest path it would
   often cross the squad's view and be spotted). Keep the hop, or make them walk (and accept being spotted on the way)?
2. **The coordinated plan's numbers.** 35 % of the ambushes, 4-6 bodies, its own alive cap 6 (plain events keep 4), the assault 1.5-4 s after
   the flanks, the flanks 15 m to the sides. OK?
3. **An all-round watch** stops a plan too (it holds until a flank side is unwatched, like a single ambush) - or should a plan strike anyway
   after some time in range?
4. **Hiding distance.** The spots stand 2.6 m off the wall so the 2.5 m spawn clearance still holds; closer (1-1.5 m, hugging the wall) would
   need a smaller clearance for ambushers (`spots.wall_standoff_cm`). And the 8 authored points of the lab are mine - move / add them in
   `L_FormationLabCombat.json` "ambush"."points" as you like.

Known limit: save / load keeps no pressure state (a reloaded lab starts its pressure afresh; the lab is a test map, not in the campaign).
