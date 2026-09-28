# HANDOFF — CodexTactics (UE 5.8 port of Operation: Cold Silence)

**Purpose.** Any agent (Claude, Gemini, …) must be able to pick up the port from this file alone.
Keep it current: every commit that adds / changes a system updates §5 (system map), §8 (next steps) and §10 (log).

Last update: 2026-09-29 by Claude, after the balance import commit (see §10).

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
| The level `L_MovementTest` PlayerStart was moved in the editor to (-180, -1490) | Layout-dependent smokes call `SmokeUtils::PlaceSquadAtTestStart` (squad at the origin, as the map script authored). Do not move the PlayerStart back without asking the user. |
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

State at last update: **115 automation tests, 19 smokes, all PASS** (`verify_all.ps1` → ALL GREEN; it also fails on an engine crash during the tests now).

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
| `TurnBasedSmoke` | wave + one brute, enter turn-based: grid registration, 8 AP, stance 1 AP, enemy turn (walk, bite 13 on a crouched commander, step back), move into a fire lane + shot -> victory -> tactical pause |
| `MissionSmoke` | objective banner texts (start → preparation → wave), an operative's death fails the mission (GameOver, reason, time stop), restart reloads a fresh exploration |
| `TurretSmoke` | turret shoots an enemy, generator breakdown unpowers / repair powers, broken turret repaired by the engineer, pick-up, F set-up |
| `LootSmoke` | crate opens without a menu → loot dialog, one stack + «Забрать ВСЁ», empty crate line, trapped crate defusal + deployables, detonation burns the loot |
| `HudShot [close] [walk] [menu] [place] [shoot] [failed] [mainmenu] [dialogue] [cutscene] [prep] [turnbased]` (`dialogue`: intro briefing window; `shoot`: slowed-down barrel shot = tracer, target flash, plan marker; `failed`: mission-failed screen; `mainmenu`: needs `-ForceMainMenu`) | rendered screenshot `Saved/Screenshots/WindowsEditor/HudShot.png` (needs rendering, run UnrealEditor.exe -game with `-ExecCmds="CodexTactics.HudShot close"`) |
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
across level reloads). `UMissionSubsystem::StartMission(Game | Combat | Exploration)`: Game / Exploration set the
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

- PlayerStart moved to (-180, -1490): intended? (left as is, committed in `11d7655`).
- `Config/DefaultEditor.ini` has local editor changes — never commit it unless asked.
- «Начать исследование»: REFERENCE_PLAYTHROUGH says «exploration only … no combat», but Godot code only changes the
  objective / radio line (the gate still starts combat). UE follows the Godot code — confirm with the user.
- «Начать бой» position: Godot hard-codes the yard behind the gate; UE uses the `CombatStart` tag (test map: (0, −2150)).
- Turn-based deviations: an operative that survives a mine ends its turn once (Godot schedules end_current_unit_turn
  twice — from the move and from the blast — which skips the next operative; treated as a bug). Squad starts facing the
  enemies' centroid (Godot hard-codes SOUTH = towards the enemies on its map). The attack line names the arc
  (фронт / фланг / тыл; Godot prints the enum number). Trapped-barricade retaliation, frozen-enemy stasis look,
  enemy idle variations not ported.
- Godot `_apply_stage_exploration_resources` (combat loadout by start mode: collected vs starting set) is not ported.
- `FString::ToLower` / `Contains(IgnoreCase)` do not fold Cyrillic: use `FText::ToLower` (see DialogueRules).
- The world is paused while the start menu is open (Godot keeps processing behind its menu) — cosmetic difference.

## 8. Next steps (in order)

1. ~~Loot crates~~ — done (see §10).
2. ~~Turrets~~, 3. ~~generator damage / repair~~ — done (see §10). Enemies do not attack turrets / barricades /
   the generator yet (Gemini's enemy AI targets operatives only) — add with the enemy AI pass.
4. ~~Ctrl + click targeted shots~~ — done (see §10).
5. UI shell from REFERENCE_PLAYTHROUGH: ~~objective banner, mission failed + Ctrl + X, start menu~~ (done, §10);
   ~~dialogue window, bottom action bar, banners, cutscene card~~ (done); remaining UI: inventory drawer, transfer,
   guard, weapon selector, pause menu / save-load, radius rings (action bar slots are placeholders).
6. Turn-based combat manager on the Gorky grid (Godot `Scripts/tactics/turn_based_combat_manager.gd`).
   Done: `GorkyLineOfSight`, `TurnBasedRules`, `UTurnBasedCombatSubsystem` + overlay + controller input (see §4).
   Next (panel done): grid deployables /
   barricade relocation, exposed zones + reinforcements, weapon switching / grenades, companion drone, stasis look.
7. Phase 2 data importer (JSON / .tres → DataAssets) replacing hand-typed values (§9).
   Done: weapons — `Scripts/Editor/import_weapons.py` → `/Game/Data/Weapons/DA_Weapon_<id>` (all 9; defaults parsed
   from weapon_data.gd, enums mapped by name, `EStatusEffect::Shocked` appended); the game mode equips
   `StartingWeapon` (DA_Weapon_m16) with `StartingReserveAmmo` 60 (Godot _init_weapons) → HUD «[30/60]» like the video.
   Balance — `Scripts/Editor/import_balance.py` → `/Game/Data/Balance/DA_Balance` (balance.tres) and
   `DA_GameBalanceConfig` (game_balance_config.tres): `UGodotBalanceAsset::Numbers` holds every numeric / bool export
   by its Godot name (362; .gd defaults for fields a .tres omits). Wired so far: turn-based combat
   (`TurnBasedRules::BalanceFromGodot(DA_Balance)` + step durations, loaded at combat start via the game mode's
   `TurnBasedBalance`). Next: wire the other hand-typed values to `GameBalanceConfig` (operative health / fortitude /
   speeds, cold, enemies, deployables, camera — look each Godot consumer up to pick the right file), then enemies
   (`Scenes/movements/enemy_*.gd` exports) and levels (`data/configs/levels/*.json`).
   Source trap: Godot has TWO GameBalanceConfig files with different values. The turn-based manager loads
   `resources/balance.tres` first (squad 8 AP, enemy 6 AP — the UE `FTurnBasedBalance` defaults), while camera, enemies,
   turrets, mines, barricades load `resources/game_balance_config.tres` (e.g. tactical_squad_max_ap = 3, enemy 4,
   turret_shot_delay 1.0). Import both and wire each consumer to the file its Godot counterpart loads. Weapons:
   `resources/weapons/*.tres` → `UWeaponDataAsset` (same field names). Levels: `data/configs/levels/*.json`.
8. Content: level, VFX, cutscene; character "twisted" look issue (§6).

## 9. Known gaps / tech debt

- Balance values are hand-typed in C++ (CLAUDE.md wants a Phase 2 importer). Examples: operative max health is 100
  for all (Godot 130 / 150 / 120 per role), weapon / enemy stats (Gemini).
- Deployable overhead labels (Godot Label3D «🧱 Баррикада: HP»), floating combat texts, mine "ВЗВЕДЕНА" text: not ported.
- Barricade contact damage (spikes / fire / cryo / energy) not ported (Godot default is NONE).
- Pushing / defusal / set-up animations: none (operatives only slow down / crouch).
- Hidden mines are revealed by a distance scan in the mine's Tick (Godot scans from each operative) — same result.
- Relocation ghost is opaque (swap `GhostBaseMaterial` for a translucent hologram).
- Godot starts relocation directly when a relocatable object is clicked in the tactical pause / live combat
  (`main.gd` click branch "is_relocatable_obj"); UE always goes through the action menu. Port with the pause UI.
- Consumables (medkits, food) are collected but cannot be used yet (inventory drawer / use_squad_item not ported).
- The squad panel does not list provisions / extra ammo yet (inventory drawer UI).
- Deployable overhead labels (turret / generator HP and state texts) not ported (Godot Label3D).
- Targeted shots: crouching behind a barricade does not apply Godot's 0.8 cover to the priority target (UE
  `ShootAtTarget` has no cover factor yet); rage / panic refusal not ported (no rage / panic components). The barrel
  line «💥 Прицельный выстрел…» is posted only when the shot actually fires (Godot posts it even when frozen).
- **Deliberate deviation (user decision 2026-09-28):** a shot at an untrapped supply crate only posts «💥 Пуля пробила
  ящик снабжения.» — Godot also detonates it (bug: `detonate_trap` always exists on loot_crate.gd).
- Tracer muzzle = feet + stance height (1.4 / 0.85 / 0.25 m), not a weapon socket; light intensity mapping
  (`FeedbackLightPerEnergy` 1500 per Godot light_energy) is a first guess for the user to tune.

## 10. Change log (newest first)

| Commit | What |
|---|---|
| (balance import commit) | Both Godot balance files imported into data assets; turn-based combat reads DA_Balance |
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
