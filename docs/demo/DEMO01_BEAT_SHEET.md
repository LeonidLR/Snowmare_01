# DEMO 01: Beat Sheet and Graybox Spec: «Frozen Lake» -> «Auto-Repair Garage»

> **User decisions 2026-10-11 (rev 2 review):** scope = **Location A «Frozen Lake» only** for now (one map `L_Demo01_A_FrozenLake`); the garage, the map transfer and the 4-minute horde are postponed. Generator: **knocked out only, repairable** (never a mission loss). PMC tripwires: **hidden until spotted, cold lowers the disarm chance, anyone may try (role chance)**. Followers in cover: **NO automatic stacking — cover is taught as manual orders per operative** (G7 dropped; the cover beat asks the player to put each operative into cover). Lesson failures before the lake fight: **a lethal hit only downs the operative at 1 HP and he GETS UP by himself (current behaviour of the tutorial lesson guard; "Retry from checkpoint" stays available; nobody lies until Retry)**; normal rules in the fight. Barrel push in live real time stays **pause-only** (2026-10-08 rule). **The Frozen Lake has NO enemies before the attack** (2026-10-11): it is a normal level that teaches the basic mechanics; the only dangers before the fight are cold and PMC traps (mines, tripwires, trapped crates). No stealth / patrol / vision-cone / guard-post lesson beats on the lake (none were in beats A0-A7; A6 already states "no enemies"; `demo_01_steps.json` has none); enemies appear only in the A8-A10 attack. Patrols, guard posts and stealth belong to the postponed garage (B). Cautious cover-to-cover approach (A6) stays. Beat numbering A0-A12 and the ~45 min estimate are unchanged. **Intro briefing window OFF on the demo level; its content becomes A0 walk-and-talk** (level JSON `demo.intro_dialogue: false`, the default when a `demo` object exists; ids `WT_A0_BRIEFING_1..3`, texts via the narrative sheet). Open: Susanin rescue + enemy counts (recommendation stands), TB rule fixes, housekeeping commit.

> **Change log**
> - **2026-10-11 rev 2:** barrel relocation in exploration / RT / pause / TB (new §2.5; beats A2, A4, A7, §5 W1–W3, B2–B3).
>   Squad-placed traps: mines, tripwires, and booby-trapping objects ("mining a barricade") (new §2.6; beats A7, §5 W2, B1–B3).
>   **No feeling of safety (sudden horde, no 60 s prep):** A7 becomes "read the ground": approach signs, a hidden east lane, choke points.
>   Also in A7: turret training (deploy, rotate, power from the generator) and the generator as the key object (heat, turret power, door).
>   Engineer repair moments; the new blast-door rule (opens only while the generator runs, full after W3; another agent implements it).
>   Lake waves on three lanes + the breach; anticipation debrief (§5.3); garage door traps before the 4-min horde.
>   Mechanics check (§9), build lists (§10), questions (§12) updated. Barrel heat radius corrected to 5.5 m.
> - 2026-10-11 rev 1: first draft (Lead Level Designer).

Target path: `docs/demo/DEMO01_BEAT_SHEET.md`. Author: Lead Level Designer (studio role), creative-director lens applied.
Status: DRAFT for the user's review. Where this sheet and `05_producer_plan.md` disagree, this sheet wins, because it follows
the user's demo description of 2026-10-11 and the cover-movement addition.

Sources checked in code (read-only): `SquadFireRules` (elevation), `DeployableRules` (defusal and cold), `TripwireRules` / `ATripwireActor`,
`AProximityMineActor`, `ALootCrateActor`, `AEnemySpawnPoint` + `UWaveSubsystem` (flank breach), `URecruitSubsystem` (Susanin),
`UHordeSubsystem` + `Content/Data/AI/horde.json`, `ULevelEncounterSubsystem` (ambush), `FGameFlowStateMachine`,
`UQuestSubsystem` / `QuestChain`, `ACameraZoneVolume` + `UColdSurvivalComponent`, `ColdRules`, `CoverTraceRules` /
`CodexTacticsPlayerController::TryHandleCoverClick`, `USquadAutonomySubsystem`, `UDialogueSubsystem` / `ADialogueTriggerVolume`,
`USaveGameSubsystem` / `UMissionSessionSubsystem`, `enemy_perception.json`, `tactical_heat.json`, `OperativeMovementRules`,
`GorkyGridManager`, `GameFlowTypes`. Godot concept art `design/art/concepts/concept_bunker_frozen_lake_isometric.png`
(beacon poles across a cracked lake with pressure ridges, a fortified station on the shore) is the mood reference for A.

---

## 0. One-paragraph pitch

The game starts from the CommonUI menu with NEW GAME. Three operatives (Commander, Engineer, Medic-Sapper Sokolova) set off across a frozen lake
in a rising wind, **talking as they walk**. The lake teaches the body: move, split up, regroup, crouch, go prone. Then it teaches the cold:
barrel and heat pack. Then the buildings: scout inside, loot crates, learn that the **PMC left traps in the worst places**, and
that **numb fingers cannot disarm a mine**. Stage two is a **quest across the ice**: find fuel, siphon diesel from a stranded APC
(approached **cover to cover, like soldiers**), power the weather station. The radio mast's power-up wakes the lake. The station-yard
fight is taught in **three forms** (real time -> tactical pause -> turn-based). It brings a **flank breach** through the boathouse
and a **freezing survivor (Susanin)** to rescue. The squad then takes the road to a **closed auto-repair garage**: patrols, tripwires,
mines, an ambush. If that fight runs past **4 minutes**, a **second horde** arrives, so the player learns to finish fast from the **mezzanine**.
Total ~45 min first-time (cut list to ~35 min in §2.4).

---

## 1. Structure and transition

### 1.1 Two locations

| | Location A «Frozen Lake» | Location B «Auto-Repair Garage» |
|---|---|---|
| Map | `L_Demo01_A_FrozenLake` (new, user) | `L_Demo01_B_Garage` (new, user) |
| Level JSON | `demo01_a_frozen_lake.json`: `combat_start: "button"`, 3 sudden waves, `horde_enabled: false`, `breach_wave: 2`*, `susanin_wave: 3`* | `demo01_b_garage.json`: `combat_start: "ambush"`, no waves (placed enemies = the fight), `horde_enabled: true`, `horde: { trigger_seconds: 240, min_distance_m: 20, max_distance_m: 40 }` |
| GameMode | BP child of `ACodexTacticsGameMode`, `LevelJsonFile` = A's JSON; Panic off on the operative BPs or via a demo flag; vision cones on only for the attack (no enemies before it) | same BP class with B's JSON (2nd BP or a per-map World Settings override) |
| Space | Open ice ~260 x 180 m, 4 building clusters, station yard arena | Yard ~70 x 50 m around a closed hall 36 x 24 m, height 7 m, mezzanine at 3.0 m |
| Fight type | Wave defence (quest-triggered sudden horde) | Stealth -> ambush (patrols / guards) + 4-min mid-fight horde |
| Time | ~30 min | ~15 min |

`*` = new JSON keys (Claude, S). Today the breach and rescue waves are random draws from [1,2,3] (`UWaveSubsystem::Initialize`).

### 1.2 Transition A -> B: recommendation = **two maps + squad carry-over**

What exists:
- `USaveGameSubsystem::LoadGameWithTravel(slot)` reopens **the save's own map** and applies the **whole** save (squad + world + quest + flow).
- `UMissionSessionSubsystem::PendingLoadSlot` survives `OpenLevel`.

What does not exist: "open the NEXT map and bring only the squad". Applying an A save to B would also try to restore A's world (crates, quest, gate).

| Option | How | Claude cost | User cost | Verdict |
|---|---|---|---|---|
| **A. Two maps + squad transfer** | New `ALocationExitVolume` (target map, CP name). On entry outside combat it writes a transfer slot with the existing save writer, then `OpenLevel(B)`. B applies **only** the squad sections: members, health, cold, items/ammo, weapons, stance, leader, Susanin recruited. It skips world, quest and flow. B gets autosave `CP_B0`. The black load cover already exists. | **M** (2-3 d incl. smoke `DemoTravelSmoke`) | Two small maps, one exit volume, one `PlayerStart` | **Recommended** |
| B. One map, two zones, no streaming | Both locations in one level | **L**: `UWaveSubsystem::AdoptLevelEnemies` adopts **every enemy in the world**, so the garage patrols would join the lake fight and must die to win it. Any placed enemy turns the whole level into an ambush level (`ULevelEncounterSubsystem`: Auto -> ambush), which breaks A's quest -> wave flow. There is one level JSON per world (waves, combat start, horde). All three need an "encounter zone" refactor. | One huge map, mixed exterior/interior lighting | Reject |
| C. Persistent level + streamed sublevels | As B with streaming | **L+**: same refactor plus streaming state in saves | Sublevel setup | Reject for demo 1 |
| D. Two maps, no carry | B starts with its JSON `squad_loadout`, cold 0 | **S** | same as A | Fallback only: loses Susanin, wounds and the items found, which breaks the "your choices carry" feeling |

Narrative bridge: after the A fight the boathouse gate stays open, and the road east leads to "the PMC motor pool". `ALocationExitVolume` sits 15 m
down the road. Feed line `WT_A12_01`, then a fade, then B. B opens on the yard edge with walk-and-talk `WT_B0_*`.

---

## 2. Global design rules and metrics (both locations)

### 2.1 Hard numbers from code (build to these)

| Topic | Number | Source |
|---|---|---|
| Camera | Isometric, pitch -35.26 deg, FOV 30, distance 26 m explore / 28 m combat, zoom 8-40 m | `TacticalCameraRules` |
| Occlusion | A wall of height H hides H / tan(35 deg) = **1.43 x H** of ground behind it (2.5 m wall -> 3.6 m). **No roof cutaway exists.** | derived |
| Ground height | **Keep walkable base ground at world Z ~ 0.** The cold "elevated wind x2" test uses absolute Z >= 140 cm (`FColdConfig::ElevatedHeight`, feet Z). A lake at Z = 500 freezes everyone twice as fast. | `ColdSurvivalComponent.cpp:159` |
| Speeds | walk 2.2 m/s, sprint 7.25, crouch 1.25, crawl 0.62. Patrol enemies walk 1.9 | `OperativeMovementRules`, `AEnemyCharacter` |
| Capsule | r 0.40 m, h 1.8 / 1.17 / 0.63 m | `AOperativeCharacter` |
| Doors | personnel **>= 1.6 m** clear (2 capsules + nav margin), roller doors **4.5 m**, stairs **>= 1.2 m** | LD rule |
| Cover | min 40 cm; **low < 130 cm** (crouch behind, fire over); 130-180 counts as low (avoid: awkward); **high >= 180 cm** (fire round corners / blind). Use **low 90-110 cm, high 220-250 cm**. | `CoverTraceRules` |
| Cover click | Wall surface within 1.6 m of the click. Slot 45 cm off the wall. Corners found only within **2.0 m** of the slot (5 x 40 cm probes). Shimmy = click along the same wall. | `CoverTraceRules` |
| Cover effect | High cover absorbs 90 % in the frontal arc, low 35 % crouched, prone behind low cover counts as high; **cover halves wind chill** (`CoverWindChillMultiplier` 0.5); hides from observers in the arc (`IsHiddenInCoverFrom`) | `CoverRules`, `ColdRules` |
| Partition length | **3-5 m** so both corners are reachable from the middle; spacing between cover pieces **4-7 m** (2-3 s sprint, 3-5 s crouch) | LD rule from the above |
| Elevation | Shooter **feet >= 0.5 m above the target's feet** = elevated: **+25 % range, +15 % damage, shots pass over low cover/barricades**. Dead zone: a target >= 0.8 m below within **2.4 m** horizontally at >= 45 deg cannot be shot. | `SquadFireRules` (Godot parity) |
| M16 range | 25 m; crouched x1.15 = 28.8; prone x1.35 = 33.8; elevated x1.25 = 31.3 | weapons + `SquadFireRules` |
| Turn-based | Hold Space 1.5 s. Grid 14 x 14 x 1.5 m = **21 x 21 m**, centred on the squad. Squad feet within **+-0.5 m** of the median ground within 10.5 m, otherwise refused ("not on high ground or in a pit"). 8 AP squad / 6 enemy. | `CodexTacticsPlayerController`, `GorkyGridManager`, `TurnBasedRules` |
| Tactical pause | Tap Space. 30 s, 3 charges per wave, 20 s recharge, orders within 12 m. The free pause at a sudden wave is unlimited and spends no charge. | `FGameFlowConfig`, `PauseOrderRules` |
| Enemy sight | hound 15 m / 70 deg half, frostbitten 20 m / 50, brute 20 / 45, marksman 45 / 30; visibility crouch x0.7-0.75, prone x0.35-0.4 | `enemy_perception.json` |
| Enemy hearing | walk 5-7 m, run 10-14, crouch-walk 2-3.5, crawl 1-1.5; gunshot 35-45; explosion 50-60; hound smell 12 m; walls x0.5 each | same |
| Mines (level) | Hidden until spotted at 4.5 m (sapper 6 m), trigger 1.6 m, blast 3.5 m / 120 (squad 75 %) | `AProximityMineActor` |
| Trapped object | Trap 85 dmg / 3.5 m, crate contact 1.8 m, crate defuse 2 s, open 1.8 s | `AInteractableActor`, `ALootCrateActor` |
| Tripwire | 1-5 m span, wire 30 cm high (**prone passes under**), blast 140 / 4.5 m, stagger 1.5 s, disarm 3 s medic-sapper only, 10 % fumble | `TripwireRules` |
| Heat | Barrel heat radius **5.5 m** (+0.5), burns 35 s, recovery 8 %/s; a hound within 3 m of a burning barrel flees (fear of fire). Generator warm zone 4 m. Heat pack -35 cold + 45 s at x0.5 accumulation (stack 3; start kit C1 / E1 / M2) | `HeatSourceComponent`, `tactical_heat.json` |
| Cold | Open 1 %/s (fortitude cuts up to 40 %), crouch x0.85, prone x0.6, sprint warms 1.5 %/s. Zones: Closed x0 / Shelter x0.5 / Standard x1 / Blizzard x2.5. Misfire from 60, aim penalty from 50, weapon freezes at 90, HP loss at 100 | `ColdRules`, `ACameraZoneVolume` |

### 2.2 Defusal under cold (the core "traps + cold" lesson, existing formula)

`chance = role base + stance (stand -15 / crouch +10 / prone +25) + luck x 0.5 - cold (>25: 15, >50: 30, >75: 50) - 25 per failed attempt`.
"Dangerous" below 45 % gives a warning first; a failure detonates 40-80 %.

| Operative (base, luck) | crouched, cold <= 25 | 26-50 | 51-75 | > 75 | prone, cold <= 25 | prone, 51-75 |
|---|---|---|---|---|---|---|
| Medic-Sapper (60, 35) | **88 %** | 73 % | 58 % | **38 % (danger)** | 99 % | 73 % |
| Engineer (45, 30) | 70 % | 55 % | 40 % (danger) | 20 % | 85 % | 55 % |
| Commander (35, 25) | 58 % | 43 % (danger) | 28 % | 8 % | 73 % | 43 % |

The action menu already prints "Operator: X (Stance) | Success chance ~N %", and the warning line says "my fingers are numb" when cold.
Design every trap lesson so that the **cold Medic-Sapper, crouched, reads ~38-58 %**, warms up, and comes back to **88-99 %**.

### 2.3 Text and voice pipeline

- **Walk-and-talk** = `ADialogueTriggerVolume` -> `UDialogueSubsystem::PlayInFeed`, which is non-blocking and timed per line. Ids below are placeholders
  (`WT_<beat>_<n>`), written in the narrative sheet (Gemini + writer) and exported to `narrative_manifest.json`.
- **Hint cards** (`TUT_<beat>_<verb>`) = the tutorial layer from `03_ux_designer.md` (not built yet): key on the card, reason in the radio line.
- **Blocking windows** (`StartDialogue`) only for the Susanin distress/recruit (existing) and nowhere else. The 15-line intro is replaced
  by the walk-and-talk A0 (see gap G2).

### 2.4 Pacing chart (intensity 1-5)

```
min  0    5    10   15   20   25   30   35   40   45
A    1 1  1 2  2 2  3 2  2 3  3 4  5 5  4 1
     A0 A1 A2   A3 A4  A5 A6 A7  A8-A10   A11/12
B                                         2 3 3 4 5 5 1
                                          B0 B1 B2 B3 B4 B5
```
Rest points: A2 barrel, A4 crate loot, A7 station interior (Closed, x0 cold), between-wave panels, A12 road, B5 end.
**Cut list to ~35 min:** merge A1 + A2 (barrel at the end of the crossing), drop A5's west canister detour (canister in the depot), drop W3's Brute.

### 2.5 Moving barrels in the four modes (rev 2; facts from code)

The same verb is taught in all four modes because each mode invokes it differently.

| Mode | How it is invoked | Limits | Status |
|---|---|---|---|
| **Exploration** | Click the barrel; the leader walks up (2.2 m) and the action menu opens with **Light (1 match)** and **Push**. Push shows a placement ghost: R or the wheel rotates 45 deg, LMB places, RMB/Esc cancels. The worker walks up, pushes it (it rides 1.35 m in front of him), stops 1.8 m from the spot and steps back 1.6 m. **A burning barrel can be pushed**, and its warmth moves with it. | Spot within the worker's **15 m** placement radius (cyan ring). Refused at **cold >= 80 %** or **HP < 50 %** ("too cold to lift"). Dropped when a mine is spotted or the carrier is hit. A leader alone in a camera zone may push even in a fight. | **EXISTS** (`URelocationSubsystem`, `ABarrelActor::CanPushNow`; `RelocationSmoke`, `BarrelSmoke`) |
| **Real-time combat** | The barrel menu opens at once but offers **only Light** (user decision 2026-10-08: no relocation in live combat). **Ctrl+click** on a barrel = targeted shot, which detonates it: fire blast **120 dmg in 5.5 m** to enemies (burning 4 s), 80 to operatives, 10 s fire, then a fire puddle. A push ordered in a pause keeps running in real time after the release. | No direct push in live real time. A carry started outside the pause is dropped when combat begins ("Combat alert!"). | **PARTIAL, by design** (push = pause order; Ctrl+click EXISTS, `TargetedShotSmoke`). See Q6 |
| **Tactical pause** | Tap Space, click the barrel -> menu -> **Push** -> spot. Or **Light** (planned, marker, the match is spent only on execution). Or **Ctrl+click** = planned shot (it fires on release, after a reload if needed). Everything **executes on release**, and the push keeps running in real time. | Paid pause: **12 m** order radius from where the worker stood when the pause began; 30 s; 3 charges per wave. The **free pause** at a sudden wave has **no radius limit** and spends no charge. | **EXISTS** (`PauseBarrelOrderSmoke`, `RadiusRingSmoke`) |
| **Turn-based** | The active unit stands on a cell **orthogonally adjacent** to the barrel. Click the barrel (or the panel button «Barrel [F]») to pick it up; free adjacent cells highlight; click one. The barrel moves **1 cell (1.5 m)**, the unit steps onto its old cell, **2 AP per step**. Ctrl+click a barrel = grid shot, **3x3 cells, 80 dmg**, burns 3 rounds. Esc/RMB or a click on its own cell cancels. | 8 AP, so up to 4 steps (6 m) if the unit does nothing else. Flat ground only (TB rule). | **EXISTS** (`TryPushAdjacentBarrel`, `TurnBasedPushSmoke`) |

Completion events (tutorial layer G1, all new): `RelocationOrdered(bInPause)`, `RelocationDone(Object, Worker)`, `BarrelLit`, `BarrelExploded(Instigator, Kills)`, `TurnBasedRelocate(Object)`.

### 2.6 Squad-placed traps (rev 2; facts from code)

| Trap | How it is placed | Who / cost | Arming / trigger / damage | Modes | Status |
|---|---|---|---|---|---|
| **Proximity mine** (deployable) | Action bar or drawer «Mine», or **F set-up from supply**: a placement ghost (R/wheel 45 deg) and a click; the operative walks there and lays it. Items route to the role specialist (mines go to the Medic-Sapper first). | Anyone with a mine in supply. **Mishap** (the mine goes off while laying): Medic-Sapper **2 %**, others **10 %**, **+ cold/100 x 20 %** (a frozen Commander has 30 %). | Arms after **3 s**. Trigger **1.6 m**, blast **120 / 3.5 m**. **Friendly fire ON**: it also triggers on operatives (squad takes 75 %). Visible amber to the squad. Can be picked up again (defusal first). | Exploration / preparation: radius 15 m. **Pause: planned, 12 m** (free pause unlimited), runs on release. **Live RT: not available.** **TB: F ghost on a cell, 2 AP.** | **EXISTS** (`DeployableSmoke`, `TurnBasedDeploySmoke`) |
| **Barricade** (deployable, cover) | Same F flow, rotate 45 deg. 3 x 0.6 x 1 m low cover, 200 HP; enemies never vault it, the squad may. | Engineer routing; anyone may place. | Blocks prone shooters, crouched cover 0.8. | as mine; **TB 3 AP** | **EXISTS** |
| **Turret** (deployable, rev 2) | F -> Turret -> ghost, R rotates, click; routed to the Commander first. | anyone may place | Fires at the nearest visible enemy **<= 12 m**, 16 dmg / 0.45 s, walls block, a barricade in its line = 60 %; 120 HP, breaks at 0. **Powered by the running generator** (a breakdown unpowers every turret); powered = heat source 4.5 m. Repair: Engineer 2 s, others 4 s. Enemies target turrets (small 0.5 x, large 0.85 x and any turret within 10 m). | as mine; **TB 3 AP** | **EXISTS** (`TurretSmoke`) |
| **Tripwire** (MUV-3 + 2 grenades) | Drawer line «Tripwire»: **click 1 = anchor A** (a bracket if an object stands there, else a ground peg), **click 2 = anchor B** (cyan valid / red invalid, span **1-5 m**). The **Medic-Sapper** (else the worker) walks there and rigs it in **2 s**. | Costs **2 grenades** from the squad (start: 2 per operative). | Arms **1.5 s** after rigging (the rigger is ignored until he leaves). Wire at **30 cm**: **prone passes under**. Blast **140 / 4.5 m**, full armour penetration, **1.5 s stagger**, squad 75 % (friendly fire ON). **Enemies do not avoid wires** (nav cost 1); operatives path round their own. | Exploration / pause (to verify in the pause); **refused in TB** ("in the turn-based fight"). Not in live RT. | **EXISTS** (squad); level-placed PMC wires = G3 |
| **Booby-trap an object** ("mine the barricade") | Any interactable's action menu has **«Set trap (N)»**, where N = the **leader's** grenades. It is instant once the menu is open (the leader stands within 1.5 m). | 1 grenade from the leader's personal inventory; any role. | Grenade charge **85 / 4 m**. **Enemy contact 1.8 m** sets it off on **barricades, crates and turrets** (a trapped barricade is wrecked by its own blast; an enemy that strikes it also sets it off). **Ctrl+click on a trapped object = remote detonation**, also plannable in the pause. A trapped **barrel** blows the barrel itself (fire blast 120 / 5.5 m), but **only on a shot or a failed defusal**: barrels have no enemy-contact check. Squad members get the defusal warning on their own trap. | Exploration: yes. RT fight: only where the menu opens (barrel menu yes; **barricade menu only in the pause**). Pause: to verify (the leader must walk to 1.5 m in a 0.02 dilated world). **TB: no grid action.** | **EXISTS** for barricade / crate / turret / barrel / quest objects (`TrapWithGrenade`, `DeployableSmoke`). **PARTIAL**: no TB action; barrels lack contact trigger; no "door" object (use a tripwire across the doorway) |

Loadout source: level JSON `squad_loadout` (`mines_count`, `barricades_count`, `turrets_count`, `matches`) + grenades 2 per operative (`AOperativeCharacter::GrenadesCount`) + crates. Demo A start: **mines 2, barricades 1, turret 1, matches 3**; the A4 crate adds the tripwire kit (2 grenades) + 2 grenades. Demo B: carried from A (G8).

Completion events (G1, new): `DeployablePlaced(Type, Worker, bInPause, bTurnBased)`, `TripwireRigged`, `ObjectTrapped(Object)`, `TrapTriggered(Trap, Victims, bByEnemy)`, `MineMishap`.

---

## 3. Location A «Frozen Lake»: layout

North up. Base ground Z = 0. Ice is flat (+-0.2 m). Pressure ridges are the low cover. Beacon poles (amber lamp, 4 m) every 25 m mark the safe ice road
(navigation language: **amber = path / warmth**, cold blue = everything else; art bible).

```
                         N
  +----------------------------------------------------------------------------+
  |  rocky shore / cliffs (blocking)                                           |
  |        [A4 PMC DEPOT]                    [A7 WEATHER STATION]   BOATHOUSE  |
  |        shed 10x8  guard hut 5x4          main bldg 18x10 + mast  =GATE=>  road east -> EXIT(A12)
  |           \ tripwire door / mines          yard 30x30 = ARENA  (TB pad)    |
  |            \                                  ^ spawn lanes N on ice 45-60 m
  |   B7 o canister cache                        /                             |
  |   (beacon 7, west)        ridge field       /                              |
  |        \               [A6 STRANDED APC]---/                               |
  |         \                pressure ridges 60-110 cm, 2 wrecked containers   |
  |          o---o---o---o-----[A3 FISHING CAMP]  3 huts + barrels             |
  |         beacons 1..5   (A1 crossing, 130 m)                                |
  | [A0 DROP] snowcat wreck, shore lee (Shelter)                               |
  +----------------------------------------------------------------------------+
  overall ~260 x 180 m; critical path ~520 m walking (~4 min pure walking)
```

| Zone | Size | Key geometry | Cold zone (`ACameraZoneVolume`, camera switch OFF where noted) |
|---|---|---|---|
| A0 Drop | 40 x 40 m shore lee | snowcat wreck (high cover 2.4 m), 2 boulders, PlayerStart | **Shelter x0.5** (switch off). With gap G5: float x0.2 |
| A1 Crossing | corridor 130 x 30 m | beacons 1-5 every 25 m, 3 pressure ridges (low 0.9-1.1 m, 6-10 m long, 1 with `Vault` tag), 1 crack 2 m wide (nav blocker, forces a 20 m detour), fork at beacon 3 | Standard x1 |
| A3 Fishing camp | 30 x 25 m | hut 1 (6 x 4, door 1.6 m, heated? no), hut 2 (6 x 4), shed (8 x 6, two doors), 2 fuel barrels outside, low fish-rack cover 1.0 m | outside x1; huts Shelter x0.5 with fixed camera ON |
| A4 PMC depot | 40 x 30 m, north shore | storage shed 10 x 8 x 3 m (one door 1.6 m: **PMC tripwire across it**), guard hut 5 x 4, sandbag ring (low 1.0 m), 2 hidden mines on the shortcut path, wire fence (high) | outside x1; shed Shelter x0.5 (fixed camera ON) |
| A5 Canister cache (beacon 7) | 15 x 15 m | beacon 7, tarp-covered sled, ice-fishing tent (low cover) | x1 (optional Blizzard x2.5 strip of 20 m to force a heat-pack use) |
| A6 Ridge field + APC | 60 x 40 m | stranded APC (Vehicle quest object, 7 x 3 x 2.6 m = high cover), 2 wrecked containers (6 x 2.4 x 2.6 m), 5 ice-ridge partitions (3-5 m long, alternating low 1.0 m / high 2.2 m), spacing 5-6 m | x1. Cover halves the wind: this is where cover gets taught as warmth too |
| A7 Weather station | main building 18 x 10 m (2 rooms + corridor 2 m), yard **30 x 30 m flat (contains a 21 x 21 m TB pad, +-0.2 m)**, radio mast, boathouse 12 x 8 m with GateActor (door 7 m) | yard cover: 4 sandbag walls (low 1.0 m, 4 m long), 2 concrete blocks (high 2.2 m, 3 m long), 3 fuel barrels (explosive targets), generator shed 4 x 3 | building interior **Closed x0** after the generator runs (Shelter before). Yard x1 |
| A8-A10 Arena | yard + 60 m of ice north | spawn points on the ice 45-60 m N (beyond the 25 m M16 range and the 20 m frostbitten sight). Dynamic breach point behind the boathouse 18 m S of the line. SusaninSpawn 28 m W on the shore with an unlit barrel 2.5 m from it | waves `cold_drain_mult` 1.0 |
| A12 Road | 20 m wide, 60 m east | `ALocationExitVolume` 15 m past the gate | x1 |

---

## 4. Location A: beats

Format per beat: **Goal / Teaches (key) / Walk-and-talk / Hint cards / Setup (place) / Metrics / Cold / Success-fail / Checkpoint / min**.
Completion events marked (E) need the tutorial command choke point (UX W2). Marked (X) they exist today.

### STAGE 1: LEARN TO MOVE AND SURVIVE

#### A0. Menu -> drop (cold open)
- **Goal:** start moving within 20 s of NEW GAME. No standing dialogue.
- **Teaches:** camera (WASD / edge, wheel, Q/E), click-move.
- **Walk-and-talk:** `WT_A0_01..06` start on spawn as feed lines while the camera eases onto the squad. Content: HQ drop confirmation, Sokolova on the cold, Engineer on the dead radio. **Replace the 15-line blocking intro** (gap G2).
- **Hint cards:** `TUT_A0_CAMERA` (pan / zoom / rotate; done after 2 camera commands or 20 s), `TUT_A0_MOVE` (click the ground at the amber ring 12 m ahead; done when the leader enters the ring).
- **Setup:** PlayerStart; snowcat wreck (art); `ATutorialMarkerActor` ring at beacon 1 (gap G4); `ADialogueTriggerVolume` (box 10 x 10 m on the start).
- **Metrics:** start -> beacon 1 = 25 m (11 s walk).
- **Cold:** Shelter x0.5 (G5: x0.2).
- **Success:** leader at beacon 1. **Fail:** none.
- **Checkpoint:** `CP_A0` = mission start.
- **~1.5 min**

#### A1. The crossing: squad body language (walk and talk)
- **Goal:** cross 130 m of ice to the fishing camp while the squad banters.
- **Teaches:** double-click sprint; select 1/2/3 and drag box-select; **B solo (split up) and B again (regroup into formation)**; **T guard**; **C crouch, V prone, Z stand, Alt + C/V for the whole squad**; vault a low ridge.
- **Walk-and-talk (triggered by volumes along the beacons):**
  `WT_A1_01-03` (beacon 1, sprint: "Pick up the pace, Captain, standing still kills here"),
  `WT_A1_04-06` (beacon 2, crack ahead: Engineer volunteers to scout, which sets up SOLO),
  `WT_A1_07-09` (beacon 3, regroup: "Formation, close it up"),
  `WT_A1_10-12` (beacon 4, wind gust: Sokolova "Get low, the wind cuts less", which sets up crouch / prone; prone is also "how you crawl under a wire").
- **Hint cards in order:** `TUT_A1_SPRINT` (double-click), `TUT_A1_SELECT` (press 2: the Engineer leads), `TUT_A1_SOLO` (B: the others hold; scout the crack edge 20 m away), `TUT_A1_REGROUP` (B again), `TUT_A1_GUARD` (optional: T keeps one in place), `TUT_A1_STANCE` (C / V / Z; Alt = whole squad), `TUT_A1_VAULT` (click past the ridge).
  Done by: stance changed (X `OnStanceChanged`), solo toggled (E), leader changed (X), vault (E).
- **Setup:** beacons 1-5 (art + amber light); 3 pressure ridges (one `Vault` tag, 0.8 m high, 8 m long); a 2 m crack 40 m long across the direct line (nav blocker), whose detour around the end is 20 m; 4 `ADialogueTriggerVolume` (8 x 30 m strips across the corridor at beacons 1-4); 2 tutorial trigger volumes for SOLO / STANCE.
- **Metrics:** corridor 130 x 30 m, beacons 25 m apart; solo scout point 20 m off the line (the "split up" distance must be visible on screen: <= 26 m camera distance).
- **Cold:** Standard x1. At walk pace plus lessons (~3 min) the squad reaches **25-40 %** at the camp. That is intended: they "start to freeze" exactly at A2. With x1 a slow player can reach 60 %, so the A2 barrel must be on the arrival path.
- **Success:** leader inside the camp volume. **Fail:** none (cold cannot kill before A2 at these rates: 100 % needs > 100 s standing still in x1 after fortitude).
- **Checkpoint:** `CP_A1` at beacon 3 (exploration autosave, gap G6).
- **~4 min**

#### A2. The cold bites: barrel and heat pack
- **Goal:** get everyone <= 30 % cold.
- **Teaches:** read the cold bars / vignette; **move a barrel in exploration** (click barrel -> menu -> **Push** -> ghost -> click the spot, see §2.5) **into the wind shadow of hut 1** where the squad shelters; **light it with a match** (menu -> Light); warm zone 5.5 m; **U heat pack** (drag onto a mate in the drawer); crouch / prone slow the cold; **"too cold to lift" at 80 %** (send the warmest member).
- **Walk-and-talk:** `WT_A2_01-04` fire on the first cold tier (event, gap G1 "cold tier changed"). Sokolova: "Fingers first, then the trigger finger." Commander: "Barrels by the huts. Fishermen kept fires." `WT_A2_05` Engineer: "Not out here in the wind. Roll it to the hut."
- **Hint cards:**
  - `TUT_A2_COLDBAR` (first tier).
  - `TUT_A2_PUSH` (marker on barrel 1: "Click the barrel -> Push, then click where it should stand. Put it by the hut wall." Done on `RelocationDone` (E) with the barrel within 3 m of the hut-1 wall marker).
  - `TUT_A2_LIGHT` (done on barrel lit (E)).
  - `TUT_A2_HEATPACK` (when one member is > 40 % and outside the 5.5 m disc; done on item used (X `OnItemUsed`)).
  - `TUT_A2_TOOCOLD` (remedial, on the "too cold to lift" refusal: "Above 80 % cold nobody can lift. Send the warmest operative (1/2/3).").
- **Setup:**
  - Barrel 1 (unlit) at the **open ice edge, 10 m from hut 1**, in the wind (x1), and an amber floor decal "wind shadow" spot against hut 1's lee wall.
  - Barrel 2 (unlit) 6 m from barrel 1 (spare, second match).
  - Matches 3 in the loadout (JSON `squad_loadout`); heat packs from the start kit (C1 / E1 / M2).
  - Hut 1: door **2.0 m** (the only hut a barrel may be pushed into: the barrel is 1.2 m wide); interior Shelter x0.5 + fixed camera (first building entry, see A3).
  - Optional: a `ACameraZoneVolume` **Shelter x0.5** (camera switch off) 4 x 6 m along the lee wall, so "by the wall" really is warmer.
- **Metrics:** push distance 10 m (inside the 15 m radius; ~6 s of pushing). The lit barrel's 5.5 m disc must cover the lee-wall zone and the hut door; in the hut, 6 x 4 m, it covers the whole floor. It burns 35 s, enough to go from 50 % to 0 % at 8 %/s.
- **Cold:** outside x1, lee wall x0.5, hut Shelter x0.5.
- **Success:** barrel pushed and lit, all <= 30 %. **Fail:** none (spare barrel; heat packs; if the player lights it in place, the push card stays and the debrief notes "barrel push skipped").
- **Checkpoint:** none (A1 is close).
- **~3 min**

#### A3. Buildings: the leader scouts inside
- **Goal:** explore the three camp buildings; find the note that names the PMC.
- **Teaches:** entering a building = **fixed interior camera; the others hold the perimeter outside** (existing camera-zone rule). This is the second, natural form of "split up". Read a note (narrative element). Loot a clean crate (loot dialog, take all). Open the inventory drawer.
- **Walk-and-talk:** `WT_A3_01-03` on entering the shed: "PMC stencils. They didn't leave in a hurry. They left things for us."
- **Hint cards:** `TUT_A3_ENTER` ("the squad holds outside while you scout"), `TUT_A3_LOOT` (done on container looted (E)), `TUT_A3_DRAWER` (done on drawer opened (E)).
- **Setup:** 3 `ACameraZoneVolume` (one per hut, Environment Shelter, own `CameraActor` placed high in a corner, looking down at 55-60 deg, not isometric, so the interior reads), 1 clean `ALootCrateActor` (canned food 2, matches 2, M16 ammo 30), `ANarrativeElementActor` note `NOTE_A3_PMC` ("Depot: north shore. Don't go near the shed door. - K."), 1 `ADialogueTriggerVolume`.
- **Metrics:** huts 6 x 4 m, door 1.6 m, interior wall height 2.6 m (with a ceiling the fixed camera must sit inside); shed 8 x 6 m with 2 doors (a loop teaches "buildings have two exits").
- **Cold:** Shelter x0.5 inside, x1 outside.
- **Success:** note read and crate looted. **Fail:** none.
- **Checkpoint:** `CP_A3` on leaving the camp toward the depot.
- **~4 min**

#### A4. The PMC depot: traps, and cold hands cannot disarm
- **Goal:** get into the depot shed and loot the trapped crate (grenades, tripwire kit, heat pack).
- **Teaches:** **hidden mines** (spotted at 4.5 m, 6 m for the sapper; the squad stops; Ctrl+click to shoot from afar or defuse); **PMC tripwire on the shed door** (prone passes under; Medic-Sapper disarms it and keeps the grenades); **trapped crate** (defusal menu shows the chance); **cold reduces the chance** (§2.2); stance (crouch +10, prone +25); role (use the Medic-Sapper, key 3).
- **Walk-and-talk:** `WT_A4_01` (mine spotted: Sokolova "Nobody move."), `WT_A4_02-03` (tripwire spotted: "Wire at shin height. They like doors."), `WT_A4_04` (cold chance < 45 %: "My hands are shaking. I need heat first.").
- **Hint cards:** `TUT_A4_MINE` (spotted (X `OnMineSpotted`) -> "select 3 (Medic-Sapper), click the mine -> Defuse; or Ctrl+click to shoot it from > 4 m"), `TUT_A4_WIRE` (prone under it **or** disarm), `TUT_A4_CHANCE` (when the defusal menu opens with chance < 60 %: "Cold costs up to 50 % chance. Warm up at the barrel, then crouch or go prone"), `TUT_A4_TRAP_CRATE` (done on defused).
- **Setup:**
  - 2 hidden `AProximityMineActor` (level mines, not `bPlacedBySquad`) on the **shortcut** between the sandbag ring and the shed (straight line 25 m). The long path round the fence (40 m) is clean. Mines 5 m apart.
  - 1 **PMC tripwire** across the shed door (1.6 m span, 30 cm), **needs gap G3** (level-placed armed tripwire). Until G3 lands: a trapped `AInteractableActor` placeholder "Door charge" next to the door (bTrapped, existing defusal and cold formula).
  - 1 trapped `ALootCrateActor` inside the shed (`bTrapped`; contents: 2 grenades, 1 tripwire kit (2 grenades), 1 heat pack, 1 medkit).
  - 1 clean crate in the guard hut (ammo).
  - **1 unlit barrel 12 m from the shed door** by the guard hut (warm-up spot for the defusal lesson). Push reprise without a card unless the player stalls: "Bring the fire to the door". Pushed to within 4 m of the door, its 5.5 m disc covers the Medic-Sapper while she works. The bonus card `TUT_A4_FIREDOOR` appears if her chance reads < 60 % for 20 s.
  - Shed interior: Shelter x0.5 so cold barely moves while disarming.
- **Metrics:** shed 10 x 8 x 3 m, door 1.6 m wide (the wire spans it); sandbag ring low 1.0 m; mine spacing 5 m (one spot reveals one mine, which stops the squad before the second); barrel 12 m from the door, pushed to <= 4 m (warm while defusing, cold rise ~0).
- **Cold:** outside x1 (the squad arrives at ~30-50 % from A3, so the first chance reads 58-73 % for the Medic-Sapper crouched, worse standing).
- **Success:** trapped crate looted. **Fail:** a detonation is survivable at full HP (85-140 dmg is high: **set the demo trap damage 60 and the mine damage 80 on these placed actors**, which are editable per actor). A death of a non-commander here = permanent loss; producer decision D4 (downed-not-dead in lessons) still applies.
- **Checkpoint:** `CP_A4` at the depot gate (before the mines).
- **~5 min**

### STAGE 2: THE QUEST: NAVIGATE, MOVE LIKE SOLDIERS

The existing hard-coded chain (`FQuestChainState`) is reused with lake fiction, so no new quest code is needed:

| Chain step (code type) | Lake fiction | Where | From previous |
|---|---|---|---|
| Station terminal first look | "Station dark, generator dry" (objective set by the chain start) | A7 station, 70 m E of the depot | - |
| `Canister` | Jerrycan at the ice-road cache, **beacon 7** | A5 west | 110 m |
| `Vehicle` | Stranded PMC APC: siphon diesel | A6 ridge field, centre-north | 70 m |
| `Generator` | Station generator shed | A7 | 80 m |
| `GateTerminal` | Radio-mast console that powers the boathouse gate | A7 main building | 15 m |

#### A5. Find the fuel: navigation
- **Goal:** find the canister using the note plus the beacons.
- **Teaches:** read the objective banner; follow the numbered beacons; use the map-less navigation language (amber = path); optional heat-pack use in a blizzard strip.
- **Walk-and-talk:** `WT_A5_01-04` (Engineer at the dead generator: "Dry. The PMC's cache was on the ice road, marker seven." Commander: "Seven. Follow the lamps west.").
- **Hint cards:** `TUT_A5_OBJECTIVE` (points at the objective tracker), `TUT_A5_BEACON` (the world marker on beacon 7 appears after 30 s without progress: gap G4).
- **Setup:** beacons 6-7 with **painted numbers** (art, 1 m high digits on the lamp post); canister `AInteractableActor` (`ObjectType = Canister`) on a tarp sled at beacon 7; optional `ACameraZoneVolume` **Blizzard x2.5**, 20 m strip, camera switch off, between beacons 6 and 7.
- **Metrics:** station -> beacon 7 = 110 m (50 s walk, 15 s sprint); the beacon lamp must be visible from 60 m at camera distance 26 m (lamp light radius >= 8 m, emissive).
- **Cold:** x1 (x2.5 strip: ~10 s inside = +25 %).
- **Success:** canister picked up (X `EQuestEvent::CanisterPickedUp`). **Fail:** none.
- **Checkpoint:** `CP_A5` on pickup.
- **~4 min**

#### A6. The stranded APC: cover-to-cover approach (the user's "move carefully" addition)
- **Goal:** reach the APC across the ridge field **as soldiers do**: cover to cover, no running in the open. Then siphon the diesel.
- **Fiction:** the APC is PMC. Its hatch is open and there are boot prints, so someone may be inside. **There are no enemies** (a placed enemy would turn the level into an ambush level). The threat is suspected only: fresh tracks, a still-warm stove in the container, a radio crackling inside the APC (sound cue). The lesson is the posture, not survival.
- **Teaches (in order):**
  1. **Wall hug / cover slot:** click a high partition, the ghost appears; click again to confirm. The operative stands with his back to the wall.
  2. **Low cover:** click a low ridge. He crouches behind it (default stance for low cover).
  3. **Shimmy:** click further along the same wall; he slides along it.
  4. **Corner peek / hold:** take a slot near a corner (<= 2 m from the edge). With no known threat he plays the corner look-at idle; teach that the corner is where you look and fire from (Ctrl+click = corner shot later in the fight).
  5. **Bound between partitions:** the next cover is 5-6 m away; move in a crouch (C) between them.
  6. **Cover = warmth:** wind chill is halved in cover (feed line + card), which matters because the field is x1.
  7. **Squad stacks on you:** followers take nearby slots on the same cover. **This does not exist** (gap G7, see below). Until it lands, teach "select each operative (1/2/3) and put each into cover", which is the slow version.
- **Walk-and-talk:** `WT_A6_01` (entering the field: Commander "Tracks. Fresh. We go wall to wall."), `WT_A6_02` (first cover: Sokolova "Out of the wind, at least."), `WT_A6_03` (at the last partition: Engineer "Radio's alive in there... it's a loop. Recording."), `WT_A6_04` (APC reached: relief beat).
- **Hint cards:**
  - `TUT_A6_COVER`: "Click a wall to preview cover, click again to take it." Done on cover entered (E) at a high slot.
  - `TUT_A6_LOWCOVER`: "Low cover: you crouch behind it and fire over it." Done on cover entered (E) at a low slot.
  - `TUT_A6_SHIMMY`: "Click along the same wall to slide." Done on shimmy (E).
  - `TUT_A6_CORNER`: "Corners are where you look and fire from. Stop near an edge." Done on a slot with an exposed edge (E).
  - `TUT_A6_BOUND`: "Crouch (C) and move to the next cover. Don't stop in the open." Done on 3 cover entries in the zone within 90 s each.
  - `TUT_A6_SQUAD`: (with G7) "Your squad stacks on your cover." Otherwise: "Press 2 / 3 and give each their own cover."
  - **Completion of A6:** leader reaches the APC slot volume AND >= 4 cover entries in the field (any mix, including 1 high, 1 low, 1 shimmy). Not hard-gated: walking straight in also works, but the cards stay and the debrief (UX W11) notes "cover lesson skipped".
- **Setup:**
  - **Ridge-field lane** 60 m long, 20 m wide, leading to the APC from the south-west.
  - 6 cover pieces, alternating: P1 high 2.2 m x 4 m (concrete ice slab, corner on the APC side); P2 low 1.0 m x 5 m (pressure ridge); P3 high 2.4 m x 6 m (wrecked container, **two corners**, both reachable only by shimmy, so the shimmy lesson lives here); P4 low 1.0 m x 3 m; P5 high 2.2 m x 4 m; P6 = the APC itself (7 x 3 x 2.6 m, high, corners at both ends).
  - Spacing **5-6 m** between pieces (bounding at crouch speed = 4-5 s exposed).
  - Offset each piece **2-3 m laterally** from the previous so the line of advance zigzags (players must aim clicks).
  - **No piece longer than 6 m** (corners must stay within the 2 m probe from a mid-slot when needed: P3 is the exception, which forces shimmy).
  - 1 `ADialogueTriggerVolume` per step (4).
  - 2 tutorial trigger volumes.
  - APC: `AInteractableActor` `ObjectType = Vehicle` (siphon), interaction side = rear hatch, which must face the last cover (P5).
  - Optional: an empty trapped crate behind P5 to keep the trap reflex alive (bTrapped, damage 60).
- **Metrics:** lane 60 x 20 m. High pieces 2.2-2.6 m (hide a standing body; the camera occlusion is 3.1-3.7 m behind, so place the next piece **in front** of the occluded strip from the camera's default yaw -45 deg). Low pieces 1.0 m. Piece spacing 5-6 m. The open stretch never exceeds 7 m.
- **Cold:** x1 in the open, **x0.5 in cover** (existing). Arriving cold is fine: the station (Closed) is the next stop.
- **Success:** diesel siphoned (X quest event). **Fail:** none.
- **Checkpoint:** `CP_A6` at P1.
- **~5 min**

#### A7. Power the generator, READ THE GROUND, prepare (no answers given)
**Design principle (user, rev 2):** the sudden horde start removed the 60 s preparation. The player must **stop feeling safe** and must **not be told where the attack comes from**. A7 teaches him to **read the terrain and anticipate**: approach lanes, choke points and environmental spawn signs. Then he decides where the wire, the mines, the barricade and the **turret** go, and what to protect: the **generator**. Without it there is no heat, the turret has no power, and the **blast door** does not open.

**Gate rule (being implemented by another agent; this sheet assumes it):** the blast door (`AGateActor`, the boathouse hangar = the way out to the road) starts opening when the console is powered. It opens **very slowly during the whole fight** and **only while the generator runs**; when the generator is down, opening **pauses**. It is **fully open only after wave 3 is cleared**, so the squad cannot retreat inside early. The door's progress is the fight's visible clock.

- **Goal:** start the generator; walk the perimeter and find the approach signs; set the defence; then power the console.
- **Teaches (in this order):**
  1. **Generator = the heart.** Warm zone 4 m, it powers the turret (a powered turret is itself a heat source of 4.5 m), and it drives the door. It has **200 HP**, and **small enemies prefer it** (target weight 0.4 x vs squad 1 x: hounds rush it from the start of a wave).
  2. **Read the ground (recon walk).** Three approach signs around the yard (below). Nothing marks them as spawns. The cards ask questions; the radio gives fiction.
  3. **Place your traps where the signs point** (§2.6): mines on a lane (Medic-Sapper: mishap 2 % vs 10 % + cold), a **tripwire across a choke** (2 grenades; friendly fire on), a **barricade** in a gap, **mine the barricade** (Set trap: 85 / 4 m, enemy contact 1.8 m).
  4. **Turret** (loadout 1, routed to the Commander): F -> Turret -> ghost, **R rotates**, click; 3 AP in TB. Range **12 m**, 16 dmg / 0.45 s, 120 HP, walls block, a barricade in its line = 60 %. **Unpowered when the generator is down.** The right answer is the one that covers the generator **and** a lane. The card never says which.
  5. **Barrels as bombs:** push dump barrels onto a lane you expect (exploration push, 15 m); in the fight, Ctrl+click.
  6. **Who repairs:** the **Engineer** (key 2) repairs the generator in **2.5 s** (others 5 s) and a turret in **2 s** (others 4 s). Keep him near the generator.
- **Walk-and-talk / radio (fiction, never the answer):**
  - `WT_A7_01-03` generator starts: "Heat. Real heat." Engineer: "And it hums like a dinner bell."
  - `WT_A7_04` Commander: "Before we light that mast, we walk the perimeter. All of it."
  - `WT_A7_05` HQ: "Satellite thermal is useless in this weather, Captain. Animals use gullies, and the east cliffs have one."
  - `WT_A7_06` Sokolova at the west sign: "Fishermen's path. Somebody dragged something along it. Recently."
  - `WT_A7_07` Engineer at the shed: "The door's clawed. Whatever came here wanted the warm thing."
  - `WT_A7_08` Sokolova laying a mine: "Mines are mine. Steady hands, warm hands."
  - `WT_A7_09` before the console: "The mast will light up like a flare. Anything on this lake will hear it."
- **Hint cards (questions first, keys second; one at a time):**
  - `TUT_A7_GENERATOR` (on start): "This generator is your heat, your turret's power and the door. What will they go for first?"
  - `TUT_A7_RECON`: "Walk the perimeter. Tracks, broken fences, carcasses: where did they come from?" Done when the leader has visited >= 2 of the 3 sign volumes (E).
  - `TUT_A7_MINE`: "Mines go where they will run. Select 3, F -> Mine." Done on `DeployablePlaced(Mine)` (E).
  - `TUT_A7_WIRE`: "A wire belongs in a narrow place they must pass. Drawer -> Tripwire: anchor A, anchor B. Friendly fire is on." Done on `TripwireRigged` (E).
  - `TUT_A7_TURRET`: "F -> Turret, R rotates. It needs the generator's power. Reach 12 m." Done on `DeployablePlaced(Turret)` (E).
  - `TUT_A7_BARRICADE` + `TUT_A7_TRAPOBJ`: "F -> Barricade. Then click it -> Set trap: whoever touches it, regrets it." Done on `DeployablePlaced(Barricade)` + `ObjectTrapped` (E).
  - `TUT_A7_BARRELS`: "Fuel burns. Put it where they will bunch." Done on a `RelocationDone(Barrel)` (E).
  - `TUT_A7_REPAIR` (shown now, completed in the fight): "If the generator stops, send the Engineer (2): he repairs it twice as fast."
  - `TUT_A7_SAVE` (F5; the autosave also fires before combat (X)).
  - The console card appears after `TUT_A7_RECON` + any 3 of the placement cards, or when the player uses the console anyway. There is **no forced checklist**: the debrief grades the result (§5.3).
- **Setup:**
  - Generator `AInteractableActor` (Generator, 200 HP) in a **generator shed 4 x 3 m at the yard's NE corner**, 6 m from the yard's east edge: exposed on purpose, close to the hidden east lane.
  - Console `AInteractableActor` (GateTerminal) in the main building's radio room.
  - `AGateActor` = the boathouse hangar **blast door** at the yard's SE corner (DoorSize 40 x 350 x 400 per leaf).
  - `ACameraZoneVolume` main building **Closed** (fixed camera ON). `TargetPoint` tagged `CombatStart` in the yard centre (dev "Start battle" only).
  - **Approach signs** (art + a `ATutorialTriggerVolume` 6 x 6 m each, gap G1):
    - **N (obvious):** the open ice and the pressure ridge 30-35 m out. Nothing to find: it is visible.
    - **E (the hidden lane, to be discovered):** a **cliff gully** 35 m E of the yard, screened from the yard by a **2.5 m snow drift** and the generator shed. Tells: a **trail of hound tracks** (decal, 30 m long) from the gully to the shed, **claw marks** on the shed door, a **gnawed PMC body** 15 m E, an occasional **howl from the east** (3D audio).
    - **W (shore path):** the fishermen's path along the west shore. Tells: a **broken fence**, a **drag trail with blood**, an ice-fishing hole with a torn net. (Susanin comes this way in W3.)
    - **S (the breach):** the boathouse **bulkhead wall plate is dented and frost-split** (art only). It hints but never confirms: the "never feel safe" lesson.
  - Fuel dump: 3 barrels 10 m behind the north line. Loadout **mines 2, barricades 1, turret 1**, matches 3; grenades 2 per operative + 4 from the A4 crate.
- **Metrics:**
  - Main building 18 x 10 m, corridor 2 m wide, rooms 8 x 8 and 8 x 6 m, 2 exits (front 1.6 m, yard side 1.6 m). Generator to console 20 m.
  - **Choke points** (where a wire / barricade fits): the **east gully mouth 3 m wide** (between the drift and the cliff, the best wire spot); the **west fence gap 2.5 m**; **two 3 m gaps** in the north sandbag line. Tripwire span 1-5 m, barricade 3 m long.
  - **Mines** 8-15 m out on a lane (the 3.5 m blast must not reach the line; the squad takes 75 %).
  - **Turret**: generator-to-east-drift 8 m, so a turret 3-5 m south-west of the shed covers both the shed and the drift edge within 12 m. Covering the north lane from there is impossible: one turret, one choice.
  - **Barrels** 8-12 m out: the 5.5 m blast stays off the line, and they sit inside the 25 m M16 range.
  - Keep one gap open: the squad's own way out, and the reason to say "go prone under your wire".
- **Cold:** Closed x0 inside, generator 4 m warm zone outside. Recon outside is x1; the drift and gully are x1 in the open. Prep near the generator keeps a slow preparer warm.
- **Success:** console used -> door starts opening -> `TriggerCombatZone` -> the sudden wave 1 spawns. **Fail:** none.
- **Checkpoint:** autosave **BeforeCombat** (existing) = `CP_A7`.
- **~6 min** (recon + prep)

### STAGE 3: THE LAKE FIGHT (three forms, breach, rescue): see §5

#### A8-A10. Station-yard fight (3 sudden waves)
Detailed in §5. ~9 min including the between-wave panels.

#### A11. Aftermath
- **Goal:** catch breath, loot the station locker, regroup with Susanin (member 4).
- **Teaches:** post-combat recovery (the deployables return to the supply; existing); Susanin as a squad member (select 4); medkit H on the wounded.
- **Walk-and-talk:** `WT_A11_01-05` (Susanin intro banter: who he is, "the garage on the east road, the PMC motor pool. They had a heater and a radio.").
- **Setup:** 1 clean crate in the station (medkits 2, ammo, heat packs 2).
- **Checkpoint:** autosave **AfterCombat** (existing) = `CP_A11`.
- **~2 min**

#### A12. The road east (transition)
- **Goal:** walk through the boathouse gate onto the road; the level ends.
- **Walk-and-talk:** `WT_A12_01-03` (walking).
- **Setup:** `ALocationExitVolume` (gap G8) 15 m past the gate, 20 x 10 m, target `L_Demo01_B_Garage`; blocked-off road beyond (snow wall).
- **~1 min**

---

## 5. Combat section A: three forms, flank breach, rescue

### 5.1 How the existing systems behave (facts)
- **Trigger:** using the console (`GateTerminal`) calls `UQuestSubsystem::StartPreCombatCutscene` -> `UGameFlowSubsystem::TriggerCombatZone`. With the default **sudden start** the wave spawns at once with no countdown.
- **Sudden start timeline** (`HordeIntroRules`): wave spawns -> **real time** while the mass forms (min delay, max wait) -> **free tactical pause** (no charge, no timer) + slow-motion flyover over every new enemy type + "HORDE INCOMING" -> Space tap resumes real time; **hold Space 1.5 s = turn-based**.
  So every sudden wave already shows real time -> pause -> (turn-based on hold), in that order.
- **Between waves:** `WaveCleared` stops time and shows the victory panel with "Start the next wave (N/3)". With `sudden_start` true the next wave has no rest countdown. Each panel is a natural lesson break.
- **Flank breach** (`AEnemySpawnPoint.bIsDynamic`, `UWaveSubsystem::CheckDynamicFlankSpawners` / `TriggerBreach`, Godot `_check_dynamic_flank_spawners`): in the mission's breach wave the first dynamic point that passes `ActivationChance` (default 0.7) breaks through **4-7 s after the wave starts**. `EnemyCount` (default 4) of `BreachEnemyType` spawn round the point (types other than Spitter / Brute / Cutter spawn hounds, Godot parity). HQ feed line "FLANK BREACH!". After 1 s the camera shows the point for 1.8 s with "Damn it, where did they come from?", then returns. **During turn-based combat the breach is deferred until TB ends** (`RunOrDeferRandomEvent`). One per mission. **Dormant today:** no map has a dynamic point; the wave is random in [1,2,3].
  A second, TB-only kind of breakthrough exists too: **exposed zones** (`ExposedZones`). A squad that stays exposed in one quadrant for 3 TB turns gets a warning (1), danger (2), then a breach of 1-2 non-elite enemies, once per fight.
- **Rescue** (`URecruitSubsystem`, Godot `recruit_susanin.gd`): in the mission's rescue wave, 5-8 s after it starts (deferred past TB), **Ivan Susanin** spawns from the operative BP at the `TargetPoint` tagged `SusaninSpawn`. He is freezing (cold 85), with a narrative pause, a camera focus and a blocking distress dialogue. **"Help" = any living operative within 2.8 m of him, or Susanin inside any active heat source** (a lit barrel within 4 m). Then a recruitment dialogue, and he joins as **member 4** (warm, armed). His death does not fail the mission. Today the wave is random and must differ from the breach wave.
- **Generator under attack** (`AInteractableActor` Generator): small enemies weigh the generator at 0.4 x the squad's distance (so they rush it), turrets at 0.5 x. At 0 HP: breakdown, heat off, **all turrets unpowered**, the «[EMERGENCY]» menu appears, and (new gate rule) **door opening pauses**. Repair = select the Engineer, click the generator -> Repair (**2.5 s**; others 5 s). Turret repair: Engineer 2 s, others 4 s. **Barricades cannot be repaired** (gap G18). Repair inside a running fight: the use-order path exists for barrel lighting (`UseOrderRules`, real time = walk up and use; pause = planned). For generator / turret repair in a live fight it **must be verified** (BotRepairSmoke covers only the preparation phase): smoke `RepairInCombatSmoke` (G18).
- **Victory:** the last wave dead -> "End combat and continue exploring" -> PostCombat (deployables recovered, the squad set back at its stations) -> exploration. With the new gate rule the blast door finishes opening at this point and is the exit.
  **Defeat:** only the Commander's death (death cinematic, "MISSION FAILED"). There is no defeat for losing the generator: it only costs heat, turret power and door time.

### 5.2 Staging: one fight, three waves, one form taught per wave

| Wave | Composition (JSON) | Spawn | What is taught | Cards / trigger | Done when |
|---|---|---|---|---|---|
| **W1 «Contact»** | 4 FROSTBITTEN + 2 FROST_HOUND, hp/dmg x1.0, cold x1.0 | **N** ice: 4 FROSTBITTEN from 2 static points 45-55 m N. **E** gully (the hidden lane): the 2 FROST_HOUND, 35 m E, which **go for the generator** (target weight) | **Real time:** the squad fires by itself; click an enemy = priority target; **whatever the player set in A7 fires, or does not** (a trap on the east gully kills hounds; without one they reach the generator). **Generator down -> repair:** the turret goes silent, the warm zone dies, the door stops. Send the Engineer (2).  **Then the free pause** (it opens on its own): orders in a stopped world, including **a barrel push as a planned order** (barrel 3 from the dump onto the lane where the pack bunches; unlimited radius in the free pause). It runs on release. **Then real time: Ctrl+click** a barrel when 3+ enemies are within 5.5 m of it. **Then tap-pause:** tap Space once when the nearest enemy is <= 20 m. | `TUT_A8_REALTIME` (on spawn), `TUT_A8_GENERATOR` (on the first generator damage: "Something is chewing on your power." No direction given), `TUT_A8_REPAIR` (on breakdown: "Generator down: no heat, no turret, the door stops. Engineer, key 2: Repair."; done on `GeneratorRepaired` (X event bus)), `TUT_A8_FREEPAUSE` (on free pause: "Time is stopped. Click to move / take cover. Space to resume."), `TUT_A8_PAUSEPUSH` (in the free pause: "Orders wait for the release: Push barrel 3 onto the lane now."), `TUT_A8_BOOM` (RT, 3+ enemies within 5.5 m of a barrel: "Ctrl+click the barrel."), `TUT_A8_TAP` (enemy <= 20 m, after resume). `WT_A8_01-03` HQ / squad lines, `WT_A8_04` on the first trap kill ("That's the PMC's trick. Works both ways.") | W1 dead **and** one paid pause used (E). Pause push = `RelocationOrdered(bInPause)` then `RelocationDone` (E). Barrel kill = `BarrelExploded` with kills >= 1 (E). Trap kills = `TrapTriggered(bByEnemy)` (E). Cards not done return in W2 |
| **W2 «Grid and breach»** | 6 FROSTBITTEN + 2 FROST_HOUND; **breach**: dynamic point `BOATHOUSE_BULKHEAD`, `EnemyCount` 3, FrostHound, `ActivationChance` 1.0, JSON `breach_wave: 2` | N ice 6 FROSTBITTEN; **W shore path** 2 FROST_HOUND (signs found in A7 or not); breach 18 m **S** behind the line (the dented bulkhead) | **Turn-based (forced once):** at the free pause, or when the first enemy is <= 18 m: "HOLD Space". In the grid: Tab next operative, AP, hit %, Enter end turn; hold Space to leave. **Grid barrel push:** a barrel stands **2 cells (3 m) in front of the line inside the 21 x 21 pad**. Step next to it (orthogonal), click it, push it **1-2 cells (2 AP each)** into the enemies' path, then Ctrl+click it (3x3, 80 dmg). **Grid mine:** F -> Mine on a cell, 2 AP (if one is left in supply). **The breach is deferred while in TB, so it fires on returning to real time**, a deliberate shock: "they came from behind". Then tap-pause to turn the line, and **push the burning barrel into the rear gap** (hounds flee within 3 m of fire). | `TUT_A9_HOLD` (hard prompt, repeats every 20 s until done or W2 ends), `TUT_A9_GRID` (in TB), `TUT_A9_GRIDPUSH` ("Stand next to the barrel, click it, pick a cell: 2 AP per step."), `TUT_A9_GRIDMINE` (optional), `TUT_A9_EXIT`, `TUT_A9_BREACH` (on the breach: "Tap Space, turn someone to face the rear. A burning barrel scares hounds.") | TB entered once (X `OnGameFlowChanged`) + `TurnBasedRelocate(Barrel)` (E) + W2 dead |
| **W3 «Survivor»** | 6 FROSTBITTEN + 4 FROST_HOUND (+ optional 1 BRUTE, cut first); JSON `susanin_wave: 3` | **All three lanes:** N 4 FROSTBITTEN, E 2 FROST_HOUND (generator again), W 2 FROSTBITTEN + 2 FROST_HOUND (past Susanin) | **Rescue:** Susanin appears 28 m west, freezing. Send one operative (select him, **B solo** so the rest hold the line) **or** bring fire to him: the barrel 2.5 m from him can be lit, or **pushed / lit in a pause** by the operative sent (heat source rescues). Free choice of time mode. | `TUT_A10_RESCUE` (on the distress dialogue close: "Reach him (2.8 m) or put fire next to him."), `WT_A10_01-03` | W3 dead; Susanin recruited (optional; the debrief notes it) |

**Why staged by wave, not all three in W1:** the free pause always opens a sudden wave, so W1 naturally shows real time -> pause.
Forcing TB in W1 as well would stack three new verbs inside ~60 s. W2's forced TB also turns the breach deferral into a scripted-feeling
surprise without any new code. Alternative (user choice): all three forms in W1, with W2 / W3 for breach and rescue only.

**Arena metrics (rev 2: four sides, one generator, one door):**

```
                     N  open ice 25 m, pressure ridge 30-35 m (2 spawn points 45-55 m)
        W shore path      ====sandbag line (4 x 1.0 x 4 m, 3 m gaps) + 2 blocks 2.2 m====      [GEN SHED 4x3] ..drift 2.5 m.. E gully
   (spawn 40 m W)    fence gap 2.5 m          YARD 30 x 30 (TB pad 21 x 21)          shed 6 m from E edge    mouth 3 m (spawn 35 m E)
        Susanin 28 m W                           fuel dump (3 barrels)
                          [MAIN BUILDING 18 x 10]                [BOATHOUSE 12 x 8 | BLAST DOOR -> road]
                                                                  dented bulkhead -> breach point 18 m S
```

- Yard 30 x 30 m flat (TB pad 21 x 21 inside, +-0.2 m, **no curbs > 0.4 m**: 50 cm breaks the TB check). North line: 4 sandbag walls (1.0 m x 4 m, 3 m gaps) + 2 concrete blocks (2.2 m x 3 m) for corner shots.
- Ice in front: open for 25 m (= M16 range), then a pressure ridge at 30-35 m where the mass forms (flyover framing).
- **Lanes:** N (obvious), **E gully** (hidden by the drift, ends at the generator shed), **W shore path** (fence gap 2.5 m), **S breach** (dynamic). Spawn points: N x2 `NORTH_ICE`, E x1 `EAST_GULLY` (accepts FROST_HOUND only), W x1 `WEST_SHORE`, S dynamic `BOATHOUSE_BULKHEAD`. **Name the `SpawnLane` on each point exactly as in the JSON** (the existing lane-name trap in HANDOFF §9).
- **Distances that make anticipation matter:** E spawn -> generator 35 m (hounds ~4-5 s at a run); the generator is 20+ m from the north line, so a squad that only watches north arrives late. W spawn -> line 40 m. Breach -> line 18 m.
- **Generator exposure:** shed walls 2.4 m with a 1.6 m door on the east side (the side the hounds come from). The drift blocks the line of sight from the yard to the gully mouth, so only an operative / turret placed at the drift edge sees it.
- Fuel dump 3 barrels 10 m behind the line; extra barrels 8-12 m in front (Ctrl+click lesson). SusaninSpawn 28 m W on the shore, an unlit barrel 2.5 m from it, a wrecked snowmobile (low cover, 1.0 m) next to him.
- **Door clock:** with the new gate rule, the door needs the whole fight (W1-W3, ~6-8 min of fight) with the generator running. Every second of generator downtime is visible as a stalled door.

### 5.3 Debrief: what the lake fight evaluates (demo end card G16 + telemetry)
The debrief grades **anticipation**, not only survival. Each line is a short sentence plus a value, with no punishment:
1. **Lanes read:** sign volumes visited in A7 (0-3) and **lanes covered before contact** (a trap, turret or operative facing it within 10 m), N / E / W.
2. **Traps that paid off:** kills by own mines / wire / trapped barricade / barrels, per lane. **Friendly-fire trap damage** is shown separately.
3. **Generator:** breakdowns, **total downtime (s)**, **repair response time** (breakdown -> repair started), **who repaired** (Engineer / other).
4. **Door:** time to full open vs the ideal (no downtime); door-paused seconds.
5. **Turret:** uptime %, kills, powered-off seconds.
6. **Breach reaction:** seconds from the breach to the first operative facing / firing south.
7. **Time control:** pauses used, TB entered (yes / no), barrel pushes per mode (exploration / pause / TB).
8. **People:** Susanin rescued (time), members lost, max cold.
Each line links to its lesson ("Next time: the claw marks on the shed pointed east").
Telemetry: per-beat timestamps + these counters into `Saved/Telemetry/tutorial.jsonl` (UX W11), digested with Jev.

**Win target (bot, VETERAN-like TUTORIAL profile):** 85-90 %. Wave sizes are a first guess; tune with the Wave Editor + bot. Keep `cold_drain_mult` 1.0.

---

## 6. Location B «Auto-Repair Garage»: layout

```
                                N  (road from A arrives at the SW corner)
  +-----------------------------------------------------------------------+
  | YARD 70 x 50 m (snow, x1)   P2 patrol loop (80 m) round the hall       |
  |   scrap cars (high 1.6-1.8)   fuel pumps (low 1.1)  tyre stacks (0.9)  |
  |  +----------------------- HALL 36 x 24 x 7 m ----------------------+   |
  |  | MEZZANINE 3.0 m along N wall (36 x 4) + W wall (24 x 4)          |  |
  |  |  [OFFICE 6x5 on mezz]  railing 1.0 m   stairs SW + NE (1.2 m)   |  |
  |  |                                                                 |  |
  |  |  BAY1 [lift+car]   BAY2 [pit 1.2 deep]   BAY3 [pit] [truck]      |  |
  |  |  workbenches 1.0 m, shelves 2.2 m, partitions 2.4 m (4 m long)  |  |
  |  |  P1 patrol loop (45 m) through the bays                         |  |
  |  +--ROLLER DOOR 4.5 m (S, half open)--SIDE DOOR 1.6 m (E, wired)--+   |
  |        REAR DOOR 1.6 m (N, under the mezzanine; 2 mines outside)       |
  |  PlayerStart SW yard edge                                            |
  +-----------------------------------------------------------------------+
```

| Element | Metrics | Why |
|---|---|---|
| Hall | 36 x 24 m, 7 m high; **roof mesh hidden in game** (or a camera cutaway: gap G9). Interior: Shelter x0.5 cold | Isometric 26 m camera; a 7 m wall hides 10 m of floor, so the outer walls facing the camera need to be cut / hidden |
| Mezzanine (heights) | **3.0 m** high, 4 m deep along N (36 m) and W (24 m); railing 1.0 m (low cover, see-through mesh); 2 stairs 1.2 m wide at SW and NE corners; office 6 x 5 m with 2 windows | 3.0 m >> 0.5 m elevation threshold: +25 % range (M16 31 m covers the whole hall diagonal 43 m only partly, so pick a side), +15 % damage, fire over benches / barricades. The stairs are the only melee approach (chokepoints 1.2 m). Under-edge dead zone 2.4 m |
| Bays | 3 bays 10 x 8 m; 2 inspection pits 1.2 m deep, 1 m wide, 5 m long (nav: pits walkable via end steps, so enemies inside are 1.2 m lower = elevated targets for floor shooters) | Shows elevation both ways; mines at pit ends |
| Interior cover | workbenches low 1.0 m x 2-3 m; shelving high 2.2 m x 4 m; partition walls high 2.4 m x 4 m, spaced 5-6 m (the cover-to-cover rhythm from A6); car on lift 1.6 m (avoid 130-180: put the lift up at 2.0 m or down at 1.1 m) | §2.1 cover classes |
| Doors | front roller 4.5 m (half open, enemies' main path, horde entry); side personnel 1.6 m E (**PMC tripwire**); rear 1.6 m N (**2 hidden mines** outside, 4 m apart) | Two trapped entries, one noisy obvious entry: a choice |
| Yard | 70 x 50 m, x1 cold; scrap cars high 1.6-1.8 m (treat as low!), fuel pumps 1.1 m, tyre stacks 0.9 m | Approach in cover; horde spawn ring 20-40 m from the squad lands in the yard |
| TB floor | Bay floor flat; **TB impossible on the mezzanine** (refused: "not on high ground") | Deliberate tension: heights = real time + pause; floor = grid |

---

## 7. Location B: beats

#### B0. Arrival (walk and talk)
- **Goal:** reach cover at the yard edge unseen.
- **Teaches:** reprise crouch + cover (A6) without cards; vision cones read (enemy cones visible: `bShowVisionCone` on the placed enemy BPs / demo flag).
- **Walk-and-talk:** `WT_B0_01-05` (Susanin: "Motor pool. PMC used it as a depot. The heater ran on diesel... the lights are on."). Commander: "Cones. Stay out of their eyes."
- **Hint cards:** `TUT_B0_CONES` (vision cones; crouch x0.7, prone x0.4 visibility; hearing by gait).
- **Setup:** PlayerStart behind a scrap-car row 30 m from the hall; P2 patrol (2 FROSTBITTEN on an 80 m `APatrolRouteActor` loop round the hall, walk 1.9 m/s, so one lap is ~42 s, a readable rhythm).
- **Cold:** x1 (squad arrives with A's carried cold, see the G8 decision).
- **Checkpoint:** `CP_B0` (the transfer autosave).
- **~2 min**

#### B1. Recon and the PMC's welcome: tripwires and mines
- **Goal:** choose an entry: side door (wired), rear door (mined) or the front roller door (open, but P1 sees it).
- **Teaches:** spotting the tripwire (needs G3: hidden until within 4.5 / 6 m) and disarming it (Medic-Sapper, cold matters: needs G3 cold rule); prone under it; mines (reprise); **using the PMC's own wire against them**: re-rig the tripwire on P1's route (enemies do not avoid wires: nav cost 1 for enemies).
  **Reading the hall (anticipation, rev 2):** the hall has exactly **three doors**. Whatever comes later (the 4-minute horde spawns at a random out-of-sight point 20-40 m away, in the yard) **must come through one of them**. The player is not told which one.
- **Walk-and-talk:** `WT_B1_01-04` (Sokolova: "Same hand as the lake. Doorways and shortcuts."), `WT_B1_05` (Commander, at the yard edge: "Three ways in. If this goes loud, three ways for company too.").
- **Hint cards:** `TUT_B1_REWIRE` (after disarm: "Rig it where the patrol walks: two clicks (F)"), `TUT_B1_DOORS` (question card: "Three doors. Which ones can you close before it goes loud?").
- **Setup:** 1 PMC tripwire (G3) across the side door; 2 hidden mines on the rear approach; 1 trapped crate in the yard shed (heat pack 1, grenades 2, damage 60); unlit barrel by the yard shed (warm-up spot again, 6 m from the side door).
- **Metrics:** side-door wire 1.6 m span at 30 cm. The P1 route passes 3 m inside the side door, so a re-rigged wire there catches P1 (trap blasts start a **search**, not a fight, per `enemy_perception.json`; to verify: does a squad trap kill count as `EnemyDamaged` and start the ambush?).
- **Checkpoint:** `CP_B1` (exploration autosave, G6).
- **~4 min**

#### B2. Inside: stealth to the heights
- **Goal:** get at least two operatives onto the mezzanine before contact.
- **Teaches:** cover-to-cover indoors (partitions 5-6 m apart), guard posts (route-less placed enemies stand watch until they perceive the squad), escort hound smell 12 m (keep hounds at a distance), **why heights**: card "From 3 m: +25 % range, +15 % damage, shots over low cover. Melee must use the stairs." (Melee needs <= 1.2 m height difference, so hounds and frostbitten must climb.)
  **Prepare before contact (exploration rules: 15 m radius, no time pressure while unseen):**
  - **Turret** (carried from A) inside the hall covering the **roller door** (12 m) from the stair foot. There is no generator here, so it is powered by default. Commander deploys it, R rotates.
  - **Mines** (what A left over, or from the yard-shed crate) **inside the roller door**, 3-6 m in, on the hall floor.
  - **Mine a barricade:** place the carried barricade across half of the roller door, then **Set trap** (enemy contact 1.8 m).
  - **Push the 2 oil drums** (barrels) from bay 2 to the **SW stair foot** (12 m push): the melee chokepoint. Ctrl+click from the mezzanine.
- **Walk-and-talk:** `WT_B2_01-03` (Engineer: "Up there. They'll have to come to us one at a time."), `WT_B2_04` (Sokolova: "Give me two minutes at that door and nothing walks through it in one piece.").
- **Hint cards:** `TUT_B2_HEIGHTS` (on the first stairs step), `TUT_B2_GUARD` (T: post an operative at the stair top), `TUT_B2_PREP` (question card, no answer: "When it goes loud, where will they come from, and what will be waiting there?"). Done on any `DeployablePlaced` / `ObjectTrapped` / `TripwireRigged` / `RelocationDone(Barrel)` inside the hall before contact (E).
- **Setup (placed enemies = the ambush fight; total 8):**
  - P1 patrol: 1 FROSTBITTEN + escort FROST_HOUND, 45 m loop through bays 1-3.
  - P2 (yard): 2 FROSTBITTEN.
  - G1: FROST_HOUND guard at the rear door (inside).
  - G2: FROSTBITTEN guard in bay 3 by the truck.
  - G3: FROSTBITTEN guard on the mezzanine NE end (the player must take the heights from someone).
  - Optional G4: 1 MARKSMAN in the office (user decision Q3; bot rates him too accurate).
- **Metrics:** stairs SW are 8 m from the side door (stealth route), NE stairs 30 m (G3 sits there). P1 passes 6 m from the SW stairs every ~24 s (window ~15 s at crouch 1.25 m/s up 8 m of stairs).
  Door traps: roller door 4.5 m wide = a 3 m barricade + a 1.5 m gap (one mine 3 m inside the gap). Side door 1.6 m and rear door 1.6 m: wire spans of 1.6 m. Turret at the SW stair foot -> roller door 10-11 m (inside 12 m). Oil drums at the stair foot: their 5.5 m blast covers the stairs' first 4 m, and the mezzanine edge is 3 m up (inside the operatives' 80-dmg radius: **stand back 2 m from the railing when you fire them**).
- **Cold:** Shelter x0.5 (unheated hall). On the mezzanine the "elevated wind x2" also applies (absolute Z >= 1.4 m; indoors this is a bug-like effect: G10), so the net x1.
- **~3 min**

#### B3. The fight: ambush, heights, the 4-minute clock
See §8. ~5 min.

#### B4. Demo end
- **Goal:** victory -> end card.
- **Teaches:** nothing; reflect.
- **Walk-and-talk:** `WT_B4_01-04` (the garage radio: a PMC voice loop mentions "Sector-17 perimeter": the hook to the next demo).
- **Setup:** "DEMO COMPLETE / thank you, tester" end card with stats and feedback (UX W11, producer M1).
- **~2 min**

---

## 8. Combat section B: patrols, traps, 4-minute horde, heights

### 8.1 Facts from code
- **Ambush start** (`ULevelEncounterSubsystem`): with `combat_start: "ambush"` (or Auto with placed enemies), the fight starts **once**, when a patrol detects the squad (sight / hearing / smell), the squad hurts or attacks an enemy, an aggressive operative opens fire, or an enemy attacks. All living enemies in the world become the wave (`AdoptLevelEnemies`). `bAmbushSingleFight` = the fight is the whole battle -> PostCombat.
- **Mid-fight horde** (`UHordeSubsystem`, `horde.json`): a clock counts **only full real-time fight seconds** (not the tactical pause, **not turn-based**, not dialogues). At **240 s** it releases **8** enemies (5 FROST_HOUND + 3 FROSTBITTEN) at a reachable point **25-45 m** from the squad centre, out of sight, clustered within 4 m. They know where the squad is and rush it, with a HUD "HORDE!" banner + arrow + radio line (warning 6 s). Once per fight (`repeats: false`). Per-level override `"horde": {...}`; console `Codex.Horde.TriggerSeconds`.
- **Heights:** see §2.1. Real time only (TB requires flat ground). Spitters prefer elevated targets (+50), Marksmen go prone on heights, the bot values elevated cover +25. **There is no on-screen feedback that the bonus is active** (gap G11).

### 8.2 Encounter flow
1. **Contact** (any trigger) -> "We've been spotted! Engage!" -> real-time fight with 8 placed enemies (BeforeCombat autosave = `CP_B3`).
2. **Ideal play (~2.5-3.5 min):** two operatives on the mezzanine fire at +25 % / +15 %, a third holds the SW stair top (T guard, crouched behind the railing). The re-rigged tripwire and the tripwire grenades take P1. The oil drums pushed to the stair foot are Ctrl+click targets. The turret covers the roller door; if it breaks, the Engineer repairs it (2 s).
3. **Clock pressure** (new, G12): at **180 s** of real-time fight a radio warning: `WT_B3_CLOCK` "Captain, that noise carries for kilometres. Finish it or we'll have company." Plus a small fight timer on the combat badge (mm:ss of the horde clock). **Card `TUT_B3_LASTCHANCE`:** "Tap Space. In the pause you can still lay a mine or a barricade within 12 m: which door is open?" (pause placement, §2.6). Done on `DeployablePlaced(bInPause)` (E).
4. **240 s** -> the horde: 8 enemies appear **out of sight, 20-40 m away** (random reachable point), so they come through **whichever door faces them**. The door traps from B2 / B3 decide how many arrive. Per-level override: `"horde": { "trigger_seconds": 240, "min_distance_m": 20, "max_distance_m": 40 }`, so they spawn in the yard, not beyond the map edge (45 m from a hall-centred squad is off the 70 x 50 yard). The heights make this survivable: one entry (stairs) per side.
5. **Victory:** all adopted + horde enemies dead -> PostCombat -> B4.

**Design note: TB stops the horde clock** (existing rule). A player who fights the whole garage in turn-based never sees the horde. Options in Q4.

### 8.3 Hint cards
`TUT_B3_CLOCK` (on contact: "Long fights draw more of them: the clock counts real time only"), `TUT_B3_HEIGHTS` (if nobody is elevated 20 s into the fight: "Get up the stairs"), `TUT_B3_HORDE` (on release: "They come through the roller door: hold the stairs").

---

## 9. Mechanics check (everything the user named)

| User item | Status | System / file | What is needed |
|---|---|---|---|
| CommonUI menu -> New Game | **EXISTS** | `UCodexFrontendSubsystem::StartNewGame`, `UCodexFrontendSettings::NewGameLevel` | Point `NewGameLevel` at `L_Demo01_A_FrozenLake` (config) |
| Walk-and-talk (non-blocking lines while moving) | **PARTIAL** | `ADialogueTriggerVolume` -> `UDialogueSubsystem::PlayInFeed` (timed, non-blocking; a newer sequence cancels the older one) | G2: the intro on spawn as feed (not the blocking `StartDialogue`); a subtitle / radio strip that does not compete with system feed lines (UX W5); sequence assets + manifest ids via the narrative sheet (Gemini). No VO |
| Solo / regroup / formation | **EXISTS** | `USquadSubsystem` (B solo toggle, T guard, formation column 1.5 m) | Tutorial cards only |
| Crouch / prone / squad stance | **EXISTS** | Z / C / V, Alt = squad | cards |
| Cold, heat pack, barrel | **EXISTS** | `UColdSurvivalComponent`, `ABarrelActor`, `TacticalHeatSubsystem` (heat pack Sprint 15, awaiting the user's confirmation) | Confirm heat pack design (producer D5) |
| Cold zones per area | **PARTIAL** | `ACameraZoneVolume` (camera switch off = pure cold zone; enum x0 / x0.5 / x1 / x2.5) | G5 (S): float multiplier (`AColdZoneVolume` or a float override) for x0.2 in A0 |
| Buildings / interiors | **PARTIAL** | Camera zones: fixed camera, the leader alone inside, the squad holds outside, environment cold | G9: no roof cutaway / occlusion fade for a whole-interior map (B). Roofs hidden in game as a workaround |
| Crates trapped / untrapped | **EXISTS** | `ALootCrateActor` (`bTrapped`, `Contents`, defuse 2 s) | per-actor demo damage values |
| Mines (PMC) | **EXISTS** | `AProximityMineActor` (hidden, spotted 4.5 / 6 m, defuse menu, Ctrl+click) | - |
| Tripwire disarm | **PARTIAL** | `ATripwireActor` / `TripwireRules`: squad deployable only; Medic-Sapper disarm 3 s, 10 % fumble | G3 (M): **level-placed PMC tripwire** (editable anchors, armed at begin play, hidden until spotted like mines, saved), disarm by the cold-aware formula |
| Too cold -> cannot disarm | **EXISTS** for mines / crates / trapped objects; **MISSING** for tripwires | `DeployableRules::CalculateDefusal` (-15 / -30 / -50 at > 25 / 50 / 75 cold), warning "my fingers are numb" | G3 extends it to wires |
| Quest: navigate to important objects | **PARTIAL** | `FQuestChainState` hard-coded canister -> vehicle -> generator -> terminal -> gate (reused in A); objective banner | G4 (M): objective marker (world beacon + screen-edge arrow + distance). Generic quests are not needed for demo 1 |
| Cover-to-cover movement (exploration) | **EXISTS for one operative** | `TryHandleCoverClick` (leader / TB active unit): ghost preview + confirm, shimmy, corners within 2 m, N lean / blind, Ctrl+click corner shot, Alt+click on objects; cover halves wind chill; hides from observers | G7 (M): **followers do not take cover**. In formation they stand in the open behind a covered leader. Commander Mode autonomy seeks only **barricade actors** and only with a threat <= 25 m; the "within 2.2 m of a barricade, crouch" rule is combat-only. Proposal below |
| Enemy attack (wave) | **EXISTS** | Quest console -> `TriggerCombatZone` -> sudden wave + flyover | demo JSON |
| Three fight forms | **EXISTS** | RT auto fire, tap pause (3 x 30 s, 12 m), hold 1.5 s TB (21 x 21 m flat), free pause on sudden waves | Tutorial layer (UX W1-W13) to stage it |
| Dynamic breakthrough (flank breach) | **EXISTS, dormant** | `AEnemySpawnPoint.bIsDynamic` + `UWaveSubsystem::CheckDynamicFlankSpawners`, TB deferral, camera reveal; plus TB `ExposedZones` breach | G13 (S): JSON `breach_wave` (deterministic); place the point with `ActivationChance` 1.0 |
| Rescue character | **EXISTS** | `URecruitSubsystem` (Susanin; 2.8 m or heat source; joins as member 4; expendable) | G13 (S): JSON `susanin_wave`; user BP look (pink-tint placeholder) |
| Next location / transition | **MISSING** | `LoadGameWithTravel` reopens the same map only | G8 (M): `ALocationExitVolume` + squad-only transfer |
| Patrols / guard posts / ambush | **EXISTS** | `APatrolRouteActor`, route-less guards, escort hounds, `ULevelEncounterSubsystem` | - |
| 4-min mid-fight horde | **EXISTS** | `UHordeSubsystem`, `horde.json` 240 s, per-level override | G12 (S): 180 s warning + visible fight clock; decide the TB rule (Q4) |
| Height advantage | **EXISTS (real time)** | `SquadFireRules::GetElevationAdvantage` (>= 0.5 m feet difference: +25 % range, +15 % dmg, over low cover; dead zone) | G11 (S): "HIGH GROUND" floating text / badge the first time per fight; G10 (S): elevated wind uses absolute Z (relative to the zone floor would be correct; indoors it should not apply) |
| Checkpoints | **PARTIAL** | Autosave BeforeCombat / AfterCombat, F5 / F9 (newest save) | G6 (S): `ACheckpointVolume` (exploration autosave, named slot) + "Retry from checkpoint" on the failure screen |
| Panic off / vision cones on | **EXISTS** (per component / BP) | `UPanicComponent::bEnabled`, `bShowVisionCone`, `Codex.Debug.VisionCones` | Demo flag on the GameMode (producer C10) |
| **Barrel move: exploration** (rev 2) | **EXISTS** | `URelocationSubsystem` push (15 m, cold < 80 %, HP >= 50 %), burning barrels too | cards + events (G1) |
| **Barrel move: live real time** | **PARTIAL (by design)** | Menu offers only Light in a live fight (user decision 2026-10-08); Ctrl+click detonation EXISTS | Taught as a pause order; Q6 decides whether to allow a direct RT push (G19, S) |
| **Barrel move: tactical pause** | **EXISTS** | Planned push / light / Ctrl+click run on release (12 m; free pause unlimited) | cards |
| **Barrel move: turn-based** | **EXISTS** | `TryPushAdjacentBarrel`: orthogonal neighbour, 1 cell per 2 AP; grid shot 3x3 / 80 | cards |
| **Squad mines / barricade / turret placement** | **EXISTS** | F set-up from supply, R rotate; exploration 15 m, pause 12 m (planned), TB mine 2 AP / others 3 AP; not in live RT | cards + events |
| **Squad tripwire** | **EXISTS** (squad) | Drawer -> two anchors, 2 grenades, Medic-Sapper rigs 2 s, arms 1.5 s; friendly fire; refused in TB | in the pause: verify (smoke) |
| **Booby-trap an object (mine a barricade)** | **EXISTS / PARTIAL** | Action menu **Set trap (N)** on any interactable (leader's grenade, 85 / 4 m); enemy contact 1.8 m on barricade / crate / turret; Ctrl+click = remote detonation | PARTIAL: no TB action; barrels lack an enemy-contact trigger; trap in the pause to verify. G20 (S): TB «Set trap» (1 AP) + barrel contact trigger |
| **Key object: generator** | **EXISTS** | 200 HP, small enemies prefer it (0.4 x), breakdown = heat off + turrets unpowered, Engineer repair 2.5 s | Repair in a live fight: **verify** (G18) |
| **Blast door opens only while the generator runs, full after W3** | **IN PROGRESS (another agent)** | `AGateActor` + flow | Door progress on the HUD (part of that work); debrief metric (G16) |
| **Engineer repairs barricades** | **MISSING** | Barricades 200 HP, no repair action | G18 (S): «Repair» on a damaged barricade (Engineer 3 s, others 6 s, +50 % HP) |
| **Reading the terrain (approach signs)** | **MISSING (content + tutorial)** | Spawn lanes exist (`EnemySpawnPoint.SpawnLane`, type filter); signs are art | User art (tracks, claw marks, fence gaps, howls) + `ATutorialTriggerVolume` sign volumes (G1) + debrief (G16) |

### 9.1 G7 proposal: "squad stacks on the leader's cover" (minimal)
- **When:** the leader confirms a cover slot (any phase, real time or pause) and the followers are in formation (not solo, not guard, not under a direct order).
- **What:** each follower gets the nearest free slot within **6 m** of the leader's slot. First on the same wall (slots 1.2 m apart along the wall via `CoverTraceRules::FindShimmySlot`). Then on another cover surface within 6 m, probed from the formation positions. Same cover class: low -> crouch; high -> stand with the back to the wall. If no slot is found, the follower crouches at his formation point.
- **Leave:** when the leader moves away more than 3 m, followers leave cover and resume formation (the existing `LeaveCoverToMove`).
- **Not in TB** (TB units are ordered one by one). Not for the leader's corner slot (followers never take the exposed edge).
- **Completion event:** `OnCoverEntered(Operative, Height, bExposedEdge)` on the event bus (also needed by the tutorial).
- **Size:** M (rules + test `CodexTactics.Tactics.Cover.SquadStack.*` + `CoverSquadSmoke`). It also helps stealth: today a hidden leader with visible followers gives the squad away.

---

## 10. Build lists

### 10.1 User: Location A (block out in this order)
1. `L_Demo01_A_FrozenLake`: ground plane ~260 x 180 m **at Z = 0**, NavMeshBounds over everything, World Settings GameMode = demo A BP.
2. **Critical path graybox:** A0 shore + PlayerStart -> beacons 1-7 (cylinders + point lights) -> camp (3 boxes 6 x 4, 8 x 6) -> depot (10 x 8 shed, 5 x 4 hut, sandbag ring) -> station (18 x 10 building, generator shed 4 x 3, boathouse 12 x 8 with GateActor) -> road east.
3. **Station yard arena** 30 x 30 m flat: 4 low walls 1.0 x 4 m, 2 high blocks 2.2 x 3 m, 3 barrels; the ice north open 25 m then a ridge at 30-35 m.
4. **A6 ridge field:** 6 cover pieces per §4 A6 (heights 2.2 / 1.0 / 2.4 / 1.0 / 2.2 / APC 2.6 m; 5-6 m spacing; 2-3 m zigzag).
5. A1 ridges (one `Vault`-tagged 0.8 m) + the 2 m crack (nav blocker).
6. Place actors: quest objects (Canister, Vehicle, Generator, GateTerminal), `CombatStart` target point, `EnemySpawnPoint` x4 on the ice N (lane `NORTH_ICE`, 2 also `WEST_SHORE`), 1 dynamic `EnemySpawnPoint` behind the boathouse (`bIsDynamic`, chance 1.0, count 3), `SusaninSpawn` target point + unlit barrel 2.5 m away, barrels (A2 x2, A4 x1, arena x3), crates (A3 clean, A4 trapped + clean, A6 optional trapped, A11 clean), 2 level mines (A4), the PMC door charge / tripwire (A4), notes (A3, A5).
6b. **Rev 2 (anticipation):** generator shed at the yard NE corner; the east gully + 2.5 m drift; west fence gap 2.5 m; approach-sign art (hound tracks decal, claw marks, PMC body, drag trail, dented bulkhead); spawn points named `NORTH_ICE` x2, `EAST_GULLY` (hounds only), `WEST_SHORE`, dynamic `BOATHOUSE_BULKHEAD`; fuel dump 3 barrels; barrel 1 at 10 m from hut 1 + lee-wall spot; hut 1 door 2.0 m; depot barrel 12 m from the shed door; TB-pad barrel 3 m in front of the line.
7. Volumes: `ACameraZoneVolume` (A0 Shelter switch off; huts / shed Shelter + fixed cameras; station Closed + fixed camera; optional A5 blizzard strip), `ADialogueTriggerVolume` per `WT_*` row, tutorial trigger volumes (when G-layer lands), `ACheckpointVolume`s (G6), `ALocationExitVolume` (G8).
8. Art pass later (beacons with numbers, PMC stencils, APC, ice cracks).

### 10.2 User: Location B (block out in this order)
1. `L_Demo01_B_Garage`: yard 70 x 50 at Z = 0, NavMeshBounds, GameMode B BP.
2. **Hall shell** 36 x 24 x 7 m with doors: roller 4.5 m S, side 1.6 m E, rear 1.6 m N; roof hidden in game.
3. **Mezzanine** 3.0 m along N + W, 4 m deep, railing 1.0 m, 2 stairs 1.2 m wide (SW, NE), office 6 x 5.
4. Bays: 2 pits (1.2 deep x 1 x 5 m, step at each end), lifts, partitions 2.4 x 4 m, benches 1.0 m, shelves 2.2 m (spacing 5-6 m).
5. Yard cover: scrap cars, pumps, tyre stacks; yard shed + barrel.
6. Rev 2: 2 oil drums in bay 2 (12 m from the SW stair foot); roller door 4.5 m wide (room for a 3 m barricade + 1.5 m gap).
7. Place: PlayerStart SW, `APatrolRouteActor` P1 (45 m, through the bays) + P2 (80 m, round the hall), placed `BP_Enemy_*` per §7 B2 (8, + optional Marksman), the side-door tripwire (G3), 2 rear mines, trapped crate, 2 oil drums in bay 2, Shelter cold zone on the hall (camera switch off), checkpoint volumes.

### 10.3 Claude work list

| Id | Item | Size |
|---|---|---|
| G1 | Tutorial layer (UX W1-W13: command registry, `UTutorialSubsystem`, hint card, tracker, trigger volume, events incl. cold tier changed / solo toggled / cover entered / shimmy / deployable placed / looted / drawer opened / pause used / TB entered) | L (already in producer M1) |
| G2 | Intro as walk-and-talk: mission start plays a feed sequence instead of the blocking intro (per GameMode flag); subtitle / radio strip | S + (W5 M) |
| G3 | Level-placed PMC tripwire: editable anchors, armed at begin play, hidden until spotted (mine rules 4.5 / 6 m), cold-aware disarm via `DeployableRules::CalculateDefusal` (role / stance / cold), save / load, smoke | M |
| G4 | Objective / tutorial marker actor (world beacon + edge arrow + distance), driven by quest state and tutorial steps | M |
| G5 | Float cold multiplier zone (`AColdZoneVolume` or a float override on the camera zone) | S |
| G6 | `ACheckpointVolume` (exploration autosave, once) + "Retry from checkpoint" on Mission Failed | S |
| G7 | Squad stacks on the leader's cover (§9.1) + `OnCoverEntered` event | M |
| G8 | `ALocationExitVolume` + squad-only transfer save/apply across maps (incl. Susanin) + `DemoTravelSmoke` | M |
| G9 | Interior roof hiding / occlusion fade for B (or confirm "roof hidden in game") | S (hide) / M (fade; material side goes to Gemini via TANDEM) |
| G10 | Elevated wind relative to the local floor / off indoors | S |
| G11 | "HIGH GROUND" feedback (floating text once per fight + combat badge icon) | S |
| G12 | Horde pre-warning radio line at `trigger - 60 s` + visible fight clock | S |
| G13 | Level JSON `breach_wave`, `susanin_wave` (deterministic events) + Wave Editor fields | S |
| G14 | Demo data: `demo01_a_frozen_lake.json`, `demo01_b_garage.json` (Wave Editor), GameMode BPs flags (Panic off, cones on), `NewGameLevel` | S |
| G15 | `DemoMapLint` (required actors / tags per beat) + `DemoBeatSmoke` (bot walks A0 -> A12, B0 -> B4) + bot tuning to 85-90 % | M |
| G16 | Demo end card + stats + feedback (producer M1 / UX W11), **incl. the anticipation debrief §5.3** | M |
| G17 | Tutorial events for rev 2: `RelocationOrdered/Done`, `BarrelLit/Exploded`, `TurnBasedRelocate`, `DeployablePlaced`, `TripwireRigged`, `ObjectTrapped`, `TrapTriggered`, `MineMishap`, sign-volume visited, generator damaged / down / repaired (the last two exist) | S (part of G1) |
| G18 | Repair in a live fight verified (generator / turret use order in RT + pause, `RepairInCombatSmoke`) + **barricade repair** action | S + S |
| G19 | (only if Q6 = yes) direct barrel push in live real time | S |
| G20 | «Set trap» in turn-based (1 AP, adjacent object) + enemy-contact trigger for trapped barrels | S |

Critical path for the user to start building: none. Every actor needed for a graybox exists today, except the tripwire (use the trapped-object placeholder) and the exit / checkpoint volumes (place a plain box and swap later).

---

## 11. Total length

| Part | Beats | First-time minutes |
|---|---|---|
| A Stage 1 (learn) | A0-A4 | 17.5 |
| A Stage 2 (quest + recon / prep) | A5-A7 | 15 |
| A Stage 3 (fight) + aftermath | A8-A12 | 12 |
| B | B0-B4 | 16 |
| **Total** | | **~47-52 min** (veteran ~30). Cut list (§2.4) -> ~35-38 min |

---

## 12. Questions for the user (with recommendations)

1. **Two maps or one?** *Recommended: two maps* (`L_Demo01_A_FrozenLake`, `L_Demo01_B_Garage`) plus a squad carry-over (G8). One map would pull the garage enemies into the lake fight (every enemy in the world joins an ambush / wave), and the lake would turn into an ambush level. Fixing that is a large refactor.
2. **Rescue and enemy counts.** *Recommended:* **Susanin** (the existing recruit) in lake wave 3, 28 m west, saved by walking up or by fire, carried into the garage. Lake waves on **three lanes** (N 4-6, the hidden E gully 2 hounds going for the generator, W 2-4) + 3 breach hounds in W2. Garage 8 placed + an 8-enemy horde. **No Marksman** in demo 1.
3. **Generator in the lake fight:** can the generator be **destroyed for good**, or only knocked out (repairable)? *Recommended: only knocked out* (today's rule: breakdown at 0 HP, repair restores it), with the cost being heat, turret power and door time, never a game over.
4. **The 4-minute horde in the garage: real or only a warning? And should turn-based time count?** *Recommended: real*, with a radio warning at 3:00 and a visible fight clock, and spawns pulled into the yard (20-40 m). Today **pause and turn-based stop the clock**, so a TB-only player never meets the horde. Recommendation: keep that rule (TB is the "safe but slow" option) and say it on the hint card. The alternative is to count each TB round as ~10 s.
5. **PMC tripwires:** should they be **hidden until spotted** like mines, should **cold lower the disarm chance** as it does for mines, and may **anyone** disarm (role chance) or only the Medic-Sapper (today: 3 s, medic only, no cold)? *Recommended: hidden; cold applies; anyone may try with the role chance* (the Medic-Sapper stays the obvious pick: 88 % vs 58 % for the Commander).
6. **Followers in cover (G7) and barrels in live real time (G19):** (a) when the leader takes cover, should the formation **stack into nearby cover automatically**? *Recommended: yes* (exploration and real time; not solo / guard / TB). (b) A **direct barrel push in live real time** is off today by your 2026-10-08 decision, so it is taught as a pause order. Keep it that way? *Recommended: keep* (moving a barrel is a plan, and the pause is the planning mode).

Выполнено моделью: Claude Opus 5.5 (claude-opus-5-5)
