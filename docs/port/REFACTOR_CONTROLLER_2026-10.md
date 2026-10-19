# Refactor: ACodexTacticsPlayerController split (cleanup, 2026-10-15)

Follow-up of the `AOperativeCharacter` split (`docs/port/REFACTOR_OPERATIVE_2026-10.md`, whose §6 proposed this one):
`Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp` (2 978 lines at base `8db4f94`) was touched by almost every
tutorial / input / squad change. It is now split into translation units of the same class by input area — **behaviour
unchanged**. Branch `agents/work`, base `8db4f94`.

## 1. Map of the file before the split (base 8db4f94)

90 top-level definitions: 87 members of `ACodexTacticsPlayerController` (constructor included) + three named namespaces
(`ControllerCommands` — the Demo 01 command reporter `Report` + `ForStance` / `ForDeployable`; `SquadKeys` — the Slate key
pre-processor for 1-4; `OrderLock` — the `Codex.RealTimeOrders` CVar). Refreshed against §6 of the operative document (the
file grew by 6 lines since; the clusters held):

| Area | Lines (definitions) | Notes |
|---|---:|---|
| main: ctor, BeginPlay / EndPlay, input actions + bindings, PlayerTick, shared queries | 394 | PlayerTick 151, SetupInputComponent 84; `SquadKeys` lives here (BeginPlay registers it) |
| Selection | 251 | OnClick 67, SelectMember 48 |
| Orders | 671 | **HandleWorldHit 358**, IssueTargetedShot 106, OrderGroupMove 67 |
| Stance | 125 | ApplyStance 68 |
| Items | 395 | UseSquadItem 71, GrenadeKey 55 |
| Time | 158 | HandleSpaceHold 79 |
| Menus | 192 | DialogueSkip 46 |
| Camera | 177 | CameraDragRotateStart 60 |
| Cover | 407 | TryHandleCoverClick 119, ConfirmCoverPreview 83 |

Changes against the §6 proposal: `PostHeadquarters`, `RefuseIfLocked`, `GetActiveTurnBased`, `GetSquad`,
`IsAttackModifierDown` / `IsSquadWideModifierDown` stay in the main file (every area calls them); `HandleSquadNumberKey`
went to Selection (the Slate processor in the main file only calls it).

Shared file-local state: `ControllerCommands::*` is called from every area; `OrderLock::CVarRealTimeOrders` from Orders
(`IsRealTimeOrderLocked`, `HandleWorldHit`) and Cover (`ConfirmCoverPreview`). Everything else is members of the class.
The header (every UPROPERTY / UFUNCTION, the reflection data) is unchanged apart from the class comment, so no asset or
Blueprint can notice the move.

## 2. Rules (same technique as the operative split)

- Translation units of the same class (`CodexTacticsPlayerController<Area>.cpp`), header unchanged except a layout note in
  the class comment (UHT needs the UCLASS in one header).
- Definitions moved verbatim, in their original order; no logic edits. Two textual edits in total, both forced by the
  linkage of the shared helpers:
  - `ControllerCommands` moved into `Private/Core/CodexTacticsPlayerControllerInternal.h` with `inline` on its three
    functions (call sites unchanged: the namespace name is the same).
  - `OrderLock::CVarRealTimeOrders` lost its `static` (defined once in `CodexTacticsPlayerControllerOrders.cpp`, declared
    `extern` in the internal header so Cover reads the same object — a second `static` copy would register the console
    variable twice).
- `SquadKeys` (one user, BeginPlay) stays a named namespace in the main file — never an anonymous namespace duplicated in
  two files (docs/port/TRAPS.md: unity builds merge the .cpp files).
- Every unit keeps `LOCTEXT_NAMESPACE "CodexTacticsPlayerController"` (keys unchanged) and closes it with
  `#undef LOCTEXT_NAMESPACE` at its end.
- Each unit includes `Core/CodexTacticsPlayerController.h` first and only the headers it uses (picked by the identifiers it
  names, proven by a non-unity build).

## 3. Layout after the split

`Private/Core/`:

| File | Area |
|---|---|
| `CodexTacticsPlayerController.cpp` | ctor, `BeginPlay` / `EndPlay` (+ `SquadKeys` pre-processor), `CreateInputActions`, `SetupInputComponent`, `PlayerTick`, shared queries (`IsAttackModifierDown`, `IsSquadWideModifierDown`, `PostHeadquarters`, `RefuseIfLocked`, `GetActiveTurnBased`, `GetSquad`) |
| `CodexTacticsPlayerControllerSelection.cpp` | `HandleSquadNumberKey`, `FindClickedMember`, `OnClick` / `OnClickReleased`, `GetSelectionBox`, `SelectInBox`, `SelectMember`, `ToggleSoloMode` |
| `CodexTacticsPlayerControllerOrders.cpp` | `OrderLock` CVar, `IsRealTimeOrderLocked`, `ApplyFirePosture`, `BlockRealTimeOrder`, `ToggleAutonomy`, `GuardKey`, `OrderGroupMove`, **`HandleWorldHit`**, `AssignDefenseUnderCursor`, `IssueTargetedShot`, `IssueCtrlClick` / `IssueQuickFire` (Ctrl + click on a point = quick fire, 2026-10-19), `SetSquadCeaseFire`, `VehicleKey` / `VehicleKeyPressed` |
| `CodexTacticsPlayerControllerStance.cpp` | `SetEntireSquadStance`, `ApplyStance`, `CycleLeaderStance` |
| `CodexTacticsPlayerControllerItems.cpp` | `GetPlacingRelocation`, grenade aim (`GetGrenadeAimPoint`, `CancelGrenadeAim`, `GrenadeKey`, `StartFlareAim`), `UseSquadItem`, placement (`StartPlacementForType`, `StartTripwirePlacement`, `GetPlacementPoint`, `DeployAbility`, `RotatePlacement`), `CycleWeaponKey`, `ToggleRelocateSelectMode` |
| `CodexTacticsPlayerControllerTime.cpp` | `SpacePressed`, `SpaceReleased`, `HandleSpaceTap`, `HandleSpaceHold` |
| `CodexTacticsPlayerControllerMenus.cpp` | `QuickSaveKey`, `QuickLoadKey`, dialogue keys (`IsDialogueOpen`, `DialogueNext`, `HasDialogueForKey`, `ApplyDialogueKey`, `EnterPressed` / `EnterReleased`, `DialogueSkip`), `ProfilePressed`, `TabPressed`, `ControlsKey`, `InventoryKey`, `VisionConesKey`, `RestartMission` |
| `CodexTacticsPlayerControllerCamera.cpp` | `InteriorViewKey`, `GetCameraPawn`, mouse wheel, rotate keys, RMB drag rotate, MMB drag pan |
| `CodexTacticsPlayerControllerCover.cpp` | cover ghost (`HasCoverPreview`, `FindCoverSlotForHit`, `TryHandleCoverClick`, `ShowCoverPreview`, `HideCoverPreview`, `ConfirmCoverPreview`), corner peek (`FindCornerPeekForHit`, `IsCornerPeekPreviewShown`, `UpdateCornerPeekHover`), `ToggleCoverFireModeKey` |
| `CodexTacticsPlayerControllerMap.cpp` | (2026-10-18, a new area) the minimap: `MapPanelKey` (F2), `FullMapKey` (Tab outside turn-based / F3), `CloseFullMap` (Esc), `IsCursorOverMinimap`, `HandleMapWheel`, `HandleMapClick` (LMB camera / RMB move via `HandleWorldHit`) - `docs/design/MINIMAP.md` |
| `CodexTacticsPlayerControllerInternal.h` | / `ForStance` / `ForDeployable` (inline), `extern OrderLock::CVarRealTimeOrders` |

Where to put new code: the file of its input area; a helper two units need goes into the internal header (`inline`), a
helper one unit needs into a prefixed named namespace or `static` function of that unit. A new input area gets a new
`CodexTacticsPlayerController<Area>.cpp` (+ a row here, in docs/port/TRAPS.md / docs/systems/squad-and-orders.md, and a rule in `Scripts/test_map.json`).
The impact map routes each unit to its area's smokes (`controller-<area>` rules) on top of the `game-framework` rule.

## 4. Result

Line counts, `Private/Core/` (header `CodexTacticsPlayerController.h` 440 -> 443: only the class comment's layout note):

| File | Lines |
|---|---:|
| `CodexTacticsPlayerController.cpp` | 2 978 -> 446 |
| `CodexTacticsPlayerControllerOrders.cpp` | 729 |
| `CodexTacticsPlayerControllerCover.cpp` | 446 |
| `CodexTacticsPlayerControllerItems.cpp` | 432 |
| `CodexTacticsPlayerControllerSelection.cpp` | 283 |
| `CodexTacticsPlayerControllerMenus.cpp` | 233 |
| `CodexTacticsPlayerControllerCamera.cpp` | 207 |
| `CodexTacticsPlayerControllerTime.cpp` | 183 |
| `CodexTacticsPlayerControllerStance.cpp` | 144 |
| `CodexTacticsPlayerControllerInternal.h` | 70 (new) |

Verbatim check (script, §6): all 90 top-level definitions (87 members + `ControllerCommands`, `SquadKeys`, `OrderLock`) present
exactly once, in the base order within each file, byte-identical to base `8db4f94` apart from the two documented edits
(`inline` on the three `ControllerCommands` functions, `static` dropped on `OrderLock::CVarRealTimeOrders`). A mutated copy
(one space added in `ApplyStance`) was reported as FAIL, so the check is not vacuous. The only non-mechanical fixes were
includes found by the non-unity build: `Characters/OperativeCharacter.h` in the internal header (`GetLeader` result),
`Combat/HealthComponent.h` + `UI/ProfileDialogWidget.h` (Selection), `Combat/HoldSphereActor.h` + `Combat/HealthComponent.h`
(Time), `Components/BoxComponent.h` (Orders: the tripwire snap builds an `FHitResult` from `Wire->Box`) — members reached
through `TObjectPtr` fields without naming the type, which the old file got from its long include list.

Builds: a full non-unity build (`-DisableUnity`: every unit compiles on its own includes) and a full unity build with adaptive
unity OFF (`-DisableAdaptiveUnity`: the new units merged into the 14 unity blobs like committed files — the plain `build.ps1`
keeps working-set files out of the blobs) succeed; no new warnings.

Unit tests: 468 before -> 470 after, all pass (new `CodexTactics.Core.ControllerUnits.WorldFreeMembers`: world-free members of
the main, Selection, Orders and Cover units on one controller + the `Codex.RealTimeOrders` CVar registered once with its
default 1; `CodexTactics.Core.ControllerUnits.SourceLayout`: the 9 units exist, each includes the class header first, uses the
controller's LOCTEXT namespace and ends with its `#undef`, no unit re-defines `ControllerCommands`, the CVar is registered by the
Orders unit only, every member is defined once, `HandleWorldHit` in Orders and `PlayerTick` in the main unit).

Smokes, the same 46-smoke list before (unmodified base `8db4f94` build) and after (final tree, full unity build),
`smoke_parallel.ps1 -Jobs 3` on the fixed 60 fps step (exclusive ones alone after the batch):

| Smokes | Before (base) | After (split) |
|---|---|---|
| Movement, RealtimeSelect, GroupSelect, ClickRules, SoloRejoin, CommanderMode, Guard, Defend | PASS | PASS |
| Cover, CoverHug, CornerHold, CornerEntry, Stance, Vault | PASS | PASS |
| Grenade, GrenadeAnim, Deployable, Inventory, Transfer, Loot, WeaponSelector, Relocation, Barrel | PASS | PASS |
| TurnBased, TurnBasedCamera, TurnSelect, TargetedShot, ActionBar | PASS | PASS |
| CameraZone, CameraZoneMode, InteriorExit, RoofCutaway | PASS | PASS |
| ControlsScreen, PauseMenu, Frontend, SaveRoundTrip, SaveLoad, Dialogue | PASS | PASS |
| Tutorial, TutorialSave, TrapSolo, PeekMines, PMCTripwire, BridgeFence, Vehicle, VehicleCombat | PASS | PASS |
| Total | 46 / 46 PASS, no re-run (wall 859 s) | 46 / 46 PASS, no re-run (wall 856 s) |

## 5. Follow-up: break up `HandleWorldHit` (not done in this pass)

`HandleWorldHit` (358 lines, Orders unit) is an ordered chain of "this click is mine" checks; each early `return` ends the
click. Plan for its own cleanup step — move each block verbatim into a private `bool TryHandle...Click(const FHitResult& Hit,
...)` helper (header change: new private declarations only) and keep `HandleWorldHit` as the ordered list of calls, the order
unchanged:
1. tripwire snap (a ground click next to a spotted wire = the wire) — stays inline (it rewrites `Hit`);
2. `TryHandleExitArrowClick` (interior EXIT arrow), 3. `TryHandleTurnBasedClick` (grid), 4. `TryHandleLockedRealTimeClick`
   (optional real-time order lock: select only), 5. `TryHandleRelocatePickClick` (action bar MOVE), 6. `TryHandleVehicleClick`
   (leader aboard: drive / turret target), 7. `TryHandleSilhouetteClick` (blind fire), 8. Ctrl -> `IssueTargetedShot` (already
   a member), 9. `TryHandleCoverClick` (already a member) + `HideCoverPreview`;
10. `TryHandleFightObjectClick` (Godot main.gd plain-click rules: enemy priority target, set-up items / movable objects in the
    fight / pause / preparation), 11. `TryHandleRecruitClick`, 12. `TryHandleMemberClick` (selection, interaction cancel);
13. `HandleGroundClick` (double click, tutorial move / confinement / sprint locks, object walk-up, Shift + click facing, order
    dispatch Execute / Queue / Blocked, `OrderGroupMove` + the command report).
The shared locals (`Squad`, `Leader`, the flow / phase flags, `ClickMessages`) become parameters or a small
`FWorldClickContext` struct. Optionally the dispatch order itself becomes a pure, unit-tested `WorldClickRules` function.
Net: ClickRules, PauseBarrelOrder, TargetedShot, RealTimeControl, Cover, CoverHug, InteriorExit, Vehicle, VehicleCombat,
TrapSolo, Tutorial, CommanderMode, Relocation, Barrel, Loot smokes + the byte-identity script (bodies moved verbatim).

## 6. How the split was made (reproducible)

A throw-away script (session scratchpad, not committed; same approach as `split_operative.py`) parsed the base file into
top-level definitions (a column-0 line naming `ACodexTacticsPlayerController::` or `namespace`, plus the comment block right
above, up to the first column-0 `}`), assigned each by name to an area, wrote the bodies verbatim in their original order, and
picked each unit's includes by the types / namespaces it names (+ the five includes the non-unity build asked for). A second
script compared the definitions before / after (count, bytes, order).
