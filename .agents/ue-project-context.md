# UE Project Context

*Engine: UE 5.8 · Last updated: 2026-09-30*

## Engine & Project
**Engine version:** 5.8.2 — launcher build at `C:/Program Files/Epic Games/UE_5.8`
**Project name:** CodexTactics («Operation: Cold Silence») · **Type:** game · **Genre:** tactical arctic survival RPG (Gorky 17 tribute), real-time squad play + turn-based grid combat
**Description:** C++ port of a Godot 4.7 game (`C:/Users/Zephyrus15Duo/Documents/Codex/godot-test-01`, read-only reference) with identical behaviour.
**Target platforms:** Windows (Win64) [others unknown]

## Modules
**Primary game module:** CodexTactics

| Module | Host type | Public deps | Private deps | Notes |
|---|---|---|---|---|
| CodexTactics | Runtime | Core, CoreUObject, Engine, InputCore, EnhancedInput, GameplayTags, AIModule, GameplayTasks, NavigationSystem, Niagara, UMG, Slate, SlateCore, CommonUI, CommonInput, DeveloperSettings | Json | All gameplay code; `Public/` + `Private/` grouped by system (Core, Combat, Survival, Characters, Data, Subsystems, Camera, Tactics, Interactables, Quests, UI (+ UI/Frontend: CommonUI frontend), Debug) |
| CodexTacticsTests | DeveloperTool | — | Core, CoreUObject, Engine, Json, UMG, CommonUI, GameplayTags, CodexTactics | Automation tests `CodexTactics.<System>.<Feature>.<Case>` |
| CodexTacticsEditor | Editor (PostEngineInit) | — | Core, CoreUObject, Engine, UnrealEd, BlueprintGraph, Kismet, AssetRegistry, ToolsetRegistry, AnimGraph, AnimGraphRuntime, UMG, UMGEditor, CommonUI, CodexTactics | `UBlueprintGraphToolset` — Blueprint / AnimBP graph tools served over the Unreal MCP plugin; `UOperativeAnimGraphLibrary`; `UFrontendWidgetGenerator` (placeholder frontend WBP trees) |

## Plugins
| Plugin | Maturity | Used for |
|---|---|---|
| EnhancedInput | stable | all input (actions + mapping context created in C++) |
| Niagara | stable | VFX (baseline; user polishes) |
| ModularGameplay | stable | enabled for later use |
| StateTree, GameplayStateTree | stable | enabled; enemy AI is plain C++ (Godot parity) |
| ProceduralMeshComponent | stable | enabled |
| CommonUI (+ CommonInput) | stable | frontend / pause menu framework (2026-10-08): activatable screens on layer stacks, `CommonGameViewportClient`, input data `/Game/UI/Frontend/Input`; `bEnableDefaultInputConfig=False` |
| PythonScriptPlugin, EditorScriptingUtilities | stable (editor) | asset / import scripts in `Scripts/Editor/*.py` |
| ModelingToolsEditorMode | stable (editor) | editor tooling |
| ModelContextProtocol (Unreal MCP), ToolsetRegistry, EditorToolset, LiveCodingToolset, AutomationTestToolset, UMGToolSet, NiagaraToolsets, ConfigSettingsToolset, AIModuleToolset, StateTreeToolset | Experimental (editor) | AI agent access to the open editor at `http://127.0.0.1:8000/mcp` (auto-start on) |

**In-house plugins:** none · **Fab / marketplace:** Ultra Dynamic Sky / Weather planned for Phase 6 (not installed)

## Coding conventions
**Prefixes:** Epic standard (A/U/F/E/I/T)
**Object references:** `TObjectPtr` in UPROPERTY; `TWeakObjectPtr` for non-owning cross-actor links
**API macro style:** `CODEXTACTICS_API` per declaration
**Log categories:** `LogCodexTactics` — all gameplay logging
**Assertions:** [unknown — code prefers null guards; tests / smokes check behaviour]
**Header layout:** Public/Private per module, system subfolders; each class header names its Godot reference file
**Enforced rules:**
- **No GAS.** Plain C++ components and subsystems.
- Balance values only via importers from the Godot data (`Scripts/Editor/import_*.py` → DataAssets); never hand-typed.
- Pure-logic systems live in `*Rules` namespaces with parity tests; every system gets a headless smoke (`Private/Debug/*SmokeCommand.cpp`).
- Doc comments on public API; IWYU; unity-build-safe names in anonymous namespaces (prefix them per file).
- `FString::ToUpper/ToLower` do not fold Cyrillic — use `FText::FromString(x).ToUpper()/ToLower()`.

## Gameplay framework
| Role | Class |
|---|---|
| GameMode | `ACodexTacticsGameMode` (spawns the squad, level config, balance assets) |
| GameState | `ACodexTacticsGameState` |
| PlayerController | `ACodexTacticsPlayerController` |
| PlayerState | [none custom] |
| Pawn / Character | `ATacticalCameraPawn` (player pawn = camera); squad `AOperativeCharacter` (BP_Operative), enemies `AEnemyCharacter` |
| GameInstance | engine default |

**Subsystems:** `UCodexEventBus`, `UMissionSessionSubsystem` (UGameInstanceSubsystem); world subsystems: GameFlow, Mission, Squad, Wave, WaveVictory, TurnBasedCombat, Interaction, Relocation, RadiusRing, Grenade, CombatFeedback, Quest, Dialogue, GameMessage, FloatingText, Recruit, SquadTransfer, SaveGame.

## GAS
**Enabled:** no (project rule)

## Networking
**Model:** single-player

## Input
**Stack:** EnhancedInput — input actions and one mapping context built in C++ by `ACodexTacticsPlayerController::CreateInputActions`; mouse wheel / a few keys via `BindKey`.

## UI
**Stack:** UMG widgets built in C++ (Widget Blueprint subclasses may restyle by widget names) + `ACodexTacticsHUD` canvas drawing (feed, squad panel, overhead labels, floating texts)

## AI
**Stack:** custom C++ (`AEnemyCharacter` + `EnemyAIRules`, Godot parity); turn-based grid AI in `UTurnBasedCombatSubsystem`
**Navigation:** NavMesh (RecastNavMesh created at map load, dynamic)

## Streaming
**Model:** single persistent level (`/Game/Maps/L_MovementTest`); Stage 01 level planned

## Saves
**SaveGame:** `USaveGameSubsystem` — JSON files (Godot save_manager.gd format), slots autosave / quicksave / manual

## Build
**Targets:** Game (`CodexTactics.Target.cs`), Editor (`CodexTacticsEditor.Target.cs`)
**Scripts:** `Scripts/build.ps1`, `Scripts/test.ps1 [-Filter]`, `Scripts/smoke.ps1 -Command CodexTactics.X`, `Scripts/verify_all.ps1` (build + all tests + all smokes)
**Engine modifications:** none

## Team
**Size & roles:** one developer (the user: art, animation Blueprints, shaders, design decisions) + AI agents (Claude Code; Gemini on the Godot side / in tandem)
**Source control:** Git, `main` branch, Conventional Commits · **Docs:** `docs/port/HANDOFF.md` (read first), `docs/port/PORT_MATRIX.md`, `docs/port/TANDEM.md`, `production/session-state/active.md`
