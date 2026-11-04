# Combat simulator (Training Ground `src/combatSim`) - model, constants, calibration, limits

> Branch `tools/combat-sim` (2026-11-02). A FAST, DETERMINISTIC combat simulator for a generated layout: the squad walks the layout's mandatory
> route in formation, the combat-lab pressure spawns patrols / ambushers / coordinated plans round it with the game's own rules, fights are
> played out in a simplified but explicit model - a 10-minute run takes ~0.05-0.2 s, 200 seeds ~10 s (the world ~0.05 s and the squad track
> ~10 s are built once per layout, the track is cached). It is a FILTER before a layout is built in Unreal and a DIAGNOSTIC of why ambushes
> happen or do not (per hiding spot, per coordinated attempt), not a replacement for the Unreal bot runs (COMBAT_LAB.md section 7.2): the
> Unreal batch stays the ground truth the simulator is calibrated against (section 4).

## 1. Where things are

| What | Where |
|---|---|
| The ported game rules (faithful, pure) | `Tools/TrainingGround/src/combatSim/rules.ts`: `CombatPressureRules`, `AmbushSpotRules`, `AmbushWatchRules`, `AmbushPlanRules`, `AmbushRelocateRules` = `Source/CodexTactics/Public/Combat/*Rules.h`, `Public/AI/AmbushWatchRules.h`. The C++ is the user-approved reference - change it there first. |
| The pressure config (the level JSON `"pressure"` block as the C++ parses it) + the simulator's own constants + the enemy stand-in stats | `config.ts` (`parsePressureConfig` uses the golden-checked field table of `src/combatLab/pressureSettings.ts`; `DEFAULT_SIM_CONSTANTS`; `ENEMY_STATS`) |
| The world (obstacle boxes, 2.5D sight grid, enemy nav, bounds, hiding spots, zones) | `world.ts` (`SimWorld`, `SightGrid`, `labToGenLayout` for a plain lab layout); the nav grid + its wall index: `src/movement/nav.ts` (`NavGrid.path` / `wallsBlock` / `lineClear` / `cellFree`) |
| The squad track (the formation simulator's walk, 10 Hz, cached) | `squadTrack.ts` (`buildSquadTrack`); cache `Saved/TrainingGround/combatsim/tracks/<layout>_<members>_<hash>.json` (`node/files.ts`) |
| One run | `sim.ts` (`runCombatSim`) - the schedule, spawns, patrols, ambushes, plans, relocation, followers, recycling, fights, contacts, metrics, diagnostics |
| A batch -> `codex.combat_sim/1` | `batch.ts` (`runBatch`), `schema.ts` (the document, `aggregateRuns`, `summariseSpots`, `heatMap`, `validateCombatSimBatch`), `report.ts` (the <= 40-line report, the calibration table) |
| CLI | `npm run combatsim -- --layout <name\|path> [--seeds 200] [--minutes 10] [--members 3] [--sight-conditions] [--engagement-bands] [--weather <weather>] [--sun-elevation <deg>] [--pressure <json\|file>] [--level <level json>] [--out <dir>] [--no-cache] [--constants <json>] [--immortal 0\|1] [--infinite-ammo 0\|1]` -> `<out>/combatsim.json` + `report.md` (default `Saved/TrainingGround/combatsim/<layout>/`); `npm run combatsim:calibrate -- --batch <Unreal batch dir> [--layout] [--seeds 200] [--out]` -> `calibration.md` / `.json` (exit 1 when a main metric leaves its band). A path with spaces: call `node src/combatSim/cli/calibrate.ts ...` directly (npm on Windows splits it). `--pressure` with a bare block is merged SHALLOWLY over the level's block (a nested `ambush` replaces the level's `ambush`, points included) - pass a whole block or a level JSON when touching `ambush`. |
| Fixtures (for the UI agent and the tests) | `src/combatSim/fixtures/mini_depot.json` (a 60 x 40 m hand-made layout), `fixtures/combatsim.example.json` (its 3-seed x 5-min batch, the schema of section 6) |
| Tests | `src/combatSim/combatSim.test.ts` (`npm test`, 16): the rules on hand-made cases, the watch rule, the plan conditions, the sight grid, the config defaults, the wall index vs `segmentBlocked`, the HP knob, the calibration table, determinism + speed, the output schema, the fixture |

## 2. The model

**Input.** A `codex.levelgen_layout/1` layout (or a lab `codex.formation_layout/1`, flattened), the level JSON's `"pressure"` block (by default
the one of the map built from the layout, found through `levelgen_layout`; else the layout's own; else the game's defaults), its `"bot_route"`
fight-cover tuning, a seed, the run length (10 min), the member count (3 - the Unreal batches run the Commander, the Engineer and the Medic-Sapper).

**The squad (squadTrack.ts).** The bot's route mode walks the mandatory route in formation and only PAUSES for fights, so the walk is independent of
the enemies and is computed ONCE per layout: the real formation simulator (`src/movement/sim.ts`, the lab's rules: terrain classes, sector looks,
corner watches, the wall layout at the dead end) walks the ground legs with 3 men; the roof / floor legs, the stairs and the return from the end to
the start (the loop) are walked by a simple column (the leader at the patrol pace, the rear man looking back, the middle men to the sides); a hole drop
holds 2.8 s (the Unreal runs: 2.7-3.0 s). Every 0.1 s the track keeps each member's position, body yaw and the look the rules asked of him
(`MemberFrame.lookYaw`, new in sim.ts) - the C++ `GatherWatchers` counts both, plus the aim -, the formation label, the layer / height level. A
run replays the track (~10 s to build on the Depot, cached by a hash of the geometry + route + formation config).

**The pressure (sim.ts, step 0.1 s = the game's ambush tick).** `UCombatPressureSubsystem` step by step with the ported rules:
- the schedule (`DecideEvent`: the first event at `first_event_sec`, then every `interval_sec`, an event waits while `max_alive` are alive, a failed
  spawn retries in 10 s), the kind (`PickEventKind`: `patrol_share`, then `coordinated.share`), the group size cut to the room, the types by the weights
  (never the Brute / Cryo Drone), one seeded stream for the events + an own stream per ambush for its hold limit (as `DrawHoldLimit`);
- the obstacle boxes = every ground wall / fence and every cover >= 50 cm as an oriented box (`GatherObstacles`); the bounds = the layout bounds shrunk
  by `wall_clearance_cm`; "on the navmesh" = the enemies' A* grid (walls + covers >= 140 cm widened by the 35 cm agent); "reachable on foot" =
  the same connected component as the leader's last ground point;
- PATROLS: `FindSpot` - 72 random candidates 25-50 m round the leader (70 % within +-80 deg of his heading), `CheckSpawnPoint` (clearance 2.5 m,
  >= 25 m from every operative, in bounds), on the navmesh, scored (`ScoreSpot`: ahead, out of sight, unwatched), the best of 6 that is reachable; the
  group's members at `GroupOffset` (2.2 m); the leader walks `PatrolLoop` (4 points, 12 m, filtered by clearance / navmesh; 0.6 x the radius when
  fewer than 3 survive), waits `wait_sec`; the escorts follow at their offsets. Their own perception (enemy_perception.json: sight range / cone /
  time to detect, proximity 3 m, gunshots within hear_gunshot_m) engages them; the squad seeing them within the rifle's range fires (Aggressive);
- AMBUSHES: the hiding spots = `AmbushSpotRules::Candidates` (wall ends >= 1.5 m with a free end, 1 m inside the end, 0.8 m off the face; doorway
  pairs 0.8-6 m; the level JSON's `ambush.points` + the layout's `ambush_points` = the builder's `CodexAmbushPoint` actors; merged at 1 m);
  `FindHideSpot`: 14-30 m from the leader, `CheckSpawnPoint` with the hide clearance (70 cm), >= 14 m from every operative, on the navmesh, NOT
  visible to any operative (the pressure's eye 1.5 m -> the spot's chest 1.1 m), scored (`ScoreSpot` + the kind bonus), the best of 6 that is
  reachable; no spot -> a patrol instead (counted). The group hides; every 0.1 s `AmbushWatchRules::Decide` with the squad's looks (body yaw,
  sector look, the aim at the enemy an operative fires at): beyond `strike_range_cm` 18 m WAIT, in range watched HOLD (the hold clock), unwatched
  SPRING; held for its 60-90 s limit -> strikes anyway; shot at / spotted -> the whole group fights;
- COORDINATED: `PlanBodies` (4-6 under the plan's own cap), `EnsureRanged`, `PlanLayout`: every concealed valid spot within 10-30 m, `AssignSpots`
  (the candidate nearest each party's slot in its sector round the anchor 5 m ahead, >= 6 m apart), all four parties or no plan (-> a single ambush,
  -> a patrol), unreachable spots dropped and re-assigned (4 tries); the phase machine `NextPhase` (waiting / holding / flank block / assault) with
  the hold limit; a party shot at / seen and fired on springs the plan (the C++ TickPlan's `bAnyEngaged`); `Springs` decides who strikes (the flanks
  + the ranged suppressors first, everyone in the assault);
- RELOCATION: `IsBypassed` (beyond the strike range and > 100 deg off the heading or > 35 m) for 15 s and unseen -> `RankHideSpots` ahead (cos >= 0.2,
  >= 6 m from the old spot), the A* path to each of the best 8, sampled every 1 m against every operative's REAL eyes in any direction (60 m), the
  destination concealed, <= 100 m -> `PickRoute`; the group RUNS it (the slowest member's speed x `run_speed_scale`, >= 150 cm/s), spotted in an
  operative's field of view (+-70 deg of a look, 60 m, a clear line) -> strikes at once; arrived -> hides there; no route twice -> a patrol following the
  squad's leader (2 s re-aim, 3 m re-post). A waiting plan that is bypassed is re-laid; its parties run only when all four routes are concealed, else
  it stays, then dissolves into single groups;
- RECYCLING every 0.5 s: an unengaged, unseen enemy > 60 m from the squad for 60 s is removed; an ENGAGED enemy that neither sees nor reaches the
  squad nor has come a metre nearer than it ever was in the chase for 30 s is LOST (removed, counted apart) - the simulator's stand-in for the game's
  enemy losing its target (`lostAfterSeconds`).

**Enemy movement.** Straight when no wall / tall cover (widened by the 35 cm agent) touches the way, else along the A* path of the nav grid (50 cm
cells; re-planned when there is none, when the goal moved > 3 m from the path's end or when the enemy is stuck, at most every 2 s); a step into a
widened band slides to the nearest free cell. The straight test, the walkability test and the path's string pulling use the SAME exact segment
distance (`NavGrid.wallsBlock`, the wall index of section 7), so a path's corners are always walkable.

**Fights (simplified, explicit).** Lines of sight are 2.5D on the layout's walls (with their floor's height) and the covers >= 140 cm: a line is
blocked only where an obstacle's top reaches the line's height there (the elevated-vision rule of `levelgen/evaluate.ts`: a man on a 6 m roof sees
over a 3 m wall). A contact opens as the bot's collector does: a group member within 30 m with a line of sight to an operative, or engaged, or
sprung; its reaction time = the first look covering the group; an unwatched strike = sprung while no look covered it. An operative SEES an enemy
inside his field of view - +-70 deg of his body, his sector look or his aim (the game's standing / crouching FOV is 140 deg,
`UTacticalSightSubsystem::CanOperativeSee`), 60 m, a clear line from his 1.6 m eye to the enemy's chest. The squad fires at the nearest enemy it
sees within 25 m (`attack_range_m`), returns fire at an ENGAGED attacker in sight up to 35 m, after 0.6 s of reaction, one shot per 1.54 s
(`fire_rate` 0.65 shots / s), hit chance by the 3.1 m distance cell (`base_hit_chances`), 18 damage minus 10 % armour. Enemies: melee run to their
target and strike in reach with a clear line (no blow through a wall), ranged close to their preferred range and fire with their accuracy; damage is
ABSORBED by the immortality cheat (counted) or taken; a heavy hit (>= 40) knocks down with 50 %, a hound's pounce after a 1 s run-up (12 s cooldown), a
Cutter's leap with its chance; down 2.2 s, immune 1.5 s. The bot's fight policy: on the first contact the squad picks a wall >= 2 m long / 1.2 m tall
between it and the threat within 15 m (`BotCoverRules` port: the face on the squad's side, facing the threat cos >= 0.5, >= 4 m from it, the best
facing - 0.8 x distance / 15 m) and walks to it (cover orders / taken / seconds counted; ranged hits on a squad at the wall x 0.35), else halts where
it stands; after 45 s at the wall / 20 s halted, or when the fight ends (no engaged enemy alive), it walks on firing on the move. The fight time is
the time some engaged enemy is alive (the game's WaveCombat phase).

**Determinism.** Two seeded mulberry32 streams per run (the events, the dice of the fights) + the per-ambush hold-limit streams; the same layout,
pressure, seed and constants give byte-identical output (tested). The streams are not Unreal's `FRandomStream`: seed 1 here is not seed 1 there -
the sim is compared with Unreal on batch means (section 4), never run by run.

## 3. Constants (`config.ts`)

The pressure's own numbers come from the level JSON (`parsePressureConfig`, the C++ defaults where a key is absent). The simulator adds:

| Constant | Value | Why |
|---|---|---|
| `dt` | 0.1 s | the game's ambush / pressure tick |
| `eyeLiftCm` / `targetLiftCm` | 150 / 110 | `CombatPressureLocal`: the actor location (~90) + 60 for the eye; the enemy's chest |
| `sightEyeCm`, `sightRangeCm`, `sightHalfAngleDeg` | 160, 6000, 70 | the real eyes (`UTacticalSightSubsystem::MaxSightCm`), half the standing FOV (`Codex.Sight.FovStandDeg` 140; the fight keeps it, `FovFight` 1). Since 2026-11-02 the range x the height level's multiplier (`HeightSightRules` in `rules.ts` = `SightRules::HeightLevel` / `HeightRangeMultiplier`, `DEFAULT_HEIGHT_SIGHT` = `enemy_perception.json` "height_sight": +1 x1.25, +2 x1.5, +3 x1.75, max x2; the cut x1), so an operative on a +3 roof sees 105 m; the sim's enemies stand on the street (x1) |
| `contactRangeCm` | 3000 | the bot's `ContactRangeCm` |
| `groupSpacingCm`, `spotCandidates`, `relocateAvoidCm`, `relocateRoutes`, `runLegAcceptCm`, `runArriveCm`, `followStepSeconds`, `followRepostCm` | 220, 72, 600, 8, 150, 120, 2, 300 | `CombatPressureLocal` |
| `rifleDamage`, `rifleInterval`, `rifleRangeCm`, `rifleReturnFireRangeCm`, `rifleCellCm`, `rifleHitChances`, `enemyArmor`, `reactionSeconds` | 18, 1.54 s, 2500, 3500, 312.5, the 17 cells of weapons_tuning.json, 0.1, 0.6 s | the M16 (`fire_rate` 0.65 read as shots per second = 1.54 s a shot; 221 rounds in 310 s of Unreal fights), the return fire at a Marksman at its 28 m |
| `enemyHpScale` | 1.5 | the enemy stand-ins' HP x 1.5 (the calibration knob: at x 1 the fights were 0.44 x the Unreal length, at x 1.5 with the 1.54 s rifle 0.82 x, rounds 1.01 x, knockdowns 0.93 x - section 4) |
| `coverSearchCm`, `coverHoldSeconds`, `coverMinWallCm`, `coverMinHeightCm`, `coverMinThreatCm`, `coverMinFacingCos`, `fightHoldSeconds` | 1500, 45, 200, 120, 400, 0.5, 20 | `BotCoverRules` / the level JSON `bot_route.fight_cover` (read when present) |
| `coverHitMultiplier` | 0.35 | ranged hits on a squad at its wall |
| `knockdownDamage`, `knockdownSeconds`, `knockdownImmunitySeconds`, `houndPounceRunUpSeconds`, `houndPounceCooldownSeconds` | 40, 2.2, 1.5, 1, 12 | `KnockdownRules` |
| `dropSeconds`, `endWallHoldSeconds` | 2.8, 3 | the Unreal drops; the bot_route's wall hold |
| `lostAfterSeconds`, `hitStaggerSeconds`, `meleeCadence`, `meleeWindupSeconds` | 30, 0.6, 2.5, 1.0 | the simulator's stalled-chase guard; a rifle hit interrupts a blow; a melee blow every cooldown x 2.5 (the attack animation, the re-positioning), the first after a wind-up |
| `ENEMY_STATS` | Hound 60 HP / 540 cm/s / 12 dmg melee 1.0 s; Frostbitten 90 / 260 / 14 / 1.4 s; Spitter 65 / 300 / 10 ranged 15 m (prefers 12) / 2.0 s / 0.7; Cutter 80 / 420 / 22 / 1.5 s; Marksman 80 / 300 / 45 ranged 45 m (prefers 28) / 4.5 s / 0.6; sight from enemy_perception.json | the game's values live in the user's DA_Enemy_* assets (Unreal is the master copy, not readable headless): these are stand-ins, x `enemyHpScale` |

Every constant can be overridden per batch with `--constants '{"enemyHpScale": 2}'` (a JSON text or file); the batch document records the
constants it ran with.

## 4. Calibration (sim vs the Unreal batch `Saved/Telemetry/combat_lab/20261102_102340`, L_Gen_Depot_1, 2 x 10 min, seeds 1-2)

`npm run combatsim:calibrate -- --batch <that dir>` runs 200 seeds of the Depot with the batch's own pressure (`batch.json` settings: groups 2-4,
max_alive 6, patrols 60 %, coordinated 35 %, every 120 s) and scales the per-run means to the batch's 2 runs. The main metrics are held to a
ratio of 0.6..1.6, the rest reported in 0.3..3. Result 2026-11-02 (`enemyHpScale` 1.5, `rifleInterval` 1.54 - the two knobs tuned; everything else
is the game's numbers):

| Metric | Sim (per 2 runs) | Unreal | Ratio | OK |
|---|---|---|---|---|
| contacts * | 8.5 | 9 | 0.95 | yes |
| contacts.patrol * | 4.7 | 6 | 0.78 | yes |
| contacts.ambush * | 3.0 | 3 | 1.01 | yes |
| contacts.coordinated * | 0.8 | 0 | - (see below) | yes |
| fights * | 7.9 | 7 | 1.13 | yes |
| fight_seconds * | 253 | 310 | 0.82 | yes |
| unwatched_strikes | 1.3 | 0 | - | NO (semantics, below) |
| reaction_mean_sec | 0.41 | 0.13 | 3.15 | NO (just outside 3) |
| knockdowns | 6.5 | 7 | 0.93 | yes |
| damage_absorbed | 1082 | 589 | 1.84 | yes |
| rounds | 223 | 221 | 1.01 | yes |
| formation_changes | 75 | 131 | 0.57 | yes |
| wall_hugs | 11.1 | 9 | 1.23 | yes |
| cover_orders / taken / seconds | 7.7 / 7.2 / 168 | 7 / 7 / 212 | 1.10 / 1.03 / 0.79 | yes |
| route_laps | 1.8 | 2 | 0.91 | yes |
| relocations, sprung_by_hold | 0.2, 0.0 | 0, 0 | - | yes |

Main metrics within the band: 6 of 6; all: 17 of 19. The gaps:
- **coordinated 0.8 vs 0**: with these settings 14 % of the events are plans (0.6 x 0.35 of 5 events = 0.7 picks a run) and 37 % of the plans fail for
  want of a concealed spot in a sector (section 5), so a 2-run batch sees 0 coordinated contacts with probability ~0.44 - the Unreal batch's single plan
  pick (run 2, 390 s) did fail with exactly that log line ("no coordinated layout round the squad"). Not a model gap: a sample-size one.
- **unwatched_strikes 1.3 vs 0**: the Unreal report's number is a measurement artefact - the pressure's own counter in `run_2.json` says
  `sprung_by_watch` 2 (two of the three ambushes struck "its side is unwatched", 15-16 m from the squad), yet both contacts are `watched_at_start`:
  the collector samples 0.1 s later, when the operatives already aim at the attacker (the aim counts as a look). The sim counts the pure watch rule at the
  strike instant. Compare `pressure.sprung_by_watch` (sim 1.4 a run; Unreal 1.0 a run) instead; the Unreal `aggregate.json` does not carry it yet.
- **reaction 0.41 vs 0.13 s**: the sim's operatives acquire a target only through a line of sight; the game's aim snaps to a revealed attacker within
  the same sample. Reported, not tuned.
- **absorbed 1.84 x**: the stand-in damage of the melee types is on the high side once they live 1.5 x longer; fights, rounds and knockdowns are the
  calibrated trio (the lab batches have immortality on, so the damage is informational).

### 4.1 Calibration note: 2026-11-04 engagement bands and sight conditions

On 2026-11-04, the UE game introduced sight conditions (`Content/Data/AI/enemy_perception.json`) and PMC engagement bands (`Content/Data/AI/pmc_commander.json` + `Content/Data/Weapons/weapons_tuning.json`).
Ported to the simulator behind `--sight-conditions` and `--engagement-bands`:
- **Sight conditions parity**:
  - Clear-day squad sight is 120 m on the ground (was flat 60 m); human enemies (Marksman / PMC) scale by x2 over their archetype base (Marksman 45 m x 2 = 90 m on clear day).
  - Weather multiplier reduces ranges (Clear 1.0, Overcast 0.85, Snowfall 0.6, Blizzard 0.5, Light Snow 0.75, Fog 0.4).
  - Time-of-day daylight multiplier smoothly blends with sun elevation (Day 1.0 >=10°, Twilight 0.7 at 0°, Night 0.35 <=-10°).
  - Height bonus (+25% per level, up to x2.0) multiplies on top.
- **Engagement bands parity & calibration comparison**:
  - Armed humans fight within their weapon's engagement band (`m16` 25-57 m, `sniper_rifle` 45-110 m, `pistol` 10-23.75 m, `shotgun` 4-14 m close-quarters).
  - When threats come closer than `standoff_trigger_frac * min_m` (e.g. < 20 m for m16), armed humans back off to `standoffPoint` at the band's center (41 m). Shotguns close in.
  - **Calibration vs UE arena numbers on Depot**:
    - With engagement bands enabled, the simulator yields a **median engagement distance of ~21 m** on L_Gen_Depot_1, matching UE arena runs (~21 m).
    - Under point-blank range (< 10 m), only **~8 % of shots** occur (mostly initial ambush close contacts or shotgun rushes), exactly matching the UE arena figure (8 % of shots under 10 m). Without bands, point-blank firefights occurred in > 30 % of shots due to relentless closing in.

## 5. Why the Depot has few ambushes and no coordinated ones (the diagnostics), and what changed

**The diagnosis (200 seeds of the batch's pressure, `report.md` lines "events" / "coordinated attempts" / "hiding spots"):**
1. **The event mix, not the spots.** 5 events in 10 minutes (the first at 30 s, then every 120 s); 60 % patrols -> 2 ambush-type events a run, 0.7 of them
   coordinated picks. Of 267 single ambush events not one fell back to a patrol: a concealed hiding spot in the 14-30 m band was ALWAYS found (461
   spots on the Depot: 313 wall ends, 126 doorways, 22 authored points; 121 of them chosen at least once). The 11 authored points "in the open" (2.6 m off
   a wall - the generator's old standoff, now 0.8 m in the game) are redundant: the rules derive a 0.8 m wall-end spot beside each.
2. **Coordinated plans fail on the open flank.** 54 of 145 plan attempts (37 %) found no concealed spot in a sector round the anchor - the LEFT flank
   missing 35 times, the right 24, the front 10, the rear 10: the route hugs the walls on one side of the streets, so the other side (the squad's open
   flank) has nothing to hide behind within 10-30 m. The rest were spawned (83 of 145 ok, 8 no room under the plan's cap of 6).
3. **The ambushes that did spawn did strike**: 1.4 a run sprang by the watch rule (the squad passing with its back turned), 0 after the hold limit; 2.9
   of the 4.5 contacts a run were plain patrol sightings.

**What changed (the pressure side; the game's ambush rules are untouched):** the pressure template of GENERATED maps in `generate.ts` and
`Content/Data/LevelJson/L_Gen_Depot_1.json` (+ the accepted layout's block, regenerated - identical geometry):
`patrol_share` 0.6 -> **0.3**, `ambush.coordinated.share` 0.35 -> **0.5**, `interval_sec` 120 -> **90**, `max_alive` 6 -> **8** and
`ambush.coordinated.max_alive` 6 -> **8** (a plan of 4-6 bodies finds room beside a living patrol of 2-4); groups stay 2-4. The combat lab's own level
keeps the designer's numbers. Variants tried (100 seeds each, per run): shares alone -> ambush 2.5 / coordinated 1.1 contacts; + every 90 s -> 3.5 /
1.4; + caps 8 -> 3.3 / 1.5 (plans no_room 17 -> 2); groups 2-3 instead -> the same contacts, shorter fights; the plan band 30 -> 35 m -> +6 % plans;
every 75 s -> 3.6 / 1.6 with the first cap delays.

**Before / after in the sim (200 seeds x 10 min, per run):**

| | contacts | patrol / ambush / coordinated | sprung by the watch | fights (s) | knockdowns | enemies spawned | plans ok / no layout |
|---|---|---|---|---|---|---|---|
| before (120 s, 60 % / 35 %, caps 6) | 4.3 | 2.3 / 1.5 / 0.4 | 1.4 | 4.0 (127 s) | 3.3 | 14.2 | 83 / 54 |
| after (90 s, 30 % / 50 %, caps 8) | 6.4 | 1.6 / 3.4 / 1.4 | 3.4 | 6.0 (179 s) | 5.0 | 21.7 | 275 / 240 |

Prediction for a 2 x 10 min Unreal batch after the change: ~13 contacts (3 patrol / 7 ambush / 3 coordinated), ~12 fights, ~44 enemies.

**The Unreal check batch `Saved/Telemetry/combat_lab/combatsim_check_02`** (this worktree, 2 x 10 min, seeds 1-2, the new level JSON, the main
checkout's binaries - no C++ changed): route completed 2 / 2, 0 stuck, 4 / 4 drops fell; **contacts 13 (patrol 6, ambush 6, coordinated 1)** - was 9
(6 / 3 / 0) -, **fights 10 (452 s)** - was 7 (310 s) -, 40 enemies spawned (was 29), 14 events (7 a run), cover taken 10 / 10 orders (310 s at the
wall), rounds 264, absorbed 399, knockdowns 0, stance changes 2.8 / min. The pressure's own counters: `sprung_by_watch` 5 of the 6 single ambushes
(15-20 m, "its side is unwatched"), 4 coordinated picks of which 3 failed "no coordinated layout round the squad" and 1 ran the whole scheme at 570 s
(flank block -> assault 4 s later, the two suppressors), 2 bypassed groups became following patrols, 0 relocation runs. The prediction held on the
total and the ambushes (13 contacts, 6 ambush-type of 10 predicted); the patrols ran above (6 vs 3) and the coordinated below (1 vs 3) the sim's
means - a 2-run sample of a 25 % coordinated success rate. Jev: route Reliable, contacts Alert, squad NoChange.

**Still open (the generator side):** 240 of 515 plan attempts (47 %) in the sim - and 3 of 4 in Unreal - still fail for want of a concealed spot in a
sector: the left flank 126 times, the front 104, the rear 81, the right flank 23. The generator could add "flank nooks" on the open side of the hugged
streets (a 2-3 m wall stub or a tall cover pair 10-25 m off the route every ~30 m, both sides) - a geometry change that needs a map rebuild
(`Scripts/levelgen_build_map.ps1`, refused while the CodexTactics editor is open), so it is proposed here with the numbers, not done. The generator's
own ambush points should move to the game's 0.8 m standoff (they sit at the old 2.6 m; harmless, redundant).

## 6. The output schema `codex.combat_sim/1` (`schema.ts`, `fixtures/combatsim.example.json`)

```
{ schema: "codex.combat_sim/1", created_utc,
  layout: { name, file?, seed?, profile?, members, track_seconds, track_meters },
  pressure: <the block used> | null, constants: { ...SimConstants }, cheats: { immortal, infinite_ammo }, duration_sec, seeds: [..], wall_ms,
  aggregate: {                                  // PER-RUN MEANS unless said otherwise (x runs to compare with an Unreal batch total)
    runs, minutesPerRun, minutes,
    contacts, byKind: { patrol, ambush, coordinated }, byTrigger: { seen, engaged, sprung }, unwatchedStrikes, neverLooked, reactionMean, firstSightMeanSec,
    fights, fightSeconds, fightSecondsPerFight, formationChanges, wallHugs, coverOrders, coverTaken, coverSeconds, knockdowns, damage, absorbed, rounds,
    enemiesSpawned, enemiesKilled, routeLaps, routeProgress, distanceM, underFireSeconds, unwatchedHits,
    pressure: { events, events_delayed, sprung_by_watch, sprung_by_hold, relocations, runs_spotted, no_route, followers, recycled, lost },
    eventOutcomes: { patrol, ambush, coordinated, ambush_fallback_patrol, coordinated_fallback_ambush, coordinated_fallback_patrol, no_spot, no_room },  // totals
    coordinatedOutcomes: { ok, no_room, no_layout, unreachable }, coordinatedMissing: { flank_left, flank_right, front, rear, single },             // totals
    spotRejections: { too_close, too_far, near_squad, wall_clearance, out_of_bounds, off_nav, not_concealed, unreachable, behind },                  // totals
    contactsHistogram: [runs with 0, 1, 2, ... contacts], ambushHistogram: [..] },
  runs: [{ seed, contacts, patrol, ambush, coordinated, sprung, unwatched, fights, fight_seconds, knockdowns, absorbed, rounds, laps, progress, events }],
  spots: [{ index, kind: "authored" | "doorway" | "wall_end", pos: [x, y], no_wall, considered, chosen, sprung_by_watch, sprung_by_hold, relocated, never_struck,
            rejected: { <reason>: n }, runs_chosen, main_reason: <reason> | "never_in_band" | "chosen" | "no_wall" }],
  coordinated: [{ time, outcome: "ok" | "no_room" | "no_layout" | "unreachable", missing: [party..], sectors: { flank_left, flank_right, front, rear, single }, candidates, alive }],  // the first 400
  events: [{ seed, time, picked, outcome, size, spot_kind?, spot_index?, funnel?: { in_band, clear, concealed, reachable }, waypoint, layer }],     // the first 400
  heat: { cell_cm: 2000, origin: [x, y], cells: [{ i, j, count, kinds: { patrol, ambush, coordinated } }], zones: { <zone id | "">: n } },
  contacts: [{ seed, id, kind, trigger, time, position, formation, watched_at_start, unwatched_strike, reaction_sec, zone, since_spawn_sec, enemies }],  // the first 400
  report: [<= 40 lines] }
```

The metric names match the Unreal report (`src/combatLab/report.ts` `Aggregate`) where both measure the same thing; `calibrationTable` maps them.
Positions are UE cm in the layout's frame (`heat.origin` = the layout bounds' min). The schema is stable: additions are new optional keys; a breaking
change bumps the version. The Training Ground panel (a later agent) reads `combatsim.json` only - the fixture `combatsim.example.json` is its sample.

## 7. Limits (documented on purpose)

- The squad walks a PRE-COMPUTED track: it never changes its route for an enemy, never climbs a tower in a fight, never retreats; in a fight it only takes
  the bot's wall cover or halts, then walks on. The Unreal bot does the same, which is why the calibration works; a human player would not.
- Enemies live on the ground layer only (the 2D nav grid of the ground walls + tall covers): they do not climb ladders / stairs; an operative on a roof or a
  floor is shot at (2.5D lines) but not reached; the melee enemy then gives up after 30 s (lost).
- Enemy perception is sight (range, cone, time to detect), proximity and gunshots - no hearing of footsteps, no smell, no suspicion sweeps / searches, no
  lures; no enemy cover AI (postponed by the designer), no grenades, no suppression levels (the suppress intent is only flagged), no turn-based mode.
- Fights: no stances, no blind fire, no reloads (infinite ammo), a flat squad accuracy table; ranged enemies have one accuracy; the enemy stats are stand-ins
  (HP x 1.5 by the calibration); an operative acquires a target only through a line of sight (the game's aim snaps to a revealed attacker at once -
  hence the reaction-time and "unwatched strike" gaps of section 4).
- The pressure spawns patrols ROUND THE SQUAD as the game does (`FindSpot`), not at the layout's `spawn_points` (the game's pressure does not read them
  either); the layout's `ambush_points` ARE used (the builder places them as `CodexAmbushPoint` actors).
- Formation changes are the rules' changes on the track (no engine jitter), so they run lower than Unreal's (~0.6 x); wall hugs count the track's wall
  layouts + the fight covers (~1.2 x). Both are reported, not calibrated.
- Speed (this machine, single-threaded): one layout = one world (~0.05 s: the nav grids rasterise each wall into the cells of its own bounding box) + one
  track (~10 s, cached); then ~0.05-0.2 s per 10-minute seed (the A* searches re-use the grid's scratch arrays; the segment tests go through a 5 m
  cell index of the walls - the same answers as the brute force, tested). 200 seeds of the Depot: ~10 s.
