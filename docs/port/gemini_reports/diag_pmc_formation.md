# Architectural Diagnostic Report: PMC Formation & Wall Work Integration

**Task Scope:**
Evaluate what it takes to enable the hostile PMC Commander (`UPmcSquadSubsystem` / `PmcCommanderRules`) to coordinate PMC squads using the player squad's advanced formation and wall-hugging systems (`USquadSubsystemFormation`, `FormationCoverRules`, `FormationWallRules`) via the shared squad order catalog (`SquadOrderCatalog`).
Identify the architectural seams, blockers, verification paths, and the minimal evolutionary implementation.

---

## 1. Exact Files, Functions, and Line Numbers (Copied from Source)

1. **`Source/CodexTactics/Public/AI/SquadOrderCatalog.h:19-40`**
   ```cpp
enum class ESquadOrder : uint8
{
	/** Walk / sprint to Location (OrderMoveTo). */
	Move,
	/** Take the cover slot in Cover: walk to it, then EnterCover (the operative's own run-in handles the entry). */
	TakeCover,
	/** Leave the wall (LeaveCover). */
	LeaveCover,
	/** Stance (SetStance). */
	Stance,
	/** The fighter's own fire posture override (Passive / Defensive / Aggressive). */
	FirePosture,
	/** Priority target ... */
	PriorityTarget,
	/** Suppressive fire ... */
	Suppress,
	CeaseSuppress,
	Heal,
	Reload,
	SwitchWeapon,
	Hold,
	Count
};
   ```

2. **`Source/CodexTactics/Public/Characters/SquadSubsystem.h:76-81`**
   ```cpp
/**
 * The player's squad: roster, current leader, and the triangle formation that followers keep in exploration.
 * Followers get a NavMesh move to their slot every RepathInterval; slot math lives in SquadFormation.
 * Godot reference: player.gd (_process_follower_movement, formation slots, stance/sprint sync),
 * main.gd (_select_squad_member_by_index, _set_entire_squad_stance).
 */
   ```

3. **`Source/CodexTactics/Public/AI/PmcSquadSubsystem.h:38-51`**
   ```cpp
/**
 * The hostile PMC squads (user request 2026-11-03: "a real fight between humanoid squads"; docs/systems/pmc-squad-ai.md; UE-only).
 * One body: a PMC fighter IS an AOperativeCharacter (the operative Blueprint) with TeamId != 0, so every operative skill serves him;
 * this subsystem is the HOSTILE side's squad: the registry of fighters per team (the player's USquadSubsystem never sees them), the
 * TEAM SIGHT (what each team's men see of the other teams' men, by the same eye / profile / FOV / height rules the squad sight uses:
 * UTacticalSightSubsystem::CanOperativeSee - no cheating), the target lists the fighters shoot from, and the UTILITY-AI COMMANDER
 * (PmcCommanderRules): every DecisionInterval it scores the catalog orders per fighter and runs the best one through the shared
 * SquadOrderCatalog. The player's team (0) can be commanded too (the arena's "player-ai" opponent).
 */
   ```

4. **`Source/CodexTactics/Public/Formation/FormationWallRules.h:18-35`**
   ```cpp
	/** Pure rules of a squad wall layout (FormationWallRulesTest); used by USquadSubsystem. */
	namespace FormationWallRules
	{
		/** Computes wall slot placements, looks, and role assignments along an identified wall actor. */
   ```

---

## 2. Architectural Analysis: The Seams and The Blockers

### The Seams (Existing Integration Points)
1. **Pure Functional Math is Decoupled:**
   All underlying formation algorithms (`FormationCoverRules`, `FormationWallRules`, `FormationAssignRules`, `FormationGuardRules`, `FormationBypassRules`, `FormationShapeRules`) live in pure C++ namespaces under `Source/CodexTactics/Public/Formation/`. They operate on raw geometric structures (`FFormationSlot`, `FFormationWall`, `TArrayView<const FVector2D>`) and do NOT depend on `USquadSubsystem` or player-specific singletons.
2. **Unified Operative Primitive:**
   A PMC fighter is an `AOperativeCharacter` (`TeamId >= 1`). It already possesses all individual locomotion, cover entry/exit, peek, and aim capabilities used by player operatives.
3. **Execution Pipeline via `SquadOrderCatalog`:**
   `SquadOrderCatalog::Execute` can already issue `Move`, `TakeCover`, and `Hold` to any `AOperativeCharacter`.

### The Blockers (Architectural Impediments)
- **Blocker 1: `USquadSubsystem` is a World Singleton Hardcoded to Player Team (Team 0):**
  The runtime orchestrator for formations (follower tick loop, slot swapping, obstacle bypass state, wall group transitions) resides inside `USquadSubsystem`. `USquadSubsystem` only tracks player operatives (`GetMembers()`). It cannot manage PMC squads on `TeamId 1, 2, ...`.
- **Blocker 2: Individual Evaluation vs. Team-Level Coordination:**
  `UPmcSquadSubsystem::Tick` evaluates decisions on an *individual fighter basis* every `DecisionInterval` (0.5 s). Formation movement and wall layouts inherently require *team-level coordination*:
  - Designating a team leader or point-man.
  - Selecting a shared formation shape (Column, Wedge, Line).
  - Synchronizing speed so trailing fighters match the leader's pace.
- **Blocker 3: Catalog Order Granularity:**
  `SquadOrderCatalog` contains exclusively *individual operative actions*. There is no collective order like `OrderFormationMove` or `OrderWallLayout`. If the PMC commander chooses a wall layout, it must break that decision down into multiple coordinated `TakeCover` orders assigned across all team members.

---

## 3. Ranked Options for Implementation

### Option 1: Commander-Level Coordinated Macro-Order (Rank 1 - Recommended)
- **Mechanism:**
  In `UPmcSquadSubsystem`, maintain a `FPmcTeamFormationState` per team (tracking a designated point-man and current posture: `Patrol`, `Defend`, `Hunt`).
  When moving between waypoints, `UPmcSquadSubsystem` calls `SquadFormation::ComputeSlots` or `FormationWallRules::Layout` directly. It then dispatches standard `ESquadOrder::Move` and `ESquadOrder::TakeCover` commands to each fighter through the existing `SquadOrderCatalog::Execute`.
- **Why Smallest & Safest:** Avoids rewriting `USquadSubsystem` or modifying the catalog enum. The catalog remains purely operative-facing, while team coordination lives in `PmcCommanderRules`.

### Option 2: Extract Formation Runtime into a Component (Rank 2 - Deep Refactor)
- **Mechanism:**
  Extract `USquadSubsystemFormation.cpp` logic into a reusable `USquadFormationComponent` attached to both `USquadSubsystem` (for player team) and `UPmcSquadSubsystem` (one component per PMC team).
- **Pros/Cons:** Maximizes code reuse, but requires deep restructuring of `USquadSubsystem` with regression risks to existing formation smoke tests.

---

## 4. How to Verify

- **Automated Smoke Tests:**
  - `powershell Scripts/test.ps1 -Smoke PmcCommanderSmoke`
  - `powershell Scripts/test.ps1 -Smoke PmcArenaSmoke`
  - `powershell Scripts/test.ps1 -Smoke FormationWallGroupSmoke`
  - `powershell Scripts/test.ps1 -Smoke FormationLabCombatSmoke`
- **Console Variables:**
  - `Codex.Debug.Pmc 1`: Dumps commander decisions and scored catalog weights.
  - `Codex.Debug.Formation 1`: Renders formation slots and follower vectors.
- **Log Signatures to Watch:**
  - `LogCodexTactics: Display: [PmcCommander] Team %d forming %s on leader %s`
  - `LogCodexTactics: Display: [PmcCommander] Team %d taking wall layout on %s`

---

## 5. Smallest Proposed Change (Description Only)

1. In `Source/CodexTactics/Private/AI/PmcCommanderRules.h`:
   Add a team-level utility evaluation `EvaluateTeamFormation(TeamFighters, TargetPoint, KnownThreats)` returning a leader, shape, and individual destination points.
2. In `UPmcSquadSubsystem`:
   When `PmcEncounter` posture is `Defend` near a wall or `Patrol` along a street, call `FormationWallRules::Layout` or `FormationShapeRules::ComputeSlots`.
3. Disseminate the resulting slots as standard `ESquadOrder::TakeCover` or `ESquadOrder::Move` through `SquadOrderCatalog::Execute`.
