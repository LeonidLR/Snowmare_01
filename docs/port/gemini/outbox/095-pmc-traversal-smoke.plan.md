# 095 - PMC Traversal Smoke Plan (Smart Enemies T-12)

## Goal
Verify whether an enemy fighter (hostile operative on Team 1, commander off) can physically navigate and cross every stair flight, ladder ($\ge 7$ rungs), mantle link, and drop link of a generated greybox map (`L_Gen_Depot_X4`).

---

## 1. Reference Spec & Context
- **Spec**: Section **T-12** of `docs/design/gdd/smart-enemies-roadmap.md`:
  - Map: `L_Gen_Depot_X4` (using accepted layout `Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x4.json`).
  - Target Operative: Hostile operative on Team 1, commander OFF (`SetTeamCommanded(1, false)`).
  - Four link kinds:
    1. Every stair flight (`buildings[].stairs`)
    2. Every ladder with $\ge 7$ rungs (`ladders[]` where `rungs >= 7`)
    3. Every mantle link (`UMantleSubsystem::GetLinks()`)
    4. Every drop link (`UDropLinkSubsystem::GetLinks()`)
  - Reporting: Output "N of M crossed" per kind + failed IDs with reason (`no path` / `stuck` / `refused`).
  - Pass criterion: $\ge 95\,\%$ per kind. If today's rate is lower, register as `knownFail` in `Scripts/test_map.json` with the exact numbers in `_why`.
- **Reuse, do not reinvent**:
  - `LevelGenRampSmokeCommand.cpp`: layout loading (`LoadLayout`), stair low-to-high probing, `RoofGoal`.
  - `LevelGenLaddersSmokeCommand.cpp`: layout ladder parsing, foot/top positioning, ladder climbing verification via `ULadderClimbComponent`.
  - `MantleFenceRouteSmokeCommand.cpp`: `UMantleSubsystem` link queries, approach/goal projection, Solo mode mantle opening.
  - `DropReachSweep.h` / `DropLinkSubsystem.h`: `UDropLinkSubsystem` generated drop links traversal.
  - `LevelGenBuildingsSmokeCommand.cpp` (branch `claude/building-generator`): leg runner and `FProgress::IsStuck` stuck detection pattern.

---

## 2. Real APIs and Code Citations
1. **Spawning Hostile Fighter**:
   - `UPmcSquadSubsystem::SpawnSquad(int32 TeamId, const FVector& Location, const FRotator& Rotation, int32 Count, int32 Tier)`: `Source/CodexTactics/Public/AI/PmcSquadSubsystem.h:105`.
   - `UPmcSquadSubsystem::SetTeamCommanded(int32 TeamId, bool bCommanded)`: `Source/CodexTactics/Public/AI/PmcSquadSubsystem.h:114`.
   - Disables utility commander AI so fighter only follows explicit test orders.
2. **Player Squad Isolation**:
   - `USquadSubsystem::EnterSoloMode()`: `Source/CodexTactics/Public/Characters/SquadSubsystem.h:174`.
   - `USquadSubsystem::SetFollowersHolding(bool bHold)`: `Source/CodexTactics/Public/Characters/SquadSubsystem.h:185`.
   - Enables `bMantlePathAllowed` via `IsSoloMode()` (`Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp:68, 497`).
3. **Move Orders & Order Outcome**:
   - `AOperativeCharacter::OrderMoveTo(const FVector& Destination, bool bSprint)`: `Source/CodexTactics/Public/Characters/OperativeCharacter.h:177`.
   - Return enum `EOperativeOrderResult`: `Source/CodexTactics/Public/Characters/OperativeCharacter.h:120`:
     - `EOperativeOrderResult::Unreachable` / `NoController` $\rightarrow$ classified as `no path`.
     - `EOperativeOrderResult::Refused` $\rightarrow$ classified as `refused`.
     - `EOperativeOrderResult::Accepted` $\rightarrow$ leg runs.
4. **Link Sources**:
   - Stairs (19 flights): `LevelGenRampSmoke::LoadLayout` (`Source/CodexTacticsDev/Private/Debug/LevelGenRampSmokeCommand.cpp:147`) parsing `buildings[].stairs`.
   - Ladders (23 ladders $\ge 7$ rungs): layout `ladders` (`Source/CodexTacticsDev/Private/Debug/LevelGenLaddersSmokeCommand.cpp:61`).
   - Mantle links (3106 generated on X4): `UMantleSubsystem::GetLinks()` (`Source/CodexTactics/Public/Interactables/MantleNavigation.h:155`).
   - Drop links (409 generated on X4): `UDropLinkSubsystem::GetLinks()` (`Source/CodexTactics/Public/Falls/DropLinkSubsystem.h:103`).
5. **Leg Runner & Stuck Detection**:
   - `FProgress` pattern (`origin/claude/building-generator:Source/CodexTacticsDev/Private/Debug/LevelGenBuildingsSmokeCommand.cpp:120-160`):
     - `Reset(double Now, const FVector& Position)`
     - `IsStuck(double Now, const FVector& Position, double MoveCm, double Seconds)`: `MoveCm = 50.0`, `StuckSeconds = 4.0`.
     - Leg timeout: 15.0 s (fails as `stuck`).

---

## 3. Architecture & Execution Steps

### Phase 0: Settle & World Preload
- Wait for `UDropLinkSubsystem::HasGenerated()` and `UMantleSubsystem::HasGenerated()`.
- Park player squad followers holding in Solo mode.
- Spawn hostile operative on Team 1 via `UPmcSquadSubsystem::SpawnSquad(1, SpawnSpot, FRotator::ZeroRotator, 1, 1)`.
- Set `UPmcSquadSubsystem::SetTeamCommanded(1, false)`.

### Phase 1: Leg Sequence
- Build full list of `FLeg` items:
  1. **Stairs** (`Kind = Stair`): Start at stair foot on navmesh; goal at top roof approach; verify arrival on roof level.
  2. **Ladders** (`Kind = Ladder`): Filter `rungs >= 7`; start at foot; goal at upper floor landing; verify arrival and `ULadderClimbComponent::IsClimbing()`.
  3. **Mantle Links** (`Kind = Mantle`): Start at `Link.Start - Link.Dir * 120.f`; goal at `Link.End + Link.Dir * 120.f`; verify arrival across obstacle.
  4. **Drop Links** (`Kind = Drop`): Start at `Link.Start - Link.Dir * 150.f`; goal at `Link.End + Link.Dir * 100.f`; verify arrival on landing level.
- Execute legs sequentially:
  - Teleport operative to start approach point, stop previous movement.
  - Issue `OrderMoveTo(Leg.Goal, false)`.
  - If refused or unreachable $\rightarrow$ record failure immediately (`no path` or `refused`) and advance to next leg.
  - While running, check arrival criteria (`Dist2D <= ArriveRadiusCm`, level tolerance $Z$) or stuck condition (`FProgress::IsStuck` after 4 s without progress, or leg timeout).
  - Advance to next leg.

### Phase 2: Per-Kind Aggregation & Verdict
- Print per kind:
  - `Smoke summary: stairs: N of M crossed (P%)`
  - `Smoke summary: ladders: N of M crossed (P%)`
  - `Smoke summary: mantles: N of M crossed (P%)`
  - `Smoke summary: drops: N of M crossed (P%)`
  - List failed IDs + reasons `[id, reason: no path / stuck / refused]`.
- Check if all 4 kinds pass at $\ge 95\,\%$.
  - If all $\ge 95\,\%$ $\rightarrow$ `Smoke RESULT: PASS`.
  - If any $< 95\,\%$ $\rightarrow$ `Smoke RESULT: FAIL`.

---

## 4. Test Registration (`Scripts/test_map.json`)
- Add `PmcTraversalSmoke` to `smokeRegistry`.
- Add `smokeOptions`:
  ```json
  "PmcTraversalSmoke": {
    "map": "/Game/Maps/Generated/L_Gen_Depot_X4",
    "extra": "-LevelJson=L_Gen_Depot_X4.json",
    "timeout": 1200,
    "_why": "smart enemies T-12 traversal smoke: hostile operative (team 1, commander off) walks every stair flight, ladder (>= 7 rungs), mantle link, drop link on X4"
  }
  ```
  *(Note: if today's rate is $< 95\,\%$, set `knownFail: true` with measured numbers in `_why`)*.
- Add to `impactRules` for `pmc-traversal` and `levelgen-routes`.

---

## 5. Gate & Validation
- Check editor lock: `Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Select CommandLine`.
- Build: `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1`.
- Run smokes: `powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands PmcTraversalSmoke,LevelGenRampSmoke,LevelGenLaddersSmoke`.
- Run quality gate: `powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_gate.ps1 -Task 095 -SonnetReview -Worktree "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-gemday" -Base main`.
- Author Russian report: `docs/port/gemini/outbox/095-pmc-traversal-smoke.report.md`.

---

## 6. Rework Plan (Review #926 - 7 Must-Fix Points)

1. **Strict Mantle Verification (Up/Over vs Down)**:
   - Use `AOperativeCharacter::GetMantleCount()` and `IsMantling()` (`OperativeCharacter.h:564/513`).
   - Distinguish `Link.bDown` (down hop off low top) vs climb up/over.
   - Separate metrics: `mantles: X of Y crossed [up/over: A of B, down: C of D]`.
   - A mantle leg ONLY counts as crossed if `IsMantling()` was observed OR `GetMantleCount() > InitialMantleCount`. Walk-arounds / ledge drops are classified as `bypassed mantle up` or `bypassed mantle down`.

2. **Team 1 Mantle Permission & Squad Solo Mode Analysis**:
   - `OperativeCharacterMovement.cpp:68` checks `MantleSquad->IsSoloMode()`.
   - Log `MantleSquad->IsSoloMode()` and `Hostile->IsMantlePathAllowed()` on Setup and leg run.
   - Support testing under both `-SquadStart=solo` and formation mode to document that Team 1 mantle availability currently couples to Player squad solo mode.
   - Correct report sections 5.2 and 7 (smoke does not call `EnterSoloMode`).

3. **Ladder / Stair Height-Scaled Timeout**:
   - Tall ladders (24 rungs, 874 cm) require ~22-30s climb time, exceeding fixed 15s timeout.
   - Scale timeout dynamically:
     - Ladders: `FMath::Max(Config.LegTimeoutSeconds, 10.f + HeightCm / 40.f)`.
     - Stairs: `FMath::Max(Config.LegTimeoutSeconds, 10.f + Dist / 80.f)`.

4. **Team 0 (Player) Control Mode in Code**:
   - After Team 1 legs complete, run the same links with Player operative (Team 0, `Squad->GetMembers()[0]`).
   - Print a side-by-side comparison table in code: Team 1 (hostile) vs Team 0 (player control), proving whether failures are geometry bugs or AI filter / permission bugs.

5. **`Scripts/test_map.json` Globs & Smokes Restoration**:
   - Restore `"Source/**/Debug/Pmc*"` in `pmc-squad-ai-2026-11-03` (line 310).
   - Retain `LevelGenRampSmoke` in `pmc-traversal-2026-11-22`.
   - Document `LevelGenRampSmoke` in the report.

6. **Drop Arrival Ground Settle & Zero Velocity**:
   - After teleport, zero velocity immediately via `CharacterMovement->StopMovementImmediately()` and `Velocity = FVector::ZeroVector`.
   - Drops arrival criterion requires: `bGrounded` (`CharacterMovement->IsMovingOnGround()`) and near-zero vertical velocity (`FMath::Abs(Velocity.Z) < 30.f`). Arrival cannot trigger in mid-air.

7. **Programmatic Failure Grouping**:
   - Aggregate failure reasons in code using `TMap<FString, int32>` and print sorted by frequency descending.
   - Report must quote programmatic output directly.

