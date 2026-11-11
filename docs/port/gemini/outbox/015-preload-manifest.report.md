# Level Preload Manifest Report (L_Gen_Depot_1) - Codex 004

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Branch:** `gemini/015-1-preload-manifest`  
**Target:** Codex Task 004 (Eliminate Runtime Hitches on Enemy Spawn)  
**Companion Artifact:** `docs/port/gemini/outbox/015-preload-manifest.draft.json`

---

## 1. Executive Summary

This report delivers the complete preload manifest draft for level `L_Gen_Depot_1` to support Codex's implementation of the level preload subsystem (`004-hitch-on-enemy-appear.md`).

Runtime profiling (`014-hitch-scan.report.md`, `hitch-2026-11-11-play2.log`, `hitch-2026-11-11-play3.log`) demonstrated that dynamic spawning during combat (Combat Pressure events, Wave spawns, and PMC Encounters) causes severe game-thread stalls (up to 2.34s sync loading and 3.90s asset initialization/skin compile). 

The generated manifest `015-preload-manifest.draft.json` aggregates **356 essential assets** into structured groups, enabling a single asynchronous or post-level-load warmup step before combat begins.

---

## 2. Asset Counts Breakdown

| Asset Group | Count | JSON Source / Driver | Primary Asset Types |
| :--- | :---: | :--- | :--- |
| **`environment_and_subsystems`** | 14 | `L_Gen_Depot_1.json` [environment], `WeatherPresenterActor.cpp` | Material Parameter Collections, Snow overlays, Niagara weather systems, GameBalance DataAssets |
| **`ladders`** | 3 | `ULadderClimbComponent.cpp`, `LadderRules.h` | Root-motion climb animations (`anim_ladder_climb_up_loop`, `anim_ladder_climb_out_top`, `anim_ladder_into_from_top`) |
| **`operatives_core_and_weapons`** | 11 | `L_Gen_Depot_1.json` [squad_loadout], `weapons_tuning.json` | Operative character BP, AnimBP, SK_Mannequin, Skeleton, Weapon DataAssets (M16, Pistol, Shotgun, Sniper, RPG7) and static meshes |
| **`operatives_animation_sets`** | 224 | `Content/Data/Weapons/weapon_anim_sets.json` | Weapon-specific locomotion, aim, fire, reload, aim offsets (Shotgun 50, Pistol 44, Launcher 42, Prone crawl 12, Injured 24, Stealth 81) |
| **`operatives_cover_and_misc_animations`** | 46 | `UOperativeAnimInstance.cpp` (Hardcoded cover/dive/blizzard tables) | M4 cover pack animations (36), Blizzard walk clips (8), Prone dive root-motion sequences (2) |
| **`pmc_encounter`** | 7 | `pmc_operators.json`, `L_Gen_Depot_1.json` [pmc_encounter] | PMC Skeletal Mesh (`SKM_PmcOperator`), 3 coat variant materials (`MI_PmcOperator_Grey`, `Dark`, `Snowed`), 3 PMC primary weapon meshes/DAs |
| **`enemies`** | 51 | `L_Gen_Depot_1.json` [pressure, wave_schedule, ambush] | 5 Archetypes: Hound (12 clips), Frostbitten (7 clips), MilitaryZombie (3 meshes, 13 clips), Cutter (`SK_Cutter_1slot`, 10 clips), Marksman (`SKM_base_mesh_2_ue5`, beam material, 8 clips) |
| **TOTAL** | **356** | | |

---

## 3. Runtime Sync Resolution Analysis (File & Line Census)

Today, assets are resolved and loaded synchronously right as characters or events are spawned on the game thread. The table below documents the exact locations in source code responsible for these runtime stalls:

### 3.1. Ladders: Instance-Level Sync Loading
* **Location:** `Source/CodexTactics/Private/Ladders/LadderClimbComponent.cpp:208-240` (`ULadderClimbComponent::LoadClips()`)
* **Trigger:** Called during `CanStartClimb` or when an operative/enemy pathfinds near a ladder obstacle.
* **Mechanism:**
  ```cpp
  LoadedUpLoop = UpLoopClip.LoadSynchronous();
  LoadedClimbOut = ClimbOutClip.LoadSynchronous();
  LoadedIntoFromTop = IntoFromTopClip.LoadSynchronous();
  ```
* **Issue:** `bClipsLoaded` is an **instance** member of `ULadderClimbComponent`, NOT static or subsystem-managed. Every newly spawned enemy actor instantiates a new component and re-executes `LoadSynchronous()`. If Unreal's streaming thread is active, this calls `FlushAsyncLoading()`, blocking the main game thread.

### 3.2. Waves & Combat Pressure: Archetype & Variant Sync Loading
* **Location:** `Source/CodexTactics/Private/Combat/WaveSubsystem.cpp:447-455` (`AWaveSubsystem::SpawnWave()`)
* **Trigger:** Triggered when `CombatPressureSubsystem` or `WaveSubsystem` fires a spawn event.
* **Mechanism:**
  ```cpp
  const TSoftClassPtr<AEnemyCharacter>* VariantSoft = Variant.IsNone() ? nullptr : GameMode->EnemyVariantClasses.Find(Variant);
  UClass* VariantClass = VariantSoft ? VariantSoft->LoadSynchronous() : nullptr;
  // ...
  if (UClass* Loaded = Soft->LoadSynchronous())
  ```
* **Issue:** The Enemy Blueprint class itself is dynamically loaded synchronously right at spawn time.

### 3.3. PMC Encounters: Dynamic Mesh and Material Sync Loading
* **Location:** `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:1967-2036` (`UPmcSquadSubsystem::SpawnEncounter()`)
* **Trigger:** Triggered when the PMC timer expires (`delay_s = 30.0` in `L_Gen_Depot_1.json`).
* **Mechanism:**
  ```cpp
  if (Entry.BodyMesh.LoadSynchronous() == nullptr)
  // ...
  Operative->ApplyBodyMeshOverride(Entry.BodyMesh.Get());
  // ...
  Operative->AddArsenalWeapon(WeaponDA.LoadSynchronous());
  ```
* **Issue:** `SKM_PmcOperator` and all weapon DAs are loaded synchronously inside the spawn loop.

### 3.4. Operative Animation Sets: Dynamic Per-Skeleton / Weapon Loading
* **Location:** `Source/CodexTactics/Private/Characters/OperativeAnimInstanceShotgun.cpp:94-126` and `OperativeAnimInstanceCover.cpp:52-88`
* **Trigger:** Triggered during `NativeInitializeAnimation()` or on weapon switch.
* **Mechanism:** Synchronous `LoadObject<UAnimSequence>` iterating through hundreds of entries in `weapon_anim_sets.json`.

### 3.5. Enemy-Specific Sync Calls
* **Marksman Laser:** `Source/CodexTactics/Private/Characters/MarksmanEnemyCharacter.cpp:128` — `BeamMaterial.LoadSynchronous()`.
* **Knockdown Clips:** `Source/CodexTactics/Private/Combat/KnockdownComponent.cpp:351` — `Clip.LoadSynchronous()` upon first knockdown.
* **Vision Cone Material:** `Source/CodexTactics/Private/Combat/VisionConeComponent.cpp:1314` — `FallbackMaterial.LoadSynchronous()`.

---

## 4. Deep Dive: Origin of the 2-Second `SK_Cutter_1slot` Hitch

In `hitch-2026-11-11-play2.log` (frame 835) and `hitch-2026-11-11-play3.log` (frame 614), a 2000+ ms stall is observed when `SK_Cutter_1slot` is first touched.

### 4.1. Identity and Ownership
* **Asset Path:** `/Game/Characters/Enemies/Optimized/Cutter/SK_Cutter_1slot.SK_Cutter_1slot`
* **Owning Blueprint:** `/Game/Characters/Enemies/BP_Enemy_Cutter.uasset`
* **Archetype:** `EEnemyArchetype::Cutter`
* **Weapon / Attack:** Melee dual-blade attack (using animations from `/Game/Mutant_monster_1/Animations/Cutter/`).

### 4.2. Why It Spawns in `L_Gen_Depot_1`
In `Content/Data/LevelJson/L_Gen_Depot_1.json`:
1. **Pressure System:** 
   ```json
   "pressure": {
     "archetypes": [
       { "id": "Hound", "weight": 40 },
       { "id": "Cutter", "weight": 20 },
       { "id": "Frostbitten", "weight": 20 },
       { "id": "MilitaryZombie", "weight": 20 }
     ]
   }
   ```
   When the player stays stationary or advances past encounter triggers, `CombatPressureSubsystem` evaluates weights and randomly selects `Cutter`.
2. **Wave Schedule:** Wave 3 explicitly spawns 5 Cutters:
   ```json
   "waves": [
     { "wave_index": 3, "archetype": "Cutter", "count": 5, "interval_s": 1.5 }
   ]
   ```

### 4.3. Cause of the 2-Second Game-Thread Stall
When `BP_Enemy_Cutter` is spawned for the first time:
1. `SK_Cutter_1slot` is not memory-resident. `WaveSubsystem` calls `SpawnActorDeferred<AEnemyCharacter>(BP_Enemy_Cutter)`.
2. Blueprint construction instantiates the `USkeletalMeshComponent` with `SK_Cutter_1slot`.
3. Unreal Engine's asset loader synchronously parses the cooked/editor package.
4. **Skeletal Mesh Initialization & Skinning Setup:** The engine initializes the skin cache, allocates bone transform buffers, builds LOD render data, and compiles or binds the mesh's vertex factory against the environment's snow overlay shaders.
5. In PIE/editor mode, shader permutation / PSO validation runs on the game thread, resulting in a **2.01s - 2.34s complete game freeze**.

---

## 5. Codex 004 Integration Recommendations

1. **Preload Subsystem (`ULevelPreloadSubsystem`):**
   * Load `015-preload-manifest.draft.json` during `ACodexTacticsGameMode::StartPlay()` or `InitGame()`.
   * Use `FStreamableManager::RequestAsyncLoad()` on all 356 assets during the initial loading screen / mission countdown.
   * Store shared pointers or strong object references in the subsystem to prevent GC from purging them during the level.

2. **Ladder Clips Centralization:**
   * Move `ULadderClimbComponent::LoadClips()` to pull from a single preloaded cache on `UCodexTacticsGameInstance` or `ULadderSubsystem`.
   * Set `ULadderClimbComponent::bClipsLoaded = true` immediately if preloaded.

3. **Warmup / PSO Cache:**
   * For skeletal meshes like `SK_Cutter_1slot` and `SKM_PmcOperator`, perform a 1-frame hidden spawn or render proxy warmup off-camera at level init to force bone buffer and skin shader initialization before player input starts.
