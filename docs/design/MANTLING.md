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

> **superseded by 2026-11-15 (§11):** 110 cm is a historical example, not the new universal crate target; generated obstacles use measured clip classes once approved.
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

> **superseded by 2026-11-15 (§11):** the continuous spans below are feasibility envelopes, not permission to generate arbitrary intermediate heights. The animation pool and actual ladder/fall movement modes are corrected in §11; existing clips remain available.

| Tier | Category | Height Span | Obstacle Depth | Character Action | Animation Asset Reference | Movement Mode |
| :---: | :--- | :---: | :---: | :--- | :--- | :--- |
| **0** | **Step / Stride** | $0 - 35\text{ cm}$ | Any | Standard foot step-up / curb stride | Natural CMC step-up (`MaxStepHeight = 45 cm`) | `MOVE_Walking` |
| **1** | **Low Vault / Hurdle** | $25 - 105\text{ cm}$ | $\le 130\text{ cm}$ | Stride/hop *over* obstacle to ground on far side | `Content/RifleAnims/Rifle_2/Traverse/M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_Lfoot_Rifle.uasset`<br>`M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_Rfoot_Rifle.uasset`<br>`Content/Polygonmaker/Animations/Interactions/UE5/Anim_Regular_jump_fence.uasset` | `MOVE_Flying` / Arc Interp |
| **2** | **Low Mantle (Clamber)** | $75 - 130\text{ cm}$ | $> 80\text{ cm}$ | Hands on top edge, spring off ground, pop up into low crouch on top | `Content/Polygonmaker/Animations/Interactions/UE5/Anim_Regular_climb_up.uasset`<br>`Anim_Regular_climb_up-inplace.uasset` | `MOVE_Custom` (`CMOVE_Mantle`) |
| **3** | **High Mantle (Pull-Up)** | $130 - 240\text{ cm}$ | $> 60\text{ cm}$ | Overhead grab, hang/muscle-up, knee on edge, rise to stand | `Content/Polygonmaker/Animations/Interactions/UE5/Anim_Regular_jump_climb.uasset`<br>`Anim_Regular_jump_climb_inplace.uasset` | `MOVE_Custom` (`CMOVE_Mantle`) |
| **4** | **Ladder Ascend** | $> 240\text{ cm}$ | Any | Formal ladder rung climb | `Content/Characters/Operatives/Anims/Ladder/AS_Ladder_UpLoop.uasset`<br>`AS_Ladder_ClimbOutTop.uasset` | `MOVE_Custom` (`CMOVE_Ladder`) |
| **5** | **Controlled Drop-Down** | $60 - 250\text{ cm}$ | Downward | Controlled hop off ledge, soft bent-knee landing | `Content/Polygonmaker/Animations/Interactions/UE5/Anim_Regular_jump_down.uasset`<br>`Anim_Regular_jump_down_inplace.uasset` | `MOVE_Custom` (`CMOVE_LedgeDrop`) |

### 2.2 Weapon Discipline & Ready Times

> **2026-11-15 clarification (§11):** these timings and weapon poses remain design targets, not measured CaV clip properties; validate contacts/duration and confirm weapon policy before hookup.

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

> **superseded by 2026-11-15 (§11), height/action selection only:** use class ± tolerance plus depth, landing intent and clearance; a height inside a broad span alone does not authorize a clip. Existing low-vault limits remain unchanged.

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

> **superseded by 2026-11-15 (§11):** this is a proposal, not the current vault/ladder implementation. The new mantle plan uses a saved/restored flying lifecycle with one root-motion driver; no existing `CMOVE_Mantle` implementation is asserted.
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

> **2026-11-15 clarification (§11):** root warp targets do not by themselves guarantee hand/knee contact or sub-centimeter precision; validate retargeted contact poses, endpoints and sweeps.
To allow the animations (`Anim_Regular_climb_up`, `Anim_Regular_jump_climb`) to scale dynamically across varying obstacle heights ($75 - 240\text{ cm}$):
- Three warp targets are defined:
  1. `WarpTarget_Hands`: Located at $(P_{\text{top}}.X, P_{\text{top}}.Y, P_{\text{top}}.Z)$, aligned with ledge lip.
  2. `WarpTarget_Knees`: Located at lip $+ 15\text{ cm}$ forward, $+ 10\text{ cm}$ above top.
  3. `WarpTarget_Land`: Located at $P_{\text{land}}$ on the walkable surface.
- Motion Warping smoothly transforms the root motion curves so hand grips and foot placements land with sub-centimeter visual precision on any crate, barrier, or ledge.

### 4.3 Fallback Interpolation (Sine-Smoothstep Arc)

> **superseded by 2026-11-15 (§11):** do not combine this arc with consumed mantle root motion. The first mantle slice refuses safely if its clip/warp contract is unavailable; the existing vault retains its own manual arc.
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

> **superseded by 2026-11-15 (§11), link ownership/direction:** mantle-up needs a distinct custom-link callback/area; downward traversal stays with the fall system. `FDropLinkReach` supplies reachability checks, not an upward movement driver; costs remain tuning proposals.
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

> **superseded by 2026-11-15 (§11), input contract:** the elevated-click behavior below is an option pending designer choice. Preserve merged 054 box-selection → solo behavior and the selected subset; do not silently introduce a separate group mode.

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

> **superseded by 2026-11-15 (§11):** retain this roadmap as history; use the measured CaV pool, motion ownership and implementation sequence below.

| Phase | Milestone | Scope & Deliverables | Primary Files |
| :---: | :--- | :--- | :--- |
| **Phase 1** | **Pure Geometry & Rules** | `MantleRules` namespace: height classifications, shapecast math, clearance validation, disambiguation algorithms, and unit test suite. | `Public/Characters/MantleRules.h`<br>`Private/Characters/MantleRules.cpp`<br>`Tests/Characters/MantleRulesTest.cpp` |
| **Phase 2** | **CMC Integration & Movement Mode** | Implement `CMOVE_Mantle` in `AOperativeCharacter`, integrate Root Motion / Motion Warping montage playback, handle capsule overlap states and collision handoff. | `Public/Characters/OperativeCharacter.h`<br>`Private/Characters/OperativeCharacterMantle.cpp` |
| **Phase 3** | **NavMesh & Props Integration** | Update `CodexLevelPropsSubsystem` and `build_levelgen_map.py` to tag mantleable crate/container tops as walkable, generate off-mesh mantle links, update `VaultNavigation`. | `Private/LevelGen/CodexLevelPropsSubsystem.cpp`<br>`Private/Interactables/VaultNavigation.cpp` |
| **Phase 4** | **Squad Stagger & Smoke Gate** | Squad queue discipline in `SquadSubsystem`, overwatch aiming during teammate climb, `MantleSmokeCommand` verification on `L_Gen_Depot_1`. | `Private/Characters/SquadSubsystem.cpp`<br>`Private/Debug/MantleSmokeCommand.cpp` |

---

## 10. Summary of Architectural Guarantees

> **2026-11-15 clarification (§11):** these are acceptance goals, not evidence that mantle, its queue or its smokes already exist. The old free-height examples in §8 must be rebuilt around measured classes.

1. **Non-Regression:** The existing `VaultRules` ($25 - 105\text{ cm}$) and `LadderRules` ($> 240\text{ cm}$) remain 100% operational and undisturbed. Thin barricades and sandbags continue to be vaulted over seamlessly.
2. **Zero Glitch Ingress:** The 4-stage shapecast filter eliminates phantom ledge grabs, clipping through solid geometry, or getting stuck inside crate meshes.
3. **Deterministic Stagger:** Squad members never collide, push each other off ledges, or get softlocked in simultaneous animation montages.
4. **Data-Driven Configuration:** All height bounds, clearance tolerances, and animation speed multipliers are exposed via `FMantleConfig` for designer tuning without C++ recompilation.

## 11. 2026-11-15 implementation plan

### 11.1 Decisions, evidence and height contract

- Designer decision: DeadPixelLabs **Climbing and Vaulting Animations** is purchased: Content/CaV_Anims/Animations/ThirdPerson_Anims, 31 TPP root-motion clips, UE4 Mannequin source skeleton; retarget copies for our operatives. Source assets and all existing traversal clips stay.
- Evidence: [mantle-scout](../port/codex/outbox/mantle-scout.report.md), [CaV inventory](../port/codex/outbox/cav-healing-inventory.report.md). Filenames identify candidates; authored obstacle heights/contact phases have not been measured.
- ALL generated walls, fences and obstacles use a height class + tolerance. Approved targets (team-chat #465, from the 31-clip CSV): **low_mantle 85±10, high_mantle 140±10, tall_ledge 230±10, very_tall 310±10, low_vault 95±5, high_fence_over 135±5 cm**. Animated classes carry measured source-root provenance; step remains 30±5, unmeasured.
- Source: read-only Scripts/Editor/measure_anim_root_motion.py → Saved/Measure/anim_root_motion.csv; Scripts/Tools/apply_measured_heights.py uses endpoint dz for ON TOP and peak rise for OVER. Asset root-motion enable flags are False in this pack, but root tracks exist: retain measurements with WARN RM_FLAG_OFF; configure runtime root-motion policy only after retarget/contact inspection.
- Explicit approved targets supersede the provisional 100/120/200 cm targets. Raw source-root rise is a proxy, not authored obstacle/contact geometry: low-vault source peaks 88.4694/100.061 cm exceed the approved 95±5 band, and the original classes report spread conflicts against their old ±5 limits. Keep these flags/outliers in provenance; do not silently widen the approved tolerances or certify contact alignment.
- Generation/validation read every JSON class, including tall_ledge/very_tall/high_fence_over. Bands overlap; geometry, landing intent and an explicit class decide action. Valid structural heights such as 300 cm remain valid within very_tall's band. No map is regenerated by this data/code update.
- The traversal ceiling is 320 cm (310+10); above it, classification falls back to wall_no_traverse. Its unmeasured nominal placeholder is 330±5 cm, replacing 300 cm to avoid the 300..320 very_tall band. It has no source clip and grants no traversal action. This data update does not implement new C++ climb movement.


### 11.2 Candidate action/clip table

Names below are exact inventory names; default-name clips are standing candidates until preview confirms the start pose. Shared height class does not make distinct actions interchangeable. Land pairings marked **inspect** are not certified.

| Height class / approved target | Standing/default clip | Running/sprint clip | Destination | Land/exit clip |
|---|---|---|---|---|
| step / 30±5 cm (unmeasured) | CMC step-up | CMC step-up | Next walkable surface | Ordinary locomotion |
| low_vault / 95±5 cm | Existing compatible hurdle L/R + LowVault | RunLowVault | Far-side ground | LowVaultLand after contact review; old exits retained |
| low_mantle / 85±10 cm | Low_Climb (dz 77.1385) | RunLowClimb (dz 90.9116) | Top | Inspect embedded finish |
| high_mantle / 140±10 cm | HighClimb (131.560); HI_Climb_Climb_Up (138.871, hang start) | RunHighClimb (142.416) | Top | Inspect start/finish; hang clip is not a standing substitute |
| tall_ledge / 230±10 cm | Standing_JumpClimbUpNEW (229.216); TallLedge_ClimbUpNEW (231.046) | RunJumpClimbNEW (238.797) | Top | Inspect embedded finish / TallLand |
| very_tall / 310±10 cm | JumpClimb (308.552) | RunJumpClimb (308.253) | Top | Inspect contact/landing; runtime support remains planned |
| high_fence_over / 135±5 cm | HighFenceVault (peak 137.212) | No verified Run variant | Far side | TallFence_Land candidate; inspect |
| wall_no_traverse / 330±5 cm placeholder, above 320 cm ceiling | None | None | Blocked / existing ladder route | None |
| Downward action / existing fall rules | JumpDown; JumpDownNoWait | No named Run variant | Lower surface | Fall_SideRoll / FallHigh / Land_LowRoll / suitable TallLand after inspection |

**На просмотр дизайнеру — NEEDS_REVIEW, excluded from all height-class clip pools:**

| Clip | Endpoint dz cm | Peak rise cm | Review reason |
|---|---|---|---|
| JumpFenceVault | -0.273386 | 188.937531 | Returns to ground; inspect traversal phases/contact |
| JumpFenceClimb | -0.273386 | 268.352631 | Returns to ground; inspect action and peak vs obstacle height |
| Standing_Tall_Wall_Climb | -0.273386 | 230.653946 | No final rise; inspect phases/landing |
| Sprint_Footplant_TallWallClimb | 0 | 0 | Static root; do not substitute pelvis motion for root measurement |

- Wall run is **not used**. HI_Climb_Climb_Up is measured in high_mantle but still needs its hang-start contract; the other split hang phases remain outside the action pool.
- Filter candidates by action, measured height/tolerance, depth, arrival gait, start pose, landing surface and clearance; then use deterministic per-operative round-robin among compatible clips. Preserve verified L/R foot alternation, skip missing/incompatible assets, avoid immediate repeats when alternatives exist; never fabricate a foot label from a filename.
- Extend the current Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp::UpdateVaultClip L/R selection into a compatible pool containing the old clips and CaV low-vault variants. Running variants require a valid run-up; unavailable run clips fall back only to a verified compatible standing approach, otherwise refuse/replan.
- Store clip/action/class/gait/foot tags, contact/landing times, destination type and motion policy as data. Missing assets or unmeasured contact contracts keep that candidate disabled; no random selection across climb, fence-over and fall actions.

### 11.3 C++ ownership, motion and input

- Own transient mantle phase, source/target transforms, saved movement/collision/animation settings and path destination on AOperativeCharacter (planned OperativeCharacterMantle.cpp + header fields); reuse vault/ladder lifecycle patterns. World-owned mantle anchors hold exclusive reservations; death, destruction, cancellation and EndPlay release them. No Blueprint gameplay pack is adopted.
- **New mantle driver: clip root motion + the engine Motion Warping plugin.** Prototype a saved/restored MOVE_Flying lifecycle, verify XYZ motion and swept capsule movement; do not add the vault's smoothstep/sine translation. OperativeCharacterVault.cpp currently moves the capsule manually; CaV clips inserted into that existing vault path must ignore root translation while using its arc/timing, restoring the previous animation policy afterward.
- LadderClimbComponent.cpp currently uses flying + IgnoreRootMotion with extracted-track warping; reuse its snapshots/anchor and path-resume discipline, not a claim that ladders already consume engine root motion. LedgeFallComponent.cpp owns falling/drop movement; upward mantle never reverses its ballistic path.
- Validate feet-relative ledge height/depth/slope, approach corridor, top footprint and headroom before reservation; recheck the landing before committing. Warp only within the verified clip's contact limits. Use collision sweeps and scoped contact handling; occupied/moving/destroyed geometry causes bounded abort/replan or handoff to falls, with state restored exactly once.
- **Trigger is OPEN for the designer:** A — automatic on a path using an eligible mantle link, like vault; B — explicit click on a reachable ledge top; C — top click grants mantle intent and path-following executes the link. Do not select an option in implementation before the answer.
- Input seam: Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp, before generic cover/wrap/menu/ground consumption, preserving existing Alt/attack/tactical priority, ladder clicks and barricade menus. Route only the currently commanded operative/subset; preserve 054 solo behavior and unselected holds.

### 11.4 Navigation and ordered implementation

1. **Data then contact review:** use the approved targets and retained raw measurements above; preview hands/feet and action→land transitions before enabling clips. Map regeneration is a separate lead-owned step; this task changes no accepted maps, .umap or .uasset files.
2. **Retarget/data slice:** create operative retarget copies for one low-vault and one low-mantle pair first; compare contacts and exits before adding high/fence actions. Prepare the clip metadata above; declare Motion Warping dependencies in CodexTactics.uproject and Source/CodexTactics/CodexTactics.Build.cs when implementing, not in this documentation task.
3. **Pure rules:** planned Public/Characters/MantleRules.h, Private/Characters/MantleRules.cpp, tests under Source/CodexTacticsTests/Private/Characters/; implement class/action selection, clearance result, compatible clip rotation and refusal reasons. Preserve existing VaultRules.h/.cpp 25–105 cm and thin-side limits.
4. **One-person end-to-end:** planned OperativeCharacterMantle.cpp owns approach → reserve → climb → land → resume/abort; wire OperativeCharacter.h, OperativeCharacterVault.cpp and OperativeAnimInstance.cpp. Checkpoint: one 120 cm target, correct top landing and no double root displacement, restoration after missing clip/interruption.
5. **Mantle nav links:** planned Private/Interactables/MantleNavigation.cpp and matching public declarations create directed **upward custom links with a distinct mantle area/callback**. Keep Private/Interactables/VaultNavigation.cpp cost-3 vault area and filtering unchanged; make mantle availability in path queries follow the chosen trigger policy.
6. **Reachability/props:** connect link generation to Private/LevelGen/CodexLevelPropsSubsystem.cpp and the level builder after top collision/nav islands exist; one valid approach/landing slot per lease. Reuse Ladders/LadderActor.cpp custom-link callbacks and exclusive-queue pattern. Use Falls/DropLinkReach.cpp to reject isolated tops with no safe onward/return route; downward links remain Falls/DropLinkSubsystem.cpp → LedgeFallComponent.cpp. Revalidate/remove links when props move or are destroyed.
7. **Input and squad:** implement only the chosen trigger in CodexTacticsPlayerControllerOrders.cpp; adapt Private/Characters/SquadSubsystem.cpp to selected-member reservations, bounded wait/replan, casualty cleanup and path continuation. Keep §6 queue/overwatch distances and timings as explicit design targets until tested/confirmed.
8. **Broaden only after the slice passes:** add measured high-climb, jump/ledge, tall-wall and high-fence families with their exits; route JumpDown/rolls through the existing falls system. Weapon/stance ownership and audio timing must be validated per clip, not copied from the old fixed-duration examples.

### 11.5 Acceptance tests and designer choices

- Planned unit tests: nominal ± tolerance and just-outside boundaries, between-class rejection, tall wall refusal, thin-over vs deep-top intent, insufficient run-up, L/R/round-robin compatibility and missing-clip fallback; reservation contention, timeout/cancel/death and exactly-once restore.
- Planned MantleSmokeCommand.cpp: low/high top landings, high fence far-side landing, no doubled motion, occupied/headroom-blocked landing, destroyed prop mid-climb, stay on top, safe exit/drop, sequential squad/subset and unselected holds. Verify male/female retarget contact and action→land continuity with measured fixtures.
- Regression gates: existing VaultSmoke, DepotVaultSmoke, BarricadeVaultBackSmoke, ClickRulesSmoke, FallDropSmoke, DropLinkReachSmoke, LevelGenRouteSmoke and LevelGenLaddersSmoke; generator height gate + arena schema/simulation + props fastGate. These are future checks, not tests run by this spec update.
- **Q1 — trigger:** A automatic path links / B explicit top click / C top-click intent + automatic link execution? For B/C, should ordinary cover clicks retain their current action or require a modifier to request mantle?
- **Q2 — run-up:** A standing fallback after stopping / B refuse/replan when the run-up is insufficient? May the system initiate a sprint for a run clip, or only use an already commanded gait?
- **Q3 — weapon/stance:** A retain §2.2 weapon/ready-time targets where compatible / B clip-driven sling/restore windows after preview? On top: retain prior legal stance / crouch by default? High-fence over uses a far-side landing, not a top hold.
- **Q4 — review clips/contact:** for the four NEEDS_REVIEW clips above, A defer / B inspect authored phases and approve a separate action/height contract? What maximum contact/position warp is acceptable, particularly for the low-vault peaks outside 95±5?
- **Q5 — squad/combat:** A keep §6 stagger/overwatch targets / B revise them after the one-person slice? Which hit/knockdown cancels the climb, and retain §7 noise values or tune them after contact/landing review?

### 11.6 Designer answers 2026-11-15

- **Q1 trigger = C:** a click on a reachable ledge top orders the climb, AND a path to a clicked point that runs over an eligible obstacle mantles by itself (like the vault).
- **Q2 run-up = A:** insufficient run-up -> stop and use the standing clip; the system never starts a sprint by itself (run clips only when already running).
- **Q3 weapon/stance:** clip-driven sling/raise windows; no fire during the climb; on top the operative goes to a **crouch**.
- **Q4 review clips:** deferred (the four NEEDS_REVIEW clips stay out).
- **Q5 squad/combat:** first slice = **one operative** (solo) end-to-end; formation queue + overwatch after the designer's test. A hit with knockdown / heavy hit cancels the climb (he drops to the base).
- Order: mantling on the CURRENT L_Gen_Depot_1 (its obstacles reworked to the height classes) -> designer test -> generator v2 (board 066).
- **L_Gen_Depot_1 obstacle rework (designer 2026-11-15; the uncommitted .umap had no manual edits - may be overwritten by the rebuild, backup first):** sandbags 100 -> 95 and jersey 90 -> 95 (low_vault); barrels 90 stay, NOT traversable (explosive); wrecks 150 -> 140 (high_mantle); cut (canal) walls 220 -> 230 (tall_ledge: climb out of the canal); **crates 110 -> mix of 85 and 140** (~half each); **fences 250 -> about a third lowered to 135** (high_fence_over, only where it does not break routes / devalue ambush fields), the rest raised to 330 (blocked); **one-storey roofs (300) climbable only at 1-2 marked mantle edges per building** (pipe / annex / crates next to it), other 300 walls are not mantle targets; **boilers 160 -> 140** (climbable).
