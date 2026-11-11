# Tactical Doorway Breach & CQB Entry Specification

> **Status:** Architecture & Design Specification  
> **Author:** Gemini (Advanced Agentic Assistant)  
> **Date:** 2026-11-04  
> **Governing ADRs & References:** `docs/systems/squad-and-orders.md`, `docs/systems/cover-and-peek.md`, `docs/design/FORMATION_SECTORS.md`, `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp`  

---

## 1. Tactical Vision & Objectives

In close-quarters battle (CQB), thresholds and doorways represent the most hazardous terrain in urban combat ("The Fatal Funnel"). Entering a doorway blindly or stacking haphazardly results in immediate squad casualties from interior crossfire, prepared enemy ambushes, or threshold tripwires.

The user's core design requirement (2026-11-04):
- Hovering the cursor over an open doorway displays tactical **breach ghost positions**:
  - **One operative at the LEFT jamb** (snug against the exterior wall, in cover, head and rifle leaned past the frame, aiming inward).
  - **One operative at the RIGHT jamb** (snug against the exterior wall, in cover, head and rifle leaned past the frame, aiming inward).
  - **The third operative in the MIDDLE** (positioned in front of the door, offset 2.0–2.5 m back, aiming directly inside).
- This operates as a distinct, specialized tactical option alongside the existing linear wall stack (which remains fully preserved).
- The player must have intuitive, deterministic controls for choosing between standard wall cover and doorway breach, clear slot assignment, strict isolation between **Solo**, **Formation**, and **Box-Selection** modes, a clean follow-up **Enter / Clear** maneuver, and robust handling of architectural edge cases.

---

## 2. The Doorway Breach Triad Architecture

```
                    ROOM INTERIOR
             [Blind Left]     [Blind Right]
                    \             /
                     \   ROOM    /
                      \ INTERIOR/
                       \       /
     EXTERIOR WALL      v     v       EXTERIOR WALL
=======================[ DOOR ]=======================
   [Operative L]                        [Operative R]
   (Left Jamb Lean)                     (Right Jamb Lean)
   Aims: Deep Right                     Aims: Deep Left
   
                        2.2 m
                          |
                          v
                    [Operative C]
                    (Center Point)
                    Aims: Center
```

### 2.1 The Tactical Roles & Spatial Coordinates

1. **Left Jamb (Slot L — Point-Cover Left):**
   - **Position:** Against the exterior wall face, `30 cm` offset from the left door opening edge (`CoverCornerStandoffCm`).
   - **Stance:** Crouched (or standing if high cover), hugging wall in `bInCover`.
   - **Aim & Peek:** Leaned out past the jamb using `CoverRules::CornerLeanPoint` and `GetPeekSight()`.
   - **Assigned Sector:** **Deep Right Interior** (cross-coverage). Operative L controls the interior right corner, slicing the pie into the room from the left anchor.

2. **Right Jamb (Slot R — Point-Cover Right):**
   - **Position:** Against the exterior wall face, `30 cm` offset from the right door opening edge.
   - **Stance:** Crouched (or standing), hugging wall in `bInCover`.
   - **Aim & Peek:** Leaned out past the jamb using `CornerLeanPoint` and `GetPeekSight()`.
   - **Assigned Sector:** **Deep Left Interior** (cross-coverage). Operative R controls the interior left corner, slicing the pie into the room from the right anchor.

3. **Center Point / Over-Watch (Slot C — Center Pivot):**
   - **Position:** On the doorway's central exterior normal axis, offset `220 cm` back from the threshold line on the NavMesh.
   - **Stance:** Crouched low-ready. Not in wall cover, but stabilized with a clear sightline directly through the center of the doorway opening.
   - **Assigned Sector:** **Deep Center / Center Doorway**. Operative C watches the center corridor, suppressing any enemy charging straight through the opening while L and R clear the flanking corners.

4. **Fourth Operative (Slot Z — Rear Security, if 4 operatives active):**
   - **Position:** `350 cm` back from threshold, offset slightly opposite the approach direction.
   - **Stance:** Crouched or standing.
   - **Assigned Sector:** **180° Rear Exterior Security**. Watches the squad's six o'clock to prevent roaming patrols from collapsing onto the stacked team.

---

## 3. Player Selection & Cursor Interaction UX

### 3.1 Doorway Detection & Spatial Cursor Raycasting
In `ACodexTacticsPlayerController::UpdateCoverHover` (running in `Tick`):
1. **Raycast Query:** A cursor trace intersects geometry. If the cursor is over or near a doorway actor (`ADoorActor`, `AGateActor`, or architectural opening registered in `LevelGen` / `NavMesh` doorway tags):
2. **Hit Zone Disambiguation:**
   - **Zone A (Wall Face, > 60 cm from doorway opening):** Displays standard single/group wall cover ghosts (existing behavior, preserved 100%).
   - **Zone B (Doorway Opening, within door frame span + 40 cm threshold):** The cursor highlights the doorway portal. A subtle ground bracket illuminates the opening, and the **3-Man Doorway Breach Ghost Triad** is rendered!
3. **Execution Clicks:**
   - **First Hover / Single Click:** Renders the holographic breach triad ghosts (cyan for valid NavMesh spots, amber if partially blocked).
   - **Second Click (Confirm):** Commits the squad movement order `OrderDoorwayBreach`.
   - **Double Click:** Orders the squad to sprint into the breach positions.

### 3.2 Dual-Action Toggle (Cycling Doorway Modes)
What if the player wants a plain wall stack right next to a door instead of a breach formation?
- **Natural Boundary:** Clicking the solid wall outside the door frame always selects the wall stack.
- **Cycle Click:** If the cursor is positioned directly on the threshold, a mouse wheel scroll or pressing **Tab** cycles between:
  1. `Breach Stack (Cross-Cover L + R + Center)` *(Default)*
  2. `Single-Side Stack (Linear Wall Stack on Nearest Side)`

---

## 4. Strict Separation of the Three Squad Control Modes

Per the user's explicit directive, the three command modes must be cleanly differentiated:

| Control Mode | Action on Doorway Click | Operatives Affected | Follower Behavior |
| :--- | :--- | :--- | :--- |
| **1. Solo Mode** (`IsSoloMode() == true`, single operative selected) | **Single Jamb Anchor** | **Only the selected operative** | Unselected teammates **remain strictly stationary** in their current hold / cover / stance. The selected operative moves to the nearest door jamb, takes cover, and peeks inward. |
| **2. Formation Mode** (`Solo == false`, no box selection) | **Full Doorway Breach** | **Entire squad (Leader + Followers)** | The squad moves as a coordinated unit. Leader and followers dynamically allocate to Slots L, R, and C via minimal travel. |
| **3. Box-Selected Group** (2 or more operatives box-selected) | **Group Doorway Breach** | **Only the box-selected operatives** | - If 2 selected: fills Slots L and R (both jambs).<br>- If 3 selected: fills Slots L, R, and C.<br>- Any unselected operative **remains completely stationary** and ignores the order. |

---

## 5. Slot Assignment & Approach Mechanics

### 5.1 Minimal Travel Assignment (`DoorwayBreachRules::AssignSlots`)
To eliminate chaotic path crossing when approaching a door:
1. Candidate slots are generated: $S_L$ (Left Jamb), $S_R$ (Right Jamb), $S_C$ (Center Over-Watch).
2. For the participating operatives $\{O_1, O_2, O_3\}$, compute Euclidean distances to each slot.
3. Compute the permutation that minimizes $\sum \text{Dist}(O_i, S_j)$ subject to leader priority:
   - The Squad Leader (or selected leader in a box selection) is prioritized for the jamb that aligns with their approach angle, or the center anchor if closest.
   - Left-flank approaching operatives naturally take Slot L; right-flank operatives take Slot R.
   - Straight paths never cross ($O_1 \to S_L$ and $O_2 \to S_R$ do not intersect).

### 5.2 Approach Pathing Discipline ("Avoiding the Fatal Funnel")
When moving toward the door, operatives must not run directly through the doorway's central line of sight:
- Approach waypoints are routed along the exterior wall facade.
- Operatives L and R approach hugging their respective wall sides.
- Operative C approaches straight to the rear standby standoff (`220 cm`), establishing immediate visual lock into the center opening before L and R make their final corner step-in.

---

## 6. Physical Peek & Trap Spotting at the Jambs

Reusing the battle-tested systems from `docs/systems/cover-and-peek.md`:

1. **Automatic Corner Snap & Lean:**
   - As Operative L and Operative R reach their jamb positions, `TrySnapToCoverCorner` activates `CornerLeanPoint`.
   - Their heads and weapon muzzles physically lean $25\text{ cm}$ past the door jamb into the opening (`OperativeCharacterCornerAim.cpp`).
   - They enter the `CornerLean` pose without stepping their feet across the threshold.

2. **Cross-Cover Sight Cones:**
   - Operative L's sight cone projects into the room at $+35^\circ$ to $+75^\circ$ across the doorway, revealing the deep right interior.
   - Operative R's sight cone projects into the room at $-35^\circ$ to $-75^\circ$, revealing the deep left interior.
   - Operative C's cone covers the central cone ($0^\circ \pm 25^\circ$).
   - Together, the three cones form a unified coverage fan slicing $140^\circ$ of the interior room without a single boot stepping inside.

3. **Threshold Trap Sweeping (`TrapSpotRules.h`):**
   - Both jamb operatives run `UpdatePeekTrapSpotting` across the threshold floor.
   - Any hidden PMN antipersonnel mines, claymores, or laser tripwires placed across the doorway frame are detected physically within $1.5\text{ s}$ (`SweepSeconds 1.5`, range up to $12\text{ m}$).
   - Spotted traps are tagged and rendered on HUD/Minimap, allowing the Medic-Sapper or Engineer to disarm them before entry.

---

## 7. The Follow-Up "Enter / Clear" Action

Once all operatives have established the doorway breach stack, a follow-up breach command is presented to the player.

### 7.1 Command Triggers
- Hovering the cursor inside the room displays the **Room Clear Waypoint Vector**.
- Confirmed by a second click inside the room, pressing **Space**, or pressing **F** (Breach & Clear).

### 7.2 CQB Room Entry Sequence & Trajectories

```
                    ROOM INTERIOR
           [Zone L]                [Zone R]
              ^                       ^
              |  Hook Left            |  Hook Right
              |   (Man 2)             |   (Man 1)
              +-------+       +-------+
                      |       |
                      [ DOOR ]
                         ^
                         | Drive Center
                         |  (Man 3)
```

1. **Step 1: The First Hook (Man 1 — Right Jamb Operative):**
   - Steps across the threshold, immediately hooks right along the interior wall (`Buttonhook / Criss-Cross`).
   - Clears immediate right blind corner (Zone R), establishes dominant corner hold.
2. **Step 2: The Second Hook (Man 2 — Left Jamb Operative):**
   - Enters immediately behind Man 1 ($0.3\text{ s}$ stagger), hooks left along the interior wall.
   - Clears immediate left blind corner (Zone L), establishes opposite corner hold.
3. **Step 3: The Center Drive (Man 3 — Center Point Operative):**
   - Drives through center threshold, takes 3 paces forward, halts in center crouch covering interior depth / interior doors.
4. **Step 4: Room Cleared:**
   - Squad forms an interior pie-slice wedge.
   - Radio callout: *"Room clear! Hold sectors."*

---

## 8. Edge Cases & Resilience Rules

1. **Narrow Doorways (< 100 cm width):**
   - If a doorway is too narrow for two simultaneous jamb leans, Slot L and Slot C are preserved, while Slot R converts into a tight rear stack behind Slot L (`Linear Jamb Stack`).
2. **Damaged / Debris-Blocked Doorway Frames:**
   - NavMesh raycasts test both jamb standings. If rubble blocks the right jamb NavMesh, Slot R automatically falls back to an angled standoff `1.5 m` away.
3. **Doors on Roofs, Catwalks, or Upper Floors:**
   - Validates that the exterior jamb footing exists and is not an open fall or drop link (`UDropLinkSubsystem`). If a jamb is off a ledge, that slot is disabled and substituted with center standby.
4. **Active Enemies Spotted Inside during Hover:**
   - If an enemy inside the room is already alert and aiming at the doorway, the breach ghost indicates an amber warning outline with a projected danger vector ("Target aiming at threshold").
   - Entering operatives maintain weapon-ready stance and can fire immediately upon leaning.
5. **Locked / Blast Doors:**
   - If the door is locked, clicking the breach formation moves the squad into stack positions, and the prompt changes to **"Breach Door (Sapper / Shotgun / Kick)"**. Center man or Sapper approaches to force the lock while jamb men provide lethal cover.

---

## 9. Visuals, Ghosts & UI Feedback

1. **Holographic Breach Ghosts:**
   - Rendered using the project's translucent preview shader (`M_CoverPreviewGhost` / `M_SimpleUnlitTranslucent`).
   - Three distinct silhouettes showing crouched operatives, leaned heads, and weapon directional arrows.
2. **Dynamic Laser Aim Traces:**
   - Faint dashed lines connecting Slot L to interior right corner, Slot R to interior left corner, and Slot C straight ahead.
3. **Context Banner:**
   - Status text in tactical HUD: `[BREACH] Doorway Stack Ready (L: Cross-Right, R: Cross-Left, C: Center Point)`.

---

## 10. Code Seams & Reusable Subsystems

No new wheels invented; this design maps directly to existing C++ systems:

1. **Input & Player Controller:**
   - `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerCover.cpp`
   - Extend `UpdateCoverHover` to recognize doorway interaction bounds and instantiate `FDoorwayBreachPreview`.
2. **Squad & Movement Management:**
   - `Source/CodexTactics/Private/Characters/SquadSubsystemGroupCover.cpp`
   - Implement `OrderDoorwayBreach` using `OrderTakeCover` for jambs and `OrderMoveTo` for center point.
3. **Cover & Corner Aim:**
   - `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp`
   - Reuse `CornerLeanPoint`, `GetPeekSight()`, `FPeekSightConfig`.
4. **Trap Detection:**
   - `Source/CodexTactics/Public/Interactables/TrapSpotRules.h`
   - Reuse `UpdatePeekTrapSpotting` for threshold security.
5. **Formations & Sectors:**
   - `Source/CodexTactics/Private/Characters/Formation/`
   - Reuse sector allocations from `FORMATION_SECTORS.md`.

## The game designer's answers (2026-11-11), on Gemini's plan docs/port/gemini/outbox/011-door-breach-plan.report.md
1. Solo: clicking a doorway sends ONLY the selected operative to the nearest jamb (cover + peek); the others stay.
2. One jamb blocked (crate / drop / no navmesh): the triad is not cancelled - the open jamb is taken, the centre covers, the third
   stacks behind the jamb man.
3. Building doors AND fence / compound gates (PropsLayoutView doorways incl. gates up to 600 cm) both support the breach.
4. Choice breach vs wall cover: a click INTO the opening = the breach triad; a click on the wall more than 40 cm from the opening =
   ordinary wall cover. NO Tab toggle - the existing two-click pattern (the first click shows the ghosts, a second click on the same
   spot confirms), like cover orders.
5. The centre man crouches at 2.2 m from the door.
6. Enter / clear: once the triad is set, a click inside the room = the buttonhook entry (man 1 right corner, man 2 left, man 3 centre).
Week 2, after level authoring stage 1 (6 steps in the plan, section 4).
