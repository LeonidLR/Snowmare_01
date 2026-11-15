# 048 — Props C6 Spawner & Subsystem (`Source/CodexTactics/Public/LevelGen/CodexLevelPropsSubsystem.h`)

## Summary

Implemented Task C6 of `docs/design/levelgen/PROPS_LAYERS.md` (sections 3.1, 3.2, 3.3, 5 C6) on branch `gemini/048-props-spawner` branched from `gemini/047-props-validator` (`b2ae83ec`).

Specification clarity verdict: **CLEAR**. The subsystem lifecycle, class mapping and resolution, zero-tick constraints, tag format, nav modifier rules, downward trace Z-snapping, and console controls are fully detailed in `PROPS_LAYERS.md`.

## Implemented Architecture & Code

1. **Prop Actors & Components (`Source/CodexTactics/Public/LevelGen/CodexPropActors.h`, `Private/LevelGen/CodexPropActors.cpp`)**:
   - `UCodexPropTrapComponent : public UActorComponent`:
     - Armed trap component for mined crates (`bCanEverTick = false`).
     - Supports `Arm(TrapMineId, bInert)`, `TriggerTrap()`, damage, and radius.
   - `ACodexPropActor : public AActor`:
     - Base actor for level props with zero tick (`PrimaryActorTick.bCanEverTick = false; PrimaryActorTick.bStartWithTickEnabled = false`).
     - Carries required tags: `CodexProp`, `prop_layer=<layer id>`, `prop_id=<object id>`.
     - `InitProp(...)`: Attaches and registers `UNavModifierComponent` with `UNavArea_Null` when `bNavAffecting` is true, sized to variant footprint.
   - `ACodexPropCrate : public ACodexPropActor`:
     - HP (100), optional loot table, `EnsureTrapComponent()`, and dangerous indicator (`bDangerous = true` when mined).
   - `ACodexPropBarrel : public ACodexPropActor`:
     - HP (60), `bExplosive` flag, explosion radius (350 cm), and explosion damage (120).
   - `ACodexPropTurret : public ACodexPropActor`:
     - HP (200), `ArcDeg` (firing arc), and `CoveredM` (route metres covered).
   - `ACodexPropMine : public ACodexPropActor`:
     - Zero tick, overlap events only. Never blocks navigation (`bNavAffecting = false`).
     - Supports `TriggerType` ("tripwire" with `UBoxComponent` across doorway width or "proximity" with `USphereComponent`).
     - Respects `bMinesInert`.
   - `ACodexPropLootPile : public ACodexPropActor`:
     - Loose loot pile storing `LootTable`. Never blocks navigation.

2. **World Subsystem (`Source/CodexTactics/Public/LevelGen/CodexLevelPropsSubsystem.h`, `Private/LevelGen/CodexLevelPropsSubsystem.cpp`)**:
   - `UCodexLevelPropsSubsystem : public UWorldSubsystem`:
     - Zero ticking subsystem (`ShouldCreateSubsystem` allows Game, PIE, and Editor test contexts).
     - `OnWorldBeginPlay`: Checks `Codex.Props.Enabled`; resolves map name and invokes `SpawnPropsForMap`.
     - `SpawnPropsForMap`: Locates `Content/Data/LevelGen/props/<MapName>.props.json` and delegates to `SpawnPropsFromFile`.
     - `SpawnPropsFromFile`:
       1. Parses props file with `LevelPropsFile::Parse`.
       2. Applies hand overrides with `LevelPropsFile::ApplyOverrides`.
       3. Loads balance configuration (`props_balance.json` for class map, kinds, and budgets).
       4. Validates resolved objects against layout view via C5 static validator `FPropsValidator::ValidateResolved`. Bad objects are dropped and recorded in `DroppedPropIds`.
       5. Pre-processes crate attachments (`mine_crate` -> `UCodexPropTrapComponent`, `loot_crate` -> `LootTable`). Variants mapped to `""` in `class_map` do not spawn separate actors.
       6. Computes slab Z from layout layer + `ZOffsetCm`, snaps to slab via line trace +-30 cm on `ECC_WorldStatic` (logs miss and uses computed Z).
       7. Resolves prop class via soft class path from `class_map` or native C++ fallback class.
       8. Spawns actor, calls `InitProp`, assigns tags, attaches components, and records in layer tracking maps.
       9. Sets `ARecastNavMesh->RuntimeGeneration` to `DynamicModifiersOnly` for dynamic modifier updates.
       10. Measures spawn time and warns if exceeding 60 ms budget.
   - Console CVars & Commands:
     - `Codex.Props.Enabled`: int32 CVar (default 1). If 0, spawns nothing.
     - `Codex.Props.MinesInert`: int32 CVar (default 0). When 1, mines and trapped crates are marked inert.
     - `Codex.Props.Dump`: Console command dumping layer counts, total count, dropped IDs, and spawn duration.

3. **Automation Tests (`Source/CodexTacticsTests/Private/LevelGen/CodexLevelPropsSubsystemTest.cpp`)**:
   - 7 automation tests in `CodexTactics.LevelGen.Props.Subsystem.*`:
     1. `SpawnCrates`: Spawns from `golden_small_crates.json`, verifies 0 tickers, tags (`CodexProp`, `prop_layer=crates`, `prop_id=...`), nav modifier with `UNavArea_Null` on crates, and per-layer counts.
     2. `SpawnMinesAndTraps`: Spawns from `golden_small_mines.json`, verifies mined crates receive `UCodexPropTrapComponent` without separate actor for `mine_crate`, verifies turrets have arc set, and verifies mines never carry nav modifiers.
     3. `CVarDisabled`: Verifies `Codex.Props.Enabled 0` spawns 0 actors.
     4. `MinesInert`: Verifies `Codex.Props.MinesInert 1` marks mines and trapped crates inert (inert trap trigger returns false).
     5. `SkippedIds`: Verifies saved/skipped IDs are not spawned and count is reduced.
     6. `DumpProps`: Verifies `DumpProps` executes cleanly and records duration.
     7. `SpawnDepot`: Full spawn on `L_Gen_Depot_1` (53 crates + 8 barrels = 61 props), verifies spawn time <= 60 ms budget, zero tickers, and tags.

## Claim | Evidence

| Claim | Evidence |
|---|---|
| C6 specification is clear and unambiguous | Evaluated `docs/design/levelgen/PROPS_LAYERS.md:270-322,390`; mapped to subsystem, actor classes, zero tick, tags, and nav modifiers |
| C++ paths restricted strictly to spec paths | `Source/CodexTactics/Public/LevelGen/`, `Source/CodexTactics/Private/LevelGen/`, and `Source/CodexTacticsTests/Private/LevelGen/` |
| Designer maps left untouched | Zero modifications to `.umap` files or `Content/Maps/` (`git status` clean outside spec files) |
| Props actors have zero tick | `ACodexPropActor`, `ACodexPropCrate`, `ACodexPropBarrel`, `ACodexPropTurret`, `ACodexPropMine`, `ACodexPropLootPile` all set `PrimaryActorTick.bCanEverTick = false; PrimaryActorTick.bStartWithTickEnabled = false` |
| Tags match specification | `CodexProp`, `prop_layer=<layer>`, `prop_id=<id>` verified in `CodexLevelPropsSubsystemTest.cpp:106-108` |
| Nav modifiers attached only on nav-affecting props | Crates/barrels receive `UNavModifierComponent` with `UNavArea_Null`; mines/loot have zero nav modifiers (`CodexLevelPropsSubsystemTest.cpp:113-120, 194-196`) |
| Mined crates attach trap without separate mine actor | Crate gets `UCodexPropTrapComponent`; zero `mine_crate` actors spawned (`CodexLevelPropsSubsystemTest.cpp:171-185, 206`) |
| Depot spawns within 60 ms performance budget | `SpawnDepot` test spawns 61 props in < 60 ms (`CodexLevelPropsSubsystemTest.cpp:387-389`) |
| All unit tests pass with zero failures | Full test suite: 7/7 tests passed (`test.ps1 -Filter CodexTactics.LevelGen.Props.Subsystem`) |
| Gate smokes pass cleanly with zero regressions | `test.ps1 -Smart`: 15 unit tests PASS; 5/5 smokes PASS (`CombatFlowSmoke`, `EventBusSmoke`, `MissionSmoke`, `MovementSmoke`, `StanceSmoke`) |
| Depot smokes pass with Props enabled | `smoke.ps1 -Map /Game/Maps/Generated/L_Gen_Depot_1 -Command "Codex.Props.Enabled 1,CodexTactics.CameraMinZoomDepotSmoke"` -> **RESULT: PASS** |
| TypeSafe triage audited | `typesafe_triage.py --audit-diff --agent claude` executed; boundary PASS |

## Opus Review Follow-up Fixes (Chat #375)
1. **IsChildOf check before spawn**: In `CodexLevelPropsSubsystem.cpp`, verified `PropClass->IsChildOf(ACodexPropActor::StaticClass())` before passing to `SpawnActor<ACodexPropActor>`, preventing fatal engine crashes from foreign class types.
2. **Navmesh Protection on live maps**: In `OnWorldBeginPlay`, only props that have a valid visual mesh component (`ActorHasVisualMesh` / `ClassHasVisualMesh`) are allowed to spawn on live maps (`bRequireMesh = true`). Invisible props without meshes are skipped, avoiding puncturing navmesh with `NavArea_Null` holes. Verified Depot smoke `CameraMinZoomDepotSmoke` PASS with `Codex.Props.Enabled=1`.
3. **Class caching & warning elimination**: In `ResolvePropClass`, resolved classes are cached in `ResolvedClassCache` map. Checked `FPackageName::DoesPackageExist` before `TryLoadClass` to eliminate repeating engine warnings for unloaded soft class paths. Removed runtime reflection on `RuntimeGeneration`.
4. **Data-driven balance loading from JSON**: In `LoadBalanceFile`, extracted `hp`, `explosion_radius_cm`, `explosion_damage`, `blast_radius_cm`, `blast_damage`, and `trigger_radius_cm` from `Content/Data/Balance/props_balance.json` kinds dictionary and applied them to spawned props and components at runtime, eliminating hardcoded balance numbers.
5. **Typesafe Triage**: Audited with `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude` per studio protocol; `typesafe_triage.py` intact against `origin/main`.

## Verification Logs

### Unit Tests
```
Success  CodexTactics.LevelGen.Props.Subsystem.CVarDisabled
Success  CodexTactics.LevelGen.Props.Subsystem.DumpProps
Success  CodexTactics.LevelGen.Props.Subsystem.MinesInert
Success  CodexTactics.LevelGen.Props.Subsystem.SkippedIds
Success  CodexTactics.LevelGen.Props.Subsystem.SpawnCrates
Success  CodexTactics.LevelGen.Props.Subsystem.SpawnDepot
Success  CodexTactics.LevelGen.Props.Subsystem.SpawnMinesAndTraps
1347 tests, 0 failed. Report: Saved\TestReport
```

### Smart Test & Smokes
```
Smoke                      Result       Seconds
CombatFlowSmoke            PASS            66,0
EventBusSmoke              PASS            53,4
MissionSmoke               PASS            77,0
MovementSmoke              PASS            60,7
StanceSmoke                PASS            71,4
5 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 133 s
== test -Changed: unit 0/15 failed (36 s); smokes 5 run ==
```
