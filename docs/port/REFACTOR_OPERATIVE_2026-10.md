# Refactor: AOperativeCharacter split (cleanup day, 2026-10-14)

User decision 2026-10-14 (`production/session-state/active.md` «Planned: cleanup day», task 1): split
`Source/CodexTactics/Private/Characters/OperativeCharacter.cpp` (4 867 lines, header 1 826) into cohesive units so that a
future change touches a small file — **behaviour unchanged**. Branch `agents/work`, base `fdf53a7`.

## 1. Map of the file before the split (base fdf53a7)

The file held 133 member definitions of one class plus file-local helpers. Responsibility clusters (function lines, the
blank lines / includes / helpers in between not counted):

| Cluster | Lines | Members | Calls into other clusters | Read from outside (approx. files) |
|---|---:|---|---|---|
| Core (ctor, lifecycle, Tick, identity / body colour, rage / panic queries) | 187 + ctor 62 | `OnConstruction`, `BeginPlay`, `EndPlay`, `Tick`, `SetSquadIdentity`, `ApplyBodyColor`, `IsRaging`, `IsPanicking` | every cluster (Tick drives vault, cover, facing, fire, sniper, overlays) | many |
| Movement (move orders, formation moves, sprint / carry / wound speed, traffic hold) | 310 | `OrderMoveTo`, `FollowTo`, `RequestMove`, `StopOperative`, `HandleMoveFinished`, `SetSprinting`, `SetCarrying`, `GetMaxSpeed`, `IsMoving`, `ApplyMovementParams`, `AutonomousMoveTo`, … (19) | cover (`EnterCover` / `LeaveCover`), stance, knockdown, barricade | ~86 (controller, squad, AI, interactables, smokes) |
| Stance + prone dive + placeholder body | 268 | `SetStance`, `DropProne`, `UpdateProneDive`, `EndProneDive`, `GetStanceChangeDelay`, `ApplyStanceCapsule`, placeholder pose (12) | movement (`RequestMove`, `ApplyMovementParams`, `StopOperative`), knockdown | ~70 (`GetStance` everywhere, AnimInstance) |
| Vault (vault, obstacle step-off) | 202 | `TryVault`, `UpdateVault`, `UpdateVaultTrigger`, `UpdateObstacleStepOff`, `GetVaultSpeed` | `RequestMove`, `ApplyMovementParams`, `LeaveCover` | 2 (AnimInstance, VaultSmoke) |
| Overlays (selection ring, silhouette, vision cone) | 195 | 9 | `FindBestCombatTarget` | 5 |
| Inventory (items, deployables, field aid) | 132 | 7 | arsenal reserve | ~17 |
| Progression (level, EXP, stat points) | 107 | 8 | none | 5 |
| Health (hits, knockdown hooks, death, corpse, remains) | 315 | `TakeHit`, `TakeGridBlast`, `ResolveIncomingHit`, `HandleKnockdownPhase`, `HandleDied`, `BecomeCorpse`, … (10) | 9 (cover, corner aim, movement, stance, selection) | ~24 |
| Weapons (arsenal, ammo, reload, auto switch, AI grenades) | 303 | 15 | `IsKnockedDown` | ~29 |
| Combat (target selection, lines of fire, blind fire, posture, real-time fire, `ShootAtTarget`) | 735 | 13 | 19 (cover fire, corner hold, open shot, aim, weapons) | ~10 |
| Aim (muzzle, aim point, aim cone residual, combat facing) | 202 | 13 | cover facing, corner hold, entry blend | ~11 (AnimInstance, smokes, turn-based) |
| Targeted shots (Ctrl + click objects, planned pause shots) | 239 | 6 | 11 (weapons, aim, cover fire) | ~9 |
| Cover (orders, enter / leave, shimmy, fire mode, facing along the wall, events) | 644 | 26 | 20 (movement, stance, corner aim, entry blend, aim) | ~40 (AnimInstance, controller, squad, sight, enemy tactics, smokes) |
| Cover entry (run-in, enter-clip mesh blend, wall clearance, body-yaw blend) | 220 | 4 | 4 | 1 |
| Corner aim (corner hold / sustained aim / manual peek / snap / open shot) | 421 | 12 | 15 | ~10 |
| Sniper (already its own file `OperativeCharacterSniper.cpp`, 241) | — | 11 | — | — |

Shared state: everything is members of `AOperativeCharacter` (~70 private cover fields alone, ~30 BlueprintReadWrite cover
tunables). The AnimInstance (`UOperativeAnimInstance*.cpp`) reads ~25 cover / stance / vault / aim getters every frame;
the controller, `USquadSubsystem`, `UTurnBasedCombatSubsystem`, `UTacticalSightSubsystem`, the enemy tactics and ~60
smokes read the rest. File-local helpers: `IsRangedEnemyActor` (combat, health, corner aim), `ShotFeet` / `IsLiveEnemy`
(combat, aim, weapons), `MovingSpeedThreshold` (movement), `CVarDebugCornerAim` (cover), `OperativeShotLine` +
`LOCTEXT_NAMESPACE "OperativeTargetedShots"` (targeted shots).

### What the Blueprints reference (checked 2026-10-14)

Name-table scan of the packages (`BP_Operative`, `BP_Operative_FemaleSoldier`, `ABP_Operative`) against every
UPROPERTY / UFUNCTION of the header (the unreal MCP server was not running, so a package scan instead of an editor query):
- `BP_Operative`: `DisplayName`, `SquadOutfits` (+ `SlotMaterials`), `bVisionConeRangeFromWeapon`, and the component
  subobjects `HealthComponent`, `WeaponMesh`, `ColdSurvival`, `RageComponent`, `PanicComponent`, `KnockdownComponent`,
  `VisionCone`, `BodyMesh`, `FacingMarker`.
- `BP_Operative_FemaleSoldier`: `DisplayName` + the same component subobjects.
- `ABP_Operative`: none (its AnimGraph holds only the Output Pose; the pose is blended natively in `UOperativeAnimInstance`).
- `MovementConfig` and the cover tunables are NOT overridden by the Blueprints today — but they are EditAnywhere /
  BlueprintReadWrite and may be set on placed instances or tuned by the user later, so they stay on the character.

## 2. Decision: translation units of the same class (stage 1), components later (stage 2)

**Chosen today: split the .cpp into translation units of the same class** (`OperativeCharacter<Cluster>.cpp`, the pattern
`OperativeCharacterSniper.cpp` / `OperativeAnimInstanceSniper.cpp` already use). Why not `UActorComponent`s now:
- The clusters are tightly coupled through shared members: cover alone uses ~240 member identifiers including 20 methods of
  other clusters (`RequestMove`, `SetStance`, `StopOperative`, `GetMuzzleLocation`, `UpdateCombatFacing`, …); combat calls
  19 methods of cover / aim / weapons. A `UOperativeCoverComponent` would need dozens of back-references into the character
  and forwarding accessors for ~40 external readers (AnimInstance, controller, squad, sight, enemy tactics, smokes).
- The cover / stance / sniper tunables are BlueprintReadWrite UPROPERTYs on the character (and `MovementConfig`); moving them
  would silently reset user values unless core-redirected — the brief says leave them, so a component would read them back
  from the owner anyway.
- New default subobjects would be added to the user's Blueprints (BP re-save churn); `.uasset`s are off-limits today.
- A TU split is mechanical and verbatim (a script moved whole definitions), keeps the header, every UPROPERTY / UFUNCTION
  name, category, the save-game fields and every call site byte-identical — the safest possible first step with the
  smokes as the net.

Rules for the split:
- Definitions moved verbatim, in their original order; no logic edits. The only textual change inside bodies: the shared
  file-local helpers moved to `Private/Characters/OperativeCharacterInternal.h` (`namespace OperativeCharacterInternal`,
  `inline`), so their calls are qualified (`OperativeCharacterInternal::IsLiveEnemy(...)`). A named namespace, not an
  anonymous one per file: unity builds merge .cpp files and duplicate anonymous helpers would collide (HANDOFF §2 trap).
- Single-cluster helpers live in their file's anonymous namespace (`MovingSpeedThreshold` -> Movement, `CVarDebugCornerAim`
  -> Cover, `OperativeShotLine` + the same `LOCTEXT_NAMESPACE "OperativeTargetedShots"` -> TargetedShots: localisation keys
  unchanged).
- Each file includes `Characters/OperativeCharacter.h` first and only the headers it uses (checked with a non-unity build).
- The header stays one file (UHT needs the UCLASS in one header); its class comment lists the .cpp files per cluster.

## 3. Layout after the split

See §5 for the measured line counts. `Private/Characters/`:

| File | Cluster |
|---|---|
| `OperativeCharacter.cpp` | ctor, `OnConstruction`, `BeginPlay`, `EndPlay`, `Tick`, identity / body colour, rage / panic queries |
| `OperativeCharacterMovement.cpp` | move orders, formation moves, sprint / carry / wound speed, traffic hold, facing point, autonomy move |
| `OperativeCharacterStance.cpp` | stances, stance move hold, prone dive (root motion), capsule per stance, placeholder body |
| `OperativeCharacterVault.cpp` | vault, obstacle step-off |
| `OperativeCharacterOverlays.cpp` | selection ring, silhouette, vision cone |
| `OperativeCharacterInventory.cpp` | personal items, deployables, field aid |
| `OperativeCharacterProgression.cpp` | level, EXP, stat points |
| `OperativeCharacterHealth.cpp` | incoming hits, knockdown hooks, death, corpse, remains |
| `OperativeCharacterWeapons.cpp` | arsenal, ammo, reload, auto switch, autonomous grenades |
| `OperativeCharacterCombat.cpp` | real-time target selection, lines of fire, blind fire, fire posture, `ProcessCombatShooting`, `ShootAtTarget` |
| `OperativeCharacterAim.cpp` | muzzle, aim point, aim cone residual, combat facing, instant aimed turns |
| `OperativeCharacterTargetedShots.cpp` | Ctrl + click object shots, planned pause shots |
| `OperativeCharacterCover.cpp` | cover orders, enter / leave, shimmy, fire mode, facing along the wall, cover events |
| `OperativeCharacterCoverEntry.cpp` | run-in, enter-clip mesh blend, wall clearance, body-yaw blend |
| `OperativeCharacterCornerAim.cpp` | corner hold, sustained corner aim, manual corner peek, corner snap, open shots off the wall |
| `OperativeCharacterSniper.cpp` | (unchanged) sniper rules, per-role body, grid shot clips |
| `OperativeCharacterInternal.h` | shared helpers `IsRangedEnemyActor`, `ShotFeet`, `IsLiveEnemy` |

Where to put new code: the file of its cluster; a new cluster gets a new `OperativeCharacter<Cluster>.cpp` (plus a row
here and in HANDOFF §5). The impact map (`Scripts/test_map.json`) routes each file to its cluster's smokes.

## 4. Stage 2 roadmap (components; not done today)

In order of increasing coupling — each one its own cleanup step with its own verification:
1. **Progression -> `UOperativeProgressionComponent`**: no calls into other clusters. Blocker: `Level`, `CurrentExp`,
   `UnspentStatPoints` are EditAnywhere / BlueprintReadWrite and saved by `USaveGameSubsystem` from the character -> keep them
   on the character (component reads / writes the owner) or move them with forwarding + save mapping (keys unchanged).
2. **Vault -> `UOperativeVaultComponent`**: private state only (11 fields), 3 calls out (`RequestMove`,
   `ApplyMovementParams`, `LeaveCover`), read by the AnimInstance (`IsVaulting`, `GetVaultSpeed`, `GetVaultDuration`: keep
   forwarding accessors on the character).
3. **Overlays -> `UOperativeOverlayComponent`** (selection ring, silhouette, vision cone driver): the BP-editable settings
   (`bEnableSilhouette`, `SilhouetteMaterial`, `bShowVisionCone`, `bVisionConeRangeFromWeapon` - set in BP_Operative) stay
   on the character.
4. **Cover**: first group the ~70 private cover fields into one `FOperativeCoverState` member (pure mechanical rename,
   compiler-checked), then decide whether a `UOperativeCoverComponent` pays off; the ~30 BP tunables stay on the character.
5. Stance / prone dive and combat stay on the character (they are the character's core and touch everything).

## 5. Result

(filled in after the split — line counts before -> after, verification)

## 6. `ACodexTacticsPlayerController` (2 972 lines): split proposal (no code change today)

Same technique (translation units of one class; header unchanged), by input domain. Function line counts from base fdf53a7:

| Proposed file | Members | ~Lines |
|---|---|---:|
| `CodexTacticsPlayerController.cpp` | ctor, `BeginPlay`, `EndPlay`, `CreateInputActions`, `SetupInputComponent`, `PlayerTick`, `GetSquad`, small queries | ~470 |
| `CodexTacticsPlayerControllerSelection.cpp` | `HandleSquadNumberKey`, `FindClickedMember`, `OnClick` / `OnClickReleased`, `GetSelectionBox`, `SelectInBox`, `SelectMember`, `ToggleSoloMode` | ~280 |
| `CodexTacticsPlayerControllerOrders.cpp` | `HandleWorldHit` (353 lines: split further into per-hit-kind helpers in a later step), `OrderGroupMove`, `IssueTargetedShot`, `AssignDefenseUnderCursor`, `GuardKey`, `ToggleAutonomy`, `ApplyFirePosture`, `BlockRealTimeOrder`, `IsRealTimeOrderLocked`, `RefuseIfLocked`, `SetSquadCeaseFire`, `VehicleKey` / `VehicleKeyPressed` | ~700 |
| `CodexTacticsPlayerControllerStance.cpp` | `SetEntireSquadStance`, `ApplyStance`, `CycleLeaderStance` | ~130 |
| `CodexTacticsPlayerControllerItems.cpp` | grenade aim / flare / placement / deployables / weapon cycle / relocation select (`GetGrenadeAimPoint`, `CancelGrenadeAim`, `UseSquadItem`, `StartPlacementForType`, `StartTripwirePlacement`, `StartFlareAim`, `GrenadeKey`, `GetPlacementPoint`, `DeployAbility`, `RotatePlacement`, `CycleWeaponKey`, `ToggleRelocateSelectMode`, `GetPlacingRelocation`) | ~420 |
| `CodexTacticsPlayerControllerTime.cpp` | Space tap / hold time modes (`SpacePressed`, `SpaceReleased`, `HandleSpaceTap`, `HandleSpaceHold`, `PostHeadquarters`) | ~170 |
| `CodexTacticsPlayerControllerMenus.cpp` | dialogue keys + UI keys (`IsDialogueOpen`, `DialogueNext`, `HasDialogueForKey`, `ApplyDialogueKey`, `EnterPressed` / `EnterReleased`, `DialogueSkip`, `ProfilePressed`, `TabPressed`, `ControlsKey`, `InventoryKey`, `VisionConesKey`, `QuickSaveKey`, `QuickLoadKey`, `RestartMission`) | ~260 |
| `CodexTacticsPlayerControllerCamera.cpp` | `GetCameraPawn`, mouse wheel, rotate, drag rotate / pan, `InteriorViewKey` | ~200 |
| `CodexTacticsPlayerControllerCover.cpp` | cover click / ghost / corner peek hover (`HasCoverPreview`, `FindCoverSlotForHit`, `TryHandleCoverClick`, `FindCornerPeekForHit`, `IsCornerPeekPreviewShown`, `UpdateCornerPeekHover`, `ShowCoverPreview`, `HideCoverPreview`, `ConfirmCoverPreview`, `ToggleCoverFireModeKey`) | ~400 |

Risks: the controller binds input with member-function pointers in `SetupInputComponent` (unchanged by a TU split);
`HandleWorldHit` is the one function worth a real decomposition, best with `ClickRulesSmoke`, `PauseBarrelOrderSmoke`,
`TargetedShotSmoke`, `RealTimeControlSmoke`, `CoverSmoke` and `InteriorExitSmoke` as the net. Can reuse
`split_operative.py`'s approach (see §7).

## 7. How the split was made (reproducible)

A throw-away script (session scratchpad, not committed) parsed the file into top-level definitions (`^... AOperativeCharacter::Name(`
up to the first column-0 `}`), assigned each by name to a cluster, wrote the bodies verbatim in their original order, and
picked each file's includes by the identifiers it uses; a non-unity build (`-DisableUnity`) proved each file stands alone.
A check compared the multiset of definitions before / after (identical apart from the qualified helper calls).
