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

- **Schedule.** The first event at `first_event_sec` (15), then one every `interval_sec` (**120**). A group of `group_min`..`group_max`
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
- **Ambushers.** A spot 14-30 m from the squad, ahead of its way, that NO operative has a line of sight to (behind a wall); the group stands
  there (a guard post, its own senses MUTED - only the watch rule decides). Each 0.1 s: the squad beyond `strike_range_cm` (18 m) -> wait; in
  range and someone looks its way -> HOLD; in range and nobody looks its way -> STRIKE (Patrol -> Engage at the nearest operative, its own
  senses back). "Looks its way" (`AmbushWatchRules`): each living member's body yaw AND his formation sector look (FORMATION_SECTORS.md) count;
  a look covers +-`watch_half_angle_deg` (45) within `watch_range_cm` (50 m). So the squad turning its back to it (or a rear guard looking the
  other way) is what lets it strike; an all-round watch never gets ambushed. Shot at / spotted first, the whole group fights at once.
- **The fight.** Pressure enemies are level enemies spawned at runtime (`UWaveSubsystem::SpawnLevelEnemy` - not part of a fight until they
  engage; `ULevelEncounterSubsystem::MarkLevelEnemy`), so the tutorial's exploration -> combat start is reused: the first contact starts the
  real-time SKIRMISH (`NotifyHostileContact`), every later contact joins it, the last kill ends it straight in exploration (no panel). The squad
  keeps its formation in the fight (`Codex.Squad.CombatFormation`) and takes move orders as usual.
- **Recycling.** An unengaged pressure enemy (still patrolling / hidden) farther than `recycle.distance_cm` (60 m) from every operative,
  unseen, for `recycle.after_sec` (60 s) is removed - the endless map never clogs with enemies left behind.

## 3. Options (cheats)

`CombatCheats` (`CodexTactics.Combat.Cheats.Resolve`): **immortality** of the squad (an operative loses no health at all - hits, blasts, bleeding,
burning, falls; hits still knock down) and **infinite ammo** (no shot empties the clip; no reloads). The level JSON `"cheats": {"immortal": true,
"infinite_ammo": true}` turns them on (L_FormationLabCombat: both); console `Codex.Cheat.Immortal` / `Codex.Cheat.InfiniteAmmo` = -1 (the level
decides, default) / 0 off / 1 on. Development / editor builds only: in Shipping the console variables do not exist and every cheat is off.

## 4. Console (dev builds)

| Command | What |
|---|---|
| `Codex.Pressure.Spawn [patrol\|ambush] [count]` | a spawn event now (the seeded pick without arguments; ignores the cap) |
| `Codex.Pressure.Clear` | removes every pressure enemy |
| `Codex.Pressure.Status` | the state in the log (alive, next event, each group, the ambushers' states, the cheats) |
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

## 5a. Play-test fixes 2026-11-01 (branch `fix/cutter-walls-ladder`)

| Bug | Cause | Fix |
|---|---|---|
| 06 Cutter hits / pounces through walls | `EnemyAIRules::CanMelee` and the pounce checked the distance only (Godot too); the lab's 40 cm walls left 135 cm between two capsules - inside the 150 cm bite reach | `MeleeLineRules` (enemy-ai-and-perception.md): every enemy blow, the pounce and its landing need a clear line; the pounce's flight arc is swept with its capsule |
| 06 "for 12.6" while immortal | the log printed the damage `TakeHit` computed; the cheat absorbed it in `UHealthComponent` (no health lost) | the log says "- absorbed by the immortality cheat" |
| 07 tower ladders refused in PIE (`climb refused (6)` = NotClimbable) | the rung count was a construction-pass cache lost in the PIE copy (the -game smoke loads from disk and rebuilds: it passed) | derived from Height / Pitch (LADDERS.md 3.4); the "ladder text" was the refusal's floating "THIS LADDER CAN'T BE CLIMBED" |
| 08 the enemy count on screen | the fight banners and notifications printed it | no fight banner at all, no numbers (waves-horde-encounters.md) |

## 6. Open questions for the designer

1. **The ambusher's hiding spot.** Now: the best concealed spot 14-30 m AHEAD of the squad's current heading (the squad walks past, turns its
   back, it strikes). Alternatives: by the squad's clicked destination / a known route; at wall ends / doorways only; several "ambush points"
   placed by hand in the map.
2. **An ambusher the squad never passes** waits forever (until recycled 60 m away). Should it creep to a new spot after a while?
3. **The watch.** A look covers +-45 deg within 50 m, through walls (directional, not line of sight). Narrower / wider? Should a member in cover
   or in a fight (aiming at someone) still count as watching his old sector?
4. **Immortality** keeps the knockdown of a heavy hit (>= 40 HP) - keep, or no knockdown either?
5. **The first event at 15 s** (not 2 minutes) so the map is not empty at the start - OK?

Known limit: save / load keeps no pressure state (a reloaded lab starts its pressure afresh; the lab is a test map, not in the campaign).
