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
- **Types.** Hound, Frostbitten, Military Zombie, Cutter, Marksman by the `types` weights (equal by default). NEVER the Spitter, the Brute or the
  Cryo Drone (refused by the parser, dropped by the rules). Task #34 (user decision 2026-11-07): the **Military Zombie replaced the Spitter**
  (it took its share of the default pool; the lab presets / `L_FormationLabCombat.json` say `"MILITARY_ZOMBIE": 1`). `MILITARY_ZOMBIE` is a
  Frostbitten-archetype VARIANT: `FPressureEnemyType` = archetype + `Variant` (`CombatPressureRules::MilitaryZombieVariant`), spawned through
  `UWaveSubsystem::SpawnLevelEnemy(..., Variant)` as `ACodexTacticsGameMode::EnemyVariantClasses["MilitaryZombie"]` =
  `BP_Enemy_MilitaryZombie` (archetype stays Frostbitten: the zombie AI / stats, a LookVariants body, climbs ladders). The Spitter class /
  archetype stays in the game (stage waves, the weather roster, the horde).
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
  0.5, `spots.doorway_bonus` 0.25). No candidate in reach: a patrol instead (as before).
  **Since decision 3 (2026-11-01 night) the ambushers HUG the wall:** `spots.wall_standoff_cm` **80** (was 260) - a wall-end / doorway spot
  stands 0.8 m off its wall's face; the hiding clearance is min(`wall_clearance_cm`, standoff - 10) = **70 cm** (the 2.5 m rule stays for PATROL
  spawns). A group's other members stand only where they can walk to from its spot in a straight line (never on the wall's far side).
- **Coordinated ambushes (answer 2).** `ambush.coordinated.share` (0.35) of the ambush events are a PLAN of `bodies_min`..`bodies_max` (4-6):
  four PARTIES - flank left, flank right, front, rear (bodies 0-3, the extras to the front, then the rear) - each at its own concealed
  hiding spot in its SECTOR round the plan's anchor (`anchor_ahead_cm` 5 m ahead of the leader; front +-45 deg of the heading, the flanks
  45-135 deg, the rear beyond), the spot nearest its slot (`flank_offset_cm` 15 m to the side, `front_offset_cm` 16 m, `rear_offset_cm` 15 m),
  10-30 m from the squad (`min_squad_distance_cm` / `max_squad_distance_cm`), the parties >= `min_separation_cm` (6 m) apart. No complete
  layout (four sides) or no room: a single ambush instead. At least `min_ranged` (1) ranged member (the Marksman in the pressure roster; a
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
- **Relocation (answer 3; the HYBRID of decision 1, 2026-11-01 night - no teleport).** A hidden single group is BYPASSED while the squad is
  beyond its strike range and it lies behind the squad's way (> `relocate.behind_angle_deg` 100 deg off the heading) or farther than
  `relocate.far_cm` (35 m). Bypassed for `relocate.after_sec` (15 s) and unseen, it searches a **CONCEALED ROUTE**: the best hiding spots ahead
  of the squad (cos >= `min_ahead_cos` 0.2, >= 6 m from where it hid; up to 8 by score) and the navmesh path of its first member to each
  (its own nav filter); a path is sampled every `relocate.route_sample_cm` (100 cm) and refused at the first sample some living operative's
  EYES have a clear sight line to (`UCombatPressureSubsystem::IsPointInSquadSight`: `UTacticalSightSubsystem::EyePoint` - his stance eye
  height, on a tower / roof up there - and `HasClearSight`, 60 m, whichever way he looks: the route must survive a turn of the head), or
  longer than `relocate.max_route_cm` (100 m), or to a spot in sight (`AmbushRelocateRules::IsConcealedRoute` / `PickRoute`: the best spot
  score among the concealed routes, a tie -> the shorter path). It then **RUNS** the route on foot: each runner walks the path's corners, then
  his own spot round the new hiding spot, at the group's slowest `BaseWalkSpeed` x `relocate.run_speed_scale` (1.0), by its guard post
  (`AEnemyCharacter::MoveGuardPost(Location, Yaw, Speed)` - a leg of a run, no stop between legs); its state is `Relocating`, its own senses
  stay muted. Every 0.1 s (`AmbushRelocateRules::StepRun`): **spotted** - an operative SEES a runner with his real vision
  (`UTacticalSightSubsystem::CanOperativeSee`: field of view, stance eye, peek; a man on a tower sees over the walls) or a runner was shot at ->
  the group **strikes at once** (a plan's party spotted on the way: the whole plan springs - the flank block, then the assault); **arrived** ->
  it hides there facing the squad; far over its time (`RunLimitSeconds`: twice the path at its pace + 10 s) -> it stops and hides where it
  stands. **No concealed route: NO teleport** - it stays put; after `relocate.no_route_tries` (2) searches in a row without one (each
  `after_sec` apart) it becomes a **patrol following the squad** (`relocate.no_route_patrol` true; its own senses back, its guard post walked
  after the squad's leader every 2 s at 1.25 x the patrol pace - it engages when it perceives the squad; false = it only stays until
  recycled). A waiting coordinated plan that is bypassed is laid out afresh round the squad; its parties run there only when ALL of them have
  a concealed route, else it stays (and after `no_route_tries` searches, or with no layout at all, its parties go on as single groups).
  `relocate.enabled` false = the first version (wait until recycled). The record (`FPressureRelocationRecord`): path, length, samples,
  outcome, the largest step of a runner (a teleport would show), `Codex.Pressure.Status` lists it.
- **A squad that watches every side still gets attacked (decision 2, 2026-11-01 night).** An ambush that has HELD (in strike range, watched)
  for its own hold limit in all strikes anyway: single groups and coordinated plans (a plan watched all round -> the flank block, then the
  assault as usual) draw a limit uniformly in `ambush.hold_strike_min_sec`..`hold_strike_max_sec` (**60-90 s**; its own seeded stream, the
  event stream is untouched; max 0 = never). `AmbushWatchRules::HoldLimitSeconds` / `HeldTooLong`, `AmbushPlanRules::NextPhase`
  (`FAmbushPlanTick::HoldSeconds` / `HoldLimitSeconds`); counted apart from the watch springs (`GetSprungByHoldCount`).
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

`CodexTactics.CutterWallSmoke` (bug 06): the squad (Passive) against the south face of W_E_13, a Cutter in bite reach behind it - no blow (it holds and goes round), 5.7 m behind it - no pounce (refused); 6 m out on the squad's side - it pounces, next to them its blows land.
`CodexTactics.CombatLabTowerSmoke` (bug 07): the leader (solo) up a tower by a click on its ladder, down by a ground click, up by a double click on the ladder (a sprint), down, up by a double click on the roof - all through `HandleWorldHit`; never a refusal. `CodexTactics.CombatLabTowerSmokePIE` runs it in real PIE (the editor, `-RenderOffscreen`).

`CodexTactics.CombatLabAmbushSmoke` (same map / level JSON): the level's own timer - the first group at 30 s; the hiding-spot inventory (wall
ends, doorways, the 8 authored points; every wall-end / doorway spot exactly 0.8 m off its wall) and three squad placements whose seeded
single ambush picks an authored point / a doorway / a wall end (concealed, clear of walls by its record + its own rays, hugging its wall); a
coordinated plan of 6 (hound / frostbitten flanks, cutter + marksman front, hound + spitter rear) - each party in its own sector, 6 alive above
`max_alive` 4 within the plans' cap 6, all concealed; watched all round it holds; the flanks unwatched: both flanks + the two ranged strike
(suppress intent on an operative) while the melee front / rear wait; the assault 1.5-4 s later; deliberate rounds raise an operative's
suppression level; a single ambush hidden north of the 43 m wall W_E_13 that the squad (south of it, heading west) has passed sets off after
15 s along a CONCEALED route (34 m, 36 sight samples clear) and RUNS it on foot to the wall-end spot ahead - never seen, largest step 4.7 cm a
frame, arrived after 12 s, the same enemy still hidden; the leader firing at a frozen decoy keeps an ambusher in his aim cone holding.
`CodexTactics.CombatLabAmbushRelocateSmoke` (same map / level JSON; decisions 1-2 of 2026-11-01 night): a plan watched ALL ROUND strikes after
its hold limit (5-6 s for the smoke: the flank block after 5.5 s held); the W_E_13 runner is SPOTTED by an operative posted on the ground
10 m from it -> strikes at once (0.08 s), the fight starts; the same with the watcher on a TOWER ROOF (T_NW, 40 m) -> strikes at once; with
every route counted as seen (`SetRouteBlockedOverride`): no teleport, it stays put after the first search, after the second it is a patrol
following the squad on foot (largest step 4 cm a frame); a single ambush 12 m ahead of a squad watching every side holds 30 s, then strikes
at its own 60-90 s limit (88.5 s). `shot` saves `Saved/Screenshots/CombatLabV2/01_coordinated_plan.png`, `02_flank_block.png`, `03_suppressor.png` (every enemy revealed).

## 5a. Play-test fixes 2026-11-01 (branch `fix/cutter-walls-ladder`)

| Bug | Cause | Fix |
|---|---|---|
| 06 Cutter hits / pounces through walls | `EnemyAIRules::CanMelee` and the pounce checked the distance only (Godot too); the lab's 40 cm walls left 135 cm between two capsules - inside the 150 cm bite reach | `MeleeLineRules` (enemy-ai-and-perception.md): every enemy blow, the pounce and its landing need a clear line; the pounce's flight arc is swept with its capsule |
| 06 "for 12.6" while immortal | the log printed the damage `TakeHit` computed; the cheat absorbed it in `UHealthComponent` (no health lost) | the log says "- absorbed by the immortality cheat" |
| 07 tower ladders refused in PIE (`climb refused (6)` = NotClimbable) | the rung count was a construction-pass cache lost in the PIE copy (the -game smoke loads from disk and rebuilds: it passed) | derived from Height / Pitch (LADDERS.md 3.4); the "ladder text" was the refusal's floating "THIS LADDER CAN'T BE CLIMBED" |
| 08 the enemy count on screen | the fight banners and notifications printed it | no fight banner at all, no numbers (waves-horde-encounters.md) |

## 6. Open questions for the designer

Round 1 (answered 2026-11-01, implemented in section 2.1): 1 hiding spots = wall ends / doorways / authored points with the old selector;
2 coordinated multi-direction ambushes, ranged ones suppress; 3 a bypassed ambush relocates ahead unseen; 4 the watch stays +-45 deg, aiming at
another enemy still counts; 5 immortality keeps the knockdown; 6 the first group at 30 s; 7 save / load restarting the pressure is fine.

Round 2 (v2) - answered 2026-11-01 night (decisions 1-7; implemented on branch `combat/ambush-v3`): 1 relocation = the HYBRID (a concealed
route on foot, spotted -> strike, no route -> stay / follow as a patrol); 2 the coordinated numbers stay; 3 an always-watched ambush strikes
after 60-90 s; 4 the spots hug the wall at 0.8 m. The original questions for reference:

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

## 7. Training Ground department "Combat lab" (game designer 2026-11-01, branch `tools/tg-combat-lab`)

`Tools/TrainingGround` department **«Боевая лаборатория»** (next to Waves / Movement / Level design), for `L_FormationLabCombat` and (since
2026-11-02, section 7.6) every generated map with a `bot_route` - a map selector in the header. Four tabs:
A level settings and C bot runs are built; B (a map editor: walls, ambush points, the route) and D (a browser simulator) are reserved
tabs ("скоро"). Endpoints `server/combatLabApi.ts`; pure modules `src/combatLab/pressureSettings.ts` (A) and `src/combatLab/report.ts` (C),
tests `src/combatLab/*.test.ts`.

### 7.1 A - level settings

- Edits the level JSON's `"pressure"` block (every key of section 2 / 2.1: the schedule, groups and caps, the patrol / ambush / coordinated
  shares - shown as the three effective event shares -, the type weights of the five types, the watch cone, the hiding spots, the coordinated
  plans, the relocation, the patrols, the recycling, the seed) and `"cheats"` (immortality, infinite ammo). Each field shows the game's
  default and its range; «Подробнее» shows the rest. Saved through `POST /api/combat-lab/settings`: validated, the old file copied to
  `Saved/TrainingGround/combat_lab/backups/`, only the changed values rewritten (`patchJsonText`), the diff shown.
- **One rule set.** The page's field table (`PRESSURE_FIELDS`) mirrors `LevelJsonRules::ParsePressureSettings`: the page REJECTS what the
  C++ would clamp (min = the C++ floor, max = the C++ ceiling, integers, "max >= min (+100)"), forbids BRUTE / CRYO_DRONE / unknown types and
  malformed `ambush.points`. `npm run golden` exports the table to `Tools/TrainingGround/golden/combatlab_pressure_fields.json`;
  `CodexTactics.LevelJson.PressureFieldsGolden` checks every default, min and max against the parser (and that it clamps where the page says).
- **Presets** `Content/Data/CombatLab/presets.json` (`codex.combatlab_presets/1`): Default (as shipped), Patrols only, Ambush heavy, Coordinated
  only; one click applies one, «Сохранить как пресет» adds / replaces one. `CodexTactics.LevelJson.CombatLabPresets`: every preset parses as
  written and the level with it applied parses (the AllFilesParse rule).

### 7.2 C - bot runs in Unreal

- **The route.** Every map gets a planned route; the combat lab's is the level JSON `"bot_route"` (`BotRouteRules::Parse`,
  `FLevelCombatConfig::BotRoute`): `combat_lab_loop_01`, 21 waypoints, ~453 m a loop - from the spawn up tower T_SW, round the W_C_4 end,
  up T_NW, the inner corner of lane E, the V2 6 m doorway, round the W_E_14 end, up T_NE, a wall hug at W_G_20, up T_SE, both lane D
  corridors, the lane C 2 m doorway, a wall hug at W_B_2 and round its west end. A waypoint is `walk`, `wall` (a click on the wall face =
  the formation wall layout, held `hold_sec`) or `climb` (the leader alone - Solo - walks up the nearest tower's ladder, holds, comes down).
- **The bot's route mode** (`UPlaytestBotSubsystem`, `PlaytestBotRouteMode.cpp`): on an endless pressure level with a route (or
  `-BotRoute=1`) the bot leads the squad through the player's own click path (`HandleWorldHit`: moves in formation, the wall click + its
  confirm, the roof click), Aggressive posture. **A fight: COVER AT A WALL first** (decision 5, 2026-11-01 night): the threat = the nearest
  living enemy that fights (else the nearest one); `BotCoverRules::PickWall` on the level's obstacle boxes picks the face of a wall >= 2 m long
  and >= 1.2 m tall that stands between the squad and the threat (cos(into the wall, face -> threat) >= 0.5, the threat >= 4 m off), within
  15 m walk of the leader (round the wall's end when the squad stands on its far side), best by facing minus 0.8 x distance / 15 m; the bot
  clicks that face from the squad's side at knee height (the group cover order = the formation wall layout) and confirms the ghost; the
  squad fights from there until the fight ends or `fight_cover.hold_sec` (45 s), then walks on (firing on the move). No wall: it halts where it
  stands up to `fight_hold_sec` (20 s) - no crouch order any more (the old "halt crouched" made the stance jitter, decision 6). Level JSON
  `"bot_route"."fight_cover"` {enabled, search_cm, hold_sec, min_wall_cm, min_height_cm, min_threat_cm, min_facing_cos}. It stops by TIME (`-BotDuration=<game s>` -> `RESULT COMPLETE`). `FBotRouteTracker` (pure, `CodexTactics.Bot.Route.*`):
  reached / skipped waypoints, laps; STUCK = the leader moved < 1.5 m in 12 s (`no_move`) or a leg takes longer than 25 s + its length at 1 m/s
  (`leg_timeout`) or an order / action failed - the move is ordered again, after 2 retries the waypoint is skipped. Completed = a lap with no
  waypoint skipped.
- **The metrics** (`FCombatLabRunCollector`, pure, `CodexTactics.Bot.CombatLab.*`; sampled every 0.1 s): contacts per pressure group (a
  coordinated plan is one) - opened when a member is seen (line of sight within 30 m), engages or an ambush springs; the time from the
  contact to the first operative's look covering it (`AmbushWatchRules` looks: body, aim, formation sector, +- the watch half angle); an
  ambush that sprang while unwatched; the formation at the contact. Formation changes by type (a label holds 0.5 s; Solo / off is none; the
  diamond of 3 = wedge; the wall order = `wall_layout`), wall hugs (`IsSquadAtWall` rising after >= 1 s off), stance changes per operative /
  minute, damage taken and absorbed by immortality (`UHealthComponent::GetCheatBlockedDamage`), rounds incl. the infinite-ammo ones
  (`AOperativeCharacter::GetRoundsSpent`), ladder climbs, knockdowns, fights; **cover at a wall in fights** (`"cover"`: orders, taken = the
  squad at the wall under the order, seconds there; report column "Cover (s)") and the ambush counters (`"pressure"`: `sprung_by_hold`,
  `runs_spotted`, `no_route`, `followers`). One record per run, schema `codex.combatlab_run/1` (the new blocks are optional for older records).
- **The batch**: «Прогоны ботов» -> `POST /api/combat-lab/run` (defaults 4 runs x 10 min, 2 at a time) writes `Saved/Telemetry/combat_lab/<id>/`
  `level.json` (the page's settings, also unsaved ones) + `batch.json` and starts `Scripts/bot_run.ps1 -ReportDir <dir> -DurationSec <s>
  -SeedBase <seed> -LevelJson <dir>/level.json -Map /Game/Maps/L_FormationLabCombat` (run n: `-BotSeed=n -PressureSeed=seed+n-1`). Live:
  `bot_status.json` + each game's `run_<n>.live.json` (every 5 s). At the end: `runs.jsonl`, `jev.txt` (`typesafe_triage.py --telemetry` -
  a 3-line combat lab digest), `report.md` + `aggregate.json` (`node src/combatLab/cli/report.ts --batch <dir>`); the page shows the
  aggregate, a row per run, the map with the stuck points / contacts / end positions and the digest.

### 7.3 Open questions (round 3) - answered 2026-11-01 night

1. **Fight policy of the bot** -> cover at a wall first (decision 5, section 7.2). 2. **The route** stays (453 m). 3. **"Looked at"** stays
(body / aim / sector within +-45 deg).

### 7.4 The stance jitter (decision 6, 2026-11-01 night)

The report of 2026-11-01 19:34 counted ~6 crouch / stand changes per 2 minutes for the Engineer and the Medic. Where followers crouch on a
halt: (a) the bot's own fight halt ("halt crouched", gone - section 7.2); (b) Solo's guard crouch while the leader climbs a tower (deliberate,
"teammates hold crouched" - kept); (c) the formation wall layout at a wall (taking cover - kept); (d) in the GAME, the squad autonomy's ROE
(Commander Mode, `USquadAutonomySubsystem` 7-C: `open_ground_stance` crouch for an operative standing still with enemies about) - it crouched
at once on every stop and nothing stood him up for the next walk. Fixed there with a hysteresis: the open-ground stance only after standing
still `Content/Data/AI/squad_roe.json` **`hold_crouch_after_sec` (4 s**; 0 = at once as before; Training Ground ROE panel) -
`SquadAutonomyRules::ShouldTakeHaltStance` / `StepStillSeconds`; cover, a sniper's aim on him and a sniper rifle stay immediate. That crouch
is the "hold crouch" (`AOperativeCharacter::MarkHoldCrouch`): his next move stands him up at once - his own order (`OrderMoveTo`) or the
formation's follow (`FollowTo`, when the leader walks upright; `ShouldRiseFromHoldCrouch`). Solo, explicit stance orders and the wall
layout are unchanged. Smoke `CodexTactics.SquadHaltStanceSmoke`: in a fight with Commander Mode, a 3 s halt crouches nobody, a 10 s halt
crouches everyone 3.7 s into it, the leader's next order and the formation's follow stand them up at once (0.02 s), Solo still crouches at
once. Bot batch `Saved/Telemetry/combat_lab/20261101_205220` (2 runs x 3 min, seeds 1101-1102): stance changes **2.7 / min** (was 7.0 / min;
Engineer / Medic 4 per run of 3 min, all from the two tower climbs' Solo), cover taken 2 of 2 orders (12 s at the wall).

### 7.5 Open questions (round 4)

1. **Solo during the bot's tower climbs** still crouches the two who wait (~25 s each climb: 2 of the 4 changes per run left). Keep it
   (it is the deliberate "teammates hold crouched"), or give Solo the same 4 s hysteresis?
2. **No concealed route** -> after 2 searches (30 s) the ambush follows the squad as a patrol (its senses back). Or should it only stay put
   (`no_route_patrol` false - the recycling removes it after 60 s far away)?
3. **Concealment of a route** is tested against every operative's eyes in ALL directions (a turn of the head must not reveal it), while
   "spotted on the way" uses their real field of view. In the open lab few routes qualify (the squad sees most of it) - mostly runs behind
   long walls (W_E_13, W_E_14, the lane D corridors). Loosen it to the operatives' field of view at the time of the search?
4. **The bot's cover**: 15 m search, a wall >= 2 m long / 1.2 m tall facing the threat; 45 s at most at the wall. OK? (One fight in the batch
   found no wall within 15 m and halted.)

### 7.6 Generated maps (2026-11-02, branch `tools/levelgen-bot-runs`)

The level generator's maps (`docs/design/levelgen/LEVEL_GENERATOR.md` sections 7-8) get the same bot runs:
- **Map selector.** `GET /api/combat-lab/maps` -> `{default: "L_FormationLabCombat", maps: [{id, map, levelFile, kind: "lab" | "generated",
  hasRoute, routeName, routePoints}]}` (the lab + every `Content/Maps/Generated/<leaf>.umap` whose `Content/Data/LevelJson/<leaf>.json` has a
  `bot_route`); every endpoint takes `?map=<id>` (GET) / `map` (POST), missing = the lab (strictly validated ids). Settings, layout preview
  (a generated map: its accepted layout's walls, buildings, ladders), runs (`-Map` = that map, `batch.json` `level` = the id), batches
  (per map) and reports per map. Presets stay one shared file; applied to a generated map they keep ITS `ambush.points`, saved from one they
  carry none (pure `src/combatLab/maps.ts`, tests `maps.test.ts`).
- **The route.** The builder writes the `bot_route` from the layout's mandatory route (`Scripts/Editor/levelgen_bot_route.py`): waypoints with
  **`"z"`** (the floor height: the bot's click runs from 1.5 m above that level to 2 m below it - never the roof over a floor point - and the
  waypoint counts as reached only with the leader's feet within `reach_height_cm` (150) of it; `FBotRouteTracker::IsAt`) and **`"drop"`**
  on a leg through a roof / floor hole. The squad goes down by the builder's drop nav link (it walks off the rim and falls); the run record's
  `route.drops` lists each (`fell` / `scripted`); a drop leg that gets stuck is scripted (`ScriptedRouteDrop`: the living squad put down at
  the waypoint, marked). Tests `CodexTactics.Bot.Route.LevelsAndDrops`, `.GeneratedDepotRoute`, `CodexTactics.Bot.CombatLab.*` (drops).
- **Clicks.** A slab cut away round the squad inside a building ignores the Visibility channel: the bot's click falls back to WorldStatic.
  A move click near a wall that the controller turns into a wall-cover PREVIEW (waiting for a second click) is dropped and the leader ordered
  to the waypoint (it stalled 12 s at the depot's #19 before).
- **`Codex.Bot.Route [seconds=<game s>] [seed=<n>] [quit]`** starts the route mode in a running game (also without an endless pressure):
  `LevelGenRouteSmoke` uses it, `UPlaytestBotSubsystem::StartRouteRun`.
- **Batch** `Scripts/bot_run.ps1 ... -ReportDir <dir>` writes `<dir>/status.json` (`codex.job_status/1`, LEVEL_GENERATOR.md 8.3) and
  exits 1 when a run errored, 3 when the build lock stayed held. The first batch on `L_Gen_Depot_1`: LEVEL_GENERATOR.md 8.1.
