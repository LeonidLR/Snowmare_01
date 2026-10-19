# Refactor: AEnemyCharacter split (cleanup, 2026-10-19)

Third pass of the same cleanup (precedents: `docs/port/REFACTOR_OPERATIVE_2026-10.md`,
`docs/port/REFACTOR_CONTROLLER_2026-10.md`). `Source/CodexTactics/Private/Characters/EnemyCharacter.cpp` (2 983 lines at
base `2a6561c`) grew fast with the stealth work (patrols, guard posts, perception, SEARCH, stealth lure, hound scent, melee
close-in, flank detour). It is now split into translation units of the same class by cluster — **behaviour unchanged**.
Branch `agents/work`, base `2a6561c`. Section 2 of this document is the split proposal for `UTurnBasedCombatSubsystem`
(plan only).

## 1. AEnemyCharacter

### 1.1 Map of the file before the split (base 2a6561c)

88 top-level definitions: 87 members of `AEnemyCharacter` (constructor and the two `FScopedSpawnArchetype` members included)
+ one named namespace `EnemyCharacterLocal` holding two unrelated file-local objects: `SpawnArchetype` (the archetype a spawner
is spawning now, read by `FScopedSpawnArchetype` and `ResolvePlacedArchetype`) and the console variable `CVarMeleeCloseIn`
(`Codex.Enemy.MeleeCloseIn`, read only by `TickBehavior`). No anonymous namespace, no `static` helper functions, no
`LOCTEXT_NAMESPACE` (one `NSLOCTEXT("EnemyCharacter", ...)` in `AttackObject`). One column-0 comment banner
(`// --- Sprint 11 patrols ---`) above `SetEscortLeader`.

Clusters (definition lines; blank lines / includes not counted):

| Cluster | Lines | Members | Largest |
|---|---:|---:|---|
| main: ctor, BeginPlay / EndPlay, Tick, body facing, AI move requests, debug state, turn-based / world-AI-pause holds, overhead label | 253 | 14 | ctor 36, GetOverheadLabel 34 |
| Archetype: spawn archetype scope, placed-archetype resolution, archetype defaults, knockdown config, wave modifiers, balance | 299 | 9 + `SpawnArchetype` | **ApplyArchetypeDefaults 164** |
| Behavior: `TickBehavior` (fire fear, pack order + **flank detour**, cutter pounce trigger, obstacle smash, **melee reach / close-in**, belief hunt), target selection, fire zones, obstacles | 610 | 9 + `CVarMeleeCloseIn` | **TickBehavior 302**, FindTarget 117 |
| Combat: melee blow, object / vehicle attacks, spitter fire, cutter jump attack, knocking operatives down | 417 | 10 | TickSpitter 114, TickJumpAttack 62 |
| Health: knockdown hooks, incoming damage, trap blast scope, death, airborne-death crash | 153 | 6 | HandleDied 59, HandleDamaged 53 |
| Patrol: routes, escorts, guard posts, patrol break + propagation, trap alerts | 350 | 14 | TickPatrolRoute 67, TickPatrolBehavior 55 |
| Save: patrol / search snapshot | 71 | 2 | RestorePatrolSnapshot 52 |
| Perception: perception params, patrol sight / hearing (suspicion), vision cone, squad noise | 228 | 11 | TickPatrolPerception 62, FindVisibleOperative 53 |
| Scent: walls to the target, scent perception, sniff / follow | 145 | 4 | TickScentPerception 80 |
| Search & Lure: patrol search, lure investigate / re-target, hunting query, hearing path length | 317 | 8 | TickPatrolSearch 104, ApplySearchNoise 78 |

Cross-cluster calls are all member calls (`RequestAIMoveToLocation`, `FaceYaw`, `GodotPosition`, `IssuePatrolMove`,
`BreakPatrol`, `StartPatrolSearch*`, `ResetScent`, `FindClosestSquadMember`, ...), so a TU split needs no shared helper header.
Read from outside: `UStealthLureSubsystem` (`IsHuntingSquad`, `GetHearingPathLengthTo`, `StartPatrolSearchWith`),
`USaveGameSubsystem` (`CapturePatrolSnapshot` / `RestorePatrolSnapshot`), `UWaveSubsystem` (`FScopedSpawnArchetype`,
`ApplySpawnEntry`), `UTurnBasedCombatSubsystem` (`SetTurnBasedHeld`), `AMarksmanEnemyCharacter` (overrides the virtuals
`TickBehavior`, `StartPatrol`, `IsOnPatrol`, `BreakPatrol`, `IssuePatrolMove`, `HasLegacyPatrolRoute`,
`OnTurnBasedHeldChanged`), ~40 smokes. The header (every UPROPERTY / UFUNCTION, the reflection data) is
unchanged apart from the class comment, so no asset or Blueprint can notice the move.

### 1.2 Rules (same technique as the operative / controller splits)

- Translation units of the same class (`EnemyCharacter<Cluster>.cpp`), header unchanged except a layout note in the class
  comment (UHT needs the UCLASS in one header).
- Definitions moved verbatim, in their base order (the banner comment travels with `SetEscortLeader`); no logic edits.
- One structural edit, no linkage change: the named namespace `EnemyCharacterLocal` is split into two blocks, each a
  verbatim slice of the original — `SpawnArchetype` (external linkage, defined once) goes to the Archetype unit with its only
  readers, `static CVarMeleeCloseIn` to the Behavior unit with `TickBehavior`. Call sites unchanged (same qualified names).
  A named namespace reopened in two files is fine in a unity blob; the two names differ (`find_unity_clashes.py`: 0).
- Each unit starts with a three-line comment (cluster, "split out verbatim", Godot reference), then includes
  `Characters/EnemyCharacter.h` first and only the headers it uses (picked by the identifiers it names, proven by `-NoUnity`).

### 1.3 Layout after the split

`Private/Characters/`:

| File | Cluster |
|---|---|
| `EnemyCharacter.cpp` | ctor, `BeginPlay`, `Tick`, `FaceYaw`, `UpdateMovementFacing`, `RequestAIMoveToLocation` / `RequestAIMoveToActor`, `ResetAIMoveThrottle`, `GodotPosition`, `GetDebugState`, `SetTurnBasedHeld`, `HoldForWorldAIPause`, `EndPlay`, `GetOverheadLabel` |
| `EnemyCharacterArchetype.cpp` | `EnemyCharacterLocal::SpawnArchetype`, `FScopedSpawnArchetype`, `ResolvePlacedArchetype`, `InitializeArchetype`, `ConfigureKnockdown`, `ApplyArchetypeDefaults`, `ApplyWaveModifiers`, `ApplySpawnEntry`, `ApplyBalance` |
| `EnemyCharacterBehavior.cpp` | `EnemyCharacterLocal::CVarMeleeCloseIn`, **`TickBehavior`** (melee close-in, flank detour), `IsTargetUsableForTactics`, `FindTarget`, `IsInFearZone`, `FindNearestFireZone`, `FindNearestFire`, `FindBlockingObstacle`, `FindVehicleTarget`, `FindClosestSquadMember` |
| `EnemyCharacterCombat.cpp` | `TryKnockDownOperative`, `AttackObject`, `TickSpitter`, `TickRangedVsVehicle`, `StartAttackAnimation`, `AttackTarget`, `NotifySquadAttacked`, `StartJumpAttack`, `TickJumpAttack`, `ApplyJumpImpactDamage` |
| `EnemyCharacterHealth.cpp` | `IsKnockedDown`, `HandleKnockdownPhase`, `HandleDamaged`, `TrapBlastDepth`, `HandleDied`, `Landed` |
| `EnemyCharacterPatrol.cpp` | `SetEscortLeader`, `StartPatrol`, `InitPatrol`, `TickPatrolBehavior`, `TryStartGuardPost`, `TickGuardPost`, `LogStealthDetection`, `TickEscort`, `TickPatrolRoute`, `IssuePatrolMove`, `BreakPatrol`, `PropagatePatrolBreak`, `NotifyTrapTriggered`, `AlertPatrolsNearTrap` |
| `EnemyCharacterSave.cpp` | `CapturePatrolSnapshot`, `RestorePatrolSnapshot` |
| `EnemyCharacterPerception.cpp` | `GetPerception`, `GetPatrolPerception`, `FindVisibleOperative`, `CountHearingWalls`, `CountOccludersBetween`, `CountHearingWallsTo`, `TickPatrolPerception`, `GetVisionConeRange`, `GetVisionConeHalfAngle`, `UpdateVisionCone`, `NotifySquadNoise` |
| `EnemyCharacterScent.cpp` | `CountScentWallsTo`, `TickScentPerception`, `TickScentBehavior`, `ResetScent` |
| `EnemyCharacterSearch.cpp` | `IsHuntingSquad`, `StartPatrolSearch`, `StartPatrolSearchWith`, `ApplySearchNoise`, `GetHearingPathLengthTo`, `TickPatrolSearch`, `EndPatrolSearch`, `ResolveSearchPoint` |

Where to put new code: the file of its cluster; a file-local helper one unit needs goes into a prefixed named namespace or
an anonymous namespace with a FILE-UNIQUE name in that unit (unity blobs merge the units); a helper two units need goes into a
new `Private/Characters/EnemyCharacterInternal.h` (`inline`). A new cluster gets a new `EnemyCharacter<Cluster>.cpp` (+ a row
here and a rule in `Scripts/test_map.json`). `MarksmanEnemyCharacter.cpp` (the derived class) is untouched.

### 1.4 Result

Line counts, `Private/Characters/` (header `EnemyCharacter.h` 816 -> 822: only the class comment's layout note):

| File | Lines |
|---|---:|
| `EnemyCharacter.cpp` | 2 983 -> 291 |
| `EnemyCharacterBehavior.cpp` | 645 |
| `EnemyCharacterCombat.cpp` | 453 |
| `EnemyCharacterPatrol.cpp` | 381 |
| `EnemyCharacterSearch.cpp` | 342 |
| `EnemyCharacterArchetype.cpp` | 328 |
| `EnemyCharacterPerception.cpp` | 260 |
| `EnemyCharacterHealth.cpp` | 178 |
| `EnemyCharacterScent.cpp` | 163 |
| `EnemyCharacterSave.cpp` | 81 |

Verbatim check (script, §1.5): all 88 top-level definitions of base `2a6561c` (87 members + the namespace, checked as its two
slices) present exactly once, in base order within each file, byte-identical apart from the documented namespace split. A
mutated copy (one trailing space in `EnemyCharacterScent.cpp`, one unit duplicated) was reported as FAIL (3 bodies differ, 2
members defined twice), so the check is not vacuous. No non-mechanical fix was needed: every unit compiled on the includes the
script picked.

`find_unity_clashes.py`: 561 .cpp files, 0 clashes. Builds: `build.ps1 -NoUnity` (every enemy unit compiled alone,
`[x/301] Compile EnemyCharacter<Cluster>.cpp`), `build.ps1 -FullUnity` (the units merged into the `Module.CodexTactics.*`
blobs like committed files) and the plain `build.ps1` succeed; no new warnings (the only project warning is the existing
C4996 in `SaveGameSubsystem.cpp`).

Unit tests: 537 before -> 539 after, all pass (new `CodexTactics.Characters.EnemyUnits.WorldFreeMembers`: world-free
members of the main, Archetype, Health, Behavior, Perception, Patrol (+ Scent `ResetScent`), Search and Save units on one hound;
`CodexTactics.Characters.EnemyUnits.SourceLayout`: the 10 units exist, each includes the class header first, no anonymous
namespace, `SpawnArchetype` only in Archetype and the `Codex.Enemy.MeleeCloseIn` CVar only in Behavior, every member defined
once, `TickBehavior` / `Tick` / `TickPatrolSearch` / `RestorePatrolSnapshot` in their units). Impact map: new rules
`enemy-combat-units`, `enemy-patrol-units`, `enemy-perception-units` (the `enemy`, `stealth-lure` and `hound-scent` rules'
`EnemyCharacter*` globs already match every unit; `enemy` now also runs `CodexTactics.Characters.EnemyUnits`).

Smokes, the same 33-smoke list before (unmodified base `2a6561c`, plain build) and after (final tree, full unity build),
`smoke_parallel.ps1 -Jobs 3` on the fixed 60 fps step (SaveRoundTrip / SaveLoad / TutorialSave exclusive, alone afterwards):

| Smokes | Before (base) | After (split) |
|---|---|---|
| EnemyAI, EnemyTactics, EnemyHunt, EnemyArchetype, EnemyDeath, Knockdown, HordeIntro, LevelWave, Panic, Defend | PASS | PASS |
| Patrol, PatrolAmbush, Ambush, Sight, VisionCone, StealthLure, HoundScent, BotStealth | PASS | PASS |
| Marksman, MarksmanAdvance, BotMarksman, CornerHold, CommanderMode | PASS | PASS |
| TurnBased, TurnBasedAim, TurnBlastDeath, Vehicle, VehicleCombat, Mission | PASS | PASS |
| SaveRoundTrip, SaveLoad, TutorialSave, Tutorial | PASS | PASS |
| Total | 33 / 33 PASS, no re-run (wall 519 s) | 33 / 33 PASS, no re-run (wall 577 s) |

### 1.5 How the split was made (reproducible)

A throw-away script (session scratchpad, not committed; same approach as the operative / controller splits) read the base file
with `git show 2a6561c:...`, parsed it into top-level definitions (a column-0 line naming `AEnemyCharacter::` or `namespace`,
plus the column-0 comment lines right above, up to the first column-0 `}`), split `EnemyCharacterLocal` at its blank line,
assigned each definition by name to a cluster, wrote the bodies verbatim in base order, and picked each unit's includes from
the base include list by the identifiers the unit names. A second script compared the definitions before / after (count,
bytes, order).

## 2. UTurnBasedCombatSubsystem (4 411 lines): split proposal (plan only, no code change)

`Source/CodexTactics/Private/Tactics/TurnBasedCombatSubsystem.cpp` at base `2a6561c`: 115 members of
`UTurnBasedCombatSubsystem` (4 177 definition lines), 53 includes, two **anonymous** namespaces (no LOCTEXT, no CVars), and
eight column-0 section banners that already name the clusters (`Lifecycle`, `Helpers`, `Movement animation`, `Squad turn`,
`Object relocation`, `Deployables on the grid`, `Turrets and enemies`, `Explosions, deaths, end`).

### 2.1 Proposed layout (same technique: TUs of one class, header unchanged except the class comment)

| Proposed file | Members | ~Lines |
|---|---|---:|
| `TurnBasedCombatSubsystem.cpp` | `OnWorldBeginPlay`, `GetStatId`, `HandleGameFlowChanged`, **`StartCombat` 252**, `EndCombat`, `FreezeWorld`, `RestoreWorld`, `BakeObstacles`, **`Tick` 164**, `EnforceHeldEnemies`, `CheckBattleEnd` | 650 |
| `TurnBasedCombatSubsystemHelpers.cpp` | grid / damage / feed plumbing every phase calls: `PlaceOnCell`, `AlignFacing`, `GetHealth`, `ApplyDamage`, `ApplyEnemyHit`, `ApplySquadHit`, `ApplyBlast`, `IsDead`, `NameOf`, `WeaponOf`, `ShakeCamera`, `Log`, `Post`, `Highlight`, `GetCamera`, `GetSquadOverviewCenter`, `FocusSquadTurn`, `Changed`, `GetUnitState`, `GetActiveUnit`, `After` | 185 |
| `TurnBasedCombatSubsystemMovement.cpp` | walk animation on the grid: `PrepareSquadWalk`, `StartMoverAfter`, `StartMover`, `SampleWalkProfile`, `GetTacticalMoveSpeed` | 155 |
| `TurnBasedCombatSubsystemSquadTurn.cpp` | the squad member's turn: knocked-down turn / get up, `StartPlayerTurn`, `RefreshOverlay`, `SelectUnit`, `MoveActiveUnitTo`, stance / facing, `EndCurrentUnitTurn`, weapon switch, medkit, AP spending, `PassSquadTurn`, **`HandleWorldClick` 157**, `HandleTripwireClick`, `SetRelocationHover` | 670 |
| `TurnBasedCombatSubsystemSquadAttack.cpp` | attack mode + shots: `EnterAttackMode` / `ExitAttackMode` / `ToggleAttackMode`, `SetHoveredPoint`, `GetHoverHitChance`, `AttackCell`, **`ResolveAttackCell` 218**, `UpdatePendingShot`, `GetActiveAttackCost`, `CanAttackQuietly`, `AttackCellCinematic`, `WhenShotResolved` | 460 |
| `TurnBasedCombatSubsystemRelocation.cpp` | barricade / barrel relocation (banner `Object relocation`): `IsUnitAdjacentToObject` … `RelocateObject` (12) | 380 |
| `TurnBasedCombatSubsystemDeploy.cpp` | deployables on the grid: **`CanPlaceDeployable` 144**, `PlayWorkingDevice`, `StartGrowIn`, `RegisterDeployable`, `DeployObject` 106, `HandleDeployPlacement` | 390 |
| `TurnBasedCombatSubsystemPhases.cpp` | phase hand-over, exposed zones, reinforcements, turrets: `EndSquadPhase`, `UpdateExposedZones`, `FindSpawnCells`, `RegisterReinforcement`, `ExecuteTurretPhase`, `ProcessNextTurret`, `ResolveTurretShot` | 305 |
| `TurnBasedCombatSubsystemEnemyTurn.cpp` | the enemy phase: `ExecuteEnemyPhase`, `ProcessNextEnemy`, `GetFearCells`, **`ExecuteEnemyTurn` 180**, `EnemyAttack` 126, `ChooseEnemyTarget`, `IsCoveredFrom`, `WalkEnemy`, ranged turn (`ExecuteRangedEnemyTurn`, `EnemyRangedAttack`, `EnemyRetreat`), `FinishEnemyTurn`, `FocusMovingEnemy` | 805 |
| `TurnBasedCombatSubsystemDeaths.cpp` | explosions, deaths, end: `DetonateBarrel`, `DetonateMine`, `OnEnemyKilled`, `OnSquadMemberKilled`, `NotifyOperativeKilled`, `EndSquadPhaseAfterDeath` | 185 |
| `TurnBasedCombatSubsystemInternal.h` | the shared file-local helpers (below) | ~40 |

The enemy-turn unit stays the largest (805); if wanted, its ranged half (`ExecuteRangedEnemyTurn`, `EnemyRangedAttack`,
`EnemyRetreat`, 220 lines) becomes `TurnBasedCombatSubsystemEnemyRanged.cpp`.

### 2.2 The anonymous helpers (measured: which proposed unit uses each)

| Helper | Used by | Plan |
|---|---|---|
| `TurnGridCells`, `TurnCellSize` | main, SquadTurn, Deploy | internal header, `namespace TurnBasedCombatInternal` (`inline constexpr`) |
| `TurnBarrelBurnRounds` | main, Deaths | internal header |
| `TurnStepDir` | Movement, SquadTurn, SquadAttack, Relocation, Deploy, EnemyTurn | internal header (`inline`) |
| `TurnProfileOf` | main, Phases, EnemyTurn | internal header (`inline`; needs `EnemyTurnRules.h`, `WeaponTuning`, `EnemyCharacter.h`) |
| `TurnEncounterRadius`, `TurnEncounterMaxEnemies`, `TurnEntryDistance` | main | stay in the main unit's anonymous namespace |
| `TurnTurretSupportDistance` | Phases | Phases' anonymous namespace |
| `TurnStanceName` | SquadTurn | SquadTurn's anonymous namespace |
| `TurnArcName` | SquadAttack | SquadAttack's anonymous namespace |
| `TurnManhattan`, `EnemyFocusDistance`, `OverviewDistance` | EnemyTurn | EnemyTurn's anonymous namespace; rename the last two `TurnEnemyFocusDistance` / `TurnOverviewDistance` (file-unique, `Turn` prefix like the rest) |
| `SquadTurnDistance` | Helpers (`FocusSquadTurn`) | Helpers' anonymous namespace, renamed `TurnSquadFocusDistance` |

Textual edits this forces (the only non-verbatim changes, to be listed by the byte-identity script like the operative split's
qualified helper calls): the call sites of the five shared helpers get the `TurnBasedCombatInternal::` qualifier, and the
three renamed constants change at their few uses. No `using namespace` (find_unity_clashes flags it as a blob leak).

### 2.3 Method and net

- Script as in section 1 (parse the column-0 definitions + their banner comments, assign by name, write verbatim in base
  order, pick includes by identifiers); byte-identity check (all 115 members once, identical apart from the listed edits,
  mutated-copy negative check); `find_unity_clashes.py` 0; builds normal, `-NoUnity`, `-FullUnity`; unit tests
  (`CodexTactics.Tactics`, `CodexTactics.Combat.TurnBasedFlatGround` + a new `CodexTactics.Tactics.TurnBasedUnits.SourceLayout`).
- Smokes before / after: TurnBased, TurnWalk, TurnSelect, AttackMode, TurnBlastDeath, TurnRules, BarricadeTurnContact,
  TurnBasedAim, TurnBasedCamera, TurnBasedPush, TurnBasedBarricade, TurnBasedDeploy, ExposedZones, Knockdown, Turret,
  Vehicle / VehicleCombat (turret phase vs the APC), SaveRoundTrip / SaveLoad (mid-fight saves), Tutorial, Mission.
- Impact map: one `turn-based-<unit>` rule per unit on top of the existing `turn-based` rule (its `Source/**/Tactics/Turn*`
  glob already matches every new file).
- Follow-ups after the split (each its own step, smokes as the net): break up `StartCombat` (encounter selection / grid
  placement / camera entry), `ResolveAttackCell` (hit roll / damage / feed / cinematic), `ExecuteEnemyTurn` (decision vs walk)
  and `HandleWorldClick` (pure `TurnClickRules` dispatch already exists — move the remaining branches into it).
