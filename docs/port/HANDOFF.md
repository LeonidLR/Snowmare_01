# HANDOFF — CodexTactics (UE 5.8 port of Operation: Cold Silence)

**Purpose.** Any agent (Claude, Gemini, …) must be able to pick up the port from this file alone.
Keep it current: every commit that adds / changes a system updates §5 (system map), §8 (next steps) and §10 (log).

Last update: 2026-09-28 by Claude, after commit `d72d131`.

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
| Default canvas / Slate fonts have no emoji | HUD / menu strip them (`ACodexTacticsHUD::StripUnsupportedGlyphs`); texts stay verbatim Godot with emoji in code. |
| Bash heredocs with long / complex Python sometimes break in this harness | Write the Python to the scratchpad with the file tool and run `python <file>`. |

## 3. Build, test, verify

```
powershell -ExecutionPolicy Bypass -File Scripts/verify_all.ps1              # build + all tests + all smokes, summary
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 [-Filter CodexTactics.Cold]
powershell -ExecutionPolicy Bypass -File Scripts/smoke.ps1 -Command CodexTactics.DeployableSmoke
```

State at last update: **103 automation tests, 11 smokes, all PASS** (`verify_all.ps1` → ALL GREEN).

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
| `LootSmoke` | crate opens without a menu → loot dialog, one stack + «Забрать ВСЁ», empty crate line, trapped crate defusal + deployables, detonation burns the loot |
| `HudShot [close] [walk] [menu] [place]` | rendered screenshot `Saved/Screenshots/WindowsEditor/HudShot.png` (needs rendering, run UnrealEditor.exe -game with `-ExecCmds="CodexTactics.HudShot close"`) |
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
- `Quests/QuestChain`, `QuestSubsystem`, `Interactables/GateActor`. Godot `quest_manager.gd`, `gate.gd`.

**UI** — `UI/CodexTacticsHUD` (canvas: message feed, squad panel with supply, labels; owns the action menu widget),
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

## 8. Next steps (in order)

1. ~~Loot crates~~ — done (see §10).
2. **Turrets** — Godot `deployables/turret.gd`: power from the generator, targeting enemies, repair (engineer 2 s /
   others 4 s), menus (broken / unpowered / trapped / pick-up). Add `TurretClass` to the game mode, deploy branch
   already routes `EDeployableType::Turret`.
3. Generator damage / repair menu (Godot `interactable.gd breakdown_generator`, `repair_generator`).
4. Ctrl + click targeted shots (barrel explode, mine / trap detonation by shot) — Godot `main.gd` Ctrl branch,
   `player.gd shoot_at_barrel_object`, `shoot_at_mine_object`, `shoot_at_trapped_object`.
5. UI shell from REFERENCE_PLAYTHROUGH: main menu, dialogue, objective banner, bottom action bar, mission failed.
6. Turn-based combat manager on the Gorky grid (Godot `Scripts/tactics/turn_based_combat_manager.gd`).
7. Phase 2 data importer (JSON / .tres → DataAssets) replacing hand-typed values (§9).
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

## 10. Change log (newest first)

| Commit | What |
|---|---|
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
