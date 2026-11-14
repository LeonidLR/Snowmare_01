# 050 Tactical Doorway Breach (Steps 1–3) Verification Report

- **Branch:** `gemini/050-door-breach`
- **Base Commit:** `664d78b27f54c0cda71a1651d8f6f8ce3bdf6ca1` (`main`)
- **Governing Specs:** `docs/design/DOORWAY_BREACH.md`, `docs/port/gemini/outbox/011-door-breach-plan.report.md`, and game designer decisions (2026-11-11)
- **Safety & Non-Regression:** C++ strictly restricted to spec paths (`Source/CodexTactics/Public/Tactics/`, `Private/Tactics/`, `Public/Interactables/`, `Private/Interactables/`, `Source/CodexTacticsTests/Private/Tactics/`). Designer maps (`L_Gen_Depot_1`, `L_MovementTest`) and engine/content packages untouched. All 7 new unit tests pass; all 5 smart smokes pass (zero regressions).

---

## 1. Summary of Work Completed

### Step 1: `DoorwayBreachRules` (Pure Math & Slot Logic)
- **Files:** `Source/CodexTactics/Public/Tactics/DoorwayBreachRules.h`, `Source/CodexTactics/Private/Tactics/DoorwayBreachRules.cpp`
- **Implemented:**
  - `FDoorwayBreachConfig`: Default standoffs ($30\text{ cm}$ along wall corner standoff, $45\text{ cm}$ wall surface standoff, $220\text{ cm}$ center overwatch standoff, $350\text{ cm}$ rear security standoff, $60\text{ cm}$ in-line fallback stack spacing, $60-600\text{ cm}$ doorway/gate width span, $45^\circ$ interior cross-aim angle, and $40\text{ cm}$ portal slack).
  - `FDoorwayBreachSlot` & `FDoorwayBreachTriad`: Rich data structures holding world foot location, rotation, aim direction vector, `FCoverSlot` data for wall-hugging jambs (`HighCover`, exposed edge flags, standoff distance), stance (`EOperativeStance::Crouching`), and obstruction flags.
  - `CalculateBreachSlots`:
    - Derives geometric coordinates for the full 4-slot constellation ($S_L, S_R, S_C, S_Z$) from doorway center, opening width, wall tangent, and outward normal.
    - $S_L$ (Left Jamb): Positioned on exterior wall face, $30\text{ cm}$ offset from left opening edge. Back to wall, peek facing Right, aiming cross-right into interior.
    - $S_R$ (Right Jamb): Positioned on exterior wall face, $30\text{ cm}$ offset from right opening edge. Back to wall, peek facing Left, aiming cross-left into interior.
    - $S_C$ (Center Point): Positioned $220\text{ cm}$ back on outward normal. Crouched low-ready, aiming straight through opening into center corridor.
    - $S_Z$ (Rear Security): Positioned $350\text{ cm}$ back on outward normal. Facing $180^\circ$ reverse direction, covering rear flank.
  - `ApplyJambObstruction`:
    - Handles asymmetric obstruction (Designer Answer #2: when one jamb is blocked by debris/missing navmesh, open jamb is kept, center covers, and 3rd operative stacks in-line behind open jamb operative).
  - `AssignSlots`:
    - **Solo Mode** (Designer Answer #1): When `bSoloMode == true` or 1 operative selected, assigns ONLY the selected operative to the nearest valid jamb; teammates remain stationary.
    - **Squad Mode** ($N = 2, 3, 4$): Bipartite Euclidean distance minimization over all slot permutations. Guarantees minimal squad travel and prevents crossing approach paths.
  - `IsPointInDoorwayPortal`: Fast lateral and depth check determining whether a cursor click falls inside a doorway portal interaction zone.

### Step 2: `UDoorwaySubsystem` (Runtime Discovery & Spatial Query)
- **Files:** `Source/CodexTactics/Public/Interactables/DoorwaySubsystem.h`, `Source/CodexTactics/Private/Interactables/DoorwaySubsystem.cpp`
- **Implemented:**
  - `UDoorwaySubsystem : public UWorldSubsystem`, zero tick (`PrimaryActorTick.bCanEverTick = false`).
  - `FRuntimeDoorway`: Stores doorway ID, kind (building door vs gate), building ID, wall group ID, 3D world center, outward normal, wall tangent, width, floor, walk layer, and mandatory route flag.
  - `LoadFromLayout` & `LoadFromLayoutJson`: Leverages `FPropsLayoutView` to parse layout JSON and populate runtime doorways.
  - `AutoDiscoverDoorways`: Automatically locates level layout on `OnWorldBeginPlay` (via `LevelJson` mapping or accepted layout fallback).
  - Spatial Queries:
    - `FindDoorwayNear(WorldPos, RadiusCm)`: Finds closest doorway within radius.
    - `FindDoorwayAtCursor(WorldHitPoint, SlackCm)`: Detects if cursor hit point lies within doorway portal opening.
    - `FindDoorwayById(Id)`: Direct ID lookup.
    - `GetBreachTriadForDoorway(Doorway, OutTriad, ApproachOrigin)`: Resolves outward approach normal for compound fence gates (Designer Answer #3), runs `CalculateBreachSlots`, tests NavMesh reachability for jambs, and applies obstruction fallbacks automatically if needed.

### Step 3: `ADoorwayBreachGhostActor` (Visual Triad Preview)
- **Files:** `Source/CodexTactics/Public/Tactics/DoorwayBreachGhostActor.h`, `Source/CodexTactics/Private/Tactics/DoorwayBreachGhostActor.cpp`
- **Implemented:**
  - Multi-silhouette ghost preview actor (`ADoorwayBreachGhostActor : public AActor`, zero tick).
  - 4 sub-ghost slots (`LeftVisual`, `RightVisual`, `CenterVisual`, `RearVisual`), each possessing a `USkeletalMeshComponent` and `UStaticMeshComponent` (fallback cylinder).
  - Uses translucent unlit silhouette material (`M_Silhouette` MID) with Cyan (`0.0, 0.85, 1.0, 0.55`) for valid breach spots and Amber (`1.0, 0.6, 0.0, 0.65`) for blocked/warning spots.
  - Replicates operative body mesh, transform, and anim poses:
    - Jambs: `CoverCrouchFireIdle` (leaning outward past frame with weapon raised).
    - Center/Rear: `Animations.CrouchIdle` (crouched low-ready).
  - Console Command `Codex.Test.ShowBreachGhost`:
    - Allows direct in-game and automation testing (`Codex.Test.ShowBreachGhost [doorway_id | hide]`).
    - Discovers doorway via `UDoorwaySubsystem`, queries squad via `USquadSubsystem`, and spawns/updates the breach ghost preview.

---

## 2. Verification Results

### A. Full Compilation (`powershell .\Scripts\build.ps1`)
- **Status:** **`Result: Succeeded`** (Exit code 0, 12.44 s build time).
- Zero warnings or errors in new codebase.

### B. Unit Automation Tests (`Source/CodexTacticsTests/Private/Tactics/DoorwayBreachRulesTest.cpp`)
All 7 tests passed:
1. `CodexTactics.Tactics.DoorwayBreach.StandardDoorSlots` — **PASS**
   - Validates exact 3D coordinates for $120\text{ cm}$ door at $(1000, 2000, 0)$ ($S_L = (1090, 2045, 0)$, $S_R = (910, 2045, 0)$, $S_C = (1000, 2220, 0)$, $S_Z = (1000, 2350, 0)$), peek facings, cover slot attributes, and stances.
2. `CodexTactics.Tactics.DoorwayBreach.WideGateSlots` — **PASS**
   - Validates slot offsets scaling for $300\text{ cm}$ compound gate ($|dy| = 180\text{ cm}$, $dx = 45\text{ cm}$).
3. `CodexTactics.Tactics.DoorwayBreach.SoloModeNearestJamb` — **PASS**
   - Validates that solo operative approaching door is assigned solely to the closest valid jamb (1 assignment returned).
4. `CodexTactics.Tactics.DoorwayBreach.MinimalTravelSquad` — **PASS**
   - Validates 3-man squad approaching from left, right, and center allocates optimally without path crossing ($Op_0 \to S_L, Op_1 \to S_R, Op_2 \to S_C$).
5. `CodexTactics.Tactics.DoorwayBreach.AsymmetricObstructionFallback` — **PASS**
   - Validates that when Left Jamb is blocked, Right Jamb and Center remain active, and 3rd operative stacks in-line behind Right Jamb.
6. `CodexTactics.Tactics.DoorwayBreach.PortalDetection` — **PASS**
   - Validates portal interaction bounding box (within opening $\pm 40\text{ cm}$ lateral, $\pm 50\text{ cm}$ depth).
7. `CodexTactics.Tactics.DoorwayBreach.Subsystem.DepotLayoutDiscovery` — **PASS**
   - Loads `gen_ref_01_02_depot_1.json` via `UDoorwaySubsystem`.
   - Discovers all **23 doorways** on Depot (14 building doors + 9 compound fence gates).
   - Validates gate `F_compound_N#g0` ($300\text{ cm}$), ruin door `B_ruin#0`, and spatial lookup `FindDoorwayNear(1400, -6084)`.

### C. Smart Regression Verification (`powershell .\Scripts\test.ps1 -Smart`)
- **Unit Tests:** **15 / 15 PASS** (0 failed).
- **Smoke Tests (5 / 5 PASS):**
  - `CombatFlowSmoke`: **PASS** (67.4 s)
  - `EventBusSmoke`: **PASS** (53.0 s)
  - `MissionSmoke`: **PASS** (74.8 s)
  - `MovementSmoke`: **PASS** (59.9 s)
  - `StanceSmoke`: **PASS** (72.1 s)
- Zero re-runs needed; zero regressions.

### D. Typesafe Audit
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` -> **PASS**.

---

## 3. Ambiguities & Open Questions
None. All design specifications from `DOORWAY_BREACH.md` and designer answers from 2026-11-11 were verified and satisfied cleanly in Steps 1–3.

Ready for Step 4 (Player Controller Hover & Click Integration) and Step 5 (Squad Movement Order Dispatch).
