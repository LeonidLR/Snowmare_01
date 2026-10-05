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

- **Holding a montage on its last frame:** clear `bEnableAutoBlendOut` on the running instance (`GetActiveInstanceForMontage(Montage)->bEnableAutoBlendOut = false`); the instance copies the montage's flag when it starts, so changing the montage after `PlaySlotAnimationAsDynamicMontage` has no effect.
- **Blend spaces made by script** (`UOperativeAnimGraphLibrary::FillDirectionalBlendSpace`) need `ValidateSampleData` + `ResampleData`; `PostEditChange()` alone leaves them empty and the player outputs the reference pose (T-pose height) with no error.

- **Two agents, one checkout:** set `$env:CODEX_AGENT` (claude / gemini); every build / test / smoke / verify_all takes `Saved/agent.lock` and waits for the other agent (`Scripts/agent_lock.ps1 -Status` shows the holder). Roles and file ownership: `docs/port/TANDEM.md`.
## 3. Build, test, verify

**Unreal MCP (AI access to the open editor):** plugins ModelContextProtocol + toolsets are enabled and the server
auto-starts with the CodexTactics editor at `http://127.0.0.1:8000/mcp` (`.mcp.json` server `unreal`). A Claude
session connects at its start, so open the editor first. Tools: `list_toolsets`, `describe_toolset`, `call_tool`;
project toolset `CodexTacticsEditor.BlueprintGraphToolset` (read any Blueprint / AnimBP graph as text, paste nodes,
compile; pasted changes are never saved automatically). An open CodexTactics editor locks the DLLs: close it before
`build.ps1` / `verify_all.ps1` (another project's editor may stay open). Note: ABP_Operative's AnimGraph holds only the
Output Pose — the pose is blended natively in `UOperativeAnimInstance`.

```
powershell -ExecutionPolicy Bypass -File Scripts/verify_all.ps1              # build + all tests + all smokes (3 at a time, -Parallel N; save-slot checks and parallel failures re-run alone; logs Saved/Logs/Smoke-<Name>.log)
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 [-Filter CodexTactics.Cold]
powershell -ExecutionPolicy Bypass -File Scripts/smoke.ps1 -Command CodexTactics.DeployableSmoke
```

State at last update: **145 automation tests, 56 smokes, all PASS** (`verify_all.ps1` → ALL GREEN; it also fails on an engine crash during the tests now).

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
| `MarksmanSmoke` | Marksman 25 m out: crouched / prone firing stance with the lowered capsule, 2 s aim, shot; a hit from afar -> ambushed prone, then a flank run; teleported 8 m from the squad -> retreats at a sprint |
| `EnemyHitLayerSmoke` | Frostbitten / hound hit while running: the hit plays on the UpperBody slot, not the full-body slot, and they keep running (cutter / brute without hit clips play nothing) |
| `HoldSphereSmoke` | Space hold: the dome grows (eased) and the squad holds fire; release hides it and lifts the cease fire |
| `TurnBasedCameraSmoke` | Cinematics forced on: 16 m on the operative's turn, the camera on the enemy during its turn and back on the active operative, the dramatic shot (framing 10..19 m, the round lands at 0.75 s, orders wait), the glide back at the combat distance, the pre-combat zoom restored |
| `EventBusSmoke` | A listener hears every `UCodexEventBus` event from its real emitter (leader change, profile point, drawer medkit, feed line, dialogue close, rage, generator down / repaired, save / load, a fallen operative) |
| `SilhouetteSmoke` | A block between the camera and the leader turns his cyan see-through overlay on; removing it turns it off |
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

- Operatives (user art, 2026-09-30): BP_Operative uses the user's `/Game/Post_Apo_Survivor` mesh (UE4 mannequin rig) and
  the `/Game/RifleAnims` pack. `Scripts/Editor/setup_operative_rifle_animation.py` makes the two UE4_Mannequin_Skeleton
  assets compatible, builds ABP_Operative's graph with `UOperativeAnimGraphLibrary` (editor module: stand / crouch / aim
  blend spaces by Direction + speed axis, prone = crouch set for now, cached locomotion, upper-body Layered blend from
  spine_01 with the pack's "Fire" montage slot; fire montages and the reload clip stretched to the reload time are
  played by `UOperativeAnimInstance`) and sets `AOperativeCharacter::SquadOutfits` (hoodie + pants: base / _Inst_2nd /
  _Inst_3rd for commander / engineer / medic-sapper). Re-running keeps hand edits: the graph is built only while empty (`CountAnimGraphNodes`; CODEX_REBUILD_ANIM_GRAPHS=1 forces it), clips / outfits only while unset.
  The old Explorer clips were deleted by the user; the M16 offset on hand_r is still the Explorer one.
- Enemies (user art, 2026-09-30): `Scripts/Editor/setup_enemy_animation.py` builds `/Game/Characters/Enemies/<Type>/`
  ABP_Enemy_* (graph: idle / walk / run sequence players + full-body slot; clips on the class defaults) and BP_Enemy_*
  for HOUND = Combat_Dog, BRUTE = Mutant_monster_1, FROSTBITTEN = mutant_monster_2, CUTTER = Biochemical_Monster_2 (ue4
  set, the only one with a death clip) — the mapping follows the clip names in Godot resources/enemies/anims/*.tres.
  `UEnemyAnimInstance` plays attacks / hits / the pounce / the held death; `ACodexTacticsGameMode::EnemyClasses` spawns
  them. The Blueprints own their look (user decision 2026-09-30): capsule, mesh offset / rotation / scale are authored in
  BP_Enemy_* (the script wrote the Godot per-type values once — hound / cutter 35/60 cm, frostbitten 40/90, brute 60/120,
  model scale 0.8 / 0.85 / 1.0 / 1.4 — and tags the CDO `LookAuthored`); `AEnemyCharacter::ApplyArchetypeDefaults` only
  sizes the C++ placeholder body. Stats stay Godot balance data. Re-running the script keeps every hand edit. With a death
  clip the body stays `DeathDecayDelay` 5 s (Godot death_decay_delay), otherwise 2 s.
- Placeholder meshes (engine shapes) for barrels, barricades, mines, quest objects; the user will replace them.
- The user reported the character looks "перекручено" (twisted) — not investigated yet (ask what exactly: spine /
  hands / pose), see §8.

## 7. Open user decisions / questions

- **Enemy AI direction (user decisions 2026-10-04):** bot VETERAN target win rate 40-75 % (the Jev coach's band);
  turn-based ranged enemies (spitter, marksman) shoot at range with line of fire and a hit roll (deliberate departure
  from Godot, where every turn-based enemy only bites); per-archetype morale (fast ones fall back to the pack and come
  again, brutes / frostbitten never fall back, marksmen relocate); real-time enemy tactics first, then turn-based on
  the same shared rules. Enemy *power* knobs (damage, accuracy, ...) stay proposals for the user; enemy *intelligence*
  knobs may be tuned by the coach. Marksman kiting limit (cooldown 10 s, one 3.5 s dash) = user decision 2026-10-04.
- Jev coach finding 2026-10-04 (user's current waves, VETERAN, 8 runs per batch): 0-1 / 8 victories whatever the bot
  knobs; Jev fairness ~1 / 4 every iteration, enemy issue «marksman too accurate / too evasive» — wave 3 (20 hounds,
  brute, 6 cutters, 4 marksmen) is the user's to rebalance; the coach only proposes enemy power changes.
- 2026-10-04 bot balance batch (25 runs, level_01_outpost / stage_01 data): NORMAL 2/10, VETERAN 4/10, CASUAL 1/5 victories; 16 of 19 defeats are FREEZING_FATIGUE, almost all in wave 3 (squad cold at the end 72-97 %), the medic freezes most often. Open for the user: tune the cold (e.g. wave 3 cold_drain_mult in the Wave Editor / cold rates in DA_GameBalanceConfig) or keep it.
- Spawn lanes: resolved 2026-10-04 (Sprint 05-C aliases).

- L_MovementTest re-laid by the user (rotated 180°, moved): kept (user decision 2026-09-29); smokes are layout-relative.
- `Config/DefaultEditor.ini` has local editor changes — never commit it unless asked.
- M16 clip (user decision 2026-09-29): 30 for every operative like the reference video. The current Godot scene sets
  reload_after_shots 10 (commander) / 15 (engineer) — deliberately not ported.
- Level waves (user decision 2026-09-29): like the Godot code — `main.gd _spawn_custom_json_wave` spawns the whole wave
  at once; `spawn_delay_sec`, `initial_delay_sec`, `max_simultaneous_enemies` (described in DATA_CONTRACTS.md) are
  ignored. Wave modifiers apply (hp / damage / speed per enemy, `cold_drain_mult` → operatives' cold outside camera
  zones, kept after the wave like Godot's cold_rate_modifier), `custom_stats.health` × hp_mult.
- L_MovementTest (user, 2026-09-30): PlayerStart moved ~170 m south of the gate; the four EnemySpawn_* points were moved
  next to it on the user's request (same 20-28 m spread, north of the start — the south wall is 9 m behind it). Smokes
  anchor the layout on the checkpoint gate (`SmokeUtils::LayoutTransform`) since the floor was stretched.
- Monster models: the user imports them personally; the enemy BPs / AnimBPs are generated from them by
  setup_enemy_animation.py (user request 2026-09-30). Spitter / cryo drone have no art yet (C++ placeholder body).
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
- Turn-based entry on flat ground only (user decision 2026-09-30; Godot allows platforms): the controller samples the floor
  under the future grid (7 x 7 over 21 m, `CombatQueries::SampleGroundHeights`) and refuses when a living operative's feet
  are more than 50 cm from the median («⚠️ Пошаговый бой можно начать только на ровной поверхности…»). A plateau wider
  than the grid counts as flat. Smokes call `RequestEnterTurnBased` directly and are not affected.
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
   `DA_GameBalanceConfig` (game_balance_config.tres). **Since 2026-10-01 (user decision) Unreal is the master copy:** the
   assets are `UGameBalanceConfig` — one typed field per Godot export (364, Godot names, Categories = the Godot
   @export_group / @export_subgroup sections, ## comments as tooltips, @export_range as sliders / clamps), generated
   into `Public/Data/GameBalanceConfig.h` by `Scripts/generate_balance_config.py` (plain Python; re-run when Godot adds
   parameters). `UGodotBalanceAsset::GetNumber` / `SetNumber` reach the fields by name via reflection (then `Numbers`
   for keys without a field), so every system reads the tuned values. `import_balance.py` creates missing assets from
   Godot once; afterwards it only writes a Godot-vs-Unreal difference report (`CODEX_BALANCE_REIMPORT=1` overwrites).
   Rule tests use `CodexTacticsTests/Private/GodotBalanceFixture.h` (Godot values), not the tunable assets. Wired so far: turn-based combat
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
  Rage uses the general keys (see Rage below); panic uses the susanin_* stress keys (ordinary susceptibility, user
  decision 2026-10-01). His death does not fail the mission (expendable) and leaves searchable remains (§10).
  The Godot `dialogue_susanin_recruitment.tres` export is unused by Godot's code (it builds both dialogues inline) — same here.
- Waves beyond the level config (or without one) follow Godot's fallback (`FallbackWaveRules` + test,
  `UWaveSubsystem::StartWave`): balance-driven hound / spitter / brute counts from DA_GameBalanceConfig, the whole wave
  at once at the type-filtered spawn points (Gemini's queued built-in waves are gone).
- Level spawn `custom_stats` are all imported (health, damage, speed m/s, attack_range m, attack_cooldown s; `AEnemyCharacter::ApplySpawnEntry`, Godot _spawn_custom_json_wave; no Godot level sets them yet).
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
  Turn-based barrel / mine blasts (`ApplyBlast`, Godot _detonate_barrel / _detonate_mine): the unit's own hit rules
  (enemy armor; operative dodge / stance / fortitude on top of the grid stance multiplier), and an enemy dies when its
  health could not take the raw blast (Godot grid hp decides the kill and calls die()). **Deviation:** operatives die by
  real health only — Godot's grid hp would drop a living operative from the fight.
- **Parity fixes (squad control):** operatives are wounded below `wounded_health_threshold_percent` of max health
  (Godot is_wounded; before, `bWounded` was never set, so the wounded speed / no-sprint rules never applied — the speed
  now follows every health change; `UHealthComponent::ApplyDirectHealthLoss` broadcasts OnHealthChanged); a ground click
  during an active wave outside the tactical pause is refused («Перемещение во время боя возможно только в режиме
  тактической паузы»); double-click sprint lines (frozen / wounded / «Бегом к позиции!»). New: Alt + Z / C / V squad
  stance, Shift + click sector facing (no facing indicator arrow; UE has no persistent fixed-facing, the operative just
  turns), auto-cover crouch on arrival next to a barricade in combat with the radio callout. Box selection (Godot
  _perform_box_selection / _set_selected_squad / _get_group_target_positions): LMB acts on release; a drag past 12 px
  draws the cyan box (HUD `DrawSelectionBox`) and selects the living members whose feet / centre / head fall inside
  (`ACodexTacticsPlayerController::SelectInBox`, `USquadSubsystem::SetSelectedGroup`); rings under them (gold leader
  only in a group); ground orders move / plan the whole group in Godot's slots around the click (`OrderGroupMove`,
  `SquadFormation::ComputeGroupTargets`); picking a leader by number drops the group; not in the grid fight (clicks are
  grid orders there). In exploration the formation still pulls followers (as in Godot). The action bar marks
  a squad slot in barricade cover (green frame) or holding (guard / solo; blue) with « ●» (no emoji in the UI font) and
  Godot's tooltips.
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
  ВОЗДУХЕ», «КРАХ»). The airborne kill gives the cutter EXP (16) and counts in the kill statistics (every death goes through HandleDied).
- Frost vignette: drawn with UMG (a UI material drawn by the canvas HUD ignores its opacity). Canvas HUD texts sit under
  it, like Godot's squad HUD under the FrostOverlay. Material traps met on the way (see the script): in a Custom node
  the float3 colour path rendered nothing — keep Custom nodes to scalar math and do colour / opacity with nodes; with
  MaterialEditingLibrary connect a Clamp's first input with the pin name "" (not "Input").
- Silhouette (Godot player.gd + silhouette.gdshader): every operative traces the Visibility channel from the camera to
  its centre (feet + 1 m) 20 times a second; a hit more than 35 cm short puts `M_Silhouette` (colour per role, leader
  cyan) as the overlay material of its visible meshes. Other characters block the ray too (Godot's mask 1 | 2 includes
  bodies on layer 1).
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
- Panic is ported and ON (user decision 2026-10-01; Godot ships it disabled), see §10 `UPanicComponent`. The allegiance
  component is only used by Godot tests — not ported.
- Narrative elements (`ANarrativeElementActor`; Godot narrative_element.gd) and dialogue triggers (`ADialogueTriggerVolume`;
  dialogue_trigger.gd): L_MovementTest gets the Godot note (by the generator), signpost (central road), poster (gate wall)
  and the wave-rest trigger (right side, midway) at semantic spots — the Godot scene coordinates don't map onto the UE
  layout. The type emoji marker is a small square (no emoji in the HUD font); in-world text wraps at 48 characters and
  is not occluded by walls (Godot Label3D no_depth_test = false). has_been_read is not saved (Godot neither).
- Space hold (Godot main.gd + tactical_hold_sphere.gd + gorky17_combat_hud charge bar): `AHoldSphereActor` (additive dome
  + growing ring + 15 m boundary ring, eased p x (2 - p)), the squad holds fire while Space is held
  (`AOperativeCharacter::bTacticalCeaseFire`), HUD `DrawSpaceCharge` («ВХОД В ПОШАГОВЫЙ БОЙ (GORKY 17): x.xc / y.yc» /
  «ВОЗВРАТ В ТАКТИЧЕСКУЮ ПАУЗУ»). The dome centre is the leader's feet (Godot ray-casts the floor below the leader).
- Barricade contact damage (Godot barricade.gd contact_*; `ABarricadeActor::ContactType / ContactDamage`): every interval
  enemies within 2.2 m take half of it, a striking enemy takes all of it (not when trapped — the trap goes off), fire
  burns 3 s (tick max(1, dmg / 4)), cryo freezes 2.5 s, energy staggers 1 s, 15 % armour pen, source «Баррикада»; the
  label adds [шипы] / [огонь] / [холод] / [ток]. L_MovementTest's generator barricade (Barricade_Abandoned_West, Godot
  AbandonedBarricadeGenerator) has spikes 5 and vault off, like Godot (BarricadeContactSmoke).
- Pushing / defusal / set-up animations: none (operatives only slow down / crouch).
- **Deliberate deviation (user request 2026-09-30):** enemies that fear fire walk round burning barrels / active heat zones on an arc (35° steps on a circle 1 m outside the zone, `FireDetourWaypoint`) and wait at the edge for a target inside one (the running generator); once panicking they calm down 1 m past the edge. Godot only flees, which made them shake at the edge.
- Turn-based walk: units and pushed objects follow one continuous speed profile over the path (accelerate over the first cell, cruise, decelerate over the last) in Godot's total time; Godot eases each step (first in, last out, middle linear) and snaps the facing per step — UE turns smoothly (~0.15 s).
- Vault (Godot locomotion_controller.gd check_vault_obstacle / start_vault, player.gd try_vault_obstacle; `VaultRules` + test,
  `AOperativeCharacter::TryVault` / `UpdateVault`, `VaultNavigation`): barricades (`bVaultable`, on by default like Godot)
  and level objects tagged `Vault` stop cutting the navmesh and get a `UNavArea_Vault` (cost 3) instead — operatives path
  across and vault (knee-height probe 1.25 m ahead, 25..105 cm, landing 1.35 m behind within ±1 m, arc apex height + 15 cm,
  0.83 s running / 1.09 s walking, collision off, 1.2 s cooldown, the walk resumes); not on the run unless stopped, followers
  only when blocked, never prone or in the grid fight. Enemies use `UNavFilter_NoVault` (they go round / smash, as in Godot).
  No vault clips yet: the anim instance exposes `bIsVaulting` and feeds the arc speed to the locomotion (VaultSmoke).
- Hidden mines are revealed by a distance scan in the mine's Tick (Godot scans from each operative) — same result.
- Weapon switching does not change the weapon mesh (no pistol / shotgun / knife models in the art yet — ASSETS has only
  pistol animations) (the operative Blueprint owns WeaponMesh) and has no holster
  animation. X cycles the arsenal like Godot switch_weapon (grenade -> aim, «🔫 Оружие: …»), R reloads outside placement / the grid fight (Godot KEY_R, «🔄 Перезаряжаю …»).
- Grenades: placeholder sphere mesh, no explosion VFX / sound (Godot has none either) — `AGrenadeActor` Blueprint events
  On Landed / On Detonated and the operative's On Grenade Throw + GrenadeThrowDuration are the hooks.
- Turn-based set-up: the item grows in like Godot (0.2 s, then scale 0.05 -> 1 in 0.45 s, back ease-out; `StartGrowIn`); no assembly animation (Godot play_action_animation "working_device" — no clip yet);
  a squad mine placed in turn-based skips the real-time mishap roll like Godot.
- Turn-based relocation: a hologram of the object follows the hovered cell (Godot _create_relocate_ghost_preview; `SetRelocationHover`, green on a target cell, red elsewhere);
  target cells use the reachable layer (for barricades: cells valid at the current angle). The barricade glides to
  its new place in 0.25 s like Godot (quad ease-out; instant headless).
- Exposed-zone outlines pulse like Godot (alpha 0.65 ± 0.35 at 6 rad/s as glow brightness; the target warning blinks 0.25..0.95 at 9 rad/s, `ATurnGridOverlayActor::Tick`); the additive M_CombatFeedback glow reads pink-white on the
  light floor instead of red — a material for the user to tune (layer colours / intensity in TurnGridOverlayActor.cpp).
- Relocation / placement ghost: `M_GhostHologram` (create_ghost_hologram_material.py; unlit translucent, opacity 0.65, emission x2 like Godot's ghost material).
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
- Tracers leave `GetWeaponMuzzleLocation` (the weapon's "Muzzle" socket or `MuzzleOffset`); the line of fire keeps
  Godot's stance heights (1.4 / 0.85 / 0.25 m). Light intensity mapping
  (`FeedbackLightPerEnergy` 1500 per Godot light_energy) is a first guess for the user to tune.

## 10. Change log (newest first)

| Commit | What |
|---|---|
| (this) | **Sprint 10: defense line «Рубеж обороны» (Hold Objective at all costs, Commander Mode).** `FDefenseDirective` {DefendedActor, DefendedLocation, bHoldAtAllCosts, InterceptRadiusCm 1200, MaxDefenseLeashCm 500} on `FTacticalAnchor::Defense` (a player move order clears it: `MakeAnchor` builds a plain anchor). Rules (`SquadAutonomyRules`, the directive's «PickTarget» / «CanGiveSafeAid»): `PickTarget` / `DefenseTargetScore` in tiers — point-blank body-block (defense_body_block_priority) > intruders (within the intercept radius of the object or attacking it), the one closest to the object first (500 per metre) > the ROE policy; `CanGiveSafeAid` refuses aid while intruders are there and (defense_ignore_distant_aid) for mates beyond the defense leash; `DefenseLeashRadius` (Strict 5 m, Flexible 10 m for aid); `AllowsRetreat` false; `HoldsGround` (an enemy inside the emergency distance: no walk at all); `ShouldDrawMelee` (knife at half of it). `USquadAutonomySubsystem::SetDefenseObjective` (anchor at the closest walkable point of the object, the defender walks there), console `CodexTactics.DefendObjective [index] [here]` (nearest generator / terminal / gate, else barricade), stats DefenseHolds / AidRefusedDefense / MeleeDraws. Shift + RMB (`ACodexTacticsPlayerController::AssignDefenseUnderCursor`, leader or selected group) — only in the pause / outside the fight (user decision 2026-10-05: no orders in real time), not in turn-based. HUD squad panel «[РУБЕЖ: Защита]». ROE +4 (`squad_roe.json`, `SquadROE`, Wave Editor section «Рубеж обороны»): defense_intercept_radius_m 12, defense_leash_strictness Strict, defense_body_block_priority true, defense_ignore_distant_aid true. Tests `Characters.SquadAutonomy.DefendObjective.*` (3), smoke `CodexTactics.DefendSmoke` (7 checks). Jev `jev_validate_roe.py` with 6 defense scenarios: 13 / 16 (81 %) — the first run (69 %) caught a real bug: «closest to the object first» lost to the threat tiers, hence the tiered scores; the remaining defense disagreement (point-blank first vs the attacker at the generator) is Jev at confidence 0.12 against the directive's body-block |
| (this) | **Sprint 09: tripwire mine «Растяжка» МУВ-3 + 2 Ф-1 (Gemini directive in TANDEM).** `TripwireRules` (pure, tests `Interactables.Tripwire.*`): 2 grenades, span 1-5 m, wire 30 cm, fuse 0.25 s, blast 140 / 4.5 m (squad 75 %), armour penetration 1 + explosive shred + stagger 1.5 s, rig 2 s, disarm 3 s (medic-sapper only, 10 % fumble -> 1 grenade back), arming 1.5 s. `ATripwireActor` (`AInteractableActor`): wire bar + ground pegs / object brackets, trigger = any operative / enemy whose body (Sprint 08 profile heights; prone 25 cm crawls under) touches the segment near its ground, «ЩЁЛК!» -> `ApplyBlast` once at the wire's middle (the paired Ф-1), action menu disarm, `UNavArea_Tripwire` (cost 25) via a nav modifier — operatives' default filter detours, `UNavFilter_NoVault` overrides it to 1 for enemies. `URelocationSubsystem::StartTripwirePlacement` (inventory line «🪤 Растяжка», `ACodexTacticsPlayerController::StartTripwirePlacement`, real-time order lock, refused in turn-based / with < 2 squad grenades): click A / click B (`HasAnchorObject`: bracket if something stands at 30 cm, else peg), cyan / red preview wire (span + wall trace), the medic-sapper (else the opener) walks beside the middle (sprints in the preparation) and rigs it 2 s, grenades taken from the squad at the end; in the tactical pause it is planned and runs on release. Smoke `CodexTactics.TripwireSmoke` (7 checks); design check `Scripts/Tools/jev_validate_tripwire.py` 7 / 9 (Jev low-confidence against ignoring the rigger's first step and against a 10 % fumble). No sound / VFX code (Blueprint `On Pin Pulled`, `On Exploded`) |
| (this) | **Fix: CombatFlowSmoke flaky («free pause 17.5 s»).** Not the pause timer: the user's L_MovementTest has its 4 enemy spawn points 1.8 / 4.3 / 15.3 / 15.3 m around the squad start and the waves use spawn lane ANY, so (since the crowd limit 250 lets late spawns move) hounds randomly spawned on the squad — the leader's planned walk was blocked and the «hold without enemies» check entered turn-based a stage early, shifting the free pause by 2 s. The smoke now parks every wave enemy frozen 40 m away each step (4 / 4 runs PASS, 19.48 s); diagnostic lines kept. **Game note for the user:** with spawn lane ANY, waves can spawn right next to the squad on this map |
| (this) | **Sprint 08: tactical line of sight, cover occlusion, LKP silhouettes (Gemini directive in TANDEM).** User decisions 2026-10-05: the barricade itself is 60 cm high now (`ABarricadeActor::HeightCm`, box 3 x 0.6 x 0.6 m, map-placed ones `SettleOnGround` onto the ground at BeginPlay; vaulting / fire lines follow the real geometry), and melee enemies that lose an operative hunt his last known spot. `SightRules` (pure, tests `Combat.Sight.*`): eyes 160 / 95 / 25, profiles 150 / 90 / 25 cm, demask 2 s, squad hearing 12 m, enemy hearing 12 m (a prone operative 5 m), blind fire x0.2 (0 when the enemy left the spot), pack alert 15 m, search the last known spot 5 s then forget, or after 20 s — prone hearing, search and blind fire set by the user 2026-10-05 after the Jev check `Scripts/Tools/jev_validate_sight.py` (directive: -40 %). `UTacticalSightSubsystem` (world, 0.2 s, WaveCombat real time + pause, not turn-based; `Codex.Sight 0` off): squad -> enemy ECC_Visibility traces eyes -> profile (pawns ignored; enemies by size, the marksman by stance) — unseen enemies `SetActorHiddenInGame` (+ attached actors, HUD plates skipped) with an `AEnemyGhostActor` (posed copy of its meshes in `M_TacticalStasis`) frozen at the last confirmed spot; within 12 m the silhouette follows the sound; `NotifyFired` (operative shots, spitter spit, marksman shot, melee blows) demasks for 2 s at once. Enemy -> squad symmetric: per-enemy intel (seen / heard / demasked, shared with pack mates within 15 m; a new enemy knows where the squad stood) — `AEnemyCharacter::FindTarget` / `IsTargetUsableForTactics` / spitter / `EnemyTacticsSubsystem` use `GetBelief`, unperceived targets are approached at the last known spot. Operatives never auto-fire at hidden enemies (`EvaluateShotLine`); a click on a silhouette (pause; group too) orders blind fire (`SetBlindFireTarget`, `EvaluateBlindLine`, -80 %, tracer to the silhouette), which turns into a priority target when the enemy is seen again. Smoke `CodexTactics.SightSmoke` (11 checks). **Note:** with a 60 cm cover a crouched operative behind it is now hit over it by spitters / the marksman (their lines pass above 60 cm) — prone is the protection; balance to watch |
| (this) | **Fix (user report 2026-10-05, second time): keys 1-4 ignored in the fight.** The user's PIE log showed only 5 of the presses reached the game (each then worked; clicks always worked): a widget holding the Slate keyboard focus took them. `ACodexTacticsPlayerController` registers a Slate input pre-processor (`SquadKeys::FSquadKeyProcessor`, BeginPlay / EndPlay) that catches 1-4 before any widget — only for the game's window, not while typing in a text box, not with the game paused, no repeats / Ctrl / Alt — and calls `HandleSquadNumberKey` -> `SelectMember`; the Enhanced Input binding stays for simulated input (smokes). Each press logs `Select key N (keyboard focus: <widget type>)` |
| (this) | **Preparation set-up: the closest operative runs (user decision 2026-10-05).** In the preparation phase a turret / barricade / mine marked from the inventory (or F) is set up by the free squad member closest to the spot (`RelocationRules::ChooseNearestWorker`, planar; alive, not carrying / vaulting / panicking / raging, no set-up task yet), the squad's items being shared (handed over from the one who opened the inventory, else any carrier), and he sprints there (`URelocationSubsystem::PickPreparationWorker`, `ExecuteDeploy(..., bSprint)`, sprint kept on the retry walk). Exploration, the tactical pause plan and the turn-based fight are unchanged. Test `Interactables.RelocationRules.NearestWorker`, smoke `CodexTactics.PrepDeploySmoke` |
| (this) | **User decisions 2026-10-05: no orders in the real-time fight; Commander Mode switch on screen; 1-4 recentre the camera.** In a real-time wave fight (WaveCombat + RealTime) the player only pauses (Space), toggles Commander Mode (Ctrl + T / the new action bar button «АВТО ВКЛ / ВЫКЛ») and picks the operative (1-4, a click on him; the camera follows): `ACodexTacticsPlayerController::BlockRealTimeOrder` refuses with the HQ hint «приказы только в тактической паузе» — ground / enemy / object clicks, stances (Z / C / V, Alt, action bar), guard, solo mode, weapon cycle / selector, grenades, deploy / relocation, item use (H / J / K / L, inventory drawer: no medkit in real time), transfer; grenade aim / placement / transfer modes left over from a pause ignore clicks. Everything works in the tactical pause and the turn-based fight as before. `Codex.RealTimeOrders 1` restores the old real-time control (ClickRulesSmoke sets it; the playtest bot never goes through the controller). `FindClickedMember` shared by both click paths. 1-4: the code path worked (new `RealtimeSelectSmoke`: keys through Enhanced Input in real time and in the pause, leader + camera target, medkit refused / used, switch works); the visible fault was the camera keeping a WASD / drag pan in the fight view — a leader change now returns the pan (`ATacticalCameraPawn::HandleLeaderChanged`). The in-game dialog buttons (profile, transfer, loot, dialogue) are non-focusable now (a focused button swallows 1-4); `Select key N` is logged for the next report |
| (this) | **Sprint 07: Commander Mode (autonomous squad combat, Gemini directive in TANDEM).** Off by default (`USquadSubsystem::bAutonomousSquadCombat`; Ctrl + T, `CodexTactics.AutonomousSquad [0|1]`, `CodexTactics.ToggleAutonomousCombat`; HUD «АВТОНОМИЯ: …»). Every player move order pins `AOperativeCharacter::TacticalAnchor` (7 m). `USquadAutonomySubsystem` (world, decisions every 0.3 s, only in a real-time wave fight; a tactical pause / turn-based fight freezes it: autonomous walks stop, autonomy targets dropped, the player's orders untouched): field aid first (`HealAlly`: a medkit's heal on a mate below 25 % within the leash — flexible: 10 m — when the Safe Aid Check passes: no marksman aim on rescuer / patient, nobody within 6 m of the patient; the last medkit is kept below 50 % own HP; then back to the anchor), leash return, barricade cover against the nearest threat / the aiming marksman with the stand point inside the leash (`PlaytestBotRules::CoverStandPoint / CoverScore`, elevated +25 when prefer_high_ground), cover-side change against a flanker > 75 deg off the barricade direction within 12 m, ROE stance (cover crouch, open crouch, sniper -> prone without reachable cover / DropProne), target by policy (`AOperativeCharacter::SetAutonomyTarget`, fired at after the Ctrl-click priority target; point-blank < 3.5 m first; ThreatLevel tiers marksman / spitter > hound / drone > cutter / brute > rest), reload below 25 % in cover (or nobody within 8 m), pistol / shotgun at < 3.5 m with an empty or reloading primary and back at 7 m. Not called for panicking / raging / carrying / vaulting operatives; a moving operative on a player order is never overridden. ROE: `Content/Data/AI/squad_roe.json` (13 params, `SquadROE`, Wave Editor tab «Тактика отряда (ROE)», `/api/squad-roe`). Tests `Characters.SquadAutonomy.*`, `Data.SquadROE.JsonRoundTrip`; smoke `CodexTactics.CommanderModeSmoke` (default off, leash, aid +80 HP, pause freeze: PASS). Jev design check `Scripts/Tools/jev_validate_roe.py` 8 / 10. **Not covered yet:** «Downed» state does not exist in the game (aid triggers on HP only); elevation is only scored for barricade cover (no free high-ground search); the smoke's map had no barricade inside the leash, so cover moves are only unit-tested |
| (this) | **Marksman kiting limit + bot storms marksmen + Jev AI coach** (user decision 2026-10-04: limited retreat). `FMarksmanConfig::RetreatCooldownSeconds` 10 (after a retreat / back-off he holds and fires at any distance below the band; `MarksmanAIRules::ChooseMove(..., bCanKite)`), `RetreatMaxSeconds` 3.5 (one dash). `UPlaytestBotSubsystem::AssaultMarksman`: a marksman beyond rifle reach with no other enemy within `Codex.Bot.AssaultClearRadius` (1500) -> the squad sprints at him fanned out (0 / +-35 deg) and stops `Codex.Bot.AssaultStopDistance` (900) short. Tunable console variables `Codex.Marksman.*` (RetreatCooldown, RetreatMaxSeconds, ShotDamage, BaseAccuracy, AimDuration, ShotCooldown; -1 = asset) and `Codex.Bot.*`; `[Marksman] ... fires at` / `retreats` log lines. `Scripts/Tools/jev_ai_coach.py`: bot batch -> facts from the logs -> one Jev request (bot weakness / enemy issue choices, fairness score, avoidable noul) -> one confidence-gated bounded knob step -> next batch, kept only on a measured gain; enemy knobs only with `--tune-enemies` (balance = user), else listed as proposals; reports in `Saved/Telemetry/ai_coach/`. Note: `typesafe_triage.py --telemetry` sends a hard-coded sample, not the runs. Batch (8 x VETERAN, user's waves): 1 / 8 (was 0 / 8); first Jev read: fairness 1 / 4, defeats avoidable 0.70, enemy issue marksman_too_accurate |
| (this) | **Fix (user report 2026-10-05): frostbitten stood still in waves.** Detour crowd `MaxAgents` defaulted to 50: enemies past it (wave 1 = 45 hounds + 10 frostbitten, the frostbitten spawned last) were never simulated — path following «Moving», full path, speed 0, every operative marked a dead end in turn («no way to …»). `Config/DefaultEngine.ini` `[/Script/AIModule.CrowdManager] MaxAgents=250`. **Fix: Wave Editor weapon tuning never reached the game** — at StartPlay the asset registry was still scanning (`Weapon tuning: 0 weapons`); `WeaponTuning::ApplyFile` now loads `DA_Weapon_<id>` by name (9 weapons). New dev probe `CodexTactics.EnemyStuckProbe` (every enemy's path / movement / `AEnemyCharacter::GetDebugState` after 13 s of wave 1; 0 / 55 stuck now) |
| (this) | «Правила пошагового боя» also carries the stance damage: `turn_based_rules.crouch_damage_multiplier` / `prone_damage_multiplier` (0.7 / 0.5, seeded from DA_Balance via the dump), applied over DA_Balance's tactical_stance_*_dmg_mult when a fight starts |
| (this) | Wave Editor «Оружие отряда» → «Правила пошагового боя»: `weapons_tuning.json` `turn_based_rules` (crouch_move_cost_multiplier 2, cover_fire_accuracy_multiplier 0.75, enemy_fire_at_cover_multiplier 0.6 — the last one was hard-coded, now `FTurnBasedBalance::EnemyFireAtCoverMultiplier`, passed to `EnemyTurnRules::RangedHitChance`); applied when a fight starts (`WeaponTuning::ApplyTurnRules` after `BalanceFromGodot`). Test Data.WeaponTuning.TurnBasedRules |
| (this) | **Turn-based rules (user decisions 2026-10-04).** (1) Crouched (and prone — he walks crouched) every grid step costs `FTurnBasedBalance::CrouchMoveCostMultiplier` (2) times the AP: the reachable overlay, `MoveActiveUnitTo` and the deploy walk use AP / multiplier steps (`TurnBasedRules::MoveCostMultiplier`); the stance damage cut (crouch x0.70, prone x0.50) is unchanged. (2) A barricade right next to the shooter or the target no longer blocks the line of fire (`GorkyLineOfSight::HasLineOfFireThroughCover`; one in the open between them still does): the operative fires past his own barricade at `CoverFireAccuracyMultiplier` (0.75) accuracy (attack cells `bThroughCover`, feed «🧱 Огонь из-за баррикады»), ranged enemies shoot at an operative behind it at x0.6 (`IsCoveredFrom`, already). Real time already worked this way (`SquadFireRules::JudgeLine`: standing over it, crouched x0.8, prone blocked). (3) A medkit used by the active operative ends his turn (`UTurnBasedCombatSubsystem::EndTurnAfterMedkit`, from `UseSquadItem`). Test Tactics.CoverFire.*, TurnRulesSmoke; TurnWalkSmoke picks a walk the doubled cost allows |
| (this) | **User reports 2026-10-04 + Wave Editor weapons.** (1) Waves spawned the whole wave into one point (capsules inside each other, standing still; Godot's physics pushed bodies apart, UE's does not): `UWaveSubsystem::FindFreeSpawnSpot` — the point, else rings 1.5-9 m, projected on the navmesh, capsule free of pawns / walls; a spawn point > 6 m off the navmesh is logged once; LevelWaveSmoke checks waves 1 and 2 (separation >= 80 cm, on the navmesh, 90 % moving after 6 s). (2) An operative stuck on top of a barricade after a vault (landing inside a 3 m barricade when vaulting along it), a cutter's pounce too: `TryVault` lands only on free ground past the obstacle (+60 cm steps up to +240), and `VaultNavigation::IsStandingOnObstacle` / `FindStepOffSpot` make operatives (short vault arc, the walk resumes at `LastMoveDestination`) and enemies (teleport) jump off any barricade / barrel top after 0.3 s; VaultSmoke checks it. (3) Pack tactics polish: flank hysteresis (`FlankDoneTarget`), enemies jostling slowly within 6 m of their prey face it (FacingSmoke). (4) Wave Editor: tabs «Оружие отряда» (per weapon: damage, range, fire rate, penetration, clip, reserve, reload, status, turn-based range / hit chances / distance multipliers; «Гранаты»: damage, radius, throw range, carry limit) and «Оружие врагов» (marksman rifle, real time + turn based) over `Content/Data/Weapons/weapons_tuning.json` (`/api/get-weapons`, `/api/save-weapons`); the game applies it at StartPlay (`WeaponTuning`: DA_Weapon_* in memory, operative grenades, `ApplyMarksmanRifle`, `ApplyEnemyTurnWeapon`; Codex.Marksman.* cvars still win); `CodexTactics.DumpWeaponTuning` wrote the file from the assets. Loadout «Гранаты» per operative (`squad_loadout.grenades_count`, `LoadoutRules::GrenadesFor`: presets 1 / 2 / 4, CUSTOM the value, clamped to the carry limit). Spawn sliders up to 100 for every type. Smokes adapted to the user's re-laid map: `SmokeUtils::ClearPoint` / `FreeSpot` (StanceSmoke passes). **Open:** CombatMoveSmoke — on the new map the commander at the design start (-800, 1130) does not move although on the navmesh, with a full path and path following «Moving» (speed 0; passes on the previous map) — something at that spot of the user's layout; diagnostic lines left in the smoke. Note: canned_food / matches of the editor's loadout are not read by the game (Godot did not either) |
| (this) | **Turn-based enemy AI (user decisions 2026-10-04, step 2 of the enemy AI plan).** `EnemyTurnRules` (pure, tests `Tactics.EnemyTurn.*`): per-archetype turn profiles — hound 6 AP / 18 dmg, bite and step back, flank-arc preference; cutter 7 AP / x1.2, back-arc first; frostbitten 4 AP / x1.1, stays on its victim; brute 5 AP / x1.8, 3 AP blow, stays; spitter / cryo drone ranged 2-6 cells (prefers 3-5), 3 AP shot, x0.8, hit 75 % - 6 % per cell; marksman 5 AP ranged 3-10 (prefers 5-9), 4 AP shot, x1.6, hit 80 % - 3 % per cell; hit chance x0.8 crouched / x0.6 prone / x0.6 next to a barricade on the shooter's side, clamped 10-90 % (first-guess numbers, the user's to tune). `UTurnBasedCombatSubsystem`: max AP / base damage per archetype at registration; `ChooseEnemyTarget` = the real-time `EnemyTacticsRules::ChooseTarget` (wounded / straggler / exposed / turned away, attackers already sent this phase); melee picks the orthogonal cell by arc (`ChooseMeleeCell`), so cutters walk round to the back; ranged `ExecuteRangedEnemyTurn` -> `ChooseFiringCell` over the reachable cells (line of fire `GorkyLineOfSight`, not next to an operative, preferred band), `EnemyRangedAttack` (roll, tracer, feed «🎯 … шанс N%» / «💨 … промах», `[EnemyTurn]` log), else it moves towards the band; `WalkEnemy` (AP-truncated walk, stops on a mine); an enemy that did not reach its target ends its turn (Godot stepped back pointlessly). TurnBasedSmoke: the brute starts 3 cells out (5 AP + 3 AP blow) |
| (this) | **Jev tooling made real (user request: use Jev maximally to save agent tokens).** `Scripts/Tools/jev_client.py` (shared: key from env / Windows registry / Saved/Config/typesafe.key, retries, usage on stderr, no invented answers). `typesafe_triage.py` (Gemini's tool, fixed with the user's go): the key now found (the triage commands silently used the offline heuristic); `--audit-diff` audits the *staged* files (it read `git diff --stat`, i.e. other agents' unstaged leftovers) with a code check of protected paths + Jev regression risk; `--telemetry` reads the real records (`--last N`; it sent a hard-coded sample); `--smart-test` maps changed headers to the tests / smokes that include them (filters joined with `+`; the old group names matched no test) and asks Jev only for unmapped files; `--early-stop` is a code rule over the current batch (`bot_run.ps1` passes `--since`; it read `death_cause`, which records do not have). New `Scripts/Tools/jev_digest.py`: `smoke` / `tests` / `bot` / `log --ask` digests (a line per check, Jev's failure class on FAIL). Agents: read digests, not raw logs |
| (this) | **Enemy pack tactics, real time (user decisions 2026-10-04, step 1 of the enemy AI plan).** `EnemyTacticsRules` (pure, tests `Characters.EnemyTactics.*`): per-archetype profiles (hound pack hunter: wounded / stragglers, half flank, morale at 25 % HP or 3 pack deaths within 10 m / 6 s; cutter: backs + open ground, 2/3 flank, morale 30 %; frostbitten: nearest, pile on, never break; brute: one per target, goes for the one in cover, never breaks), `TargetCost` (distance minus preferences, 8 m per attacker over the cap, current target 20 % cheaper), `AssignRoles` (flank share of the non-holders, at least one pins, returning roles kept), `FlankPoint`, `IsBehind`, `ShouldFallBack`, `FallBackPoint`. `UEnemyTacticsSubsystem` (world) refreshes every 0.4 s: targets + roles for hound / cutter / frostbitten / brute / base, morale fall-backs (3.5 s, cooldown 20 s, «↩ ОТХОД»), `[EnemyTactics]` 10 s summary (flank orders, fallbacks, backstabs, retargets). `AEnemyCharacter`: keeps the Godot turret / generator target, else follows the order (flank route via MoveToLocation until 5 m), `IsTargetUsableForTactics`. Knobs `Codex.Enemy.Tactics / FlankShareScale / FocusCapBonus / PreferenceScale / Morale / MoraleDeathsBonus`. Coach: enemy-intelligence knobs tuned to the 40-75 % win band + Jev engagement; bot knobs to wins; power knobs proposals only. A/B (user's waves, VETERAN, 8 runs, deterministic): tactics off 3/8, on 3/8 (284 flank orders, 44 fallbacks, 4 backstabs per run, fights 267 s vs 183 s); Jev fairness 1.8 / 4, engagement 2.0 / 4. EnemyTacticsSmoke |
| (this) | **Jev AI coach + Gemini's marksman / bot work finished (user decision).** `MarksmanAIRules::ChooseMove(..., bCanKite)`, `FMarksmanConfig::RetreatCooldownSeconds` 10 / `RetreatMaxSeconds` 3.5 (caught inside the band he holds and fights), `[Marksman] fires / retreats / dies at` log lines; `UPlaytestBotSubsystem::AssaultMarksman` (a lone marksman beyond rifle reach: the squad sprints at him fanned out, stops `Codex.Bot.AssaultStopDistance` away); `Codex.Marksman.*` / `Codex.Bot.*` console variables; `AITuning` (Data/AITuning.h) applies `Content/Data/AI/ai_tuning.json` at StartPlay (game-setting priority, -dpcvars wins, -NoAITuning skips; Codex.* only). `Scripts/Tools/jev_ai_coach.py`: hill-climbs one knob per iteration, Jev (one request: bot weakness / enemy issue choices, fairness score, avoidable noul over a worded summary — numbers bucketed in code) picks the step when confident, a step is kept only above a 0.1 score margin, writes the best bot knobs to ai_tuning.json (enemy knobs only with --tune-enemies; else listed as proposals). Runs are NOT fully deterministic (the same knobs gave 1/8 and 3/8 in two batches; parallel fixed-step games still differ) — 8 runs per batch is noisy, verify a kept step with 16+ runs. First loop: AssaultStopDistance 900 -> 750 (0 -> 1 / 8). `bot_run.ps1 -EarlyStop` and `test.ps1 -Smart` (Gemini) committed with it. Test AI.Tuning.AppliesCodexCVarsOnly |
| (this) | `-LevelJson=<name or absolute path>` game argument overrides the map's level JSON for a run (`ACodexTacticsGameMode::ApplyLevelConfig`, `LevelJsonRules::LoadLevel` accepts absolute paths) — bot A / B batches on other waves without touching the Wave Editor files: `bot_run.ps1 -Extra "-LevelJson=C:\...\level_01_outpost.json"`. Bot batch after 06-E..H (8 x VETERAN): 0 / 8 on both the committed and the user's current waves — marksmen now hold 20-35 m with a line of fire, the squad's weapons reach 14 m (prone 18.9 m), the last 2 marksmen pick the squad off (open user decision, §7) |
| (this) | **Sprint 06-E..H (architect contracts).** 6-E: RMB cancels the leader's / selected operatives' running relocation (pushing: set down where it is, collision back, step back; walking up / deploy walk: stop), «❌ Доставка объекта отменена.». 6-F: push distance = capsule radius + the box's extent along the push + 25 cm (floor 135), the follow lag clamped so the object never comes inside it. 6-G: marksmen engage at spawn during a wave / preparation; a hit in a fight turns him to the shooter (by `AttackerSource` name, else the closest), alerts the marksmen in `AlertRadius`, then retreat (< 12 m) or firing stance + aim + return fire (in range with a line of fire; the shooter stays his target 6 s) or a firing-position search; the prone ambush stays for patrols. 6-H: `MoveTo` from prone stops, stands up and runs only after `RiseDelay` 0.45 s. Extra: target = closest operative in the line of fire (else the closest), retreat goal = a firing position, re-search on arrival without a line of fire / stall (flank only without one). Smokes: RelocationSmoke (+RMB cancel mid-push, push gap), MarksmanSmoke (hit while turned away -> faces, aims, fires back in 2 s) |
| (this) | BotRepairSmoke: «Начать бой», the generator breaks in the preparation, the bot's engineer repairs it (menu confirmed, 2.5 s work, 9.2 s with the walk) and the preparation waits. Bot batch after Sprint 06 (8 x VETERAN, the user's marksman waves): 8 / 8 victories, the generator never broke (marksmen do not attack it) |
| (this) | **Sprint 06 (architect contracts, user go 2026-10-04) + bot repairs the generator.** 6-A: the active leader always has his gold selection ring, every selected operative a cyan one (exploration, preparation, fight; `UpdateSelectionRing` public, `USquadSubsystem::SetLeader` refreshes all rings at once; Godot only ringed group members). 6-B: real-time / preparation move orders ping each operative's target (`UCombatFeedbackSubsystem::SpawnMovePing`, cyan disc fading in 1.75 s, a new order clears the old pings; planned orders keep their persistent markers). 6-C: the contact barricade timer stands still in turn-based combat (it hurt every second of the planning); `ABarricadeActor::ApplyTurnContact` hits an enemy within 2.2 m once when its turn starts (`UTurnBasedCombatSubsystem::ProcessNextEnemy`, `GetContactHitsThisFight`). 6-D: a marksman is in Engage during a wave / preparation even with a patrol route; without a line of fire he no longer retreats from operatives behind a wall (`MarksmanAIRules::ChooseMove`) but walks to a firing position (`FindFiringPosition`: 48 navmesh samples on rings 22 / 27.5 / 33 m with a line of fire from a standing scope and a full path, the shortest wins, every 3 s), flanks after 3 s without progress, and a flank / retreat stuck 1.5 s ends at once (he looped retreat / approach at the yard wall 12 m from the squad). Bot: a broken generator is repaired by the engineer (`TickGeneratorRepair`: in the preparation, which waits, or in a wave with no enemy within 12 m; the engineer leads for the repair, the commander after). Tests: LevelJson.AllFilesParse replaces the DA_Level parity test (the JSON is edited in the Wave Editor now — the user turned the brutes of level_01_outpost / stage_01 into 4-5 marksmen per wave, committed with Sprint 05); smokes BarricadeTurnContactSmoke, MarksmanAdvanceSmoke, GroupSelectSmoke (rings, pings) |
| (this) | **Sprint 05 (Gemini's spec, user go 2026-10-04).** 5-A: `Scripts/bot_run.ps1 -Parallel N` (default 4) runs the games at once (8 runs 165 s instead of ~574 s); each game writes its record to its own `-TelemetryRunsFile=` (`URunTelemetrySubsystem::GetRunsFilePath`) and the script alone appends it to runs.jsonl; run / spatial session ids are GUIDs (parallel games got the same unseeded FMath::Rand); the script keeps `Saved/Telemetry/bot_status.json` current. 5-B: Wave Editor `/api/bot-status` (status file + runner PID check) and `BotStatusBar` (progress bar, wins / losses, active runs, timer; compact badge in the header; reloads the analytics when a batch ends). 5-C (decisions Q8 / Q9): `SpawnLaneRules` — NORTH_GATE / WEST_FLANK / EAST_FLANK / FAR_PERIMETER are aliases of the points' Russian lanes (Godot's containment rule kept; the points' type filters still apply), `UWaveSubsystem::GetSpawnLocationForLane`; wave 3 `cold_drain_mult` 1.1 -> 0.75 in level_01_outpost.json / stage_01.json. 5-D: `UPlaytestBotSubsystem::ReactToMarksman` (a marksman aiming at a squad member: the squad crouches, the leader takes a barricade within 20 m at once), `FindCover`; warm-up trips only to active heat whose generator is neither broken nor drained, and a heat that did not warm the leader is skipped 30 s. Tests: CodexTactics.Combat.SpawnLanes.Aliases; BotMarksmanSmoke. Bot finding: enemies break the diesel generator, its heat goes out and the squad (no food left) still freezes in wave 3 — the bot does not repair the generator yet |
| (this) | **Marksman Scope Beam VFX + Tri-Agent Collaboration Architecture** (Gemini & TypeSafe Jev System One): generated `/Game/VFX/Materials/M_SniperScope_Beam` with unlit additive core, cross-section falloff, pulse flicker, and dynamic `AimProgress` intensity boost (`create_sniper_beam_material.py`). 5/5 unit tests and `MarksmanSmoke` PASS. Formulated authoritative architect decisions in `TANDEM.md` answering open questions on Wave 3 cold fatigue and spawn lanes. Integrated `typesafe_triage.py` as an ultra-fast gatekeeper for boundary auditing and telemetry distillation for Opus 5.5 |
| (this) | **GC crash fixed** (1 of ~10 bot runs; reproduced 5 of 6 with `bot_run.ps1 -Extra "-dpcvars=gc.TimeBetweenPurgingPendingKillObjects=1,gc.ForceEnableGCProcessor=1"`, which named the culprit: `Invalid object in GC … Referencer: BP_Enemy_*.CharacterMesh0, MemberId OverlayMaterial`). Overlapping hit flashes (`ACombatFeedbackActor::SetupOverlayFlash`, 0.08 s): the second flash saved the first flash's glow as the mesh's overlay and restored it after the first flash actor — the glow's outer — was destroyed, leaving the mesh on a freed material. A flash now saves the mesh's own overlay (through the running flash's `GetSavedOverlay`) and keeps the saved overlays in a UPROPERTY. HitFlashOverlaySmoke; the same stress series then ran 8 of 8 clean. Trap for the future: never store another short-lived actor's MID as something to restore |
| (this) | Spatial telemetry for the Wave Editor's replay player (Godot archive tools/bot/spatial_telemetry_recorder.gd, data/schemas/spatial_telemetry.schema.json): `FSpatialTelemetryRecorder` (Bot/) records from the bot's fight start — level layout (bounds around the squad and spawn points + 10 m, barricades as covers, static meshes with collision as box obstacles, spawn points, the generator as the defend point; no elevation polygons), a frame every 0.1 s game time (squad: position, HP, stance, cover, weapon, cold; enemies: type, HP, velocity, target), events (GRENADE_THROWN, COVER_ENTER, COVER_LEAVE on a fallback, ENEMY_DEATH with killer height, CHOKE_CONGESTION from Godot's 3 m cell stall grid) and the summary (cover use, choke points, height use) -> `Saved/Telemetry/spatial_runs/run_<session>.json` in Godot coordinates (metres; Unreal X -> x, Y -> z, feet Z -> y). Checked in the editor's 2D tactical player (a VETERAN run: 2120 frames, 120 events, plays). `.claude/launch.json` `wave-editor` starts the editor's dev server |
| (this) | Expendable member's remains (Godot player.gd corpse_loot / get_items_list, main.gd _handle_expendable_member_death): when the recruit dies a hidden `ALootCrateActor` tagged `CorpseLoot` («Останки: <name>», 0.6 s search) lies at his feet as the body's hit box with his ammo (m16 / pistol clip + reserve), medkits, canned food, bread and chocolate — the regular loot dialog takes them; HQ line «… погиб в бою! Обыщите останки…». Grenades and his weapon have no loot stack in Unreal (not carried over). RecruitDeathSmoke |
| (this) | Enemy AI, user decision 2026-10-02 (found by the playtest bot: hounds / cutters waited forever at the edge of the generator's warm zone where the squad, the turret and the generator stood, the wave never ended): when no target is usable (all in fire / heat zones or recently unreachable) `AEnemyCharacter::FindTarget` goes for the nearest operative and sets `bBravingFire` — the fear-of-fire flight and the zone detour are skipped until a usable target exists (it used to fall back to the plain choice and wait at the edge). Bot: with no warming food left a cold squad walks into the nearest active heat zone (deviation; Godot's bot lowered the cold by decree); the status lists the last enemies' target distance and height. Bot results after the fix (3 runs each): casual 0/3 (frozen twice), normal 0/2, veteran 1/3. The GC assert was fixed later (overlapping hit flashes, see the row above) |
| (this) | **Playtest bot + run telemetry** (Godot archive tools/bot/bot_driver.gd + smart_tactical_bot.gd, main.gd _record_telemetry). `URunTelemetrySubsystem` writes every run (tester_profile HUMAN, or the bot's profile; `engine: unreal`) to `Saved/Telemetry/raw_runs/runs.jsonl` in the record the Wave Editor reads: per-operative hits per wave and weapon (`ShootAtTarget`), turret / barricade / mine damage and kills (enemy health events by attacker source), cold damage / 100 % time (`UColdSurvivalComponent` counters), remaining supplies, death cause (FREEZING_FATIGUE when the fallen froze); DEFEAT at GameOver, VICTORY at PostCombat; `-NoTelemetry` (smoke.ps1 passes it) keeps it off; `RunTelemetryRules` + tests. `UPlaytestBotSubsystem` (`-CodexBot -BotProfile= -BotLoadout= -BotTimeout=`, console `CodexTactics.Bot`): explores (loot crates, loose deployables), «Начать бой» (`StartMission(Combat)`), skips the cutscene / dialogues, deploys the profile's defences on navigable points (veteran: turret, 2 barricades one after the other, mine, engineer / medic crouch + guard; normal: one each; casual: none), plays the waves (grenade at clusters of 3 within 4 m, leader fallback under 3.8 m, nearest barricade cover within 20 m, crouch in cover), uses real medkits / warming food (**deviation**: Godot healed with pause charges and lowered the cold by decree), hunts the last <= 3 enemies after a 30 s stall (not in Godot), exits with the result; `PlaytestBotRules` + tests. `Scripts/bot_run.ps1` / `Scripts/run_simulations.bat` (the Godot menu; fixed-step `-benchmark -FPS=60` without rendering: a 3-wave run takes 20-60 s real). Port gap found by the bot and fixed: Godot is_expendable — the recruit's (Susanin's) death no longer fails the mission (he leaves the squad, HQ line); his body search for supplies (Godot corpse_loot) is not ported yet. Spatial telemetry for the editor's replays (spatial_telemetry_recorder.gd) is not ported yet |
| (this) | Timers ready for the playtest bot: the bot will run fixed-step (`-benchmark -fps=60` style, no rendering, as fast as the CPU goes), so game logic may only use world time or **FApp time** (`FApp::GetDeltaTime` / `GetCurrentTime` follow the fixed step; in normal play they are real time). The game flow / Space hold / camera already used FApp; the rage crit window and the double click used the wall clock (`FPlatformTime::Seconds`) and now use `FApp::GetCurrentTime`. UI-only real time (HUD pulses, message ages) is left as is. RageSmoke / ClickRulesSmoke |
| (this) | Wave Editor moved into this repo (user decision 2026-10-02): `Tools/WaveEditor` (copy of the Godot archive's tools/editor, React / Vite; `Scripts/run_wave_editor.bat`, npm packages / dist ignored). Its dev server now reads / writes `Content/Data/LevelJson/*.json` (the game's runtime level data), bot presets `Content/Data/Bot/bot_presets.json`, telemetry `Saved/Telemetry/{raw_runs,spatial_runs,exports_gif}`; «run bot» starts `Scripts/run_simulations.bat` (the coming C++ bot) and says so while it is missing. MARKSMAN added (enemy list, composition chart, `Content/Data/Schemas/level_config.schema.json`); schemas copied to `Content/Data/Schemas`. Checked: tsc, vite build, API (health / list-stages / run-bot) |
| (this) | **Unreal is the reference (user decision 2026-10-02)**: Godot development stopped — the Godot repo is a frozen read-only archive; CLAUDE.md updated. Level / wave data migrated into this repo: `Content/Data/LevelJson/*.json` (copied from Godot data/configs/levels, staged as UFS in DefaultGame.ini) read at the start by `LevelJsonRules` (`ACodexTacticsGameMode::LevelJsonFile` = level_01_outpost.json, `GetActiveLevelConfig`; DA_Level_* only as the fallback, import_levels.py is legacy); enemy type MARKSMAN accepted. Tests CodexTactics.LevelJson.* (contract + every file equals its imported asset). Next: Wave Editor copied to Tools/WaveEditor, playtest bot in C++ |
| (this) | Marksman art on the user's Biochemical_Monster_1 (SKM_baze_mesh1 = the model with its scoped rifle; UE4 mannequin rig): `Scripts/Editor/setup_marksman_animation.py` makes its skeleton compatible with the RifleAnims and Crawl_MocapAnimPack skeletons and creates `/Game/Characters/Enemies/Marksman/ABP_Enemy_Marksman` (parent `UMarksmanAnimInstance`, graph `UOperativeAnimGraphLibrary::BuildMarksmanLocomotionGraph`: still pose per stance x aim crossfaded by bIsProne / bIsCrouched / bIsAiming 0.25 s, walk / run, upper-body + full-body slots; the enemy graph tail is shared, `FinishEnemyGraph`) and `BP_Enemy_Marksman` (`ACodexTacticsGameMode::EnemyClasses`). Clips: monster idles (anim_idle_1 = aim hold), walk, run, four scoped shots, death; crouch idle / aim / upper-body fire from RifleAnims; prone idle / aim / fire / hit / death and stand / crouch <-> prone transitions from the crawl pack. No standing hit clip exists in any pack (empty). `UEnemyAnimInstance::NotifyAttack` is virtual; `AMarksmanEnemyCharacter::Fire` plays the stance's fire clip. MarksmanSmoke checks the art Blueprint, the transition, the prone pelvis height and the fire clip; `CodexTactics.MarksmanShot` (rendered) shoots three marksmen standing / crouching / prone |
| (this) | **Marksman enemy** (UE-only archetype, Gemini's spec in TANDEM request 3 — no Godot reference): `EEnemyArchetype::Marksman`, `AMarksmanEnemyCharacter` (overrides the now-virtual `AEnemyCharacter::TickBehavior`; `UWaveSubsystem::SpawnEnemy` uses it for the type, no art Blueprint yet = placeholder body), tunables `FMarksmanConfig` on the class, pure `MarksmanAIRules` (ShouldRetreat / ShouldFlank / EvaluateBestStance / ChooseMove / ComputeFlankDestination / ComputeSniperHitChance; tests CodexTactics.Marksman.*). Patrols `PatrolRoute` (relative waypoints) until an operative is in sight within 45 m (no route = hunts at once); holds 20-35 m, backs off under 20 m, retreats at a sprint under 12 m, closes in without a line of fire; a target in hard cover for 4 s -> flank point 45-90 deg off its facing; holding: prone on open / high ground (+35 % accuracy, capsule 1/3 with a narrower radius), crouched behind low cover; 2 s telegraphed aim with a beam (engine cylinder, `/Game/VFX/Materials/M_SniperScope_Beam` with scalar AimProgress once Gemini adds it) that breaks without a line of fire, then 45 damage (crit 25 % x2), hit chance by stances / cover / range; a hit from afar drops him prone (ambush, alerts marksmen within 15 m), then he relocates on a flank. Affinities / armour 0.15 / 14 EXP are provisional. MarksmanSmoke |
| (this) | Enemy hit reactions while running (TANDEM request 1): ABP_Enemy_Hound / Frostbitten / Cutter / Brute regenerated (`BuildEnemyLocomotionGraph` with an UpperBody slot over the cached locomotion, LayeredBoneBlend from spine_01, the hound from bip001-neck; generated node count 18) — `UEnemyAnimInstance::bUpperBodyHitReactions` plays the hit on `UpperBodySlot` while moving, full body when standing; no more sliding full-body hits. EnemyHitLayerSmoke (cutter: no hit clip by Godot design; the brute's hit clips are empty in the user's ABP) |
| (this) | Tactical pause abuse (TANDEM request 2): the pause cooldown no longer runs while in turn-based (`FGameFlowStateMachine::Tick`), so charges and the cooldown timer are exactly as before the fight; entering turn-based from the pause stays rejected. Test GameFlow.TurnBased.KeepsPauseChargesAndCooldown |
| (this) | Trembling / spinning bodies and jerky formation turns (user video UE_AnimBugs_01): operatives and enemies no longer use the engine's `bOrientRotationToMovement` (constant 802 / 360 deg/s chasing every wobble of the velocity while braking at the goal or in crowd separation); `FacingRules` turns them like Godot (_safe_look_at / _smooth_look_at: lerp_angle(turn_speed * delta) — operatives the stance turn speed, enemies turn_speed 8) towards the smoothed velocity above 0.35 / 0.2 m/s and hold below it; enemy explicit turns (attack lock, aiming, pounce) go through `AEnemyCharacter::FaceYaw` and keep the movement facing out of that frame (Tick = TickBehavior + UpdateMovementFacing); parked followers align with the leader every frame (`SetIdleFacingYaw`; it stepped only with the 0.2 s formation repath = the jerky 'hold' turns); the operative blend-space direction is smoothed and held below 0.3 m/s; enemies switch idle / walk with hysteresis (25 / 8 cm/s); BeginPlay forces the flag off for older Blueprints. FacingSmoke samples the yaw every 0.05 s (walk + formation, four crowding hounds) |
| (this) | Squad control playtest (user decisions 2026-10-01). Stances: **deviation** — every operative keeps the stance he was given; Z / C / V and the stance slot change the selected operative only, the followers no longer copy the moving leader's stance (Godot _sync_squad_stances / can_sync_stance removed with `USquadSubsystem::SyncSquadStance` / `bHasCustomStance`); only Alt + Z / C / V (`SetEntireSquadStance`) sets the whole squad. Getting up from prone needs a full stop: `SetStance` from prone while crawling stops the operative first; a sprint order (double click) to a prone operative stands him up in place and the run starts after the clip (`OrderMoveTo`, `GetStanceChangeDelay`, `PendingMoveTimer`; moves ordered during a stance clip wait for it, `UOperativeAnimInstance::GetStanceTransitionTimeLeft`). Group orders: with a group selected, a click next to an operative (the 1.2 m proximity pick) is the group's move order — only a click on his body picks him (box + pause + click verified with rendering too). ActionBarSmoke, StanceSmoke, GroupSelectSmoke; PanicComponent helpers renamed (unity-build clash with OperativeCharacter.cpp) |
| (this) | Panic (Godot Scripts/components/panic_component.gd + player.gd _process_panic_movement; **Godot ships it disabled — on in Unreal by the user decision 2026-10-01**): `PanicRules` (+ PanicRulesTest) and `UPanicComponent` on every operative — stress from heavy wounds / cold / reserve ammo / monsters within reach, a jolt per hit (`TakeHit`) and when the clip runs dry, fortitude cut, calming without threats and fast by a heat source; at 100 the operative panics unless the squad limit is reached (holds on at 95) or, as the leader, while others panic (90); rage blocks it. Panicking: no shooting / reloading (`CanShoot`, `ProcessCombatShooting`), orders and Ctrl-targets refused («⚠️ В ПАНИКЕ! НЕ ПОДЧИНЯЕТСЯ!»), followers leave the formation; runs from the enemies (flee speed, pull to heat, kept within MaxFleeRadius of the leader) for FleeDistance / FleeMaxTime, then cowers crouched; recovers when the panic time is out and the enemies are far (or 2 s later), after HeatSourcePanicBreakTime of warmth, faster next to a calm leader; real-time fight only (entering turn-based ends it). Name-plate badge «ПАНИКА! [ОТБЕГАЕТ / СЖАЛСЯ В СТРАХЕ]» / «СТРЕСС: n%», red ring, radio lines, event bus OnSoldierPanicked / OnSoldierCalmed. **Data (user decision):** `Scripts/Editor/tune_panic_balance.py` → DA_GameBalanceConfig: one panicking member at a time (panic_max_panicked_members 2 → 1), the core squad far more resistant (stress gain commander 0.8 → 0.3, engineer 1.1 → 0.4, medic 0.9 → 0.35, faster recovery, wound / cold / monster triggers later), the recruit Susanin ordinary (0.1 → 1.0). PanicSmoke |
| (this) | Turn-based cursor and keys (user playtest): the hovered cell gets Godot's cursor frame (tactical_grid_overlay.gd set_hovered_cell / _rebuild_cursor_mesh — yellow frame, red frame + corner brackets over an enemy; `ATurnGridOverlayActor::SetCursorCell`, layers CursorMove / CursorEnemy; the controller feeds the hovered point every frame of the fight, a hit on a unit's body means its grid cell; hidden outside the squad phase; the Godot arrow above the cell is not ported). Number keys: real key events select fine (TurnSelectSmoke presses 2 / 3 / 4 / 1 through Enhanced Input, Susanin as 4) — the HUD buttons kept the keyboard focus after a click and swallowed the keys, so they are non-focusable now (`CodexButtonFocus::Disable`, action bar / turn-based panel / inventory / action menu / banners / victory panel). `smoke.ps1 -TimeoutSeconds` (300) kills a game that never exits (missing command / stale binaries) |
| (this) | **Deviation** (user decision 2026-10-01, not in Godot): sprinting warms the operative up — while actually running in a sprint the cold drops by `FColdConfig::SprintWarmupRate` (1.5 %/s x fortitude warm-up boost) instead of accumulating (`FColdEnvironment::bSprinting`, `ColdRules::StepCold`). The sprint itself still needs cold < max_cold_to_sprint (60 %), no heavy wound, not prone. ColdRulesTest SprintWarmsUp, ColdSmoke sprint stage |
| (this) | Roles with Gemini (user decision 2026-10-01, replaces the architect / implementer split): Claude = gameplay code, logic, AI, UI, animation, data, tests; Gemini = optimization, shaders / materials, VFX, rendering settings, profiling. Ownership table, «Requests» flow and asset claims in TANDEM.md; GEMINI.md rewritten; shared build lock `Scripts/agent_lock.ps1` (`Saved/agent.lock`, `$env:CODEX_AGENT`) taken by build / test / smoke / verify_all (children inherit it; a dead holder is stale), build.ps1 refuses while the CodexTactics editor is open |
| (this) | Playtest fixes (user 2026-10-01): own stances (Godot has_custom_stance / _sync_squad_stances / can_sync_stance): `AOperativeCharacter::bHasCustomStance` set by Z / C / V and the stance slot, `USquadSubsystem::SyncSquadStance` (no sync in the preparation / solo, skips guarding and own-stance members), followers copy the moving leader's stance only without an own stance and outside the preparation, Alt + Z / C / V clears it; action bar slot 4 (the recruit) had no click handler (`HandleSlot3`); turn-based enemy bite plays the attack clip (Godot play_tactical_attack) with tactical_enemy_hit_delay / _attack_duration / _retreat_delay timing and force-idle after it. ActionBarSmoke / TurnBasedSmoke check them |
| (this) | Fix (user playtest: dead enemies got back up into the idle before vanishing): death clips are held on the running montage instance (`FAnimMontageInstance::bEnableAutoBlendOut = false` via `GetActiveInstanceForMontage`) — the instance copies the flag from the montage at the start, so setting it on the dynamic montage afterwards did nothing; same fix for the operative death. Bodies still stay DeathDecayDelay (Godot death_decay_delay 5 s). New EnemyDeathSmoke (hound / frostbitten / cutter / brute held until removed, shots on the body) |
| (this) | Playtest fixes (user 2026-10-01). Turn-based, **deviation** (Godot crawls cell to cell at one step time): a prone operative ordered to walk / deploy / push first rises to crouching for free (`UTurnBasedCombatSubsystem::PrepareSquadWalk`, the turn state stance follows, the walk waits for the ProneToCrouch clip via `StartMoverAfter`), crouched steps take tactical_step_duration / (crouch / walk speed ratio) (0.52 -> 0.92 s). Real time (Godot player.gd combat_facing_direction / _face_movement_target / _process_combat_shooting): `AOperativeCharacter::UpdateCombatFacing` keeps a non-sprinting operative facing its live combat target (bOrientRotationToMovement off -> sideways / backwards walk) with the barrel, not the chest, on it (`BarrelYawOffset` from the weapon mesh in the aim pose, `bAlignBarrelWithTarget`); no aim pose while sprinting (it fast-forwarded the 1.2 m/s aim walk); ABP_Operative regenerated: the aim blend space only drives the upper body over the standing legs; tracers leave `GetWeaponMuzzleLocation` (weapon "Muzzle" socket or `MuzzleOffset` (0,0,98) on m16_01), the line of fire keeps Godot's stance heights. New CombatMoveSmoke; TurnWalkSmoke checks the prone order |
| (this) | Fix (user playtest: the prone operative stood up after the lying-down clip and between prone shots): `FillDirectionalBlendSpace` now calls `UBlendSpace::ResampleData` — `PostEditChange()` without a property does not rebuild the runtime triangulation, so the script-made BS_Rifle_Prone / _Aim played the reference pose (pelvis 105 cm); both recreated. StanceSmoke now measures the pelvis height (bones refreshed headless) through a prone burst, the aim hold and the idle (must stay < 40 cm) |
| (this) | Prone animation on the user's Crawl_MocapAnimPack (Godot locomotion_controller.gd ProneStart / ProneIdle / ProneEnd / ProneFire, hit_prone): the pack is the UE4 mannequin rig (68 identical bones), so its three skeletons are made compatible with the survivor skeleton (no retarget; the user's RTGT/_Crawl_Death01 targets UE5 Manny and is unused); `BS_Rifle_Prone` / `BS_Rifle_Prone_Aim` (Characters/Operatives/Anims; rifle idle / aim idle + the 8 in-place _IPC crawl clips at 21 cm/s, `UOperativeAnimGraphLibrary::FillDirectionalBlendSpace`); ABP_Operative regenerated with a prone aim switch and its own speed axis (`ProneBlendSpeed` / `PronePlayRate`, `ProneBlendSpaceMaxSpeed`); stance transitions on the FullBody slot (`UpdateStanceTransition`: Crawl_from_Act / to_Act / from_Cr / to_Cr; stand <-> crouch slots empty), prone fire (Crawl_Rifle_Shoot_Light), hit (Crawl_Hit_F) and death (Crawl_Death01) full body, prone reload only with `ReloadProneAnimation` (none in the pack); `DeathStartOffset` only for standing deaths. Crawl_from_Walk / from_Run carry 3 m of root motion and are not used. StanceSmoke checks the clips |
| (this) | Game balance editable in Unreal (user decision 2026-10-01: Unreal is the master copy): `UGameBalanceConfig` generated from Godot game_balance_config.gd by `Scripts/generate_balance_config.py` (364 typed fields, Godot names, 47 categories = Godot groups, tooltips, range sliders), `UGodotBalanceAsset::GetNumber` / `SetNumber` / `HasField` by reflection; DA_Balance / DA_GameBalanceConfig recreated as UGameBalanceConfig from Godot; `import_balance.py` now reports differences only (`CODEX_BALANCE_REIMPORT=1` overwrites); rule tests on `GodotBalanceFixture.h` |
| (this) | Operative one-shot animations (Godot locomotion_controller.gd play_hit_reaction / play_grenade_throw / play_death / play_action_animation "working_device"): `UOperativeAnimInstance` listens to the health component (hit on the upper-body slot per stance, pistol variant; death on the new full-body slot `FullBody`, held on the last frame, `DeathStartOffset`) and `AOperativeCharacter::OnGrenadeThrowNative` (walk / run / crouch / prone throw clips, sets `GrenadeThrowDuration` from the clip), `PlayWorkingDevice(Seconds)` called by `UTurnBasedCombatSubsystem::DeployObject`; ABP_Operative graph regenerated with the FullBody slot (the setup script regenerates only untouched generated graphs: node counts 41 / 43). Clips are empty: the only candidates (Characters/Mannequins MM_HitReact / MM_Death) are UE5 Manny anims and need an IK retarget onto the UE4 survivor skeleton — the user assigns them in ABP_Operative Class Defaults |
| (this) | Turn-based fixes from the user's playtest: `UGorkyGridManager::FindPath` returns the steps without the start cell like Godot find_path (the start cell made every walk begin with an empty step — half a second of walking on the spot, then a snap turn — and cost enemies 1 AP); one continuous speed profile over the whole path (`SampleWalkProfile`, Godot total time) with the anim speed = the body's speed and smooth turns (TurnWalkSmoke, WalkProfile test); encounter selection like Godot TacticalEncounterSelector (15 m, cap 6, one per species first; `TacticalEncounterRules` + test) with idle variation per type; crash fix — a blast / bite that kills an operative ends the mission and the fight mid-call, every such path now stops when the fight is over (TurnBlastDeathSmoke); enemies go round fire / heat zones (`EnemyAIRules::FireDetourWaypoint` + test, fear hysteresis 1 m) instead of shaking at the edge — **deviation** (user request: Godot only flees) |
| (this) | Parity pass items: turn-based barrel / mine blasts by Godot's rules (`ApplyBlast`), fallback waves (`FallbackWaveRules` + test), cold animation layer (`ColdAnimationRules` + test, `UOperativeAnimInstance` cold state, cold blend in ABP_Operative), box selection and group orders (`SelectInBox`, `SetSelectedGroup`, `OrderGroupMove`, `SquadFormation::ComputeGroupTargets` + test, selection rings, HUD box; GroupSelectSmoke), exposed-zone / warning pulses, barricade glide and set-up grow-in, `M_GhostHologram`; enemy attack lock (Godot is_attacking) and turn-based idle reset; SquadFireSmoke keeps the frozen hound's feet level with the commander (the line over the barricade was 0.1 cm from the top) |
| (this) | Turn-based walk animation (Godot turn_based_combat_manager.gd start_tactical_walk before the path tween, per-step easing: first step ease-in, middle linear, last ease-out): `UTurnBasedCombatSubsystem::GetTacticalMoveSpeed` feeds both anim instances a constant speed over the whole path (the actor is moved directly, so it has no velocity), steps eased like Godot instead of a smoothstep per cell |
| (this) | Operative and enemy animation on the user's art: `UOperativeAnimGraphLibrary` (editor module; builds the operative and enemy AnimGraphs), ABP_Operative on RifleAnims, per-member outfits (`FOperativeOutfit`), `UEnemyAnimInstance` clips / one-shots, four enemy Blueprints, `EnemyClasses` per type (setup_operative_rifle_animation.py, setup_enemy_animation.py) |
| (this) | Turn-based attack mode (Godot is_attack_mode, tactical_grid_overlay.gd update_attack_pattern + hit_chance_label, main.gd KEY_F / RMB / Esc): `EnterAttackMode` / `ExitAttackMode` / `ToggleAttackMode`, `M_WeaponMatrixDots` with the distance falloff, HUD `DrawHitChanceLabel`, action-bar «ХОД»; grenade blast zone `M_AoeBlast`; enemy target fresnel `M_TargetFresnel`; AttackModeSmoke |
| `2671ff1` | Frost vignette (Godot UI/FrostOverlay + frost_vignette.gdshader): `M_FrostVignette` (create_frost_vignette_material.py: UI material, the Godot math in a Custom node returning edge / alpha, colour and opacity as nodes) on `UFrostVignetteWidget` (full-screen UMG image, Z 1) fed by `ACodexTacticsHUD::UpdateFrostVignette` with the coldest operative; ColdSmoke checks it, `HudShot frost`. Fix: M_Silhouette and M_TacticalStasis had an unconnected Clamp input (default material in game) — scripts fixed, assets regenerated; test `CodexTactics.Editor.Materials.VfxGraphsConnected` |
| `0d91711` | Editor module `CodexTacticsEditor` with `UBlueprintGraphToolset` (FindBlueprints, DescribeBlueprint, DumpBlueprintGraph, Export / ImportGraphNodesText, CompileBlueprint) registered in the ToolsetRegistry, served by the Unreal MCP plugin; test `CodexTactics.Editor.BlueprintTools.ReadProjectBlueprints`. `7aa7deb`: UE 5.8 agent skills in .claude/skills + .agents/ue-project-context.md |
| `8d64c5a` | See-through silhouette (Godot player.gd _check_silhouette_occlusion + silhouette.gdshader): `M_Silhouette` (Scripts/Editor/create_silhouette_material.py, unlit translucent, no depth test, fresnel alpha), `AOperativeCharacter::UpdateSilhouette` (20 Hz camera ray, overlay material, role colours); SilhouetteSmoke |
| `b800bde` | Stage loadout (Godot main.gd _apply_stage_exploration_resources): `LoadoutRules` + LoadoutRulesTest, level JSON squad_loadout imported into `FLevelCombatConfig::SquadLoadout` (import_levels.py, DA_Level_* re-imported), `UMissionSubsystem::ApplyStageLoadout` at the end of the cutscene; VictorySmoke checks it |
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
