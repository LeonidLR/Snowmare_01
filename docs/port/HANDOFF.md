# HANDOFF — CodexTactics (UE 5.8 port of Operation: Cold Silence)

**Purpose.** Any agent (Claude, Gemini, …) must be able to pick up the port from this file alone.
Keep it current: every commit that adds / changes a system updates §5 (system map), §8 (next steps) and §10 (log).

Last update: 2026-09-29 by Claude, after commit `2ca14b0`.

---

## 1. Read first (in this order)

1. This file.
2. `CLAUDE.md` — project rules (Godot is read-only, no GAS, commit per verified system, ask only on real design forks).
3. `docs/port/PORT_MATRIX.md` — Godot file → UE class table with status and tests; intentional deviations.
4. `docs/port/REFERENCE_PLAYTHROUGH_01.md` — the user's reference video, the order the port follows.
5. `docs/port/TANDEM.md` — coordination log between agents (claim files before editing, one builder at a time).
6. `docs/port/ARCHITECT_SUPERVISOR_DIRECTIVE.md` — Gemini's directive; §4 lists where Godot code wins over it.

Godot source of truth: `C:/Users/Zephyrus15Duo/Documents/Codex/godot-test-01/` (READ ONLY — never edit).
The user's rule: **Godot code values win** (except explicit user decisions listed in PORT_MATRIX "deviations").

## 2. Environment facts and traps (read before touching anything)

| Fact | Consequence |
|---|---|
| Engine `C:/Program Files/Epic Games/UE_5.8`, project `Documents/Unreal Projects/CodexTactics` | Scripts assume these paths. |
| **`Documents/Codex/unreal` is a junction to this repo** | Another agent working "in Codex/unreal" works on the *same files*. Builds / tests collide (DLL locks, overwritten `Saved/Logs`). Claim work in TANDEM and build one at a time. |
| The open **Unreal Editor locks the module DLLs** | Close it before `build.ps1` (graceful: `CloseMainWindow`, wait 45 s; never force-kill a window with a save dialog). The user allowed closing / reopening it. |
| Build log `Saved/Logs/Build.log` is UTF-16-ish | Read it with PowerShell `Get-Content`, not grep. |
| UHT forbids names shadowing `AActor` members (`Role`, `Instigator`, …) | Use `SquadRole`, `InstigatorName`. MSVC C4458 is an error here too. |
| `unreal.log()` in `-run=pythonscript` commandlets does not reach stdout | Editor scripts write their result to `Saved/Logs/*.txt` (see `add_level_objects_to_movement_test.py`). |
| Editing a Blueprint's inherited component via Python CDO | Use `set_editor_property` on the component template; `set_relative_transform` is NOT persisted. |
| Git LFS for `.uasset/.umap` (no `lockable`) | `lockable` made maps read-only; keep it off. |
| The user re-laid `L_MovementTest` in the editor (whole layout rotated 180° and moved to Floor (-800, 1130); PlayerStart (-180, -1490)) — user decision 2026-09-29: keep it | Smokes write points in the original design coordinates (map script, Floor at the origin, yaw 0) and map them with `SmokeUtils::LevelPoint` / `LayoutTransform` (the "Floor" actor's location + yaw); `PlaceSquadAtTestStart` puts the squad at the design origin facing design +X. Never hard-code world coordinates in a smoke. |
| A saved `RecastNavMesh-Default` that is rotated / not tile-aligned stays empty at runtime ("Recreating dtNavMesh instance … not aligned with tile size", then nothing is built) | The map ships without a RecastNavMesh actor; it is created and built at load (RuntimeGeneration=Dynamic). A commandlet that loads and saves the map re-creates an EMPTY one that also stays empty at runtime: editor scripts must destroy `RecastNavMesh` actors before saving (see add_level_objects_to_movement_test.py). If smokes suddenly cannot move the squad ("leader on navmesh=0"), check this first. |
| Unity builds merge .cpp files | Anonymous-namespace names collide across files (`PanelColor`, `Clean`…): prefix file-local helpers (`Menu…`, `Loot…`). Names like `FItemInfo` can also clash with engine types. |
| Default canvas / Slate fonts have no emoji | HUD / menu strip them (`ACodexTacticsHUD::StripUnsupportedGlyphs`); texts stay verbatim Godot with emoji in code. |
| Bash heredocs with long / complex Python sometimes break in this harness | Write the Python to the scratchpad with the file tool and run `python <file>`. |

## 3. Build, test, verify

```
powershell -ExecutionPolicy Bypass -File Scripts/verify_all.ps1              # build + all tests + all smokes, summary
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 [-Filter CodexTactics.Cold]
powershell -ExecutionPolicy Bypass -File Scripts/smoke.ps1 -Command CodexTactics.DeployableSmoke
```

State at last update: **136 automation tests, 48 smokes, all PASS** (`verify_all.ps1` → ALL GREEN; it also fails on an engine crash during the tests now).

Smokes (dev console commands in `Source/CodexTactics/Private/Debug/`, run headless on `/Game/Maps/L_MovementTest`):

| Command | Checks |
|---|---|
| `MovementSmoke` | sprint through the corridor, column formation |
| `CameraZoneSmoke` | leader-only camera zone, followers hold |
| `QuestChainSmoke` | canister → APC → generator → terminal → gate via the action menu, walk through the gate |
| `CombatFlowSmoke` | Space tap / hold, tactical pause planning, turn-based enter / exit, free pause |
| `WaveCombatSmoke` | enemy archetypes, wave subsystem (Gemini) |
| `ColdSmoke` | cold accumulation, Freezing tier speed, frostbite, frozen weapon |
| `StanceSmoke` | BP_Operative + Explorer mesh + ABP, capsule per stance |
| `BarrelSmoke` | light a barrel (match), warmth, burn-out, burnt menu, no matches |
| `RelocationSmoke` | «Вытолкать» a barrel 6 m, cold refusal, cancel |
| `DeployableSmoke` | barricade pick-up → F set-up with hand-over, hidden mine spotted, sapper defusal (retries), grenade trap, mine blast |
| `TargetedShotSmoke` | Ctrl + click via `IssueTargetedShot`: barrel explodes (tracer + flash spawned), prone mine shot, trapped crate / barricade detonated, untrapped crate only pierced, 1 round each, priority enemy over a nearer one, pause-planned shot (with a plan marker) fires on release, markers cleared |
| `MainMenuSmoke` (run with `-Extra "-ForceMainMenu"`, verify_all does it) | start menu open + world paused, «Начать бой» (quest chain done, squad healed / warmed, cutscene), Ctrl + X repeats the mode, «Начать заново» shows the menu |
| `DialogueSmoke` (`-ForceMainMenu`, verify_all does it) | «Начать игру» opens the 15-line intro briefing, Space advances (no pause), skip closes, preparation lines reach the feed with the Godot delay |
| `ActionBarSmoke` | action bar stance slot cycles the squad, «ПЕР» pick mode on / off, squad slot 2 selects the engineer |
| `BannersSmoke` | cutscene card + Space skip, squad warm / healed for the preparation, preparation / wave / pause banner texts |
| `SaveLoadSmoke` | (Saved/SmokeSaves) known squad / quest / crate state saved to «Леонид_01», changed, loaded back (health, cold, position, items, pistol clip, guard, quest chain, looted crate); F5 quicksave; slot list, metadata, next name, delete |
| `RadiusRingSmoke` | Ring hidden outside the pause; tactical pause: green 12 m order ring; barricade set-up in the pause: worker radius green inside / red outside; barrel relocation: cyan; released pause hides it |
| `PauseMenuSmoke` | Esc: pause menu (world paused, «Загрузить» off without saves); save dialog (suggested name, card added, overwrite confirmation, Esc closes it only), back to the menu, load closes everything and restores the squad, delete, Esc closes the menu |
| `TransferSmoke` | «ПЕРЕД» dialog (title, M16 60, hidden plasma; the drawer closes it); medkit: hand-over mode with the ring, click on himself refused, click on the engineer hands it over; M16 pack of 30 by a click near the engineer; cancel hides the ring |
| `InventorySmoke` | «ИНВ» opens the drawer (title, lines; the weapon selector closes it); H medkit +80 HP, drawer canned food -25 cold; drawer turret with none on the commander: a squad mate hands one over, placement starts |
| `GuardSmoke` | T fixes the engineer on its spot (out of the formation): the commander leads 8 m away, the engineer stays, the medic follows; T again returns it |
| `GrenadeSmoke` | grenade aim (indicators, 20 m clamped to 12 m, crouching 9 m); throw at a frozen brute: one grenade spent, release + flight + 1.2 s fuse, brute hurt, barrel in the blast burns; a grenade thrown just before turn-based combat is refunded, no grenade in hands |
| `WeaponSelectorSmoke` | arsenal on every operative (4 weapons, M16 30 / 60); weapon slot opens «ВЫБОР ВООРУЖЕНИЯ» (M16 marked «В РУКАХ»); pistol / knife taken, grenade refused (throw mode next); turn-based switch of the active operative, no AP |
| `TurnBasedDeploySmoke` | turn-based: turret on a neighbour cell (3 AP, grid + unit state, item spent); a squad mate hands a mine over, mine 3 cells away: the commander walks up, walk + 2 AP |
| `TurnBasedBarricadeSmoke` | turn-based: barricade footprint = Godot samples; the commander walks up, click picks it up, two 45° steps (target cells recomputed), click places it: new footprint, rotation, 2 AP |
| `TurnBasedPushSmoke` | turn-based: the commander walks up to a barrel; click picks it up (3 target cells), cancel, panel «Бочка» picks it up, click on a cell pushes: barrel +1 cell, commander on its old cell, 2 AP |
| `ExposedZonesSmoke [shot]` | turn-based with one hound; the squad passes 3 turns: warning (1), danger (2), breach of 1-2 hounds (non-elite pool), no second breach; `shot` (rendered, UnrealEditor.exe -game) saves `ExposedZones.png` at the danger state |
| `NarrativeSmoke` | Level narrative elements (note, signpost, poster) and the dialogue trigger: marker only from afar, the note's text within 2 m, menu «Записка дежурного инженера» / «Прочитать вслух» reads it into the feed; the trigger plays the wave-rest dialogue once |
| `HoldSphereSmoke` | Space hold: the dome grows (eased) and the squad holds fire; release hides it and lifts the cease fire |
| `TurnBasedCameraSmoke` | Cinematics forced on: 16 m on the operative's turn, the camera on the enemy during its turn and back on the active operative, the dramatic shot (framing 10..19 m, the round lands at 0.75 s, orders wait), the glide back at the combat distance, the pre-combat zoom restored |
| `EventBusSmoke` | A listener hears every `UCodexEventBus` event from its real emitter (leader change, profile point, drawer medkit, feed line, dialogue close, rage, generator down / repaired, save / load, a fallen operative) |
| `VictorySmoke` | All waves killed (commander / turret / mine sources): kill statistics, the panel per wave, «next wave (N/M)» starts the rest, «full victory» after the last; post-combat: squad at its preparation spots, commander leads, no enemies, a turret back in the supply |
| `ProgressionSmoke` | +260 EXP -> level 2 (3 points, full heal, «УРОВЕНЬ 2»); a hound kill gives every member 15 EXP; P opens the profile, + / - spend and refund points within the bounds, paging, number keys, Esc, wave-clear auto open |
| `ClickRulesSmoke` | Plain-click rules in a fight (Godot main.gd): an enemy becomes the priority target; a barrel / barricade can't be moved outside the pause and a ground click can't move the squad (HQ lines); in the pause a barrel is picked up for relocation and a barricade opens its menu at once |
| `CutterSmoke` | Cutter (Godot enemy_cutter.gd): 75 HP / 18 damage; pounces from 6 m, the landing hurts the squad within 2.2 m («НАЛЁТ»), 6 s cooldown; shot down mid-leap it crashes («СБИТ В ВОЗДУХЕ», «КРАХ») |
| `EnemyAISmoke` | Enemy AI (Godot enemy_base.gd / enemy_frost_*.gd): brute affinities (kinetic 0.25, energy 2); frost halves the speed; a hound 3 m from a burning barrel flees («СТРАХ ОГНЯ»); a hound picks a close turret (level generator set aside — it outranks everything for small enemies); a brute smashes a barricade in its way; a spitter 10 m out shoots the squad |
| `AIGrenadeSmoke` | A pack of three hounds 9 m ahead in the fight draws autonomous grenades with a radio callout; an empty M16 switches to the pistol, an empty pistol to the knife |
| `SquadControlSmoke` | Wounded below 50 % (no sprint, slower, speed follows the health), healed sprints again; Alt + C crouches the squad («👥 ОТРЯД: ПРИСЕВ», order line); Shift + click faces the point; Alt + V refused while someone moves; an operative arriving within 2.2 m of a barricade in combat crouches in cover («В УКРЫТИИ») |
| `RageSmoke` | Rage (Godot rage_component.gd): commander config 2 crits / 9 s; one crit no rage, the second from the same hound (forced roll) enters rage with «В ЯРОСТИ!» and the shout; orders refused «НЕ ПОДЧИНЯЕТСЯ»; in the fight the enemies in reach are sprayed without spending rounds; the calm line when it wears off |
| `SquadFireSmoke` | Real-time squad fire (Godot _find_shoot_target / _shoot_at_target): prone behind a barricade blocked + text, crouched cover 0.8, standing cover 1; flank hound taken only after the standing 0.15 s delay; crouched crit = 2.5 x a standing plain shot + «КРИТ x2!» |
| `FloatingTextSmoke` | Operative hit formula (Godot player.gd take_damage): forced dodge «УКЛОНЕНИЕ» costs nothing, crouched hit = 40 × 0.75 × (1 − fortitude × 1.5 %) with «-N», crit text, bypass hit ignores dodge / cuts; medkit «+N HP»; guard texts; brute armor number «🛡️ -N»; texts expire |
| `SusaninSmoke` | Rescue on wave 1: Susanin appears 3 s into the wave at the SusaninSpawn point outside the squad (cold 85, civilian kit), narrative pause + camera + distress dialogue; closing it restores time / camera, HQ line and objective; an operative within 2.8 m opens the recruitment dialogue (Susanin faces him); accepting adds him as member 4 (key 4, cold 0, radio lines, wave objective); load toggles him out of / back into the squad |
| `FlankBreachSmoke` | Spawn point type filters (spitters never at the hound-only point, hounds never at the spitter-only one, brutes only at open points), unmatched lanes fall back to any static point, dynamic points are never wave points; breach wave with instant events: pack of 3 around the dynamic point + HQ line, camera on the point after 1 s with the squad line, back on the leader 1.8 s later, one breach per mission |
| `LevelWaveSmoke` | imported level drives the flow (3 waves, 60 s / 20 s); wave 1 = 12 enemies at once at the 4 spawn points, cold drain 1.1 |
| `TurnBasedSmoke` | wave + one brute (a far hound stays outside the fight in stasis), enter turn-based: grid registration, 8 AP, stance 1 AP, enemy turn (walk, bite 13 on a crouched commander, step back), move into a fire lane + shot -> victory -> tactical pause |
| `MissionSmoke` | objective banner texts (start → preparation → wave), an operative's death fails the mission (GameOver, reason, time stop), restart reloads a fresh exploration |
| `TurretSmoke` | turret shoots an enemy, generator breakdown unpowers / repair powers, broken turret repaired by the engineer, pick-up, F set-up |
| `LootSmoke` | crate opens without a menu → loot dialog, one stack + «Забрать ВСЁ», empty crate line, trapped crate defusal + deployables, detonation burns the loot |
| `HudShot [close] [walk] [menu] [place] [shoot] [failed] [mainmenu] [dialogue] [cutscene] [prep] [turnbased] [weapons] [grenade] [inventory] [transfer] [pause] [saves]` (`dialogue`: intro briefing window; `shoot`: slowed-down barrel shot = tracer, target flash, plan marker; `failed`: mission-failed screen; `mainmenu`: needs `-ForceMainMenu`) | rendered screenshot `Saved/Screenshots/WindowsEditor/HudShot.png` (needs rendering, run UnrealEditor.exe -game with `-ExecCmds="CodexTactics.HudShot close"`) |
| `FinishPrep` | dev: skip preparation, start the wave |

Parity tests live in `Source/CodexTacticsTests/Private/<System>/` named `CodexTactics.<System>.<Case>`; they mirror
Godot numbers (same inputs → same outputs). Pure rules are kept in `*Rules` namespaces / structs so they are testable.

## 4. Conventions

- C++ first, Blueprints for look / tuning only. **Characters are Blueprints the user owns** (mesh, AnimBP, collision):
  C++ exposes EditDefaultsOnly properties + BlueprintImplementableEvents, never hardcodes visuals a BP cannot override.
- Each class header names its Godot reference file / functions. Epic coding standard (A/U/F/E/I, TObjectPtr, IWYU).
- Player-facing texts: **verbatim from Godot** (Russian, emoji included) in `LOCTEXT`.
- Colours from Godot are sRGB: use `FLinearColor::FromSRGBColor(FColor(...))`.
- Units: Godot metres × 100 = cm; Godot `+Y up` → UE `+Z up`.
- Commits: Conventional Commits, body references the Godot file(s), `Co-Authored-By` trailer. Only verified work.
- After each system: update PORT_MATRIX row, TANDEM log, this file (§5, §8, §10).

## 5. System map (UE → Godot)

Module `CodexTactics` (runtime). Folder → class → Godot reference.

**Core**
- `Core/CodexTacticsGameMode` — squad spawn from `BP_Operative` (roster: name, colour, offset, fortitude, role, luck),
  HUD class, deployable spawn classes. Godot `main.gd` bootstrap.
- `Core/CodexTacticsPlayerController` — input (Enhanced Input created at runtime): LMB click (select / object / move;
  double click = sprint), 1–3 select, Z/C/V stance, B solo, Q/E + RMB drag rotate, MMB pan, wheel zoom (BindKey),
  Space tap / hold, R rotate placement, **F set-up from supply**. Placement mode: LMB confirm, RMB cancel, wheel rotate.
- `Core/CodexTacticsGameState`.

**Game flow / combat (partly Gemini)**
- `GameFlow/*` — phases Exploration → Cutscene → Preparation → WaveCombat (RealTime / TacticalPause / TurnBased) →
  WaveCleared / PostCombat / GameOver; time dilation 0.02 in the pause. Godot `main.gd` flags.
- `Combat/SpaceInput`, `EncounterQueries`, `HealthComponent` (armour, elements, `ApplyDirectHealthLoss`),
  `WaveSubsystem`, `EnemySpawnPoint` (Gemini). `Characters/EnemyCharacter` + `EnemyAIController` (Gemini, tag `Enemy`).
- `Tactics/GorkyGridManager`, `Gorky17Types` — grid, arc zones, AP BFS, A* (Gemini). Turn-based manager NOT yet.
- `Data/CombatTypes`, `WeaponDataAsset`, `EnemyArchetypeAsset`, `WaveConfigTypes` (Gemini; values hand-typed — see §9).

**Squad / characters**
- `Characters/OperativeCharacter` — orders, stances (capsule per stance, placeholder body until a skeletal mesh),
  sprint rules, shooting (Gemini) with cold misfire / aim penalty, inventory (matches 3, grenades 2/4, turrets /
  barricades / mines with limits 2/4/5), role (Commander / Engineer / MedicSapper) + luck (25/30/35),
  carrying (`SetCarrying`), lift limits (80 % cold, 50 % HP), placement radius 15 m. Godot `player.gd`.
- `Characters/OperativeAnimInstance` — state for AnimBP + native locomotion blend (proxy `Evaluate`) until the user's
  graph exists (`bUseNativeLocomotion`). Godot `locomotion_controller.gd`.
- Ctrl + click targeted shots: `ACodexTacticsPlayerController::IssueTargetedShot` (Godot `main.gd` Ctrl branch:
  enemy → `SetManualPriorityTarget`, barrel / mine / crate / trapped object → `AOperativeCharacter::ShootAtObject`;
  in the tactical pause → `PlanTargetedShot`, run by `USquadSubsystem` on release before the planned moves).
  `Combat/TargetedShotRules` = mine hit chance clamp(max(10, Accuracy − cold·0.25)·stance − m·perMetre, 0, 95),
  stance 0.6/4.0, 1.0/1.8, 1.25/1.0; miss reasons. `Accuracy` per role 90 / 75 / 85 (roster). The priority target is
  used first by `FindBestCombatTarget` while alive, in range and in the line of fire (barricade blocks prone only).
  Godot `player.gd shoot_at_*`, `calculate_mine_shot_hit_chance`, `set_manual_priority_target`, `_find_shoot_target`.
- `Combat/CombatFeedbackSubsystem` + `CombatFeedbackActor` — shot tracers with muzzle flash (operative: weapon
  `TracerColor`, glow / fade by damage type, misses deflected; turret: green), cyan plan markers of the tactical pause
  (planned moves, interaction approach, relocation / deploy targets, targeted shots; cleared on every combat-mode
  change), Ctrl + click target flash (red light + glowing overlay on the target's meshes). Glow material
  `/Game/VFX/Materials/M_CombatFeedback` (unlit additive, `Color` × `Intensity`) from
  `Scripts/Editor/create_feedback_material.py`. Effects run on world time (slow down in the pause, as Godot tweens).
  Godot `_spawn_muzzle_tracer` (player / turret), `_spawn_waypoint_marker`, `_clear_planned_markers`,
  `_highlight_target_feedback`.
- `Characters/SquadSubsystem`, `SquadFormation`, `OperativeMovementRules`, `OperativeAIController`.
- Assets: `/Game/Characters/Operatives/BP_Operative`, `ABP_Operative`, `Explorer/…`, `Animations/…`, `/Game/Weapons/M16`.

**Camera** — `Camera/TacticalCameraPawn` (+Rules), `CameraZoneVolume`. Godot `camera.gd`, `camera_zone_trigger.gd`.

**Survival** — `Survival/ColdRules`, `ColdSurvivalComponent`; `Interactables/HeatSourceComponent`. Godot
`player.gd _process_cold_system`, `warm_zone.gd`.

**Interaction / world objects**
- `Interactables/InteractionSubsystem` — click → approach → `BuildActionMenu` → menu or feed line; confirm /
  relocate / trap / cancel. Mine "approaching defuser" mark. Godot `main.gd _trigger_menu_for_object`,
  `_open_action_menu`, `_on_action_confirmed`, `_on_relocate_confirmed`, `_on_trap_confirmed`.
- `Interactables/ActionMenuTypes` (`FActionMenuSpec`, `FActionMenuRequest`), `UI/ActionMenuWidget` (UMG built in C++,
  restylable via a WBP with the same widget names), owned by `UI/CodexTacticsHUD`.
- `Interactables/InteractableActor` — base: quest objects (menus per quest state), **traps on any object**
  (`bTrapped`, defusal attempt on confirm, `TrapWithGrenade`, `DetonateTrap`, `ApplyBlast`). Godot `interactable.gd`.
- `Interactables/BarrelActor` + `BarrelRules` — match, 35 s fire, fade 7 s, heat 5.5 m, charred, `Explode` (trap / shot).
- `Interactables/RelocationSubsystem` + `RelocationRules` + `RelocationGhostActor` — move objects (ghost, radius,
  push task, pause plans, combat / mine drop) **and set-up from supply** (F, two-click placement, deploy tasks,
  mine mishap). Godot `main.gd` relocation + placement blocks.
- `Interactables/DeployableActor` (+ `DeployableRules`) — pick-up / defuse / relocate menus, 1.1 s dismantle, role
  routing. `BarricadeActor` (200 HP, tripwire on enemy contact 1.8 m). `ProximityMineActor` (trigger 1.6 m, blast
  120 / 3.5 m, arming 3 s for squad mines, hidden level mines spotted at 4.5 m / sapper 6 m).
  Godot `deployables/barricade.gd`, `mine.gd`.
- `Interactables/LootCrateActor` + `LootRules` (`FLootContents`, `ELootItem`, `FLootEntry`) — supply crate: intact →
  `HandleDirectInteraction` opens the lid (1.8 s) and the loot dialog, trapped → defusal menu (2 s crouched work),
  detonation burns the contents, enemy contact 1.8 m, tiers Standard / Maximal, Godot colours. Items go to the
  leader: provisions / matches / M16 reserve / deployables (no limit, as Godot) / `ExtraAmmo` for weapons not ported /
  `BonusItems`. `UInteractionSubsystem::OpenLootDialog / LootItem / LootAll / CloseLootDialog`,
  `UI/LootDialogWidget` (+ `ULootEntryButton`). Godot `loot_crate.gd`, `loot_dialog.gd`, `main.gd` loot handlers.
- `Interactables/TurretActor` — powered by default; fires at the nearest visible enemy (≤ 12 m, 16 dmg / 0.45 s,
  barricade in the line of fire = 60 %, walls block); 120 HP, breaks at 0 (repair engineer 2 s / others 4 s);
  menus broken / unpowered («Закрыть») / trapped / pick-up; powered turret is a heat source (4.5 m, Godot
  heat_sources); `SetAllPowered`. Deploy / pick-up routed to the commander first. Godot `deployables/turret.gd`.
- Generator damage on `AInteractableActor` (Generator type): `TakeGeneratorDamage` (200 HP) → breakdown (heat off,
  turrets unpowered, «[АВАРИЯ]» menu), repair on confirm (engineer 2.5 s / others 5 s) → `RepairGenerator`.
  Starting the generator also powers all turrets. Godot `interactable.gd breakdown_generator / repair_generator`.
- `Quests/QuestChain`, `QuestSubsystem`, `Interactables/GateActor`. Godot `quest_manager.gd`, `gate.gd`.

**Turn-based combat** — `Tactics/TurnBasedCombatSubsystem` (starts when the flow enters TurnBased, ends on victory /
defeat / leaving): 14 x 14 grid (1.5 m) around the leader, floor traced under it, static colliders baked as obstacles,
barricades / barrels / mines / turrets registered, everything else frozen (enemy / turret / mine ticks off, wave
spawning paused in `UWaveSubsystem::Tick`). Squad round: 8 AP each — move 1 / diagonal 2 (reachable BFS, a mine on the
path stops the walk and blows), stance 1, turn 90° 1, one attack 3 (fire lane, LoS, hit chance with stance bonus, arc
damage − armour, barrel = 3 x 3 blast + 3 burning rounds with a 5 x 5 fire-fear area), Tab next operative, Enter
end the squad turn. Turret phase (nearest enemy ≤ 45 m, fallback curve, 25 dmg). Enemy phase: nearest operative, path
to an orthogonal neighbour (fire-fear aware), bite for 2 AP after a 1.3 s yellow warning (arc x stance multiplier),
step back. Steps animate 0.52 s (enemy 0.48 s, x1.414 diagonal). Victory → «🏆 ПОБЕДА…», flow → tactical pause.
`UI/TurnBasedHudWidget` (bottom-right panel «ХОД ОТРЯДА» / «ХОД ПРОТИВНИКА», unit, AP / HP, end squad turn, next
operative, stance, turn; barrel push disabled — Godot `gorky17_combat_hud.gd`). `Tactics/TurnGridOverlayActor` (instanced glow tiles: grid, reachable, attack targets, enemy reach, active, warning,
fear). Controller: clicks go to `HandleWorldClick`; 1..3 / Z C V / R / Tab / Enter as above. Grid manager gained
`FindPathToAdjacent` / `FindPathClosestOutsideForbidden`; barrels `IgniteForTurnBased` / `ExtinguishNow`.

**Phase banners** — `UI/PhaseBannersWidget`: pause banner «РЕЖИМ ПРИКАЗОВ | Зарядов… | Время планирования…»,
combat banner «ПОДГОТОВКА К БОЮ: NN сек» + «Начать бой» (`FinishPreparation`) / «ВОЛНА N | ВРАГОВ ОСТАЛОСЬ: M»,
pre-combat cutscene card (black screen, countdown; click or Space skips = `FinishCutscene`). The mission subsystem
resets the squad (cold 0, full health, stop) when the cutscene ends (Godot `_end_cutscene_and_start_pause`).

**Action bar** — `UI/ActionBarWidget` (Godot TacticalBar, bottom centre): «ПЕРЕД» / «ИНВ» (transfer, inventory —
disabled until ported), weapon slot «M16 [clip/reserve] | [G] Граната» («Перезарядка...»), «ПЕР» (object-pick mode:
`ACodexTacticsPlayerController::ToggleRelocateSelectMode`, next object click → `StartRelocate`; «АКТИВ» while
picking / placing), stance letter С / П / Л (click → `CycleLeaderStance`), «ОБОР» (guard — disabled until ported),
squad slots [1] КОМ [2] ИНЖ [3] МЕД + HP / cold bars, [4] РЕЗ locked. Hidden while the start menu is open. Godot
`main.gd _update_tactical_command_bar`, `_cycle_leader_stance`, `_on_relocate_slot_clicked`.

**Dialogues** — `Data/DialogueSequenceAsset` (lines: speaker, text, delay) imported from the Godot .tres by
`Scripts/Editor/import_dialogues.py` into `/Game/Data/Dialogues/DA_*` (intro, prep, wave rest, victory, Susanin
recruitment) — re-run it when Godot texts change, never hand-edit. `UI/DialogueSubsystem`: `StartDialogue` = bottom
window `UI/DialogueWidget` (speaker card, [N / M], «Пропустить» / «Далее»; Space / Enter / click next, Esc skip, world
orders blocked), `PlayInFeed` = timed lines in the message feed. `UI/DialogueRules` (speaker card, button texts).
Game mode soft refs `DialogueMissionStart / PreparationStarted / WaveRest / Victory`. Mission hooks: start (Game /
Exploration: intro window; headless starts get the radio line instead), first preparation (prep), later preparations
(wave rest), PostCombat (victory, then objective «РУБЕЖ ЗАЧИЩЕН: Исследуйте…» + HQ line). Portraits: Godot emoji have
no UE font glyphs → role tags КОМ / ИНЖ / МЕД / ЖИТ / ?. Godot `bottom_dialogue_dialog.gd`, `main.gd play_dialogue`.

**Start menu** — `UI/MainMenuWidget` + `Core/MissionSessionSubsystem` (GameInstance: last mode, quick-restart flag
across level reloads). `UMissionSubsystem::StartMission(Game | Combat)` (two modes — user decision): Game sets the
objective and the commander line; Combat completes the quest chain (`UQuestSubsystem::CompleteChainForCombat`: generator
running, gate open), heals / warms the squad, moves it to the actor tagged `CombatStart` (TargetPoint behind the gate on
L_MovementTest) and starts the pre-combat cutscene. The menu opens on every level start (world paused) except after
Ctrl + X (same mode again) or with `-ExecCmds` / `-NoMainMenu` on the command line (headless checks start Game);
`-ForceMainMenu` keeps it. **Every new smoke / HudShot is therefore unaffected by the menu.** Godot `main.gd _ready`,
`_on_start_game_pressed`, `_on_start_combat_pressed`, `_on_start_exploration_pressed`, StartMenu.

**Mission** — `Core/MissionSubsystem` + `Core/MissionRules`: objective text (start «Исследовать КПП…», quest chain
objectives, preparation / wave «ОБОРОНА: Отразить волну N! Врагов: M» / victory texts), mission failed on any
operative death (reason hypothermia at cold ≥ 99 else wounds, HQ radio line, GameOver = time stop), `RestartMission`
(reload level; «Начать заново» and Ctrl + X). Godot `main.gd update_objective`, `_trigger_game_over`,
`_restart_current_test_mode`.

**UI** — `UI/CodexTacticsHUD` (canvas: objective banner «ЦЕЛЬ: …» top left, message feed, squad panel with supply,
labels; owns the action menu, loot dialog and `UI/MissionFailedWidget` «МИССИЯ ПРОВАЛЕНА»). Godot colours go
through `ACodexTacticsHUD::GodotColor` (sRGB → linear) in UMG and canvas.
`UI/GameMessageSubsystem` (feed, logs every line to `LogCodexTactics`).

**Editor scripts** (`Scripts/Editor/`, run with `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path>`):
`create_movement_test_map.py` (regenerates the test map — **overwrites manual edits, avoid**),
`add_level_objects_to_movement_test.py` (adds barrel / barricades / mines / supply crates by label, keeps edits;
Python names drop the `b` prefix of bools: `bTrapped` → `trapped`),
`create_operative_blueprint.py`, `import_operative_assets.py` (Explorer glb merged + root bone baked, M16, 60 FBX
clips), `setup_operative_animation.py` (ABP + BP wiring, M16 offset from Godot). All are idempotent.

## 6. Data / content status

- Operative art imported from `Documents/Codex/ASSETS` (same as Godot). No textures exist for Explorer; the coat is
  tinted with the role colour (Godot `material_override` on the coat).
- Placeholder meshes (engine shapes) for barrels, barricades, mines, quest objects; the user will replace them.
- The user reported the character looks "перекручено" (twisted) — not investigated yet (ask what exactly: spine /
  hands / pose), see §8.

## 7. Open user decisions / questions

- L_MovementTest re-laid by the user (rotated 180°, moved): kept (user decision 2026-09-29); smokes are layout-relative.
- `Config/DefaultEditor.ini` has local editor changes — never commit it unless asked.
- M16 clip (user decision 2026-09-29): 30 for every operative like the reference video. The current Godot scene sets
  reload_after_shots 10 (commander) / 15 (engineer) — deliberately not ported.
- Level waves (user decision 2026-09-29): like the Godot code — `main.gd _spawn_custom_json_wave` spawns the whole wave
  at once; `spawn_delay_sec`, `initial_delay_sec`, `max_simultaneous_enemies` (described in DATA_CONTRACTS.md) are
  ignored. Wave modifiers apply (hp / damage / speed per enemy, `cold_drain_mult` → operatives' cold outside camera
  zones, kept after the wave like Godot's cold_rate_modifier), `custom_stats.health` × hp_mult.
- Monster models: the user imports them (GLB) personally — do not import enemy meshes / build enemy BPs.
- Start menu (user decision 2026-09-29): only «Начать игру» (exploration → combat) and «Начать бой» (preparation);
  Godot's third mode «Начать исследование» is removed from UE (menu button, `EMissionStartMode::Exploration`, texts).
- «Начать бой» position: Godot hard-codes the yard behind the gate; UE uses the `CombatStart` tag (test map: (0, −2150)).
- Turn-based deviations: an operative that survives a mine ends its turn once (Godot schedules end_current_unit_turn
  twice — from the move and from the blast — which skips the next operative; treated as a bug). Squad starts facing the
  enemies' centroid (Godot hard-codes SOUTH = towards the enemies on its map). The attack line names the arc
  (фронт / фланг / тыл; Godot prints the enum number). Trapped-barricade retaliation: Godot _damage_barricade checks `is_mined`, which
  nothing ever sets — dead code, not ported. Enemy idle variations: with the AnimBP.
- Stage loadout (Godot _apply_stage_exploration_resources, `LoadoutRules`): when the cutscene ends, «Начать игру» keeps
  what the squad collected (turrets to the commander ≤ 2, barricades to the engineer ≤ 4, mines to the medic-sapper ≤ 5);
  «Начать бой» turns EXPLORE_AND_COLLECT into STARTING_UNIQUE (at least 1 / 2 / 2; level_01_outpost already says
  STARTING_UNIQUE); EDITOR_PRESET tiers from the level JSON. Godot quirk kept: the specialist gets the squad total
  while the others keep theirs (items double up) — kept as in Godot (user decision 2026-09-30). The --bot-loadout CLI override is not ported.
- `FString::ToLower` / `Contains(IgnoreCase)` do not fold Cyrillic: use `FText::ToLower` (see DialogueRules).
- The world is paused while the start menu is open (Godot keeps processing behind its menu) — cosmetic difference.

## 8. Next steps (in order)

1. ~~Loot crates~~ — done (see §10).
2. ~~Turrets~~, 3. ~~generator damage / repair~~ — done (see §10). Enemies attack turrets / barricades / the generator
   since the enemy AI pass (see §9 Enemy AI).
4. ~~Ctrl + click targeted shots~~ — done (see §10).
5. UI shell from REFERENCE_PLAYTHROUGH: ~~objective banner, mission failed + Ctrl + X, start menu~~ (done, §10);
   ~~dialogue window, bottom action bar, banners, cutscene card~~ (done); remaining UI: ~~inventory drawer~~ (done: `UInventoryDrawerWidget`, H / J / K / L, `UsePersonalItem`), ~~transfer~~ (done: `UTransferDialogWidget`, `USquadTransferSubsystem`, `TransferRules`),
   ~~guard~~ (done: T / «ОБОР», `USquadSubsystem::ToggleGuard`, `AOperativeCharacter::bGuarding`), ~~weapon selector~~ (done), ~~pause menu / save-load~~ (done: `UPauseMenuWidget`, `USaveLoadDialogWidget`, `USaveGameSubsystem`, F5), ~~radius rings~~ (done: `URadiusRingSubsystem` + `ARadiusRingActor`, M_CombatFeedback segments). §8.5 is complete; next: §9 gaps.
6. Turn-based combat manager on the Gorky grid (Godot `Scripts/tactics/turn_based_combat_manager.gd`).
   Done: `GorkyLineOfSight`, `TurnBasedRules`, `UTurnBasedCombatSubsystem` + overlay + controller input (see §4).
   Exposed zones done (`FExposedZones` pure rules + `UpdateExposedZones` at the end of the squad phase, overlay
   outlines `ATurnGridOverlayActor::SetExposedZones`, reinforcements via `UWaveSubsystem::SpawnEnemy`, armor 2, facing
   north, frozen outside their turns). Barrel / turret relocation done (`StartRelocate` / `RelocateObject` /
   `TryPushAdjacentBarrel`; click on an orthogonally adjacent barrel or a Chebyshev-adjacent turret picks it up, Shift +
   barrel shoots, Esc / RMB / click on its own cell cancels, panel «📦 Бочка»). Barricade relocation done
   (`GetBarricadeCellsAt` / `CanPlaceBarricadeAt` / `RelocateBarricade`; footprints on the grid now use Godot's five
   samples along the long axis, `RegisterBarricadeCells`; Q -45°, E / R / wheel ±45° while relocating; targets within
   2 cells of the operative). Deployables on the grid done (`CanPlaceDeployable` / `DeployObject` /
   `HandleDeployPlacement`: the action-bar / F placement ghost works in turn-based, a click sets the item up on the cell,
   walking up first if needed; Q / E rotate the ghost). Weapon switching done (arsenal below). Grenades done (`UGrenadeSubsystem` aim /
   throw, `AGrenadeActor` flight / fuse / blast, `GrenadeRules`; G, the selector's grenade line, LMB throws, RMB / Esc
   cancel; in turn-based combat the grenade is a grid weapon like in Godot, entering it refunds grenades in flight).
   Stasis look done (M_TacticalStasis from Scripts/Editor/create_stasis_material.py on every mesh of the enemies left
   outside the fight, restored at the end). Companion drone: Godot _execute_companion_drone_phase only emits a request
   that main.gd completes at once (no drone exists) — nothing to port. §8.6 is complete; next: §8.5 UI (inventory
   drawer, transfer, guard).
7. Phase 2 data importer (JSON / .tres → DataAssets) replacing hand-typed values (§9).
   Done: weapons — `Scripts/Editor/import_weapons.py` → `/Game/Data/Weapons/DA_Weapon_<id>` (all 9; defaults parsed
   from weapon_data.gd, enums mapped by name, `EStatusEffect::Shocked` appended); the game mode equips
   `StartingWeapon` (DA_Weapon_m16) with `StartingReserveAmmo` 60 (Godot _init_weapons) → HUD «[30/60]» like the video.
   Balance — `Scripts/Editor/import_balance.py` → `/Game/Data/Balance/DA_Balance` (balance.tres) and
   `DA_GameBalanceConfig` (game_balance_config.tres): `UGodotBalanceAsset::Numbers` holds every numeric / bool export
   by its Godot name (362; .gd defaults for fields a .tres omits). Wired so far: turn-based combat
   (`TurnBasedRules::BalanceFromGodot(DA_Balance)` + step durations, loaded at combat start via the game mode's
   `TurnBasedBalance`), operatives (`Characters/OperativeBalance::Apply` = Godot apply_balance_config: max health
   commander 140 / engineer 150 / medic 120, matches, walk / run / crouch speeds, acceleration, sprint / lift / carry /
   wound values, commander prep radius, cold rules — rates, stance multipliers, misfire / freeze / aim; applied on
   the deferred spawn), enemies (`AEnemyCharacter::ApplyBalance` = Godot enemy apply_balance_config: crit chance /
   multiplier per type — hound 0.15, spitter 0.25, brute 0.30, frostbitten 0.10 instead of a flat 0.20 — and
   hound / spitter / brute health, speed, damage, range, cooldown). Deployables need nothing: Godot reads the config
   only when the inspector value is 0, and the UE values equal the Godot export defaults (mine 120 / 3.5 m, turret
   120 HP 16 dmg 0.45 s 12 m, barricade 200 HP). Camera NOT wired on purpose (UE distances were tuned with the user;
   Godot distance semantics differ) — ask before changing.
   Levels — `Scripts/Editor/import_levels.py` → `/Game/Data/Levels/DA_Level_<file>` (level_01_outpost + stage_01..12;
   waves with `is_active: false` dropped like main.gd). The game mode's `LevelConfig` (DA_Level_level_01_outpost = Godot
   main.gd active_level_json_path) goes to `UWaveSubsystem::SetLevelConfig` and, through `LevelFlowRules::ApplyLevel`, to
   the flow config: preparation 60 s, rest 20 s, TotalWaves = 3 (Godot max_waves = active waves; without a level Godot
   uses preparation_phase_duration / max_campaign_waves from game_balance_config.tres). L_MovementTest has four
   `AEnemySpawnPoint`s beyond the gate (NORTH_GATE, WEST_FLANK, EAST_FLANK, FAR_PERIMETER; added by
   add_level_objects_to_movement_test.py, following the Floor transform). Lane matching = Godot _get_enemy_spawn_pos
   (substring either way, else any point). Spawning = Godot _spawn_custom_json_wave (see §7).
   Source trap: Godot has TWO GameBalanceConfig files with different values. The turn-based manager loads
   `resources/balance.tres` first (squad 8 AP, enemy 6 AP — the UE `FTurnBasedBalance` defaults), while camera, enemies,
   turrets, mines, barricades load `resources/game_balance_config.tres` (e.g. tactical_squad_max_ap = 3, enemy 4,
   turret_shot_delay 1.0). Import both and wire each consumer to the file its Godot counterpart loads. Weapons:
   `resources/weapons/*.tres` → `UWeaponDataAsset` (same field names). Levels: `data/configs/levels/*.json`.
8. Content: level, VFX, cutscene; character "twisted" look issue (§6).

## 9. Known gaps / tech debt

- Enemy spawn points follow Godot exactly: L_MovementTest points carry the Godot lane names («Северные ворота»,
  «Левый фланг (Прорыв)» HOUND only, «Правый фланг» SPITTER only, «Дальний периметр») while the level JSON uses
  NORTH_GATE / WEST_FLANK / EAST_FLANK — in Godot these never match, so laned entries spawn at a random static point
  and only "ANY" entries honour the type filter. Renaming `SpawnLane` on the points (or the JSON lanes) makes lanes
  work; left as in Godot. No Godot level has a dynamic breach point (`bIsDynamic`), so the breach code is dormant
  until a designer places one; `ActiveWaves` / `WarningLeadTime` are exported but unused (like Godot). The breach
  floats «ЧЁРТ ПОДЕРИ» over the leader like Godot.
- Susanin (`URecruitSubsystem`): spawned from the operative Blueprint with `ACodexTacticsGameMode::RecruitSusanin`
  (pink tint via BodyColor — the user's BP decides the real look; Godot swaps the Explorer_Coat material). The spot is a
  TargetPoint tagged `SusaninSpawn` on the west side of the yard (Godot uses a hard-coded (-8.2, 18.0) in its own
  layout). The narrative pause uses the minimum world time dilation, restored with `UGameFlowSubsystem::ApplyTimeDilation`.
  Rage uses the general keys (see Rage below); panic (susanin_* stress keys) is not ported.
  The Godot `dialogue_susanin_recruitment.tres` export is unused by Godot's code (it builds both dialogues inline) — same here.
- Level waves without a level config still use Gemini's built-in queued waves (Godot's fallback spawns balance-driven
  counts at once: min_hounds_per_wave, base_wave_enemy_count, …) — port when a level ships without JSON waves.
- Level spawn `custom_stats` damage / speed / attack_range / attack_cooldown are not imported (only health; no level uses them).
- Still hand-typed (Phase 2 continues): camera (user-tuned, see §8.7), enemy visuals / capsules (Gemini). Imported and wired:
  weapons, turn-based balance, operative health / speeds / matches / lift & sprint limits (per role), cold rules, enemy stats and crit chances.
- Floating combat texts (`UFloatingTextSubsystem`, drawn by the HUD over the name plates): wired for operative hits /
  dodge / crit, misses, misfires, frozen / thawed weapon, frostbite, enemy numbers (type prefix, armor «🛡️», immunity,
  burn / bleed ticks), turn-based turret miss, mine armed / spotted / set-up / mishap, barricade cover, repairs, solo
  mode, guard, heal, Susanin, breach, rage, squad stance, sector, cover. Not wired (feature not ported): panic /
  allegiance texts,
  enemy fear of fire, cutter texts, solo frost-crawl. Godot moved the weapon-frozen / frostbite lines
  from the feed to floating texts; UE now does the same (they no longer appear in the message feed).
- **Parity fix:** real-time squad fire follows Godot player.gd (`SquadFireRules`, `AOperativeCharacter::FindShootTarget`):
  range x posture (1 / 1.15 / 1.35) x elevation (1.25), dead zone under platforms, line of fire from the muzzle to the
  enemy (+0.8 m) — walls block, barricades block prone shooters and give crouched ones cover 0.8, another enemy in the
  way becomes the target; target memory with the stance switch delay / ratio from game_balance_config; damage = weapon
  x stance (1 / 1.25 / 1.6) x cover x crit (luck %, x2) x elevation (1.15) x the weapon's distance factor; cryo / fire
  weapons chill / warm the shooter. Before, the nearest enemy in range was shot for flat weapon damage. Not ported:
  panic refusal, reload_after_shots (user decision: 30 rounds).
- Autonomous grenades (`AIGrenadeRules`, `AOperativeCharacter::TryAIGrenadeThrow`; Godot _evaluate_ai_grenade_opportunity):
  in the real-time fight (not raging / reloading, 3 s cooldown) an operative throws at the biggest cluster of 3+ enemies
  in its stance range, 5 m+ away, with no squad member / turret within 5 m of the aim point; the empty-weapon switch
  (Godot _auto_switch_on_empty: M16 with rounds -> pistol -> a grenade at a cluster -> knife) replaces «out of ammo».
- Rage (`URageComponent`, `RageRules`; Godot rage_component.gd): crits per attacker (real-time 25 s memory), chance decays
  with the component's lifetime like Godot's combat_time_elapsed, per-role keys (<role>_rage_*) for the squad, general
  keys for Susanin (Godot re-applies only the general ones to a recruit spawned later). Raging: chaotic target (random
  enemy within 25 m every 0.55 s), fire rate / damage multipliers, +30 luck, no ammo spent, reload paused, OrderMoveTo /
  SetManualPriorityTarget refused. HUD badge «ЯРОСТЬ!»; the fiery aura is an orange 30 cm band at 0.9 m radius under the feet (`URageComponent::UpdateAura`, M_CombatFeedback segments).
  movement_speed_multiplier is exported but unused in Godot — not applied.
- **Parity fix:** enemy attacks on operatives now use Godot player.gd take_damage (dodge luck × 0.4 %, stance defense,
  fortitude cut clamp(f × 1.5 %, 0, 50 %)); before, the enemy armor formula was used (no dodge / fortitude). Grenades /
  traps on the squad use the bypass path (max(1, amount)). A hit on a carrier drops every carried object (`URelocationSubsystem::DropAllForCombat`, alarm line).
- **Parity fix:** turn-based squad shots and turret shots on enemies go through the enemy's own armor / affinity
  (Godot occ.take_damage(final_dmg, KINETIC, 0.0)) — a brute (75 % armor) takes a quarter of the grid damage.
  Turn-based barrel / mine damage still applies directly (Godot mixes the grid hp with take_damage there; review with
  the turn-based parity pass).
- **Parity fixes (squad control):** operatives are wounded below `wounded_health_threshold_percent` of max health
  (Godot is_wounded; before, `bWounded` was never set, so the wounded speed / no-sprint rules never applied — the speed
  now follows every health change; `UHealthComponent::ApplyDirectHealthLoss` broadcasts OnHealthChanged); a ground click
  during an active wave outside the tactical pause is refused («Перемещение во время боя возможно только в режиме
  тактической паузы»); double-click sprint lines (frozen / wounded / «Бегом к позиции!»). New: Alt + Z / C / V squad
  stance, Shift + click sector facing (no facing indicator arrow; UE has no persistent fixed-facing, the operative just
  turns), auto-cover crouch on arrival next to a barricade in combat with the radio callout. Not ported: box selection
  of several operatives (group moves), the action bar «🛡️» cover / holding tag and its tooltips.
- Overhead labels (`FOverheadLabel`, `AEnemyCharacter` / `AInteractableActor::GetOverheadLabel`, HUD `DrawWorldLabels`;
  Godot Label3D): enemies (armor-tier square instead of 🟢🟡🔴, name, statuses as words — ГОРИТ / ЛЁД / ОГЛУШЁН / БРОНЯ-,
  the font has no emoji — HP; hidden for stasis enemies outside a turn-based fight), barricade, turret, generator with
  the Godot texts / colours.
- **Enemy AI (parity pass; Gemini's AEnemyCharacter::Tick replaced):** `EnemyAIRules` + `AEnemyCharacter::Tick` / `FindTarget`
  / `TickSpitter` (Godot enemy_base.gd _physics_process / _process_enemy_behavior / _find_closest_squad_member /
  _find_blocking_barricade / _find_nearest_active_fire_source, enemy_frost_spitter.gd, enemy_frost_brute.gd):
  per-type affinities and base armor (brute kinetic 0.25!, spitter 0.6, hound melee 1.3 …); stagger stops, frost halves
  the speed; hounds / cutters / frostbitten flee burning barrels and active heat sources within 6 m (x1.2 speed,
  «СТРАХ ОГНЯ»); target weights — small enemies: generator 0.4 x, turret 0.5 x, squad 1 x (so hounds rush the generator
  from the start of a wave, like Godot: the generator is always in the "generators" group); large: squad 0.7 x, turrets
  0.85 x (0.6 x after a turret hit) and any turret within 10 m; a barricade / turret within 2.2 m towards the target is
  smashed (brutes double on barricades; an enemy blow sets off a trap); melee needs range and <= 1.2 m height difference;
  spitters pick targets by 100 - distance (+50 elevated), approach / back off around 12 m, shoot only with a line of
  fire (prone behind a barricade hidden, crouched cover 0.65), red tracer. Movement stays on the navmesh (UE AI
  MoveTo) — Godot's ramp routing, wall-slide / stuck avoidance and flocking separation are covered by navigation /
  crowd avoidance. Frostbitten blows shove the operative (+2.5 m/s); hits flash (operative: red light, enemy: red
  overlay; burning ticks do not flash, Godot does). Not ported: the cryo drone (UE maps it to spitter stats), attack
  animation locks (is_attacking; with the AnimBP).
- Cutter (Godot enemy_cutter.gd): own stats (75 HP, 6.2 m/s, 18 damage, 2 m, 1.1 s, crit 0.25 x1.75), the pounce at
  3.5-9 m (windup 0.4 / 1.85 s, ballistic LaunchCharacter flight 1.3 / 1.85 s capped at 9 m, impact damage 28 within
  2.2 m on the squad and barricades, «💥 НАЛЁТ N», recovery 0.75 / 1.85 s, 6 s cooldown) with the numbers imported from
  resources/enemies/anims/cutter.tres (`Scripts/Editor/import_enemy_anim_configs.py` -> /Game/Data/Enemies/DA_EnemyAnim_*);
  Blueprint hooks On Jump Attack Started / Impact for the clips. Godot only jumps when the model has the jump clip —
  UE jumps whenever the config enables it. Shot down mid-leap it keeps falling on the world and crashes («СБИТ В
  ВОЗДУХЕ», «КРАХ»). The airborne kill gives the cutter EXP (16); kill statistics are not ported.
- Event bus (`UCodexEventBus`, Godot event_bus.gd): a hook point for Blueprints / audio / VFX; systems still call each
  other directly (Godot's bus mostly fed the UI). Only the signals Godot actually emits are there; declared-but-never-
  emitted ones, panic / allegiance ones (not ported) and camera_shake_requested (a fallback; the camera is called
  directly) are left out. item_used fires from the inventory drawer only (as in Godot; H / J / K / L do not).
- Turn-based camera choreography (user decision 2026-09-30, Godot main.gd + camera.gd): entering zooms to 14 m; each
  operative's turn / selection glides to it (0.75 s, 16 m); the active operative's own number / a click on it glides
  there (0.85 s) with «🎥 Фокус камеры на бойце»; an enemy's turn glides to it (0.4 s, 11.5 m), its walk (0.35 s,
  11.5 m); after the last enemy the camera glides to the squad centre (0.85 s, 17 m) and waits that long before the
  squad turn. A squad shot (click on an enemy / Shift + barrel / barricade) plays `AttackCellCinematic`: frame both
  (mid-point, clamp(span x 1.35 + 4 m, 10, 19 m), 0.4 s), shot + shake, the round lands 0.35 s later (`AttackCell`),
  0.65 s to read it, glide back to the shooter at `Config.DistanceCombat` (1.1 s). A turret volley: frame both 0.4 s,
  head turn (snapped, Godot tweens 0.25 s), volley + shake, 0.65 s, glide to the squad centre 0.85 s. Meanwhile
  `IsBusy()` holds every player order. Godot distances are view distances in m; UE uses the same distance in cm. In
  headless runs (no rendering, like Godot's can_tween) the delays are skipped — smokes set
  `bForceCinematicsForTesting` to check them. A turn-based non-leader follow target (enemy) is framed exactly, the
  leader with the deadzone.
- Camera shake (Godot camera.gd): only in turn-based combat, trauma from the squad attack (pistol 0.20 / rifle 0.35)
  and each turret volley (0.28), decay 4 / s, offset = amplitude 0.18 m x trauma² x Perlin noise along the view's right
  / up axes (Godot FastNoiseLite simplex; UE FMath::PerlinNoise1D), values from DA_GameBalanceConfig camera_shake_*.
  The offset is taken off before the follow smoothing so it never drifts the camera.
- Wave victory (`UWaveVictorySubsystem`, `UVictoryPanelWidget`; Godot main.gd): the panel shows while the flow is in
  WaveCleared (time stopped), with Godot's title / subtitle, the kill statistics card and «Запустить следующую волну
  (N/M)» / «Завершить бой и продолжить исследование» (-> `AdvanceAfterWave`) and «Перезапустить уровень (X)». The HUD
  font has no emoji, so the statistics' 🐺 / 🏹 / ❄️ read «гончие / плевуны / громилы». Kills count per Godot
  register_enemy_kill (last attacker; unknown sources go to the leader; the recruit has no entry; only hound / spitter /
  brute per type). Post-combat: enemies removed, the squad teleported to where it stood when the cutscene ended
  (Godot initial_prep_station; before that its position at the post-combat start, Godot: its spawn), orders dropped,
  the commander leads, every bDeployable turret / barricade / mine (abandoned level ones too, as in Godot) goes into
  the supply by Godot's member order and caps with the engineer's feed line. Not ported: deployables_combat_stats
  damage and _record_telemetry (telemetry only); a carried object is not dropped explicitly.
- Progression (Godot player.gd, profile_dialog.gd): kill EXP to every squad member (exp_reward_<type> from
  DA_GameBalanceConfig, cutter 16); level x 250 per level, cap 10; +3 points, full heal, floating text and radio line
  per level; + / - within [start value, cap] (HP +5 up to 200, luck 60, accuracy 100, fortitude 50). Profile on P, on
  the leader's own number key, and on wave clear for the first member with points; Esc closes it. A cleared wave gives
  every member exp_reward_wave_complete (25; `UWaveSubsystem`, Godot _on_wave_cleared); exp_reward_loot_crate is
  config-only in Godot and unused in UE too. Godot names the recruit's profile
  «Полковник Васин» (fallback); UE shows his own name, role «Рекрут / Проводник». The portrait emoji is the role
  initial (no emoji in the font). Level, EXP and points are saved (Godot save_manager.gd).
- Panic (Godot panic_component.gd) is inert in Godot (enable_realtime_panic = false; turn-based never uses it) and the
  allegiance component is only used by tests — neither is ported.
- Narrative elements (`ANarrativeElementActor`; Godot narrative_element.gd) and dialogue triggers (`ADialogueTriggerVolume`;
  dialogue_trigger.gd): L_MovementTest gets the Godot note (by the generator), signpost (central road), poster (gate wall)
  and the wave-rest trigger (right side, midway) at semantic spots — the Godot scene coordinates don't map onto the UE
  layout. The type emoji marker is a small square (no emoji in the HUD font); in-world text wraps at 48 characters and
  is not occluded by walls (Godot Label3D no_depth_test = false). has_been_read is not saved (Godot neither).
- Space hold (Godot main.gd + tactical_hold_sphere.gd + gorky17_combat_hud charge bar): `AHoldSphereActor` (additive dome
  + growing ring + 15 m boundary ring, eased p x (2 - p)), the squad holds fire while Space is held
  (`AOperativeCharacter::bTacticalCeaseFire`), HUD `DrawSpaceCharge` («ВХОД В ПОШАГОВЫЙ БОЙ (GORKY 17): x.xc / y.yc» /
  «ВОЗВРАТ В ТАКТИЧЕСКУЮ ПАУЗУ»). The dome centre is the leader's feet (Godot ray-casts the floor below the leader).
- Barricade contact damage (spikes / fire / cryo / energy) not ported (Godot default is NONE).
- Pushing / defusal / set-up animations: none (operatives only slow down / crouch).
- Hidden mines are revealed by a distance scan in the mine's Tick (Godot scans from each operative) — same result.
- Weapon switching does not change the weapon mesh (the operative Blueprint owns WeaponMesh) and has no holster
  animation. X (Godot switch_weapon cycle) is not bound.
- Grenades: placeholder sphere mesh, no explosion VFX / sound (Godot has none either) — `AGrenadeActor` Blueprint events
  On Landed / On Detonated and the operative's On Grenade Throw + GrenadeThrowDuration are the hooks.
- Turn-based set-up: no assembly animation / grow-in tween (Godot play_action_animation "working_device", scale 0.05 -> 1);
  a squad mine placed in turn-based skips the real-time mishap roll like Godot.
- Turn-based relocation: no hologram ghost of the object under the cursor (Godot _create_relocate_ghost_preview);
  target cells use the reachable layer (for barricades: cells valid at the current angle). The barricade jumps to
  its new place (Godot tweens 0.25 s).
- Exposed-zone outlines: no pulse (Godot alpha 0.65 ± 0.35); the additive M_CombatFeedback glow reads pink-white on the
  light floor instead of red — a material for the user to tune (layer colours / intensity in TurnGridOverlayActor.cpp).
- Relocation ghost is opaque (swap `GhostBaseMaterial` for a translucent hologram).
- Click rules (Godot main.gd plain click, `ACodexTacticsPlayerController::HandleWorldHit`): in a wave a live enemy becomes
  the priority target (direct assignment, no Ctrl); set-up items / movable objects are refused outside the pause; in
  the pause / preparation a set-up item opens its menu at once, in the pause a movable object is picked up for
  relocation at once. A leader alone in a camera zone may move / pick up objects mid-wave (Godot is_zone_solo; `bInCameraZone`).
- Targeted shots: the priority target now gets the barricade cover / blocking of `EvaluateShotLine` and is refused
  while raging (panic refusal: panic not ported). The barrel
  line «💥 Прицельный выстрел…» is posted only when the shot actually fires (Godot posts it even when frozen).
- Save / load differences: a load resumes an active wave from its preparation (Godot only flips flags; neither saves
  enemies); «В главное меню» restarts the level with the start menu (Godot shows the menu over the running scene);
  `AAutosaveTrigger` is not ported — Godot's only trigger (gate_kpp.tscn AutosaveTrigger_Gate) has no collision shape
  and never fires; the start menu has no «Загрузить» (Godot neither). The save file stores the weapon id (Godot:
  current_weapon_idx) and grenades / ExtraAmmo in addition.
- **Deliberate deviation:** the «Патроны 9мм (12 шт.)» hand-over works (Godot shows the button but _transfer_item_to_target
  has no PISTOL_AMMO branch). Ammo handed to a mate without that weapon is kept in ExtraAmmo (Godot drops it).
- **Deliberate deviation (user decision 2026-09-28):** a shot at an untrapped supply crate only posts «💥 Пуля пробила
  ящик снабжения.» — Godot also detonates it (bug: `detonate_trap` always exists on loot_crate.gd).
- Tracer muzzle = feet + stance height (1.4 / 0.85 / 0.25 m), not a weapon socket; light intensity mapping
  (`FeedbackLightPerEnergy` 1500 per Godot light_energy) is a first guess for the user to tune.

## 10. Change log (newest first)

| Commit | What |
|---|---|
| (this) | Stage loadout (Godot main.gd _apply_stage_exploration_resources): `LoadoutRules` + LoadoutRulesTest, level JSON squad_loadout imported into `FLevelCombatConfig::SquadLoadout` (import_levels.py, DA_Level_* re-imported), `UMissionSubsystem::ApplyStageLoadout` at the end of the cutscene; VictorySmoke checks it |
| `ef5f5bd` | Event bus (Godot Scripts/events/event_bus.gd): `UCodexEventBus` game-instance subsystem with the signals the game emits (squad member selected, stats updated, item used, mine spotted, soldier downed, feed line, dialogue finished, rage started / ended, generator state, game saved / loaded), broadcast from the matching UE systems; EventBusSmoke |
| `04373d1` | Turn-based camera choreography (user decision 2026-09-30; Godot camera.gd smooth_focus_on_target / _position, dramatic_action_cam_focus; main.gd _enter_turn_based_combat, _on_gorky17_turn_changed, _on_gorky17_enemy_movement_started / _finished, _perform_dramatic_tactical_attack, _on_gorky17_turret_shot_requested): `ATacticalCameraPawn` SmoothFocusOnTarget / SmoothFocusOnPosition / DramaticActionFocus / EnterTurnBasedZoom, `UTurnBasedCombatSubsystem::AttackCellCinematic` + cinematic turret volley, `IsBusy`; TurnBasedCameraSmoke, `HudShot turnbased duel` |
| `4a80593` | Camera-zone solo (Godot is_in_camera_zone / is_zone_solo in main.gd): `AOperativeCharacter::bInCameraZone` (set by `ACameraZoneVolume`), `RelocationRules::CanRelocateNow(..., bLeaderZoneSolo)`, `URelocationSubsystem::CanRelocateNow()` used by the controller, deployables and relocation; RelocationRulesTest, CameraZoneSmoke |
| `038d016` | Frostbitten shove (Godot enemy_frostbitten.gd _attack_target: +2.5 m/s away, `LaunchCharacter`) and damage flashes (Godot player.gd _spawn_damage_flash red light 0.12 s, enemy_base.gd _flash_hit red glow 0.08 s): `UCombatFeedbackSubsystem::SpawnDamageFlash` / `FlashEnemyHit`; EnemyAISmoke checks both |
| `fc3b672` | Rage aura (Godot rage_component.gd RageAura torus 0.75..1.05 m): an `ARadiusRingActor` (new `ShowRing` width parameter) follows the raging operative's feet; RageSmoke checks it, `HudShot rage` shows it |
| `e7c58cd` | A hit on an operative carrying / pushing an object drops everything the squad carries (Godot player.gd take_damage -> main.gd _cancel_or_finalize_active_relocates_for_combat): `URelocationSubsystem::DropAllForCombat`; RelocationSmoke checks it |
| `7607c2d` | Turn-based camera shake (Godot camera.gd add_trauma / trigger_weapon_shake / _process_shake; main.gd squad attack and turret volley): `CameraShakeRules` + CameraShakeRulesTest, `ATacticalCameraPawn::TriggerWeaponShake`, `UTurnBasedCombatSubsystem::ShakeCamera`; TurnBasedSmoke checks the trauma. `1615b89`: unity-build name fix in the victory panel |
| `10f1bb9` | Wave victory (Godot main.gd register_enemy_kill, _on_wave_cleared, _on_next_wave_pressed, _start_post_combat_sequence, _auto_recover_all_deployables; movements_demo.tscn VictoryPanel): `KillStatsRules` + KillStatsRulesTest, `UWaveVictorySubsystem`, `UVictoryPanelWidget`, `DeployableRules::PickRecoveryRecipient`; the flow no longer stops in WaveCleared; VictorySmoke, `HudShot victory` |
| `2996631` | Progression (Godot player.gd add_exp / _on_level_up / increase_stat / decrease_stat, enemy kill and wave-clear EXP, profile_dialog.gd + main.gd profile handling): `ProgressionRules` + ProgressionRulesTest, `AOperativeCharacter` level / EXP / stat points, `AEnemyCharacter::KillExpReward`, `UProfileDialogWidget` (P, number keys, wave-clear auto open, Esc), save fields; ProgressionSmoke, `HudShot profile` |
| `11990c4` | Narrative elements and dialogue triggers (Godot narrative_element.gd, dialogue_trigger.gd; placed on L_MovementTest by the level script), the Space-hold dome, cease fire and charge bar (Godot tactical_hold_sphere.gd, gorky17_combat_hud.gd); NarrativeSmoke, HoldSphereSmoke, `HudShot hold` |
| `3985f6c` | Plain-click rules in a fight (Godot main.gd click branches 0 / 0.5): enemy priority target, no relocation outside the pause, immediate menu / relocation in the pause; `HandleWorldHit`, `UInteractionSubsystem::OpenMenuNow`, `AssignPriorityTarget`; ClickRulesSmoke |
| `d081f34` | Cutter (Godot enemy_cutter.gd): own stats, the pounce with impact damage and cooldown, airborne death; enemy animation configs imported (`import_enemy_anim_configs.py`, DA_EnemyAnim_*); CutterSmoke |
| `cd3d61c` | Enemy AI parity (Godot enemy_base.gd, enemy_frost_spitter.gd, enemy_frost_brute.gd): affinities / armor per type, stagger / frost, fire fear, target weights (generator / turrets / squad), obstacle smashing, spitter ranged behaviour; `EnemyAIRules` + EnemyAIRulesTest, EnemyAISmoke |
| `6569254` | Overhead labels of enemies, barricades, turrets and the generator (Godot overhead Label3D: enemy_base.gd, barricade.gd, turret.gd, interactable.gd); `HudShot labels` |
| `b049423` | Autonomous grenades and the empty-weapon switch (Godot player.gd _evaluate_ai_grenade_opportunity / execute_ai_grenade_throw / _auto_switch_on_empty): `AIGrenadeRules` + AIGrenadeRulesTest, `TryAIGrenadeThrow`, `AutoSwitchOnEmpty`; AIGrenadeSmoke |
| `96d90e1` | Squad control parity (Godot player.gd is_wounded / is_behind_barricade / _on_movement_destination_reached / set_facing_point, main.gd _set_entire_squad_stance and the ground-click rules): health-driven wounded state, Alt squad stance, Shift facing, auto-cover, real-time move refusal, sprint lines, slot 4 «ИВАН»; SquadControlSmoke |
| `0fe6e46` | Rage (`URageComponent`, `RageRules` + RageRulesTest; Godot rage_component.gd and its player.gd hooks): two crits from one enemy, chaotic fire, refused orders, HUD badge; `AOperativeCharacter::TakeHit` takes the attacker; `EOperativeOrderResult::Refused`; RageSmoke, `HudShot rage` |
| `9a6bd37` | Real-time squad fire parity (`SquadFireRules` + SquadFireRulesTest, `FindShootTarget`, damage multipliers; SquadFireSmoke). Floating combat texts (`UFloatingTextSubsystem`, HUD `DrawFloatingTexts`; Godot _spawn_floating_combat_text / _spawn_heal_feedback) wired across combat, cold, mines, repairs, squad modes; operative hit formula `AOperativeCharacter::TakeHit` (Godot player.gd take_damage) for enemy attacks, grenades, traps and turn-based bites; turn-based / turret grid damage through the enemy's armor; headless checks (-ExecCmds) run without random wave events; FloatingTextSmoke, `HudShot floating`. Random wave events. Susanin rescue (Godot main.gd _check_susanin_rescue_event / _trigger_susanin_rescue_event, click branch "is_unrecruited", recruit_susanin.gd, save_manager.gd "susanin"): `URecruitSubsystem`, `EOperativeRole::Recruit`, `AOperativeCharacter::bRecruited`, game mode `SpawnOperative` / `RecruitSusanin`, key 4, HUD prompt, SusaninSpawn point; dialogue buttons widen for long finish labels; SusaninSmoke, `HudShot susanin`. Spawn point type filter and dynamic flank breach (Godot enemy_spawn_point.gd, main.gd _get_enemy_spawn_pos, _check_dynamic_flank_spawners, _pending_random_events): `AEnemySpawnPoint::AllowedEnemyType` / `bIsDynamic` + breach fields, random event waves, breach pack + camera focus, deferral past turn-based; L_MovementTest points get the Godot lane names / filters; `ATacticalCameraPawn::GetFollowTarget`; FlankBreachSmoke |
| `e66e2fb` | Radius rings (Godot main.gd radius_ring, _update_relocate_radius_ring, _set_ghost_material_valid): 12 m green order ring in the tactical pause, worker radius while placing in the pause (cyan relocation / green set-up / red outside); `URelocationSubsystem::GetPlacingWorker` / `IsGhostValid`, public `GetRadius` / `GetOrigin`; RadiusRingSmoke, `HudShot ring` |
| `d220bc2` | Save / load (Godot save_manager.gd: JSON slots in Saved/SaveGames with the Godot keys; squad, game state, quest chain, crates), F5 quicksave, Esc pause menu (Godot pause_menu_dialog.gd) and the save / load dialog (save_load_dialog.gd: suggested name, cards, overwrite confirmation, load, delete); SaveGameRulesTest, SaveLoadSmoke, PauseMenuSmoke, `HudShot pause / saves` |
| `2528c03` | Item hand-over («ПЕРЕД»; Godot transfer_dialog.gd + main.gd transfer mode): dialog, purple ring cursor, click on a mate / within 2.2 m, engineering items up to the mate's max, provisions, ammo packs; TransferRulesTest, TransferSmoke, `HudShot transfer` |
| `c44c273` | Personal inventory drawer («ИНВ»; Godot inventory_drawer.gd) with provisions (H / J / K / L, player.gd heal_with_item) and set-up from the drawer (_start_placement_for_type with hand-over); PersonalItemTest, InventorySmoke, `HudShot inventory` |
| `72d23ef` | Guard mode (Godot toggle_soldier_guard): T / action bar «ОБОР» / «ЗАФИК», guards leave the formation and hold their spot; GuardSmoke |
| `f23c23f` | Turn-based stasis look for enemies outside the fight (Godot _apply_stasis_visuals_to_enemy, tactical_stasis_enemy.gdshader); M_TacticalStasis script; TurnBasedSmoke checks it |
| `4f4fef7` | Hand grenades (Godot grenade.gd + main.gd aim / throw / refund): aim rings and arc, range by stance, release at 70 % of the throw animation, arc flight, 1.2 s fuse, blast with falloff (operatives 65 %), barrels / traps go off; G key; GrenadeRulesTest, GrenadeSmoke, `HudShot grenade` |
| `9bec55a` | Arsenal (Godot _init_weapons / switch_to_weapon_by_id / ammo_inventory): M16, pistol, grenade, knife per operative, ammo kept per weapon, loot to the right weapon, knife without ammo; weapon selector panel on the action bar; turn-based weapon switch; WeaponSelectorSmoke, `HudShot weapons` |
| `3cd1474` | Turn-based deployables on the grid (Godot can_place_tactical_deployable, deploy_tactical_object, main.gd _handle_tactical_deployable_placement); TurnBasedDeploySmoke |
| `b41448f` | Turn-based barricade relocation with 45° rotation (Godot relocate_barricade, can_place_barricade_at, get_barricade_cells_at, _register_barricade_cells); TurnBasedBarricadeSmoke |
| `afbdedd` | Turn-based barrel / turret relocation (Godot relocate_object, _start_tactical_relocate, _try_push_adjacent_barrel): click rules with Shift, target cells, cancel, panel «Бочка»; TurnBasedPushSmoke |
| `bb6bec0` | Turn-based exposed zones: quadrant counters, warnings, overlay outlines, one breach of 1-2 non-elite reinforcements per fight (Godot tactical_exposed_zones_manager.gd); ExposedZonesSmoke |
| `dbf4d29` | Godot levels imported (DA_Level_*); level_01_outpost drives waves (whole wave at once, modifiers, custom health, cold drain), preparation 60 s / rest 20 s and 3 waves; enemy spawn points beyond the gate; the wave clears only outside turn-based combat (Godot; fixes a crash when the last enemy of a wave dies on the grid); LevelWaveSmoke |
| `93d94ad` | Smokes layout-relative (`SmokeUtils::LevelPoint`), L_MovementTest navmesh actor removed (rotated navmesh stayed empty) |
| `f91f800` | Level importer script (Godot level JSON -> ULevelConfigAsset), not run / wired yet |
| `2ca14b0` | Enemy crit chances and hound / spitter / brute stats from the imported Godot config |
| `91ddb9d` | Cold rules (rates, stance multipliers, misfire / freeze / aim) read from the imported Godot config |
| `b41a184` | Operatives take health / speeds / matches from the imported Godot config (Godot apply_balance_config) |
| `2c54c20` | Both Godot balance files imported into data assets; turn-based combat reads DA_Balance |
| `71f7cb0` | Godot weapons imported into data assets, operatives start with the imported M16 (30/60) |
| `06a28ca` | Turn-based action panel (phase, unit, AP / HP, buttons) |
| `8baa4d5` | Gorky 17 turn-based combat subsystem, grid overlay, controller input, TurnBasedSmoke |
| `87b4ae7` | Gorky line of sight, turn-based pure rules (hit chance, attack cells, balance struct) |
| `444692c` | Pause / preparation / wave banners, cutscene card with skip, squad reset after the cutscene |
| `5c317b5` | Bottom tactical bar (weapon, relocation pick mode, stance cycle, squad slots with HP / cold bars) |
| `2b17909` | Dialogue assets imported from Godot, bottom dialogue window, feed dialogues for prep / wave rest / victory, input blocking |
| `d854fde` | Start menu (3 modes), session subsystem, Ctrl + X repeats the mode, CombatStart point, arrows kept as «->» |
| `70ba255` | Objective banner, mission failed screen + restart, Ctrl + X, Godot sRGB colours in UMG, `verify_all` catches test crashes |
| `f987975` | Tracers + muzzle flash (operatives, turret), plan markers, Ctrl + click target flash, untrapped crate no longer explodes when shot, weapon `TracerColor`, `HudShot shoot` |
| `fa4e66b` | Ctrl + click targeted shots (barrel, mine chance, crate, trapped object, priority enemy, pause planning), operative `Accuracy` |
| `d3c7480` | Turrets (fire, power, repair, pick-up, set-up), generator breakdown / repair, unity-build name fixes |
| `d72d131` | Supply crates + loot dialog, provisions / extra ammo / bonus items on operatives, 2 crates on the test map, smoke retries for crouched defusal |
| `d922e49` | Handoff documentation, `verify_all.ps1`, GEMINI.md |
| `11d7655` | Barricades, proximity mines, grenade traps on any object, supply + F set-up, role / luck, SmokeUtils |
| `ab0c046` | Object relocation (ghost placement, push task, pause planning, combat drop) |
| `c098323` | Action menu (UMG), quest objects via menu, fuel barrels with matches |
| `472ee5b` | User map edits |
| `bcf149d` | Explorer model, M16, 60 animations, native locomotion AnimInstance |
| `930992d` | Camera drag in pixels (was 14× too slow) |
| `cd324d4` | Blueprint operatives, visible stances, canvas HUD feed |
| `92b404f` | Real-time cold system |
| `70f0a2a` | Selection click, camera yaw, stances, solo mode, role colours |
| `a122289`, `274818b`, `b75ced1`, `5dfdbc3` | (Gemini) Gorky grid; squad shooting; enemies / waves; combat data + health |
| `6a2d5aa` … `2b3b110` | Space input & pause planning, quests, camera, reference docs, movement, game flow, skeleton |
