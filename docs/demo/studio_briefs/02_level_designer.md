# 02 - Level Designer: Tester Demo #1 (educational level)

Read-only analysis. Sources: `CLAUDE.md`, `docs/port/HANDOFF.md` (sections 3, 5, 6, 7, 9, change log), `PORT_MATRIX.md`, `REFERENCE_PLAYTHROUGH_01.md`, `Scripts/test_map.json`, `Scripts/Editor/create_movement_test_map.py` + `add_level_objects_to_movement_test.py` (design coordinates), `Content/Data/LevelJson/*`, `Content/Data/Narrative/dialogues.csv`, Godot `design/levels/stage-01-bunker-gate.md`, `design/gdd/game-concept.md`. Map contents were read from asset names inside the `.umap` binaries (the maps do not use One-File-Per-Actor), so counts are "at least". I did not open the editor.

## 1. INVENTORY

### Maps (`Content/Maps/`)

| Map | .umap | Role | Contents / used by |
|---|---|---|---|
| `L_MainMenu` | 31.5 KB | **Frontend** (not a mission: `IsMissionWorld` false) | Title + main menu camera anchors, 1 `CodexMenuCharacterActor`, 2 static meshes. `FrontendSmoke`, `MainMenuSmoke`. NEW GAME opens `CodexFrontendSettings.NewGameLevel` (single soft world setting - must point to the demo later). |
| `L_MovementTest` | 211 KB (changed 2026-10-11, uncommitted) | **User's main test map and the only content map.** Never reset, never regenerate (`create_movement_test_map.py` overwrites edits). All smokes run here and are layout-relative (anchored on the "Floor" actor and the gate). | ~60 x 60 m yard (design coords; the user rotated it 180 deg and moved it). South wall + `Gate_Checkpoint` (GateActor) with `CombatStart` target point behind it; quest chain `Quest_Canister`, `Quest_APC`, `Quest_Generator`, `Quest_GateTerminal`; a camera-zone bunker room (`CameraZone_Bunker` + `Camera_Bunker`); fuel barrels (`BarrelActor`, user BP `BP_Barrell_01` x2); 2 barricades (one with spikes, the generator one); 2 hidden mines; `Crate_Supply_Checkpoint` + `Crate_Supply_Trapped`; narrative: `Narrative_NoteGenerator`, `Narrative_SignpostCrossroads`, `Narrative_PosterBunkerWall`; `DialogueTrigger_WaveRest`; `SusaninSpawn`; 4 `EnemySpawnPoint` (the user moved them next to the PlayerStart, 20-28 m, so a wave spawns around the start); ~20 static-mesh blocks (walls, pillars, low barricade); NavMeshBounds (no saved RecastNavMesh - see trap in HANDOFF section 2). |
| `L_PatrolTest` | 120 KB (changed, uncommitted; committed version has no enemies) | **User's patrol / stealth test map.** | 2 `PatrolRouteActor`, user-placed `BP_Enemy_Hound` x2 and `BP_Enemy_Marksman` x2 (route-less ones guard their post), 9 static meshes, NavMeshBounds. `PatrolAmbushSmoke`, `VisionConeSmoke`, `BotStealthSmoke`. |
| Demo map | - | **Does not exist.** | There are no maps for stage_02..12 either: all level JSONs are wave sets for the same yard (as in Godot). |

All three are test/frontend maps. No map is "content-complete" (placeholder shapes for barrels, barricades, mines, quest objects; engine blocks for walls).

### Level data (`Content/Data/LevelJson/`, read at runtime by `LevelJsonRules`; DA_Level_* = fallback)

- `level_01_outpost.json` = `stage_01.json` (identical): "Battle 1: Checkpoint Skirmish", 3 waves, **144 enemies**: W1 45 Hound + 10 Frostbitten (all at once), W2 40 Hound + 5 Cutter + 10 Frostbitten + 2 Marksman, W3 20 Hound + 6 Cutter (200 HP) + 6 Marksman; modifiers hp/damage x1 -> 1.1, cold_drain x1; loadout: 1 turret, 2 barricades, 2 mines, 2 medkits, M16 30, pistol 24, 4 canned food, 3 matches; prep 60 s, rest 20 s (parsed, only matter for waves with `"sudden_start": false`). Game mode default `LevelJsonFile = "level_01_outpost.json"`. Bot balance: VETERAN wins 4/10, 16 of 19 losses are freezing in wave 3 - **far too hard for a tutorial**.
- `stage_02..stage_12.json`: 2-4 waves, 10-48 enemies, Hound/Spitter/Brute only (spitter has no art), three biomes. Not usable for the demo as is.
- `stage_template.json`: empty template.
- JSON holds **only waves, modifiers, timings, loadout, optional `horde`/`horde_enabled`, per-wave `sudden_start`**. It has **no combat_start, no quests, no spawn coordinates**: combat start = actor tag `CombatStart` + game-mode `ECombatStartMode` (auto / ambush / button); quests = hard-coded `FQuestChainState` (canister -> APC diesel -> generator -> terminal -> gate); spawn lanes = `EnemySpawnPoint` actors in the map (lane names in the map are Russian Godot strings and do not match the JSON NORTH_GATE etc.; laned entries fall back to a random point - known, left as in Godot).
- Sudden horde start is the default: no prep, no countdown; wave spawns into a running fight, then a free tactical pause + slow-motion flyover (`Codex.HordeIntro.*`). Mid-fight horde after 240 s of real-time fight (`Content/Data/AI/horde.json`, switch off per level with `"horde_enabled": false`).

### Narrative
`Content/Data/Narrative/dialogues.csv` -> `narrative_manifest.json` (EN only, runtime-read): `DA_DialogueIntro` (15-line banter at the drop point: Sokolova/Medic, Commander, Engineer; Sector-17 quarantine lore), `DA_DialoguePrep`, `DA_DialogueWaveRest`, `DA_DialogueVictory`, `DA_DialogueSusaninRecruitment`. In-world: 3 `ANarrativeElementActor` notes (generator note, crossroads signpost, bunker poster) and `ADialogueTriggerVolume`. Radio feed lines exist for quest steps. Missing EN lines list: `docs/port/narrative_missing_en.md`. Characters: Commander (Capt.), Engineer, Medic-Sapper Sokolova, recruit Susanin ("Colonel Vasin" in Godot).

### Content vs test
Test: L_MovementTest, L_PatrolTest. Frontend: L_MainMenu. Content: none.

## 2. MECHANICS CHECKLIST (player-facing, in code)

Maturity: **S** solid (pure rules + parity tests + smoke, user-verified), **R** works but rough/awaiting the user's review, **P** placeholder art/VFX.

| Mechanic | Input | Maturity / notes |
|---|---|---|
| Camera | WASD / edge pan, wheel zoom, Q/E (and arrows) 45 deg rotate, RMB-drag rotate, MMB pan, 1-4 recentre; camera-zone volumes (leader only) | S |
| Select / move | LMB click = move leader, double-click = sprint, 1/2/3 select, LMB-drag box select + group orders, Shift+click = face a sector | S |
| Formation / solo / guard | squad column follows; B solo (frost-crawl not ported), T guard (fix an operative on the spot) | S |
| Stances | Z stand / C crouch / V prone; Alt+Z/C/V whole squad; action-bar letter | S |
| Vault | auto when ordered across a low barricade / `Vault`-tagged object (no clips, arc only) | R |
| Cold + frostbite | automatic; cold bars, vignette, misfire / aim penalty, frozen weapon, speed tiers | S (cold clips: user art) |
| Heat sources | barrel + match (action menu), generator warm zone, heat pack (U, drag onto a cold mate), campfire / gas stove (F, drawer), signal flare (Shift+G), fire puddle | barrel/generator S; Sprint 15 heat items **R/P** (waiting for the user's confirmation, placeholder shapes, Niagara slots empty, no flamethrower, no night) |
| Consumables | H medkit, J canned food, K bread, L chocolate, U heat pack; drawer (ИНВ) | S |
| Inventory / transfer / drop / stash | drawer drag-and-drop onto a model or squad slot, quantity split dialog, drop piles, stash, lift limits (cold/HP) | S |
| Weapons | X cycle arsenal (M16 / pistol / knife / grenade / sniper for the Medic), R reload, weapon selector; M16 25 m | S; pistol/knife/shotgun have no models (switch shows no mesh) P |
| Auto combat (RTS real time) | operatives fire on their own; LMB on an enemy = priority target; Ctrl+click = targeted shot (enemy, barrel, mine, crate, trapped object) | S |
| Fire postures | `,` passive / `.` defensive / `/` aggressive (Alt = squad) | S |
| Cover | LMB on a wall / low obstacle = ghost preview, second click = confirm, click along the wall = shimmy, Alt+click on a barricade/object, N lean <-> blind fire, RMB/Esc cancel; high cover absorbs 90 %, low 35 % crouched | S logic, animation partly user polish |
| Commander Mode (autonomy) | Ctrl+T, HUD "АВТО"/"AUTONOMY"; Shift+RMB defend an object (in pause/outside fight only) | S |
| Tactical pause | Space tap; 3 charges per wave; plan moves/shots/placements; green 12 m order ring; cyan plan markers; free pause on a sudden horde | S |
| Turn-based combat (Gorky 17) | hold Space (hold sphere, 15 m) -> 14x14 grid, 8 AP, Tab next, Enter end turn, Z/C/V, R/Q/E rotate, Ctrl attack, grenades, barrels, deploy; **flat ground only** | S, enemy turn rules per user decisions |
| Grenades / flare | G aim + LMB throw (RMB/Esc cancel), Shift+G flare; AI squad also throws at clusters | S |
| Sniper (Medic-Sapper only) | kneels/prone to fire, never fires while walking | R (no real rifle mesh, M16 stand-in) |
| Knockdown / get-up | Brute blow knocks the leader down, 2.3 s, buffered order | R (tuning with the user) |
| Rage / panic / wounds | automatic; rage "В ЯРОСТИ", panic on, wounded < 50 % no sprint | S |
| Deployables | pick up / relocate (ПЕР button, F, drawer): barricade (200 HP, spikes), proximity mine (arming 3 s), tripwire (2 grenades, medic-sapper rigs), turret (powered by generator, 12 m); R/wheel rotate, 15 m placement radius; roles route work (engineer, sapper) | S; art P |
| Traps / mines | hidden level mines spotted at 4.5 m (sapper 6 m), defuse via menu; trapped crates/objects | S |
| Interactables | click -> approach -> action menu; barrels ("push", light), crates (loot dialog, defuse), gate terminal, generator (damage/repair), notes ("read aloud") | S |
| Quest chain | canister -> diesel from APC -> generator (warm zone + powers turrets) -> terminal -> gate cutscene -> combat zone | S, hard-coded for this one chain |
| Recruits | Susanin rescue on wave 1 (3 s in, narrative pause), joins as member 4 | S |
| Stealth | guard posts (placed enemies without route), spline patrols, escort hounds, sight + suspicion meter, hearing by gait (sprint far, crouch/crawl near silent), gunshot/grenade noise, hound smell, wall occlusion, trap search; **contact = ambush fight** | S (found bugs fixed 2026-10-11) |
| Vision cones | off by default; BP_Operative / BP_Enemy_* class default `bShowVisionCone`; console `Codex.Debug.VisionCones 1` | R (engine fallback material) |
| Horde | wave spawns into the running fight -> mass detection -> free pause + slow-mo flyover + alert; 4-min mid-fight horde; breach points dormant | R (reworked 2026-10-11, waiting for the user) |
| Waves / victory | 3 pause charges, victory panel with per-wave kills, EXP +25 per wave, levels, P profile (spend points) | S |
| Defeat | only the Commander's death fails (death cinematic, "MISSION FAILED"); others are permanent losses; Ctrl+X restart | S |
| Saves | F5 quicksave, F9 quickload, pause menu save/load slots, autosave right before a fight (BeforeCombat) and after (AfterCombat); **no saving in combat** | S |
| Frontend | title, main menu, new game / continue / load, pause (Esc), confirm dialogs; Options screen placeholder | S/P |
| Dialogue / feed | bottom dialogue window (Space/Enter next, Esc skip), radio feed lines with speaker, objective banner top-left | S |

Not present: any tutorial, hint, key-prompt or controls-reference UI; objective markers; scripted triggers beyond dialogue volumes, camera zones and narrative notes.

## 3. PROPOSED DEMO LEVEL #1 FLOW

Working title: **"Sector-17 Perimeter"** (matches Godot Stage 01 "Bunker Gate": drop point -> yard + generator -> gate -> arena -> airlock). Target: **~35-40 min** for a first-time tester (30 min for a veteran). Cold is the universal pressure: tutorial zones carry a **reduced cold multiplier** (CameraZoneVolume already has an environment cold multiplier) so early beats are forgiving; full cold from beat 7.

Principle: one new verb per beat, taught in a safe spot, practiced once, tested by the next beat. Everything is skippable by simply walking on (prompts are non-blocking); beats 4-6 allow "sneak OR fight".

**Map plan (one new map `L_Demo01`, user builds):** Z1 Drop point (snowmobile, trench) -> Z2 Warehouse yard with containers, barrels, crates, signpost, generator and APC (the existing L_MovementTest toolkit) -> Z3 Guard-post lane (2 route blocks + pen) -> Z4 Firing yard with low walls and a flat concrete pad -> Z5 Gate arena (the L_MovementTest south gate + CombatStart) -> Z6 Airlock vestibule (warm room, debrief). Reuse L_MovementTest's actor kit by copy (never edit the original).

| # | Beat / area | Goal | Mechanics taught | Setup | Prompting | Success / failure | Time |
|---|---|---|---|---|---|---|---|
| 0 | **Briefing** (frontend NEW GAME) | Understand who/why | Dialogue window, objective banner, feed | `DA_DialogueIntro` shortened to ~6 lines ending with the first order; no timers | existing | Skip (Esc) or Next; no fail | 1.5 |
| 1 | **Landing / Move out** (Z1) | Reach the generator yard road | Camera (pan, zoom, Q/E), LMB move, double-click sprint, 1/2/3 select, formation, Z/C/V stance, box select, B solo + T guard, vault a low barricade | Open trench, 3 pads/markers, one low barricade (tag `Vault`), a pipe you must crouch under or a "crawl" gap, a stray wall pocket for the camera-zone demo; no enemies; cold x0.3 | Beat prompts (key + one sentence) fired by trigger volumes as the leader enters; radio from Commander/Medic | Success: leader on the exit pad with all 3 alive; no fail | 4 |
| 2 | **Cold and fire** (Z2a) | Warm the squad | Cold bars and vignette, frostbite, light a barrel with a match, warm radius, heat pack (U, drag onto a mate), food (J), medkit (H), campfire / stove (F) | Cold x1.0 in a windy corridor so a bar reaches ~60 % in ~40 s; 2 fuel barrels + a crate with heat pack, firewood, canned food; matches 3 | Objective "Get the squad warm"; the Medic complains at 40 % cold | Success: all three <= 30 % cold; fail = nothing (barrels are plentiful, a heat pack in the crate) | 3 |
| 3 | **Search and salvage** (Z2b) | Take the supplies | Crate -> loot dialog, trapped crate defusal (Medic-Sapper), hidden mines (spotted 4.5 m / sapper 6 m, defuse or Ctrl+click from afar), inventory drawer, drag-and-drop transfer + split, drop, read a note | 2 plain crates (ammo, medkit), 1 trapped crate (grenades + barricade + mine), 1 hidden mine on the path, note "Duty engineer"; a spare loot item that is too heavy at high cold (lift limit) optional | Prompt on first crate click; dialogue-volume radio lines; objective "Collect gear" | Success: crate looted AND deployables in inventory; fail: detonation hurts (kept mild; Engineer can die = permanent, acceptable lesson, or place the trap near but give a safe defuse by the Medic) | 4 |
| 4 | **Silent approach** (Z3) | Pass or ambush the guard post | Vision cones (enable `bShowVisionCone` on BP_Enemy hound/marksman and BP_Operative for the demo), posture keys `, . /`, crouch/prone to be quiet, noise by gait, suspicion meter, patrol route + escort hound, mine / tripwire ambush on the route | Zone with a spline patrol (marksman + escort hound) crossing the lane, a guard-post hound; cover walls; 1 mine + the tripwire kit in the crate of beat 3 | Prompt "Enemies have vision cones - stay out of them"; Commander: "Don't sprint here"; Passive posture suggested | Success: squad past the lane undetected OR patrol dead. Failure branch: detected -> ambush fight (existing, auto) -> beat 5 skill used; commander death = mission failed | 5 |
| 5 | **Firefight school** (Z4, may start from the ambush) | Win a small fight | Real-time auto fire, LMB priority target, Ctrl+click targeted shot (barrel / mine), cover click+confirm, N lean/blind, X weapon / R reload, G grenade, Shift+G flare, Medic sniper (kneel/prone) , Commander Mode Ctrl+T | A "kennel": a pen of 4 Frostbitten + 2 Hound behind a low wall as **placed guard enemies** (dormant until perceived/hit), explosive barrel near them, cover walls, a crate with 2 grenades | Prompt on first enemy in view: "LMB on an enemy to focus; Ctrl+click on the barrel". Prompt on low health: "Take cover" | Success: pen cleared; fail: commander dies. Ammo is plentiful | 5 |
| 6 | **Time control** (Z4b, same yard) | Use pause and the grid | Space tap (charges 3, plan moves, green order ring), hold Space -> hold sphere -> turn-based grid (8 AP, Tab, Enter, stance, shot, grenade), Brute knockdown, leave grid (hold Space) | A second mini-fight on a flat concrete pad (grid needs flat ground): 1 Brute + 3 Frostbitten that approach in a line from 40 m so there is time | Hard prompt when the first enemy comes within 30 m: "Hold SPACE"; second prompt in the grid ("Tab / Enter") | Success: Brute dead; the tester must have entered the pause or the grid once (detected via flow phase change) - otherwise a gentle repeat prompt only; fail: commander dies | 5 |
| 7 | **Power the gate** (Z2c -> Z5) | Open the gate | Quest chain: canister -> APC siphon -> generator (warm zone, powers turrets) -> terminal; interaction menu; Engineer repairs; gate cutscene | The existing quest objects, full cold now; warm zone is the base | Existing objective banner texts (quest chain) + radio lines | Success: gate open; this triggers combat zone | 4 |
| 8 | **Prepare the line** (Z5) | Build the defence | Per-operative placement, F set-up from supply, R/wheel rotate, barricades, turrets (powered), mines, tripwire, guard T / defend Shift+RMB, F5 save, radius ring | Gate arena with cover blocks; **wave 1 is a prepared wave (`"sudden_start": false`, prep ~90 s)** so the countdown, "Start battle" button and prep dialogue are real; loadout 1 turret, 2 barricades, 2 mines, tripwire grenades | Prompt "Press F to set up from supply" + prep banner; autosave BeforeCombat fires | Success: player presses Start / countdown ends. A checklist (barricade placed / turret placed) is hinted not enforced | 3 |
| 9 | **The Horde** (Z5) | Hold the gate | All of the above; sudden start + flyover; Susanin rescue (wave 1); cold drain in waves; EXP/level-up; Medic revive via medkit | Waves in a new `demo_01.json`: W1 6 Hound + 4 Frostbitten (prepared); W2 (sudden, flyover) 10 Hound + 3 Cutter + 1 Marksman; W3 (sudden) 12 Hound + 8 Frostbitten + 1 Brute + 2 Marksman; cold_drain x1.0-1.1; `horde_enabled: false` (or keep as a surprise at 240 s); no spitter | HUD banners, victory panel per wave | Success: all 3 waves dead; fail: commander death -> restart from BeforeCombat autosave | 7 |
| 10 | **Debrief** (Z6 airlock) | Reflect | Victory panel, profile (P) points, warm room, summary of what was learned | Warm-room set piece, camera zone, final radio line | Dialogue + a debrief panel (new) with kill stats, time per beat, "keys you used", feedback prompt | End | 2 |

Total ~40 min (the 0.5 min items are cuttable: beats 3 and 6 can be shortened first).

Notes on the beats:
- The **gate/CombatStart** pair gives the finale; enemies also reachable earlier by "placed enemies" which already start the ambush fight on any map.
- Beats 4-6 are **placed-enemy encounters** (guard posts, a patrol): this is how a scripted fight can start mid-level today (no wave spawner involved). The ambush fight keeps WaveCombat / RealTime, so pause and turn-based work in it.
- Wave sizes in the new JSON are guesses; validate with the Wave Editor curve + the playtest bot (VETERAN band 40-75 %) before testers see it; `level_01_outpost` stays untouched.

### What the user must build in the editor (layout/art is the user's)
1. New map `L_Demo01` (World Settings GameMode override pointing to a demo GameMode BP with `LevelJsonFile = demo_01.json`, combat start mode `button`, `bShowVisionCone` where wanted). Do **not** edit L_MovementTest / L_PatrolTest; copy actors out of them.
2. Zones Z1-Z6 geometry, navmesh bounds (no saved RecastNavMesh), flat pad for the grid fight, covers (high >= 180 cm, low < 130 cm), Vault-tagged obstacles.
3. Place: PlayerStart, quest objects (canister, APC, generator, terminal), gate + `CombatStart` target point (tag), 4+ `EnemySpawnPoint` beyond the gate (lane names to match the JSON), `SusaninSpawn`, crates, barrels, mines, barricade, `PatrolRouteActor` splines, placed `BP_Enemy_*` (guard posts, pen), `ADialogueTriggerVolume` / `ANarrativeElementActor` for radio lines, `ACameraZoneVolume` (also for cold multipliers), `CameraActor` for the finale.
4. Set `CodexFrontendSettings.NewGameLevel` to the demo when it is ready.

### What code must support (new)
Everything in section 4 marked "code".

## 4. GAPS

| Gap | Needed for | Owner | Notes |
|---|---|---|---|
| **Tutorial prompt layer**: world-trigger -> on-screen hint (key icons + one line), dismiss on completion, show-once per save, optional "tutorial off" | all beats | code (Claude), look by user | No hint UI exists. Reuse `UGameMessageSubsystem` (feed with speaker) for the radio voice; add a small widget (top-centre) + `ATutorialBeatVolume` (box, text id, completion condition) driven by data (`Content/Data/Tutorial/demo_01_beats.json`, same pattern as LevelJson / narrative manifest, text ids in the manifest so writers keep one pipeline) |
| **Completion events** | beats 1, 2, 5, 6, 8 | code | `UCodexEventBus` already has: leader change, soldier events, item used (medkit...), mine spotted, line/feed, dialogue closed, generator down/repaired, save/load, rage. Missing / to verify: stance changed, sprint used, entered cover, targeted shot fired, grenade thrown, pause entered / turn-based entered (use `OnGameFlowChanged`), deployable placed, container looted. Add the missing ones as events |
| **Objective markers** (world beacon + screen-edge arrow, distance) | beats 1, 7, 8 | code + user art | Only banner text exists; no waypoint marker |
| **Objective source** | all | code | Banner is driven by `MissionSubsystem` / quest state; needs a generic objective that a beat can set (`SetObjective(text id)`), with the quest-chain text kept for beat 7 |
| **Gating / locks** between zones | order of learning | code (or user BP) | Only `GateActor` (quest-driven). Add a generic `ABeatBarrier` (blocks until a beat completes, optionally soft: a prompt and a push-back) or accept free order |
| **Checkpoints** | testers who die in beats 4-9 | code | Only the autosave before / after combat exists (BeforeCombat / AfterCombat, `OnBeforeGameFlowChanged`) and F5 / F9. Add "Checkpoint" autosave on beat completion (saving is not allowed during combat) and surface "Press F9 to retry" in the failure screen; today's failure = restart (Ctrl+X) |
| **Scripted spawns** | beat 6 (Brute line), optional ambush on beat 9 | code | `UWaveSubsystem::SpawnEnemy` exists; add a trigger actor that spawns a group (`ATutorialEnemySpawner`) - or place the enemies as guard-post enemies (no code) |
| **Per-zone cold** | beats 1-3 | none/small | `ACameraZoneVolume` has an environment cold multiplier - verify it works outside the camera role; otherwise add a `ColdZoneVolume` |
| **Controls reference** | all; testers forget keys | code | Pause menu has no help page; Options is placeholder. Add a "Controls" screen (CommonUI frontend pattern) listing every key in section 2 |
| **Debrief + tester feedback** | beat 10 | code | Victory panel exists; need a summary (per-beat time, deaths, keys not used) and an "open feedback form / save log" action. `URunTelemetrySubsystem` + the `Saved/Coach` pipeline exist - extend with beat timestamps |
| **Demo level data** | beat 9 | data (Claude via Wave Editor) | New `demo_01.json`: smaller waves, `sudden_start:false` on wave 1, `horde_enabled:false`, no spitter (no art), loadout for the tutorial |
| **Menu entry** | beat 0 | config | `NewGameLevel`; "Continue" works via saves |
| **Art / VFX gaps that testers will notice** | all | user / Gemini | Placeholder shapes for barrels / barricades / mines / quest objects; heat VFX slots empty (TANDEM request 15); no pistol / knife models; sniper uses the M16; no spitter / cryo-drone art; marksman beam material pending; no cutscene video; Cyrillic leftover in the map's actor labels / lane names (EN only on screen); Susanin pink tint placeholder |
| **Known rough behaviour to avoid or hide** | beats 5, 6, 9 | - | Melee stand-off (hounds stop ~1.7 m before the bite - gameplay decision pending), turn-based requires flat ground (keep a pad), horde flyover and knockdown tuning await the user's review, the squad is in `Exploration` until a gate / ambush: full prompts must not fire during combat dialogues (world AI pauses under dialogue) |

Layout the user must make beyond placement: nothing else in code is "layout"; but because the original L_MovementTest has the spawn points around the start, the demo needs the spawn ring beyond the gate in its own map.

## 5. QUESTIONS FOR THE USER (max 6)

1. **One continuous map or several short maps?** Recommend **one new map `L_Demo01`** (zones in one navmesh, no loading). L_MovementTest stays the sandbox.
2. **Tutorial voice:** on-screen key prompts (non-blocking, can be turned off) + radio lines from the Commander/Medic/HQ, or full modal dialogs? Recommend **prompts + short radio**, no blocking windows except the 6-line briefing.
3. **Target length and difficulty:** 35-40 min for a first-time tester? Waves must be much easier than `level_01_outpost` (it is the benchmark for later stages and nearly unwinnable for the bot). Recommend a separate **`demo_01.json`**, bot win 70-85 %, `horde_enabled:false` (keep wave 2 flyover as the surprise).
4. **Show vision cones in the demo?** They teach stealth but are "off by default as in Godot". Recommend **on for operatives and enemies in this level**, set on the demo's GameMode / placed actors, not on the global BP defaults.
5. **Do we include advanced systems (turn-based grid, tripwire, sniper, flare, campfire) or only the core loop?** Recommend **include all but mark beats 4's tripwire and 6's grid as "advanced - skippable"**, and cut Sprint 15 heat (campfire, stove, flare) from the critical path until the user confirms the design decisions.
6. **Checkpoint rule for testers:** autosave at each beat (not in combat) and allow F9 retry, vs one hard run per defeat? Recommend **per-beat autosave + clear "retry from checkpoint" on the failure screen** (testers must reach the horde; collecting feedback matters more than punishment).

Optional QA note: a `DemoSmoke` per beat (bot-style, like `BotStealthSmoke`) would keep the demo regression-safe.

Выполнено моделью: Claude Sonnet 5.5
