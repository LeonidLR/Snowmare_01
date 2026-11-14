# 047 — Props C5 Static Validator (`Source/CodexTactics/Public/LevelGen/PropsValidator.h`)

## Summary

Implemented Task C5 of `docs/design/levelgen/PROPS_LAYERS.md` (sections 2.4, 2.5, 3.1, 5, 7.1) on branch `gemini/047-props-validator` branched from `664d78b2`.

Specification clarity verdict: **CLEAR**. The static validation rules, doorway clear corridor geometry, ladder/stair keep-out disks, attach target resolution, and failure logging formats (`LOCKED/HAND INVALID`) are unambiguously defined in `PROPS_LAYERS.md`.

## Implemented Architecture & Code

1. **Header (`Source/CodexTactics/Public/LevelGen/PropsValidator.h`)**:
   - `struct FPropsValidationIssue`: Records `ObjectId`, `LayerId`, `bLocked`, `bHand`, `Reason`, and `bDropped`.
   - `struct FPropsValidationResult`: Stores `ValidObjects` (safe to spawn in C6), `DroppedObjects`, `Issues`, `TotalObjects`, and helper `HasLockedOrHandInvalid()`.
   - `class FPropsValidator`:
     - `Validate(const FPropsLayoutView& LayoutView, const FLevelPropsFile& File, FPropsValidationResult& OutResult, const FJsonObject* Balance)`: Runs `ApplyOverrides`, then validates all resolved objects.
     - `ValidateResolved(const FPropsLayoutView& LayoutView, const FLevelPropsResolved& Resolved, FPropsValidationResult& OutResult, const FJsonObject* Balance)`: Validates pre-resolved file state.
     - `ValidateObjects(const FPropsLayoutView& LayoutView, const TArray<FLevelPropsResolvedObject>& Objects, const TArray<FLevelPropsKeepOut>& KeepOuts, FPropsValidationResult& OutResult, const FJsonObject* Balance)`: Core validation pipeline.
     - `IsInDoorwayCorridor(const FPropsLayoutView& LayoutView, const FLevelPropsObject& Object, int32 HalfSize, FString* OutBlockingDoorwayId)`: Exact integer check for 90 cm corridor 150 cm deep on both sides, exempting the doorway's own mine (`Object.Door == Doorway.Id`).
     - `IsInLadderOrStairDisk(const FPropsLayoutView& LayoutView, const FLevelPropsObject& Object, int32 HalfSize, FString* OutBlockingId)`: Exact 100 cm disk check around ladder foot, ladder top, and stair ends.
     - `GetVariantHalfSize(const FString& Variant, const FJsonObject* Balance)`: Footprint half-size from balance `kinds` or built-in defaults.

2. **Implementation (`Source/CodexTactics/Private/LevelGen/PropsValidator.cpp`)**:
   - Two-pass validation pipeline:
     - **Pass 1 (Spatial & Layout Checks)**:
       1. Walk layer existence (`LayoutView.FindLayer`).
       2. Walkable position test (`LayoutView.IsWalkable`).
       3. Obstacle geometry test for non-attached props (`LayoutView.IsBlocked` with half-size).
       4. Doorway clear corridor test (width 90 cm along wall, depth 150 cm perpendicular across opening).
       5. Ladder/stair keep-out disks (100 cm around foot, top, and stair ends).
       6. Keep-out disks from deleted objects (`FLevelPropsKeepOut`).
     - **Pass 2 (Dependency Resolution)**:
       - Validates `Object.Attach`: If target object does not exist or was dropped during spatial validation, the attached object is dropped as dangling.
     - **Logging & Flagging**:
       - Dropped objects are excluded from `ValidObjects`.
       - Generated props logged as: `Props validator: dropped invalid object '<id>' on layer '<layer>': <reason>`.
       - Locked or hand-placed/edited props (`bLocked`, `bHand`, `bPinned`, `Origin == "hand"`) are logged as: `Props validator: LOCKED/HAND INVALID object '<id>' on layer '<layer>': <reason> (dropped)`.

3. **Automation Tests (`Source/CodexTacticsTests/Private/LevelGen/PropsValidatorTest.cpp`)**:
   - `ValidPropsPass`: Golden fixtures (`golden_small_crates.json`) pass with 0 dropped objects.
   - `WalkLayerAndBounds`: Objects on invalid walk layers or out of bounds are dropped.
   - `BlockedByGeometry`: Objects inside walls/fences are dropped.
   - `DoorwayCorridor`: Objects inside 90 cm corridor 150 cm deep around doors/gates are dropped; doorway's own mine is preserved.
   - `LadderDisks`: Objects within 100 cm of ladder foot or top are dropped.
   - `DanglingAttach`: Crate mines or loot attached to missing or dropped crates are dropped; valid attachments survive.
   - `LockedAndHandInvalid`: Locked and hand objects failing validation are dropped and flagged with `LOCKED/HAND INVALID`.
   - `DeletedKeepOut`: Objects inside keep-outs of deleted props are dropped.

## Claim | Evidence

| Claim | Evidence |
|---|---|
| C5 specification is clear and unambiguous | Evaluated `docs/design/levelgen/PROPS_LAYERS.md:236-265,389,603-605,662`; rules map directly to exact integer geometry checks |
| C++ paths restricted strictly to spec paths | `Source/CodexTactics/Public/LevelGen/PropsValidator.h`, `Source/CodexTactics/Private/LevelGen/PropsValidator.cpp`, `Source/CodexTacticsTests/Private/LevelGen/PropsValidatorTest.cpp` |
| Doorway corridor check enforces 90 cm width, 150 cm depth on both sides, exempting doorway's own mine | `Source/CodexTactics/Private/LevelGen/PropsValidator.cpp:189-228`; `DoorwayCorridor` test PASS in `PropsValidatorTest.cpp:208-262` |
| Ladder foot/top and stair ends enforce 100 cm keep-out disks | `Source/CodexTactics/Private/LevelGen/PropsValidator.cpp:230-258`; `LadderDisks` test PASS in `PropsValidatorTest.cpp:264-317` |
| Dangling attach targets are dropped | `Source/CodexTactics/Private/LevelGen/PropsValidator.cpp:149-170`; `DanglingAttach` test PASS in `PropsValidatorTest.cpp:319-390` |
| Locked and hand objects are flagged with `LOCKED/HAND INVALID` | `Source/CodexTactics/Private/LevelGen/PropsValidator.cpp:16-32`; `LockedAndHandInvalid` test PASS in `PropsValidatorTest.cpp:392-471` |
| All unit tests pass with zero failures | `powershell .\Scripts\test.ps1 -Test CodexTactics.LevelGen.Props.Validator`: 1340 tests, 0 failed |
| Gate smokes remain 100% green with zero regressions | `powershell .\Scripts\smoke_parallel.ps1 FormationLabSmoke,SoloStartSmoke,ProneCrawlSmoke,InjuredLocomotionSmoke,ShotgunCoverSmoke`: 5/5 PASS (0 failed) |

## Verification Logs

### Unit Tests
```
Success  CodexTactics.LevelGen.Props.Validator.BlockedByGeometry
Success  CodexTactics.LevelGen.Props.Validator.DanglingAttach
Success  CodexTactics.LevelGen.Props.Validator.DeletedKeepOut
Success  CodexTactics.LevelGen.Props.Validator.DoorwayCorridor
Success  CodexTactics.LevelGen.Props.Validator.LadderDisks
Success  CodexTactics.LevelGen.Props.Validator.LockedAndHandInvalid
Success  CodexTactics.LevelGen.Props.Validator.ValidPropsPass
Success  CodexTactics.LevelGen.Props.Validator.WalkLayerAndBounds
1340 tests, 0 failed. Report: Saved\TestReport
```

### Gate Smokes
```
[smoke_parallel] 5 smokes, 4 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  SoloStartSmoke             PASS      65,5s
  ProneCrawlSmoke            PASS      74,7s
  InjuredLocomotionSmoke     PASS      83,5s
  ShotgunCoverSmoke          PASS      51,8s
  FormationLabSmoke          PASS     214,3s
5 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 215 s
```
