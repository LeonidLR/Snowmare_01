# Tactical Mantling & Ledge Traversal Specification

> **Status:** Architecture & Design Specification  
> **Author:** Gemini (Advanced Agentic Assistant)  
> **Date:** 2026-11-15  
> **Task:** 051 (Design Document for Mantling & Vertical Ledge Traversal)  
> **Governing ADRs & References:** `docs/systems/stances-and-movement.md`, `Source/CodexTactics/Public/Characters/VaultRules.h`, `Source/CodexTactics/Private/Characters/OperativeCharacterVault.cpp`, `Source/CodexTactics/Public/Falls/DropLinkSubsystem.h`, `docs/design/levelgen/PROPS_LAYERS.md`, `docs/design/FORMATION_CONTRACT.md`

---

## 1. Executive Summary & Tactical Vision

In tactical urban combat, the environment is rarely flat. Operatives encounter freight crates, low walls, abandoned vehicles, concrete barricades, train flatcars, and elevated platforms. Currently in `CodexTactics`, infantry movement is polarized into two extremes:
1. **Low Vaulting (`VaultRules`):** An operative hops over low obstacles ($25\text{ cm} \le H \le 105\text{ cm}$) and immediately lands on the ground on the *far side*, provided the obstacle is thin ($\le 130\text{ cm}$).
2. **Ladder Climbing (`ALadderActor` / `LadderRules`):** An operative mounts a formal vertical ladder to ascend full-story structures ($> 250\text{ cm}$).

### 1.1 The "$110\text{ cm}$ Crate Paradox" on Generated Maps
On `L_Gen_Depot_1` and `L_Gen_Train_*`, freight crates have a standardized height of **$110\text{ cm}$** (`golden_small_crates.json`, `PROPS_LAYERS.md`). Because the legacy Godot vault limit was hardcoded to $105\text{ cm}$ (`VaultRules::MaxHeight = 105.f`), every crate is exactly **$5\text{ cm}$ taller than the vault threshold**. Furthermore, crates, shipping containers, and train flatcars are wide volumes ($120 - 250\text{ cm}$ deep), not thin barriers:
- The pathfinder refuses to vault because there is no ground immediately behind the front edge.
- If an operative is forced onto a crate top, `UpdateObstacleStepOff` treats it as an error ("standing on obstacle top") and kicks them off after $0.3\text{ s}$.
- Squads are forced to pathfind through lengthy, circuitous detours around rows of crates and flatcars, destroying tactical fluidity and exposing operatives to flanking crossfire.

### 1.2 The Mantling Solution
**Mantling** bridges the critical gap between horizontal vaulting and vertical ladder climbing. It allows operatives to:
- Scale obstacles ranging from **$75\text{ cm}$ up to $240\text{ cm}$** in height.
- **Climb ONTO and remain standing/crouched on the elevated surface** (crates, containers, flatcars, low roofs).
- Transition dynamically into cover, overwatch, or firing positions from the high ground.
- Execute a **controlled step-down / drop** back to ground level without triggering unguided ragdoll falls or damage.

```
      VAULTING (Over to Far Ground)                    MANTLING (Climb Onto Elevated Surface)
     [25 - 105 cm, Thin Obstacle]                    [75 - 240 cm, Deep Obstacle / Platform]

           .--.                                                    .--.
          /    \ (Apex Arc)                                       /    \ (Pull Up)
         v      v                                                v      `------> [Walkable Top]
[Ground] ===[BARRIER]=== [Ground]                       [Ground] ===[CRATE / ROOF]=================
```

---

## 2. Obstacle Height Taxonomy & Animation Asset Mapping

### 2.1 Height Classification Matrix

| Tier | Category | Height Span | Obstacle Depth | Character Action | Animation Asset Reference | Movement Mode |
| :---: | :--- | :---: | :---: | :--- | :--- | :--- |
| **0** | **Step / Stride** | $0 - 35\text{ cm}$ | Any | Standard foot step-up / curb stride | Natural CMC step-up (`MaxStepHeight = 45 cm`) | `MOVE_Walking` |
| **1** | **Low Vault / Hurdle** | $25 - 105\text{ cm}$ | $\le 130\text{ cm}$ | Stride/hop *over* obstacle to ground on far side | `Content/RifleAnims/Rifle_2/Traverse/M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_Lfoot_Rifle.uasset`<br>`M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_Rfoot_Rifle.uasset`<br>`Content/Polygonmaker/Animations/Interactions/UE5/Anim_Regular_jump_fence.uasset` | `MOVE_Flying` / Arc Interp |
| **2** | **Low Mantle (Clamber)** | $75 - 130\text{ cm}$ | $> 80\text{ cm}$ | Hands on top edge, spring off ground, pop up into low crouch on top | `Content/Polygonmaker/Animations/Interactions/UE5/Anim_Regular_climb_up.uasset`<br>`Anim_Regular_climb_up-inplace.uasset` | `MOVE_Custom` (`CMOVE_Mantle`) |
| **3** | **High Mantle (Pull-Up)** | $130 - 240\text{ cm}$ | $> 60\text{ cm}$ | Overhead grab, hang/muscle-up, knee on edge, rise to stand | `Content/Polygonmaker/Animations/Interactions/UE5/Anim_Regular_jump_climb.uasset`<br>`Anim_Regular_jump_climb_inplace.uasset` | `MOVE_Custom` (`CMOVE_Mantle`) |
| **4** | **Ladder Ascend** | $> 240\text{ cm}$ | Any | Formal ladder rung climb | `Content/Characters/Operatives/Anims/Ladder/AS_Ladder_UpLoop.uasset`<br>`AS_Ladder_ClimbOutTop.uasset` | `MOVE_Custom` (`CMOVE_Ladder`) |
| **5** | **Controlled Drop-Down** | $60 - 250\text{ cm}$ | Downward | Controlled hop off ledge, soft bent-knee landing | `Content/Polygonmaker/Animations/Interactions/UE5/Anim_Regular_jump_down.uasset`<br>`Anim_Regular_jump_down_inplace.uasset` | `MOVE_Custom` (`CMOVE_LedgeDrop`) |

### 2.2 Weapon Discipline & Ready Times

1. **Tier 2 (Low Mantle, $75 - 130\text{ cm}$):**
   - **One-Handed Retention:** The operative plants one hand (non-dominant) on the obstacle ledge while maintaining their primary weapon (rifle, SMG, shotgun) in their dominant hand at low-ready.
   - **Weapon Draw Latency:** **Zero unequip penalty**. Aim recovery takes **$0.2\text{ s}$** upon feet planting on the elevated surface.
   - **Speed / Duration:** $0.75\text{ s}$ total traversal time ($0.65\text{ s}$ while sprinting).

2. **Tier 3 (High Mantle, $130 - 240\text{ cm}$):**
   - **Two-Handed Pull-Up:** Scaling a wall or container above chest level requires both hands.
   - **Sling Latency:** Primary rifle is automatically slung across the chest/back during the pull-up phase ($0.4\text{ s}$ sling transition).
   - **Unsling Latency:** Upon vaulting knees over the lip, the operative re-grips the weapon ($0.4\text{ s}$ re-draw).
   - **Vulnerability Window:** Total unarmed window is **$0.8\text{ s}$**. Operatives are vulnerable to incoming fire during this brief transition (reinforced by squad overwatch discipline, §6).
   - **Speed / Duration:** $1.25\text{ s}$ total traversal time.

---

## 3. Geometric Detection & Tracing Pipeline

To achieve 100% deterministic, glitch-free mantling without phantom triggers or capsule snags, a 4-stage raycast/shapecast query is executed:

```
                      [Stage 2: Downward Lip Trace]
                                   |
                                   v
                         [Ledge Lip Hit]
                               +------------+
                               |            |
 [Stage 1: Forward Trace] ---->|  OBSTACLE  | [Stage 4: Top Clearance Box]
                               |   (CRATE)  |
                               |            |
===============================+------------+==============================
 [Stage 0: Ground Base]
```

### 3.1 The 4-Stage Detection Filter (`MantleTraceRules`)

1. **Stage 1: Forward Obstacle Detection (Horizontal Line Trace)**
   - **Origin:** Operative root $+ (0, 0, \text{ChestZ})$ (typically $100\text{ cm}$ above feet).
   - **Direction:** Movement direction or PathFollowing tangent.
   - **Reach:** $120\text{ cm}$ forward (`ApproachReachCm`).
   - **Filter:** Ignores living pawns; hits `ECC_WorldStatic` and `ECC_WorldDynamic`.
   - **Output:** Wall contact point $P_{\text{wall}}$ and inward wall normal $\vec{N}_{\text{wall}}$.

2. **Stage 2: Ledge Lip & Height Identification (Downward Shapecast)**
   - **Trace Start:** $P_{\text{wall}} + \vec{D}_{\text{fwd}} \times 25\text{ cm} + (0, 0, 260\text{ cm})$ (elevated above maximum mantle height).
   - **Trace End:** $P_{\text{wall}} + \vec{D}_{\text{fwd}} \times 25\text{ cm} - (0, 0, 10\text{ cm})$ (below ground).
   - **Shape:** Vertical capsule or sphere ($R = 15\text{ cm}$).
   - **Output:** Exact ledge top impact point $P_{\text{top}}$, surface normal $\vec{N}_{\text{top}}$, and obstacle height:
     $$H_{\text{obstacle}} = P_{\text{top}}.Z - \text{GroundZ}$$

3. **Stage 3: Disambiguation (Vault vs. Mantle vs. Wall)**
   - **Test 3A (Far-Side Ground Clearance):** Trace downward behind the obstacle at $P_{\text{wall}} + \vec{D}_{\text{fwd}} \times 135\text{ cm}$.
     - If firm ground exists with drop $\le 105\text{ cm}$ AND obstacle depth $\le 130\text{ cm}$:
       $$\implies \mathbf{TRIGGER\ VAULT\ (OVER)}$$
     - If no ground exists within $130\text{ cm}$ (the obstacle continues as a roof or deep box):
       $$\implies \mathbf{TRIGGER\ MANTLE\ (ONTO)}$$
   - **Test 3B (Height Gates):**
     - $H < 75\text{ cm}$: Too low for mantle (treated by Tier 0 step or Tier 1 vault).
     - $75\text{ cm} \le H \le 130\text{ cm}$: **Tier 2 Low Mantle**.
     - $130\text{ cm} < H \le 240\text{ cm}$: **Tier 3 High Mantle**.
     - $H > 240\text{ cm}$: Too tall for manual pull-up (routes to ladder or blocked).

4. **Stage 4: Landing Footprint & Head Clearance Verification**
   - **Landing Target:** $P_{\text{land}} = P_{\text{top}} + \vec{D}_{\text{fwd}} \times 45\text{ cm} + (0, 0, \text{CapsuleHalfHeight})$.
   - **Overlap Query:** Overlap check with operative capsule ($R = 34\text{ cm}, H = 88\text{ cm}$).
   - **Headroom Check:** Shapecast upwards from $P_{\text{land}}$ to $+190\text{ cm}$ to ensure no low overhang, pipe, or ceiling obstructs standing upright.
   - If landing footprint is obstructed by props or another operative: mantle is refused; operative remains in cover at base.

---

## 4. Character Movement Component (CMC) & Root Motion Architecture

### 4.1 Custom Movement Mode (`MOVE_Custom`)
Mantling cannot be handled by simple `MOVE_Walking` (which sticks to ground primitives) or raw physics velocity (which causes jittery collisions against wall lips).

Following the Unreal Engine Gameplay Framework standards:
- Define `ECustomMovementMode::CMOVE_Mantle = 1`.
- When mantle initiates:
  ```cpp
  CharacterMovement->SetMovementMode(MOVE_Custom, CMOVE_Mantle);
  ```
- During `CMOVE_Mantle`:
  - Gravity is disabled (`GravityScale = 0.f`).
  - Velocity is driven exclusively by **Root Motion** or **Motion Warping**.
  - Capsule collision against the mantleable object is switched to overlap (`IgnoreActorWhenMoving`), preventing the capsule from catching on the sharp lip.
  - Upon arrival at $P_{\text{land}}$:
    - Capsule collision restored.
    - `CharacterMovement->SetMovementMode(MOVE_Walking)`.
    - NavMesh tracking resumed.

### 4.2 Motion Warping & Target Anchors
To allow the animations (`Anim_Regular_climb_up`, `Anim_Regular_jump_climb`) to scale dynamically across varying obstacle heights ($75 - 240\text{ cm}$):
- Three warp targets are defined:
  1. `WarpTarget_Hands`: Located at $(P_{\text{top}}.X, P_{\text{top}}.Y, P_{\text{top}}.Z)$, aligned with ledge lip.
  2. `WarpTarget_Knees`: Located at lip $+ 15\text{ cm}$ forward, $+ 10\text{ cm}$ above top.
  3. `WarpTarget_Land`: Located at $P_{\text{land}}$ on the walkable surface.
- Motion Warping smoothly transforms the root motion curves so hand grips and foot placements land with sub-centimeter visual precision on any crate, barrier, or ledge.

### 4.3 Fallback Interpolation (Sine-Smoothstep Arc)
For builds or headless test environments where Motion Warping is uninitialized:
- Use an exact, pure mathematical interpolation curve:
  $$\vec{X}(\alpha) = \text{SmoothStep}(\vec{X}_{\text{start}}, \vec{X}_{\text{land}}, \alpha)$$
  $$Z(\alpha) = (1 - \alpha) \cdot Z_{\text{start}} + \alpha \cdot Z_{\text{land}} + \sin(\pi \alpha) \cdot \text{ApexBonus}$$
- Guarantees deterministic, collision-free landing in headless automation smokes.

---

## 5. NavMesh & Tactical Grid Integration

### 5.1 NavMesh Layering on Obstacle Tops
In `CodexTactics`, levels generated by `build_levelgen_map.py` place crates, jersey barriers, and containers. For the pathfinder to route operatives *onto* an elevated surface:
1. **Walkable Slope & Step Tuning:**
   - Standard NavMesh `MaxStepHeight` remains $45\text{ cm}$ (prevents walking up sheer walls).
   - Crates and flatcars have collision top faces marked with `bIsWalkable = true`.
   - `ARecastNavMesh` generates isolated navmesh islands on surfaces $\ge 60\text{ cm} \times 60\text{ cm}$.

### 5.2 Off-Mesh Mantle Links (`UNavLinkCustomComponent` / `FDropLinkReach`)
To connect the ground NavMesh to the elevated crate/container NavMesh:
- At level start (or prop placement in `CodexLevelPropsSubsystem`), every mantleable prop generates bidirectional or directional off-mesh links:
  - **Mantle-Up Link:** Links ground approach spot to crate top landing. Cost modifier = $1.5\times$ normal travel (operatives prefer walking flat ground unless detour is $> 8\text{ m}$).
  - **Drop-Down Link:** Links crate top edge to ground landing below. Cost modifier = $1.0\times$ (fast egress).
- Reuses the battle-tested `FDropLinkReach` structure from `DropLinkSubsystem.h`, preventing softlocks and ensuring pathfinding seamlessly incorporates vertical climbing.

---

## 6. Squad Coordination & Tactical Behavior

### 6.1 Leadership & Staggered Traversal Discipline
A common pitfall in squad tactics games is "capsule clumping", where 3 operatives simultaneously try to mantle the same crate and get wedged together.

`CodexTactics` implements strict **Staggered Mantle Discipline** via `SquadSubsystem`:
1. **Leader Precedence:** The squad leader (or closest operative in box selection) claims the mantle link exclusively.
2. **Follower Hold & Stagger:**
   - Followers approaching the obstacle maintain a minimum standoff queue of **$120\text{ cm}$**.
   - Follower 2 does not begin the mantle animation until Follower 1's feet have fully planted on $P_{\text{land}}$ and advanced at least $60\text{ cm}$ away from the ledge lip.
3. **Queue Timeout:** If the lead operative halts on the crate lip and refuses to move, followers dynamically re-route around the obstacle rather than waiting indefinitely.

```
       [Squad Leader] (Mantling Ledge)
             |
             v
       +-----------+
       |   CRATE   |
       +-----------+
            ^
            |  1.2 m Standoff
            |
       [Follower 1] (Covering Ground Right)
            |
            |  1.2 m Standoff
            |
       [Follower 2] (Covering Ground Rear)
```

### 6.2 Overwatch & Sector Security While Climbing
An operative who is pulling themselves up onto a ledge cannot return fire for $0.8 - 1.2\text{ s}$.
- While the leader mantles, followers automatically rotate their aim outward:
  - Follower 1 takes a crouched stance facing the primary threat sector.
  - Follower 2 guards the squad's six o'clock (rear security).
- Once the leader arrives on top, they immediately crouch and assume high-ground overwatch, covering the followers as they ascend one by one.

### 6.3 Solo vs. Formation vs. Box Selection Modes

| Selection Mode | Action on Elevated Click | Climbing Operatives | Unselected Squad Behavior |
| :--- | :--- | :--- | :--- |
| **Solo Mode** (`IsSoloMode() == true`) | Direct Mantle Order | **Active operative only** | Teammates **remain completely stationary** in their current cover/stance. |
| **Formation Mode** (`Solo == false`) | Squad Move / High Ground Order | **Entire squad in sequence** | Squad scales the ledge sequentially via Leader $\to$ Follower 1 $\to$ Follower 2 stagger. |
| **Box Selection** | Group Mantle Order | **Box-selected operatives only** | Only selected operatives climb; unselected members maintain current holds. |

---

## 7. Combat & Environmental Interactions

1. **Cover on Top of Obstacles:**
   - Once atop a crate or flatcar, operatives can take low cover against higher props or drop into prone stance (`EOperativeStance::Prone`).
   - Prone on a container provides **high cover damage absorption ($90\%$)** against ground-level shooters due to the severe upward angle occluding line of sight (`CoverRules::AbsorbFraction`).
2. **Interrupts & Knockdowns:**
   - If an operative is hit by high-caliber sniper fire, shotgun blast, or explosive knockback while mantling:
     - The mantle is aborted.
     - Operative falls backwards onto the ground with `Knockdown` recovery applied (`HealthComponent->ApplyKnockdown()`).
     - Prevents operatives from floating in mid-air if stunned during an animation.
3. **Stealth & Sound Propagation:**
   - Low Mantle produces minimal noise radius ($3\text{ m}$, muffled cloth/hand slap).
   - High Mantle produces $6\text{ m}$ sound radius on landing.
   - Stepping down onto snow creates an audible foot crunch ($8\text{ m}$ radius), alerting nearby PMC patrols if unsuppressed.

---

## 8. Test & Verification Plan

### 8.1 Unit Automation Tests (`Source/CodexTacticsTests/Private/Characters/MantleRulesTest.cpp`)

1. **`MantleRules.HeightClassification`:**
   - Tests boundary heights: $24\text{ cm}$ (Step), $25\text{ cm}$ (Vault), $105\text{ cm}$ (Vault), $110\text{ cm}$ (Low Mantle), $130\text{ cm}$ (Low Mantle), $180\text{ cm}$ (High Mantle), $240\text{ cm}$ (High Mantle), $245\text{ cm}$ (Blocked/Ladder).
2. **`MantleRules.VaultVsMantleDisambiguation`:**
   - Given a $90\text{ cm}$ jersey barrier ($40\text{ cm}$ thick with ground behind): resolves to `Vault`.
   - Given a $90\text{ cm}$ flatcar platform ($200\text{ cm}$ deep with roof top): resolves to `Mantle`.
3. **`MantleRules.ClearanceCheck`:**
   - Validates that a $110\text{ cm}$ crate with an overhead pipe at $150\text{ cm}$ fails the headroom test and refuses mantle.
4. **`MantleRules.StaggerQueue`:**
   - Verifies 3 approaching operatives generate arrival times spaced $\ge 0.6\text{ s}$ apart with zero spatial overlap.

### 8.2 In-Engine Smoke Tests (`Source/CodexTacticsDev/Private/Debug/MantleSmokeCommand.cpp`)

To be executed via `powershell .\Scripts\test.ps1 -Smoke MantleSmoke`:
1. **Depot Crate Mantle (`L_Gen_Depot_1`):**
   - Spawns squad in front of a standard $110\text{ cm}$ freight crate (`prop_id=crate_*`).
   - Leader ordered to top surface: successfully executes Low Mantle, arrives atop crate, enters crouch.
   - Zero "standing on obstacle top" errors; zero stuck warnings.
2. **High Container Mantle (`L_MovementTest`):**
   - Operative executes High Mantle onto a $180\text{ cm}$ shipping container.
   - Verifies weapon sling $\to$ pull-up $\to$ unholster transition.
3. **Controlled Drop-Down:**
   - Operative ordered from container roof back to ground: executes controlled jump-down feet-first, landing with zero fall damage.
4. **Sequential Squad Climb:**
   - Entire 3-man squad ordered across a double crate row: all 3 scale sequentially without collisions or path cancellations.

---

## 9. Implementation Roadmap & Milestones

| Phase | Milestone | Scope & Deliverables | Primary Files |
| :---: | :--- | :--- | :--- |
| **Phase 1** | **Pure Geometry & Rules** | `MantleRules` namespace: height classifications, shapecast math, clearance validation, disambiguation algorithms, and unit test suite. | `Public/Characters/MantleRules.h`<br>`Private/Characters/MantleRules.cpp`<br>`Tests/Characters/MantleRulesTest.cpp` |
| **Phase 2** | **CMC Integration & Movement Mode** | Implement `CMOVE_Mantle` in `AOperativeCharacter`, integrate Root Motion / Motion Warping montage playback, handle capsule overlap states and collision handoff. | `Public/Characters/OperativeCharacter.h`<br>`Private/Characters/OperativeCharacterMantle.cpp` |
| **Phase 3** | **NavMesh & Props Integration** | Update `CodexLevelPropsSubsystem` and `build_levelgen_map.py` to tag mantleable crate/container tops as walkable, generate off-mesh mantle links, update `VaultNavigation`. | `Private/LevelGen/CodexLevelPropsSubsystem.cpp`<br>`Private/Interactables/VaultNavigation.cpp` |
| **Phase 4** | **Squad Stagger & Smoke Gate** | Squad queue discipline in `SquadSubsystem`, overwatch aiming during teammate climb, `MantleSmokeCommand` verification on `L_Gen_Depot_1`. | `Private/Characters/SquadSubsystem.cpp`<br>`Private/Debug/MantleSmokeCommand.cpp` |

---

## 10. Summary of Architectural Guarantees

1. **Non-Regression:** The existing `VaultRules` ($25 - 105\text{ cm}$) and `LadderRules` ($> 240\text{ cm}$) remain 100% operational and undisturbed. Thin barricades and sandbags continue to be vaulted over seamlessly.
2. **Zero Glitch Ingress:** The 4-stage shapecast filter eliminates phantom ledge grabs, clipping through solid geometry, or getting stuck inside crate meshes.
3. **Deterministic Stagger:** Squad members never collide, push each other off ledges, or get softlocked in simultaneous animation montages.
4. **Data-Driven Configuration:** All height bounds, clearance tolerances, and animation speed multipliers are exposed via `FMantleConfig` for designer tuning without C++ recompilation.
