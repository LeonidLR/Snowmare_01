# Tactical Doorway Breach Implementation Plan (Current Codebase Review)

> **Document:** `docs/port/gemini/outbox/011-door-breach-plan.report.md`  
> **Reference Spec:** `docs/design/DOORWAY_BREACH.md` (authored 2026-11-04)  
> **Status:** Architecture & Implementation Plan against main as of 2026-11-11  
> **Author:** Gemini (Advanced Agentic Assistant)

---

## 1. Executive Summary & Code Audit

`docs/design/DOORWAY_BREACH.md` specified a 3-man tactical CQB doorway stack:
- **Left Jamb ($S_L$):** Hugs exterior wall in cover, leans head/rifle past left door frame, covers Deep Right Interior.
- **Right Jamb ($S_R$):** Hugs exterior wall in cover, leans head/rifle past right door frame, covers Deep Left Interior.
- **Center Point ($S_C$):** Positioned $2.0 - 2.5\text{ m}$ back on normal axis, crouched low-ready, covers Center Entryway.
- **Fourth Man ($S_Z$):** Positioned $3.5\text{ m}$ back, covers $180^\circ$ rear exterior security.

Review of the current C++ codebase (`Source/CodexTactics/**`) shows that **over 70% of the underlying mechanics already exist** in mature subsystems (corner peek aiming, trap detection cones, ghost silhouette rendering, formation wall leadership, and layout doorway data). However, they are currently disjoint and have not been unified into a dedicated Doorway Breach interaction flow.

---

## 2. Existing Reusable Code (Exact Citations)

| Capability | File & Line | Reusable Component & Mechanics |
| :--- | :--- | :--- |
| **Corner Peek & Standoff** | `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp:416-465` | `AOperativeCharacter::OrderTakeCoverForPeek(CornerSlot, EdgeOnFace, bSprint)`: Orders an operative into a corner slot and automatically arms/triggers the physical corner peek upon arrival. |
| **Corner Lean Point Math** | `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp:817` | `CoverRules::CornerLeanPoint(CoverSlot, CoverFacing, EyePos, OffsetCm)`: Computes the exact 3D position of the operative's head/weapon leaned $25\text{ cm}$ past the door jamb into the opening. |
| **Threshold Trap Spotting** | `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp:847-920` | `AOperativeCharacter::UpdatePeekTrapSpotting(DeltaTime)`: Uses `TrapSpotRules::PeekSweepRange` and `TrapSpotRules::IsInPeekSightCone` to detect tripwires/mines at the threshold. |
| **Corner Peek Ghost Visuals** | `Source/CodexTactics/Public/Tactics/CoverGhostActor.h:52-56` | `ACoverGhostActor::ShowCornerPeekFor(Operative, Slot, Side, CrouchedOverride)`: Displays translucent holographic silhouette in full corner-aim pose (`cvr_*_fire_idle` with rifle leaned outward). |
| **Cover Preview State Machine** | `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerCover.cpp:140-395, 690-750, 837-860` | `ShowCoverPreview`, `HideCoverPreview`, `ConfirmCoverPreview`, `HasCoverPreview`: Handles the two-click cover workflow (1st click = preview ghost, 2nd click = confirm order). |
| **Formation Wall Leadership** | `Source/CodexTactics/Private/Formation/FormationWallLeadershipRules.cpp:1-120` | `FormationWallLeadership::PlanGroupCornerPeek`: Given operative locations along a wall, assigns the closest operative to the corner edge and orders the others into a synchronized stack. |
| **Squad Wall Group Looks** | `Source/CodexTactics/Private/Characters/SquadSubsystemWallGroup.cpp:53-150` | `USquadSubsystem::ReallocateWallGroup`: Assigns coordinated looks (`EWallGroupLook::AlongWall`, `FacingOut`) to ensure all sectors are watched without wall stare. |
| **Wall Standoff & Geometry** | `Source/CodexTactics/Private/Tactics/WallSideRules.cpp:1-150` | Determines which side of a wall face an operative is on and handles normal/tangent projection. |
| **Canonical Doorway Data** | `Source/CodexTactics/Public/LevelGen/PropsLayoutView.h:113-147` | `FPropsDoorway` struct contains door/gate position `Pos`, opening width `WidthCm`, wall direction `Along`, exterior normal `Outward`, `Building`, `WallGroup`, and `WalkLayer`. |
| **Runtime Doorway Detection** | `Source/CodexTactics/Private/Combat/AmbushSpotRules.cpp:99-145` | `AmbushSpotRules::IsDoorwayPair`: Runtime heuristic detecting doorway openings between wall ends (`DoorwayMinCm` to `DoorwayMaxCm`). |
| **Order Dispatch Plumbing** | `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp:210-425` | `ACodexTacticsPlayerController::HandleWorldHit`: Central click dispatcher intercepting ladder, vehicle, trap, and cover clicks. |

---

## 3. What the Spec Needs That Does NOT Yet Exist

1. **`FDoorwayBreachRules` (Pure Geometry & Slot Logic):**
   - No pure struct currently computes the 4-slot breach constellation $(S_L, S_R, S_C, S_Z)$ given a doorway frame.
   - Needs pure algorithm taking Doorway Center, Opening Width, Wall Tangent, and Exterior Normal, generating:
     - $S_L$: Left door jamb corner cover slot ($30\text{ cm}$ from left edge, facing along wall into opening).
     - $S_R$: Right door jamb corner cover slot ($30\text{ cm}$ from right edge, facing along wall into opening).
     - $S_C$: Center standoff ($220\text{ cm}$ back on outward normal, NavMesh snapped).
     - $S_Z$: Rear security ($350\text{ cm}$ back on outward normal, $180^\circ$ reverse facing).
   - Needs minimal-travel bipartite assignment algorithm (`AssignSlots`) mapping $N$ operatives to the slots.

2. **Runtime Doorway Spatial Index (`UDoorwaySubsystem`):**
   - While `FPropsLayoutView` exists in `LevelGen`, it is authoring-time code. The gameplay world does not maintain an active spatial KD-tree or list of doorways for cursor raycasting.
   - Need a lightweight runtime registry populated at level start (reading `FPropsLayoutView` doorways or caching doorway wall-end pairs).

3. **Multi-Ghost Triad Preview Actor (`ADoorwayBreachGhostActor`):**
   - `ACoverGhostActor` currently only displays a single operative ghost at a single cover slot.
   - Doorway breach requires simultaneously rendering 3 (or 4) preview silhouettes with dashed sightline lasers crossing the threshold.

4. **Controller Hover & Breach Order Integration:**
   - In `CodexTacticsPlayerControllerCover.cpp`, `UpdateCoverHover` currently only searches for solid wall covers (`FindCoverSlotAt`). It must check if cursor is over a doorway threshold, highlighting the portal and showing the breach triad.
   - In `CodexTacticsPlayerControllerOrders.cpp`, `HandleWorldHit` needs to handle the second click confirming `OrderDoorwayBreach`.

5. **Follow-Up "Enter & Clear" CQB Maneuver:**
   - No sequence exists for the synchronized room entry (buttonhook right for Man 1, buttonhook left for Man 2, drive center for Man 3).

---

## 4. Implementation Step List for Claude

| Step | Scope / Files | Size | Acceptance Smoke Test |
| :---: | :--- | :---: | :--- |
| **1** | **`DoorwayBreachRules` (Pure Math)**<br>`Public/Tactics/DoorwayBreachRules.h`<br>`Private/Tactics/DoorwayBreachRules.cpp`<br>`Tests/DoorwayBreachRulesTest.cpp` | **M** | Unit test verifying: given a $120\text{ cm}$ doorway, computes exact positions for $S_L, S_R, S_C, S_Z$; correctly assigns approaching squad members by minimal Euclidean distance; drops blocked jambs cleanly. |
| **2** | **`UDoorwaySubsystem` (Runtime Discovery)**<br>`Public/Interactables/DoorwaySubsystem.h`<br>`Private/Interactables/DoorwaySubsystem.cpp` | **S** | In `BeginPlay`, populates doorways from `PropsLayoutView` / level data. Exposes `FindDoorwayNear(WorldPos, Radius)` returning `FPropsDoorway`. Smoke: Depot level finds all 23 doorways/gates. |
| **3** | **`ADoorwayBreachGhostActor` (Visual Triad)**<br>`Public/Tactics/DoorwayBreachGhostActor.h`<br>`Private/Tactics/DoorwayBreachGhostActor.cpp` | **M** | Spawns 3 translucent silhouette skeletal meshes (Slot L peek, Slot R peek, Slot C low-ready) with cyan/amber materials and forward aim arrows. Tested via console command `Codex.Test.ShowBreachGhost`. |
| **4** | **Controller Hover & Click Dispatch**<br>`Private/Core/CodexTacticsPlayerControllerCover.cpp`<br>`Private/Core/CodexTacticsPlayerControllerOrders.cpp` | **M** | Hovering cursor over doorway portal shows breach ghost triad. First click locks preview; second click commits `OrderDoorwayBreach`. Clicking outside door (> 40 cm) reverts to standard wall cover. Solo mode only moves selected operative to nearest jamb. |
| **5** | **Squad Movement & Arrival Execution**<br>`Public/Characters/SquadSubsystem.h`<br>`Private/Characters/SquadSubsystem.cpp`<br>`Private/Characters/OperativeCharacter.cpp` | **M** | Squad moves into formation: Jamb operatives approach hugging walls and execute `OrderTakeCoverForPeek`; Center operative takes crouched position at $2.2\text{ m}$; sight cones cross-cover interior; `UpdatePeekTrapSpotting` actively sweeps threshold. |
| **6** | **Enter & Clear CQB Sequence**<br>`Public/Tactics/RoomClearRules.h`<br>`Private/Tactics/RoomClearRules.cpp` | **L** | When stacked at doorway, clicking interior floor triggers Room Clear: Man 1 buttonhooks into immediate right corner; Man 2 buttonhooks left; Man 3 drives center; squad calls out "Room clear". |

---

## 5. Open Design Questions for the Game Designer

1. **Solo Mode Behavior at Doorways:**
   - *Question:* When a single operative is selected (`IsSoloMode() == true`), should clicking a doorway order a 1-man breach or summon the squad?
   - *Recommended Default:* **Solo action only.** The selected operative moves to the nearest door jamb, takes cover, and peeks inside. Unselected squad members remain strictly stationary at their current positions.

2. **Asymmetric / Partially Blocked Doorway Jambs:**
   - *Question:* What should happen if one jamb is obstructed by debris, a crate, or a drop ledge (e.g. NavMesh missing on right side)?
   - *Recommended Default:* **Asymmetric Stack.** Slot L takes the open jamb, Slot C takes center over-watch, and Slot R falls back to an in-line stack behind Slot L (or stands off $1.5\text{ m}$). The formation never cancels entirely due to one blocked jamb.

3. **Inclusion of Compound Fence Gates:**
   - *Question:* Should open perimeter gates (such as Depot's wire fence openings) support the breach stack, or only building doors?
   - *Recommended Default:* **Support both building doors and gates.** In urban tactics and in `PropsLayoutView`, fence openings operate identically to door thresholds (fatal funnels requiring flanking jamb cover).

4. **Disambiguation between Wall Cover and Breach Stack:**
   - *Question:* How should the player choose between taking standard linear wall cover next to a door versus initiating a breach stack?
   - *Recommended Default:* **Spatial Threshold + Tab Toggle.** Clicking solid wall $> 40\text{ cm}$ from the opening always gives standard wall cover. Clicking inside the opening gives Breach Stack. If hovering the boundary, pressing **Tab** (or Mouse Wheel) cycles between `Breach Stack` and `Linear Wall Stack`.

5. **Stance of Center Point Operative ($S_C$):**
   - *Question:* Should the Center Point operative stand or crouch?
   - *Recommended Default:* **Crouch low-ready at $2.2\text{ m}$ standoff.** This gives stability, reduces silhouette against exterior light, and allows an operative in Slot Z (rear security) to shoot over them if needed.

6. **Triggering the Follow-Up "Enter / Clear" Action:**
   - *Question:* How should the player order the final entry into the room once stacked?
   - *Recommended Default:* **Click inside the room or press [F].** Once all 3 operatives are settled in the breach stack, hovering inside the room highlights the interior corners with green entry arrows. A left click inside the room (or pressing [F]) initiates the buttonhook clearing maneuver.
