# Turret power: generators, fuel and turret allegiance

> Status: **design v1, waits for the designer's decisions** (section 14). Claude, 2026-11-17. TASK_BOARD R17-7. No code changed.
> Source: the game designer, 2026-11-17: turrets work only when a generator feeds them; first you find out whether a turret is powered;
> a powered one is hostile (fires at us and at monsters), neutral (monsters only) or friendly (PMC and monsters); refuelling or switching
> on a dead generator makes its turrets friendly; most turrets on a map are dark, so the player chooses where to spend fuel.
> UE-only design. The Godot archive has no spec for this (checked `design/gdd/*`, `design/levels/stage-01-bunker-gate.md`,
> `docs/GAME_DESIGN_SPEC.md`): it only has generators as heat sources, "generator repair parts" in standard loot, portable turrets in rare
> stashes, a fuel-valve + fuse puzzle and "cables on the snow lead to the generator". Nothing here contradicts it.
> Related: [interactables-traps-loot](../systems/interactables-traps-loot.md), [pmc-squad-ai](../systems/pmc-squad-ai.md),
> [TACTICAL_MAP](TACTICAL_MAP.md), [STEALTH_LURE](STEALTH_LURE.md), [SUPPRESSION](SUPPRESSION.md),
> [LEVELGEN_LAYERS_REPORT](../port/LEVELGEN_LAYERS_REPORT.md), `Content/Data/Balance/props_balance.json`.

## 0. The rules on one screen

1. **Power is a network.** One generator feeds 1-4 turrets by cable (max 40 m). A turret is live only while ITS generator runs.
2. **Allegiance belongs to the generator, not to the turret.** Whoever starts a stopped generator owns its whole network.
   Player's team = friendly, a PMC team = hostile, nobody (the facility's old perimeter automatics) = neutral.
3. **One targeting rule.** A turret fires at monsters and at every human team hostile to its owner. No owner = no humans.
4. **Fuel is the only currency.** A diesel jerrycan (10 L) is a new carried item; a tank holds 20 L; a running generator burns
   1-2 L per hour (one can = roughly one fight, 4-7 real minutes). Fuel on a map covers about 40 % of its generators once.
5. **Knowledge comes in steps.** Dark or live is visible from far (lamp, sweep, exhaust plume, snow cap). The allegiance is not: observe
   the turret (2 s, line of sight, up to 40 m), read the generator's panel, or watch whom it locks on.
6. **Power is loud.** A start is heard 30 m away, the hum 18 m: monster patrols come to look and attack a running generator.
7. **Capture = stop + restart at the generator ("Seize", 4 s).** Shooting the generator dead is the cheap loud way: the network goes
   dark, half the fuel is lost and it needs a repair.

## 1. Player fantasy and pillars

**Fantasy.** A dead industrial site under snow. Somewhere a diesel still coughs, and whatever it feeds is watching a street. You have
one jerrycan. You follow a cable under the snow to find out what a generator would wake up, and decide whether that is worth your fuel.

| Pillar | What it means here | Design consequence |
|---|---|---|
| Scarcity | Fuel never covers the map | ~40 % of generators can be powered once; tanks are small; no fuel on the main route |
| Risk / reward choice | Every litre is a bet on a place | Networks differ in size, arc quality and burn; decoy networks exist; a start draws monsters |
| Information before commitment | Scouting is the game, not a tooltip | Three knowledge steps; cables are both the clue and the map; observing costs time in the open |
| Arctic survival tone | Cold and silence are the baseline | A running generator is also a warm zone; waiting for a hostile tank to run dry costs cold; snow caps mark dead metal |
| Fair opponents | PMC play by the same rules | Same ownership rule, same sight rules, no free fuel for PMC |

Non-goals of v1: wiring puzzles, cable cutting / splicing, base building, fuel as a weapon (burning jerrycans), turret hacking.

## 2. What exists today (verified in code, main `b7831e1e`)

| Piece | Today | Gap for this design |
|---|---|---|
| `ATurretActor` (`Interactables/TurretActor.*`) | Powered by default; fires at the nearest tagged enemy (monsters and hostile PMC both carry the `Enemy` tag) within 12 m, 360 deg, 16 dmg / 0.45 s, 120 HP, barricade cover x0.6, MG suppression, warms 4.5 m; broken at 0 HP (repair Engineer 2 s / others 4 s); portable (Commander carries 2) | No owner / allegiance, no arc, no lock-on delay, no ammo, never targets operatives, head snaps instantly |
| Generator = `AInteractableActor` with `ObjectType == Generator` | ONE per level, tied to the checkpoint quest chain (canister -> diesel from the APC -> generator -> gate); 200 HP; breakdown calls `ATurretActor::SetAllPowered(World, false)`; repair Engineer 2.5 s / others 5 s; events `GeneratorDamaged` / `GeneratorDown` / `GeneratorRepaired`, bus `OnGeneratorStateChanged`; heat source; paces the blast gate | Power is global, fuel is one quest boolean (`bHasFuelCanister`), no litres, no burn, no owner |
| Fuel in the inventory | Only `FlameFuel` (legacy flamethrower fuel; that weapon is out of the roster since 2026-11-08). No diesel item in `ELootItem` / `ETransferItem` | A diesel jerrycan item is new |
| Monsters vs objects (`EnemyAIRules::SelectTarget`) | Small ones (hound, cutter, frostbitten) prefer the generator (its distance counts x0.4) and, when allowed, a turret (x0.5) over an operative; big ones take a turret within `enemy_turret_threat_distance` 10 m or one that hit them last (x0.6), never the generator. `TakeGeneratorDamage` is called only from `AEnemyCharacter` attacks | Stopped generators and dark turrets are attacked too; the squad's gunfire and blasts do not damage a generator |
| PMC (`AOperativeCharacter`, `TeamId` 1+) | Utility commander, shared order catalog, `SetTeamObjective` (defender overwatch / attacker walk), monsters attack PMC | No knowledge of turrets or generators |
| Turn-based | A turret phase: every powered, unbroken turret shoots the nearest enemy within 45 m; PMC are ignored by turn-based | Owner is not considered |
| Level generator | Props layer `turrets` (`turrets_default`: density 0.91 per 10 000 m2, min 1, max 12, anchors ambush points / roof edges / behind tall cover, range window 10-40 m, arc 90 deg, 30 m spacing, none within 40 m of spawn). Real `ATurretActor` spawn is `gameplay.classes.turret_mg` on branch `claude/g003b-real-props` (NOT in main yet; main shows a greybox) | No generator prop kind, no links, no start states |
| Audio rules | `CodexAudioRules` already maps `GeneratorStarted`, `GeneratorRestored`, `GeneratorDry` | Reuse the three ids |

## 3. States and transitions

### 3.1 Generator

Three stored values: **integrity** (health 0-200), **fuel** (0-20 L), **switch** (off / on), plus **owner** (team id or none; set at
every Start, meaningful only while running).
The shown state is derived, in this order: Broken > Running > Fuelled > Dry.

| State | Meaning | Tells (section 4) | Actions offered |
|---|---|---|---|
| **Dry** | intact, 0 L, off | no lamp, snow cap, cold | Refuel |
| **Fuelled** | intact, > 0 L, off | no lamp, snow cap; panel shows litres | Start, Refuel (if < 20 L), Siphon (if >= 10 L) |
| **Running** | intact, > 0 L, on; burns fuel; warm zone 8 m | exhaust plume, amber work lamp, hum, melted snow ring | Stop, Seize (if not ours), Refuel, Repair (if damaged) |
| **Broken** | health 0; engine stopped; keeps half of its fuel | smoke wisp, panel dark, casing torn | Repair (then Dry or Fuelled; it never restarts by itself) |

```
              Refuel (+10 L, max 20 L)                 Start: owner := the starter's team
   [ DRY ] ---------------------------> [ FUELLED ] ------------------------------------> [ RUNNING ]
   0 L, off <-- Siphon (-10 L, a can) -- > 0 L, off <---------- Stop (anyone at the panel) --- burns fuel
      ^                                                                                        |    |
      +------------------- fuel reaches 0 (warnings at 60 s and 15 s, then a sputter) ---------+    |
                                                                                                    |
   [ BROKEN ] <---- health 0 from ANY state (monster melee, gunfire, blasts): half the fuel is lost-+
       +---- Repair (Engineer 2.5 s / others 5 s, existing numbers) ----> DRY or FUELLED, owner cleared
```

- **Burn.** `burn = 1.0 L/h + 0.25 L/h per live turret on the network`. One hour = 50 s of world time (the 20-minute day / 24),
  a fixed nominal hour, NOT the phase clock: each of the four day phases lasts 300 real seconds (`FEnvironmentConfig`) but they span
  5 / 4 / 4 / 11 game hours, so a phase-clock burn would run about 2.7 times faster at night than at midday. World time means: no burn in
  the tactical pause and in turn-based combat (which already freezes the clock and the weather).

| Network | Burn | One jerrycan (10 L) | Full tank (20 L) |
|---|---|---|---|
| 1 turret | 1.25 L/h | 8.0 h = 6.7 min | 13.3 min |
| 2 turrets | 1.5 L/h | 6.7 h = 5.6 min | 11.1 min |
| 4 turrets | 2.0 L/h | 5.0 h = 4.2 min | 8.3 min |

- **Pre-powered generators** (hostile / neutral at level start) do not burn until the squad first comes within 80 m
  (`burn_before_contact` false). Otherwise every hostile network would be dead by minute 11 of a long map. After contact they burn like
  any other: **waiting a hostile network out is a valid plan, paid for in cold.**
- **Breaking** a running generator stops it at once; half of the fuel in the tank is lost; the owner is cleared.

### 3.2 Turret

| Axis | Values | Notes |
|---|---|---|
| Power | **Unpowered** / **Powered** | Powered = its generator is Running and the turret is not broken. Spin-up 2 s after power comes, wind-down 1.5 s after it goes (it finishes nothing: the gun drops) |
| Allegiance | Hostile / Neutral / Friendly | The generator's owner seen from the player's side. A dark turret has none |
| Integrity | Intact / Broken (0 HP) | Existing repair (Engineer 2 s / others 4 s). A repair does not change the allegiance |
| Ammo | belt 0-250 (fixed emplacement) | 1 round per shot. 0 = **Dry**: it tracks and clicks, never fires. A friendly turret is re-belted from rifle ammo (30 rounds / 2 s). The squad's portable turret stays unlimited in v1 (Demo 01 unchanged) |
| Awareness | Idle scan -> Locking -> Firing -> (lost 2 s) -> Idle scan | Lock-on 0.8 s against humans, 0.3 s against monsters: a visible laser and a rising beep. The delay is the player's chance to pull back |

**Detection** (fixed emplacement `turret_mg`): sensor arc 120 deg centred on its mount yaw (set by the level generator), sensor
range 30 m x stance (standing 1 / crouched 0.7 / prone 0.45) x 0.6 in stealth mode; fire range 25 m; head turn rate 120 deg/s; the rear
is blind except a 2 m touch reveal. Line of fire as today (walls block, a barricade is x0.6 cover). So a prone man in stealth is seen
from 8.1 m in the arc and can reach the turret from behind unseen. What a turret tracks is shown to its owner's team while tracked
(a friendly turret is also a sensor post).

```
 power:    UNPOWERED <---- generator not RUNNING, or turret BROKEN ----> POWERED (spin-up 2 s / wind-down 1.5 s)
 powered:  IDLE SCAN --target in arc + sensor range + line of fire--> LOCKING --0.8 s / 0.3 s--> FIRING
           FIRING --belt = 0--> DRY (tracks, clicks)      LOCKING / FIRING --target lost for 2 s--> IDLE SCAN
 any:      health 0 --> BROKEN --Repair--> back to the power state its generator gives it
```

### 3.3 Options considered (the three forks that shape everything)

| Fork | Option | Cost / effect | Verdict |
|---|---|---|---|
| Who holds the allegiance | Per turret (hack each one) | More clicks, more UI, every turret a separate decision; contradicts "its turrets become friendly" | no |
| | **Per generator network** | One decision per network; the cable is the readable link; matches the designer's wording | **yes** |
| Fuel model | One boolean per generator (today's quest) | Cheapest; no "how long", no partial tanks, no residual gamble | no |
| | **Litres + burn** | One float per generator, one new item; gives duration, load, siphoning, waiting-out | **yes** |
| Capturing a live network | Destroy and repair only | Loud, loses fuel, no stealth play | allowed, but not the only way |
| | **Stop + restart at the panel (Seize)** | One rule for hostile and neutral; the cost is reaching the generator | **yes** |
| | Engineer-only hack minigame | New UI, locks Solo play out of the mechanic | no |

## 4. Reading a turret before committing

Each turret has a **knowledge level for the player's squad**: Unknown -> Power known -> Identified. It is saved and never goes back
(the plate keeps the last known state with a "last seen" mark when the truth may have changed).

| Step | How the player gets it | Range / time | Risk |
|---|---|---|---|
| **1. Dark or live** | Automatic on sight. Live: status lamp on, the head sweeps its arc, no snow on the casing, servo whine. Dark: barrel drooped, snow cap, silence. Generator: exhaust plume above the roofs (visible well beyond 60 m in clear weather), amber lamp, hum | line of sight, 60 m (weather sight multipliers apply) | none by itself |
| **2. Which network** | **Cables** on the snow from the generator to each turret: a dark line where a live cable melted the snow, a faint ridge where a dead one is buried. Seeing a cable draws it on the minimap; a seen cable end marks its generator / turret | 15 m; 6 m in a blizzard | walking the cable takes you through its arcs |
| **3a. Allegiance by observing** | **Observe**: the first click on a turret that is not Identified makes the selected operative watch it instead of walking to it. He stays still for 2 s, then the plate shows allegiance, arc cone, belt and health in rough steps | line of sight, 40 m (70 m through a sniper scope, once that rifle is in play); 2 s still | 2 s motionless; needs a line of sight, which the turret has too if he is inside its sensor reach |
| **3b. Allegiance at the panel** | Opening the generator's menu identifies the whole network: owner, litres, minutes left, every linked turret with belt and health | at the generator | the generator of a hostile network sits behind its turrets |
| **3c. Allegiance by behaviour** | Free tells: it locks on a monster and ignores a PMC patrol (hostile or neutral); it locks on the squad (hostile; the 0.8 s lock is the warning); corpses in its arc (only monsters: not proof) | - | 3c by exposure costs health |

Lamp colour is readable only once the turret is Identified (red hostile / white neutral / green friendly); before that it is a plain
amber "live" lamp. This keeps step 3 a real action, as the designer described ("first you find out ...").

**Detection while scouting.** Observing never breaks stealth by itself. A turret is not a patrol: it does not hear, it does not
search, it does not raise an alarm. It only fires. But its tracer and gunfire are heard by PMC within the existing 60 m gunfire
hearing, and its owner's team sees what it tracks. So being seen by a hostile turret is survivable once and expensive twice.

## 5. Factions: who shoots whom, who can flip

| Allegiance | Owner | Fires at | Never fires at | Start of a level |
|---|---|---|---|---|
| **Hostile** | a PMC team (`TeamId` >= 1) | the player's squad, monsters, other PMC teams | its own team | 15 % of generators |
| **Neutral** | none (factory IFF: "humans are staff") | monsters | any human | 10 % of generators |
| **Friendly** | the player's squad (`TeamId` 0) | PMC, monsters | the squad | never: only by the player's action |

**Who can flip a network**

| Actor | Can do | Cannot do | Why |
|---|---|---|---|
| Player's operative (any role; Engineer in half the time) | Refuel, Start, Stop, Seize, Siphon, Repair, re-belt a friendly turret | Flip one turret alone | Solo is the main play mode: no role lock |
| PMC fighter, v1 | Fight inside its own arcs, defend its generator, avoid friendly arcs, shoot a friendly generator dead | Refuel, Start, Seize | No new AI order needed; nothing can loop |
| PMC fighter, v2 (after arena data; open question 2) | Stop / Seize a player-owned generator that still has fuel | Carry or pour fuel | Symmetric ownership rule, but PMC never create power from nothing |
| Monsters | Attack a RUNNING generator (existing: hounds, cutters and frostbitten go for it first) and a LIVE turret (existing weights; a big monster turns on the turret that hit it) | Attack stopped generators or dark turrets (change: a silent machine is scenery) | The noise and the light draw them: this is the price of power |
| Gunfire / blasts (change) | Damage a generator like any object with health | - | Lets both sides "pull the plug" loudly |

**Turret target priority** (score = class weight x nearness; the current target is kept unless another scores 25 % higher, so the
head never jitters between two targets):

1. whoever damaged this turret in the last 3 s (x3.0);
2. a hostile human in the arc (x2.0);
3. a big monster (Brute class, x1.5);
4. any other monster (x1.0); nearest first inside a class.

It holds fire while a human it must not hit stands within 60 cm of the line of fire (the `IsMateInLine` rule the squad uses). A neutral
turret counts every human; so a neutral turret never hurts anyone by accident in v1.

## 6. Economy

**Fuel sources**

| Source | Amount | Where | Notes |
|---|---|---|---|
| Diesel jerrycan in loot (new item, table `fuel_rare`) | 10 L each; `0.4 x generators` cans per map, min 1 | dead ends, building interiors, roofs; never within 14 m of the mandatory route (existing "far from route" band) | Carry limit 1 per operative. Dropped on death as a ground item |
| Residual fuel in a stopped generator | 3-8 L in 15 % of generators | in place | Start it as it is (1-5 min of power) or leave it |
| Siphon a tank | 10 L back into a can when the tank holds >= 10 L | any intact generator | Moves fuel between networks; 6 s, loud enough to matter (4 m) |
| Captured hostile / neutral tank | what is left of 50-100 % | in place | The reward for taking a live network instead of breaking it (breaking loses half) |
| Vehicle wrecks (v2) | 10 L, once | wreck props | Reuses the Demo 01 "drain the APC" step |

**Counts per generated map** (area scales both; all in `props_autogen.json`)

| Map | Area | Turrets (1.8 per ha, was 0.91) | Generators (0.75 per ha) | Running at start | Jerrycans |
|---|---|---|---|---|---|
| Depot | 2.2 ha | 4 | 2 | 0-1 | 1 |
| Depot X4 | 8.8 ha | 16 (cap 12 -> 18) | 7 | 2 | 3 |
| X6 | ~13 ha (estimate) | 18 (cap) | 10 (cap) | 2-3 | 4 |

Start mix of generators: running hostile 15 %, running neutral 10 %, fuelled and off 15 %, dry 40 %, broken 20 % (half of the broken
ones hold residual fuel). So about three quarters of all turrets are dark, as the designer asked.

**Links.** 1 generator : N turrets, N = 1 / 2 / 3 / 4 with weights 3 / 4 / 2 / 1 (mean 2.1). Cable 8-40 m along the ground, routed
round building footprints by the generator's own grid, exported as a polyline in the props file (the game only draws it). A turret
with no generator in cable reach gets its own or is dropped with a validator warning. Networks do not share turrets.

**Placement against the tactical map** (`analysis.*`, `DangerAt`)

| What | Rule |
|---|---|
| Generator | Anchors: behind tall cover, ground-floor interiors, dead ends. Never inside a dangerous place (place term < 0.25). Hostile network: on the far side of its turrets along the route |
| Hostile turrets | Cover a chokepoint with score >= 0.5 on the mandatory route or a variant (existing `cover_route`: >= 25 m of route in the arc) |
| Neutral turrets | Map perimeter, yard gates, facing the monsters' approach lines |
| Dark turrets | 60 % at chokepoint EXITS and over the goals (places where the squad will have to hold: powering them pays off); 40 % at ordinary anchors with poor arcs (decoys: a bad investment must exist for the choice to mean something) |
| Fairness gate (validator) | Every hostile network has an approach to its generator that stays outside its own sensor arcs, or crosses at most 6 m of arc behind cover >= 140 cm. No hostile network within 60 m of spawn. At least one stopped generator within 60 m of the mandatory route |

## 7. Interaction flow

All work is done crouched at the object (the existing repair / defuse precedent), with a progress ring over the worker.

| Action | Needs | Time (Engineer) | Noise | Result |
|---|---|---|---|---|
| Observe a turret | line of sight, <= 40 m | 2 s | none | Identified |
| Refuel | 1 jerrycan, intact generator, < 20 L | 4 s (2 s) | 4 m | +10 L |
| Start | fuel, intact, off | 3 s (1.5 s); x1.5 in a blizzard | **30 m once**, then an 18 m hum | Running, owner = his team, turrets spin up in 2 s |
| Stop | running | 1 s | none | Fuelled, turrets wind down in 1.5 s |
| Seize | running, not ours | 4 s (2 s); the network is dark from second 1 | 30 m at the restart | Running, friendly |
| Siphon | intact, off, >= 10 L, a free carry slot | 6 s (3 s) | 4 m | -10 L, +1 jerrycan |
| Repair generator / turret | broken or damaged | existing 5 s (2.5 s) / 4 s (2 s) | 4 m | intact, off |
| Re-belt a friendly turret | 30 rifle rounds | 2 s | none | +30 belt |

- **Interruption.** A move order to the worker, damage to him, a knockdown or turn-based entry cancels the action. All-or-nothing: no
  fuel moves, the can stays with him. Repair keeps its existing behaviour.
- **Solo (main mode).** Click the generator: the selected operative walks up, the menu opens with the panel data. In the tactical
  pause the walk and the action are planned like any use order and run on release (`UInteractionSubsystem::OrderUse`).
- **Formation.** The role specialist goes (Engineer first, then the leader: the rule deployables already use); the others take
  outward-facing watch slots round the generator, like the ladder rule "one works, the others cover" (R17-1). The work starts when
  the watch is set or after 2 s, whichever is first.
- **Stealth.** Refuel, Stop, Siphon and Observe keep stealth. Start and Seize keep it too (a noise is not a sighting, the lure rule),
  but every patrol or guard within 30 m (walls x0.5 each, round-wall hearing as for the whistle) comes to investigate the generator.
  A start is therefore also the biggest lure in the game: it pulls a patrol into arcs you now own.
- **Turn-based.** v1: no generator actions in turn-based (the rule deployable pick-up already has); power and fuel are frozen at
  entry. Friendly and neutral turrets fire in the existing turret phase, hostile ones in the enemy phase with the same hit table.

## 8. AI

**PMC commander** (weights in `pmc_commander.json`, bounds +-50 % for the trainer; the 4-step rule of pmc-squad-ai section 3):

| Behaviour | How | Version |
|---|---|---|
| Fight inside own arcs | Cover slots (`FindCoverNear`) inside an own live arc x (1 + `turret_arc_seek` 0.4). FallBack prefers a slot that puts the pursuer into an own arc: the bait-and-ambush the designer asked for, with a turret as the second jaw | v1 |
| Avoid friendly-to-player arcs | Known friendly live arcs are a cost on slots and walk goals: x (1 - `turret_arc_avoid` 1.0 x coverage). A PMC team knows a network only after one of its men saw a turret fire or saw the lamp (team sight; same fairness as the player) | v1 |
| Defend its generator | The owning team gets the existing objective role "defender" on its running generator (`SetTeamObjective`; arena-only today, T8 switches it on for level encounters): overwatch slots within its weapon band, retake when the squad stands on it. Placement: ambushers on `BestAmbushSpots` covering the approach | v1 |
| Pull the plug | A player-owned generator becomes a rocket / fire target when no route to it avoids its arcs (`generator_break` 0.5); the RPG gunner's existing strike order | v1 |
| Seize | New order `SeizeGenerator` (attacker role, walk, 4 s at the panel) when the generator is undefended and >= 2 men live (`generator_retake` 0.6) | v2, open question 2 |

**Monsters.** A running generator is a standing noise: patrols and guards within 18 m (hearing rules, walls count) investigate it once
(60 s re-check) and attack it when they find it running; waves keep the existing weights (the small ones go for the generator, the
big ones for the turret that hit them). A stopped generator and a dark turret are no longer targets. Monsters do not avoid arcs; the
existing "dense fire scatters a pack" rule already applies because a turret suppresses as a machine gun.

**Trainer hooks** (arena result JSON, block `power`, per team; `FPmcTeamStats` gets the counters):
`generators_started / stopped / seized / broken`, `fuel_l_spent`, `turret_uptime_s`, `turret_kills` by victim class,
`damage_taken_from_turrets`, `time_in_hostile_arc_s` (bad), `deaths_to_turrets` (bad), `generator_defended_s`,
`seize_attempts / successes`, `kills_per_litre` (the power's return), `monsters_drawn_by_generator`, `turrets_lost`.
Two arena scenarios: "hold the powered point" (the squad owns a 2-turret network, PMC attack) and "break the gate" (PMC own it).

## 9. UI and feedback

| Element | Content |
|---|---|
| Turret plate | Unknown: `TURRET ?`. Power known: `TURRET - DARK` / `TURRET - LIVE ?`. Identified: allegiance colour, arc cone on the ground while hovered or selected, belt and health |
| Generator plate / menu | State, owner, `12 L - about 4:10 left`, linked turrets (a line per turret: belt, health, distance), the action buttons with their times |
| Cables | Drawn in the world; on the minimap once seen; a live cable pulses towards its turrets |
| Cursor | First click on a not-Identified turret = Observe (eye cursor), never a walk into its arc. Matches the R17-3 "first click previews, second click commits" rule. No new key |
| Feed / audio | `Generator started - 2 turrets online`, `Fuel low: 60 s`, `15 s`, sputter, `Generator dry - turrets offline`, `Network seized`; lock-on beep; spin-up / wind-down. Reuses the audio ids `GeneratorStarted`, `GeneratorRestored`, `GeneratorDry` |
| Hostile arc warning | An operative ordered to walk through a known hostile arc gets the path drawn red in that stretch (no refusal) |

**Tutorial beat** (first generated map or the combat lab; three cards, each closes on its event):
1. "Dead metal" - a dark turret and its buried cable; walk the cable to the generator (`Power.CableSeen`).
2. "Know before you pay" - observe a live turret across the yard (`Power.TurretIdentified`); it is neutral and is chewing on a hound.
3. "One can" - the squad holds one jerrycan and sees two dry generators: a single turret at the gate, or two over the yard exit. A
   patrol comes after the start (`Power.GeneratorStarted`). No right answer is scripted.

## 10. Balance numbers (draft; every value tunable, none hard-coded)

`Content/Data/Balance/turret_power.json`:

```json
{
  "schema": "codex.turret_power/1",
  "time": { "nominal_hour_real_s": 50, "burn_before_contact": false, "contact_wake_m": 80 },
  "generator": {
    "tank_l": 20, "jerrycan_l": 10, "burn_base_l_per_h": 1.0, "burn_per_turret_l_per_h": 0.25,
    "hp": 200, "fuel_lost_on_break_frac": 0.5, "heat_radius_m": 8, "low_fuel_warn_s": [60, 15],
    "refuel_s": 4.0, "start_s": 3.0, "stop_s": 1.0, "seize_s": 4.0, "siphon_s": 6.0, "repair_s": 5.0,
    "engineer_time_mult": 0.5, "start_s_blizzard_mult": 1.5,
    "noise": { "start_m": 30, "hum_m": 18, "work_m": 4, "patrol_recheck_s": 60, "pmc_hears_start_m": 60 }
  },
  "network": { "cable_min_m": 8, "cable_max_m": 40, "max_turrets": 4, "plug_range_m": 40, "spin_up_s": 2.0, "wind_down_s": 1.5 },
  "turret": {
    "turret_mg": { "hp": 200, "damage": 16, "fire_interval_s": 0.45, "fire_range_m": 25, "sensor_range_m": 30,
                   "sensor_arc_deg": 120, "turn_rate_deg_s": 120, "belt_max": 250, "pickup": false },
    "portable":  { "hp": 120, "damage": 16, "fire_interval_s": 0.45, "fire_range_m": 12, "sensor_range_m": 12,
                   "sensor_arc_deg": 360, "turn_rate_deg_s": 240, "belt_max": 0, "pickup": true },
    "lock_on_s": { "human": 0.8, "monster": 0.3 }, "lose_lock_s": 2.0, "touch_reveal_m": 2,
    "sensor_stance_mult": { "standing": 1.0, "crouching": 0.7, "prone": 0.45 }, "sensor_stealth_mult": 0.6,
    "priority": { "attacked_me": 3.0, "human": 2.0, "big_monster": 1.5, "monster": 1.0 },
    "target_switch_margin": 0.25, "hold_fire_line_cm": 60, "shares_sight_with_owner": true,
    "start_belt_frac": { "hostile": [0.6, 1.0], "neutral": [0.3, 0.8], "dark": [0.0, 0.6] },
    "rebelt": { "rounds": 30, "seconds": 2.0 }
  },
  "knowledge": { "power_known_m": 60, "observe_m": 40, "observe_scope_m": 70, "observe_s": 2.0,
                 "cable_seen_m": 15, "cable_seen_blizzard_m": 6 },
  "carry": { "jerrycans_per_operative": 1 },
  "turn_based": { "actions_allowed": false, "hostile_turrets_in_enemy_phase": true },
  "pmc": { "can_seize": false, "retake_min_alive": 2, "defend_radius_m": 25 }
}
```
`portable.belt_max` 0 = unlimited (today's behaviour). The fixed emplacement keeps today's damage and rate; only reach, arc and the
lock-on are new, so the first arena runs compare like with like.

Level generator default (`props_balance.json` -> `presets.power_default`, used by `props_autogen.json` -> `layers.power`):

```json
{
  "generators": { "per_10000m2": 0.75, "min_count": 1, "max_count": 10, "variant": "generator_diesel", "max_place_danger": 0.25,
                  "where": { "walk_layers": ["ground"], "anchors": ["behind_tall_cover", "building_interiors", "dead_ends"] },
                  "exclude": { "$preset": "standard", "spawn_cm": 4000 } },
  "turrets": { "preset": "turrets_default", "per_10000m2": 1.8, "min_count": 2, "max_count": 18 },
  "links": { "turrets_per_generator": { "1": 3, "2": 4, "3": 2, "4": 1 }, "cable_cm": [800, 4000] },
  "start_state": { "running_hostile": 0.15, "running_neutral": 0.10, "fuelled_off": 0.15, "dry": 0.40, "broken": 0.20,
                   "residual_l": [3, 8], "running_tank_frac": [0.5, 1.0], "broken_with_fuel_share": 0.5 },
  "placement": { "hostile_min_place_score": 0.5, "dark_at_exits_share": 0.6, "neutral": "perimeter",
                 "fair_approach_max_arc_cm": 600, "fair_approach_cover_cm": 140,
                 "no_hostile_within_spawn_cm": 6000, "stopped_near_route_cm": 6000 },
  "fuel": { "jerrycans_per_generator": 0.4, "min": 1, "table": "fuel_rare", "table_value": 200 }
}
```

## 11. Edge cases

| Case | Rule |
|---|---|
| Hand-made levels with the quest generator (Demo 01 lake, gate fight) | **Legacy mode**: a generator with no links feeds every unlinked turret of the level, burns nothing, is owned by the player. `SetAllPowered` keeps working. Nothing changes there |
| The squad's portable turret | Plugs into the nearest friendly running generator within 40 m when set up; otherwise dark (open question 1). In legacy mode: as today |
| Fuel runs out in a fight | Warnings at 60 s and 15 s, sputter, turrets wind down 1.5 s. The generator is Dry; the owner only matters while it runs, and the next Start sets it again |
| Both sides at one generator | The action already running wins; the other side must interrupt the worker (any damage cancels) |
| Worker dies or is knocked down mid-action | Cancelled, nothing moved; his jerrycan drops as a ground item |
| Generator broken during a refuel | Cancelled; the can stays with him |
| Turn-based starts mid-action | Cancelled; state frozen until the fight ends |
| A hostile turret is repaired by the squad | Still hostile: the allegiance is the generator's. The menu says so before the confirm |
| A trapped turret / generator | Existing trap rules first (defuse before any action). PMC may rig a fuelled generator: bait (v2, with the lure work) |
| Network captured while a turret is locking on the squad | Wind-down drops the lock at once; nothing fires during the 4 s seize after second 1 |
| Save / load | Per generator: health, fuel, switch, owner, contact flag. Per turret: belt, link id, knowledge level, mount yaw. Lock-on and spin timers are not saved (like the sniff phase of the hound) |
| A turret outside every cable reach (hand-edited props) | Validator warning; in game it is permanently dark and says "no power line" when observed |
| Sight of a friendly turret through fog of war | It reveals what it tracks to the squad while tracking (as a muzzle flash does today) |
| Jerrycan and fire | Inert in v1. "Fuel burns" is parked with the other fire ideas |

## 12. Test plan

**Automation tests (pure rules, `CodexTactics.Power.*`):** `FuelBurn` (table of section 3.1, pause and turn-based freeze, contact
wake), `StateDerivation` (four states, break loses half), `OwnerFlip` (start / seize / repair clears the owner), `TargetRule`
(3 allegiances x monster / squad / PMC / other PMC), `Priority` (attacker first, 25 % margin, hold-fire line), `Sensor` (arc, stance,
stealth, touch), `Knowledge` (never goes back), `LegacyMode` (unlinked = today's behaviour), `TuningFileMirrorsDefaults`.
Training Ground: `power.test.ts` (counts by area, link reach, start mix, fairness gate, determinism, golden files).

**Smokes (full names, registered in `Scripts/test_map.json`):**

| Smoke | Checks |
|---|---|
| `CodexTactics.PowerNetworkSmoke` | 1 generator + 3 turrets: dark -> refuel -> start -> all three live in 2 s; stop -> dark in 1.5 s; fuel burns to the table within 2 %; dry at 0 with both warnings |
| `CodexTactics.TurretFactionSmoke` | Hostile shoots the squad and a hound, not a PMC; neutral shoots only the hound; friendly shoots PMC and hound, not the squad; a seize flips all turrets of the network and none of another |
| `CodexTactics.TurretObserveSmoke` | First click observes (no walk), 2 s, plate goes `?` -> allegiance; no line of sight = refusal line; panel identifies the network; prone + stealth reaches the rear unseen, standing in the arc is locked in 0.8 s |
| `CodexTactics.GeneratorFuelSmoke` | Jerrycan from a crate, carry limit, refuel interrupted by damage moves nothing, siphon returns a can, death drops it |
| `CodexTactics.GeneratorNoiseSmoke` | A patrol 25 m away investigates a start; one 40 m away does not; a running generator is attacked, a stopped one ignored |
| `CodexTactics.GeneratedPowerSmoke` (L_Gen_Depot_X4) | Counts = props file; every cable <= 40 m and on walkable ground; start mix within one item of the config; fairness gate holds for every hostile network |
| `CodexTactics.PmcGeneratorSmoke` | PMC hold slots inside their arcs; none stands in a known friendly arc for more than 3 s; defenders retake |

Must stay green unchanged: `TurretSmoke`, `GateGeneratorSmoke`, `QuestChainSmoke`, `LakeQuestSmoke`, `DeployableSmoke`,
`TurnBasedDeploySmoke`, `SaveRoundTripSmoke`, `GeneratedProps` X4 / X6.

## 13. Implementation plan

Sizes: S = up to half a day of one agent, M = about a day, L = 2-3 days. Order = dependency order.

| # | Task | Size | Depends |
|---|---|---|---|
| T1 | `PowerRules` (pure: burn, states, owner, target rule, priority, sensor, knowledge) + `turret_power.json` loader + automation tests | M | - |
| T2 | `UPowerNetworkSubsystem` (world subsystem: networks, burn tick, events `Power.*`), `APowerGeneratorActor : AInteractableActor` (menu, actions, heat, health from gunfire / blasts), legacy mode for the quest generator, save / load | L | T1 |
| T3 | `ATurretActor`: link + owner, target rule incl. operatives, arc / lock-on / turn rate, belt, 5 Hz target search, tick off while dark | L | T1 |
| T4 | Diesel jerrycan: `ELootItem` / `ETransferItem`, drawer, ground item, loot table `fuel_rare`, carry limit, refuel / siphon | M | T2 |
| T5 | Observe click, knowledge levels, plates, arc cone, cable draw (instanced segments along the exported polyline), feed lines | M | T2, T3 |
| T6 | Level generator (TypeScript): `generator_diesel` kind, layer `power`, links + cable routing, start states, fairness gate, layers panel, golden outputs; `gameplay.classes` entry (needs `claude/g003b-real-props` merged) | L | T1 numbers |
| T7 | Monsters: running-generator noise through the lure search, stopped generators ignored | S | T2 |
| T8 | PMC v1: arc seek / avoid terms, defender objective on own generator, generator as a rocket target; weights + bounds | M | T2, T3, tactical map P2 |
| T9 | Turn-based: hostile turrets in the enemy phase, freeze, action lock | S | T3 |
| T10 | Arena `power` metrics + two scenarios; trainer ingestion | S + S | T8 |
| T11 | Tutorial cards, smokes of section 12, docs (`interactables-traps-loot.md`, HANDOFF, mechanics register) | M | all |
| T12 | Look and sound: lamp, plume, snow cap, melt line, spin-up, hum (placeholders in T5; final assets are a TANDEM request) | M | T5 |

First playable = T1-T5 on a hand-placed test map (about 7-9 agent-days). Generated maps need T6. PMC and trainer follow.

**UE 5.8 notes.** Plain C++ components and a world subsystem, no GAS. Dark turrets do not tick (a trapped one keeps its contact
check); live ones search targets at 5 Hz, not per frame (16 turrets x the enemy registry). Cables are instanced static mesh segments
on a ground-projected polyline, not `UCableComponent` (physics per cable is wasted on something that never moves) - an assumption to
confirm with a 20-cable perf check in T5. The owner is a team id, so a second PMC team needs no new code.

## 14. Open questions for the designer (each with a recommendation)

| # | Question | Recommendation | If the other way |
|---|---|---|---|
| 1 | Do the squad's own portable turrets (the Commander's kit) also need a generator? | **Yes**: they plug into a friendly running generator within 40 m. Fuel stays the single currency, and the kit turret becomes "one more barrel on my network" | A 90 s internal battery: simpler to use, but two turrets in the kit bypass the whole choice |
| 2 | May PMC seize generators? | **v1 no** (they defend, avoid, and break ours); **v2 yes** for generators that still hold fuel, after arena data. PMC never carry fuel | Full symmetry now: a stronger enemy, but an untested order and a risk of the networks changing hands every 10 s |
| 3 | May the player seize a NEUTRAL network, and is a neutral turret dangerous to stand near? | **Seize allowed** (one rule for every running generator). **Not dangerous** in v1: it holds fire when a human is in the line | Neutral cannot be seized (only broken): a clearer faction, one tactic less. Collateral fire: more tension, more "unfair" deaths |
| 4 | Is a running generator loud - does it draw monster patrols (30 m start, 18 m hum)? | **Yes**. Without it power has no downside and the answer is always "start everything you can" | Silent generators: the mechanic is only an economy, the risk side is gone |
| 5 | Is fuel heavy? | **1 jerrycan per operative, no movement penalty in v1**; measure first. Scarcity is the count on the map | No sprint with a can: a real escort problem in Solo, likely frustrating before the rest is tuned |
| 6 | Turn-based: may the squad refuel / start / seize during a turn-based fight? | **v1 no** (frozen, like deployable pick-up); hostile turrets fire in the enemy phase | AP costs (refuel 2, start 1, seize 3): possible later, needs grid work and its own balance |

Numbers that are NOT questions (tuned in the arena, section 10): share of running generators, hostile / neutral mix, tank and can
size, burn, reach and arc, lock-on, counts per map.
