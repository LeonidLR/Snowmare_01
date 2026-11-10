# Systems Specification: Procedural Trap & Hazard Planner (#46a)

**Status:** DRAFT / PROPOSAL  
**Author:** Gemini (Advanced Agentic Pair-Programmer)  
**Governing Subsystems:** `LevelGenerator`, `TrapSpotRules`, `InteractionSubsystem`, `CombatPressureSubsystem`  
**Reference Document:** `docs/systems/interactables-traps-loot.md` §5, `docs/design/levelgen/LEVEL_GENERATOR.md`

---

## 1. Goals & Tactical Vision

In Milestone #46 ("Depot as a real level"), levels must not feel like sterile combat labs. Enemy PMC forces have fortified Depot (`L_Gen_Depot_1`) with concealed tactical hazards:
1. **Procedural Placement by Seed:** At level generation, mines, tripwires, booby-trapped loot containers, and autonomous sentry turrets are placed dynamically based on map geometry analysis.
2. **Tactical Chokepoint Placement:** Hazards protect high-value tactical positions: narrow doorways, bases/tops of ladders, ramp landings, blind corners, and tempting loot caches.
3. **Strict Fairness & Non-Cheating Design:** Traps are never unavoidable instant-death traps. Every hazard must provide subtle visual cues (tells), offer an alternative bypass route, and allow disarming by the engineer/sapper.
4. **No Red Glowing Cheat Outlines:** Trapped crates are **never highlighted red**. Players learn to detect booby-traps through physical inspection (tripwire cables, disturbed latches, wire sensors).

---

## 2. Hazard Catalog & Player-Facing Behaviour

| Hazard Type | Actor Class | Detection Method | Disarm / Counter-Play | Player Tell |
|---|---|---|---|---|
| **Concealed Anti-Personnel Mine** | `AProximityMineActor` | Peek sight sweep (12 m) / Engineer passive sweep (6 m) / Combat dog scent. | Engineer disarm (3 s) -> Yields explosive component; detonation via gunfire/grenade. | Disturbed snow mound, faint metallic ring flush with ground. |
| **PMC Perimeter Tripwire** | `ATripwireActor` | Visual inspection / Spotlight / Peek sight cone. | Wire cutters (`DisarmCoverSmoke`) or stepping over if low; detonated by grenade. | Taut wire running between two wall pegs 20 cm above ground. |
| **Booby-Trapped Loot Crate** | `ALootCrateActor` (`bTrapped = true`) | Manual inspection menu ("Inspect for Traps", 2 s). | Babai (Medic-Sapper) disarm option; or trigger from distance with grapple/rope. | Faint wire running from crate hinge into floor crack; subtle scraping sound on approach. |
| **Autonomous Sentry Turret** | `ATurretActor` | Audible mechanical servo whine / Vision laser sweep. | EMP grenade / Sniping optical sensor / Flanking outside cone to rear battery. | Red scanning laser beam oscillating in 60° arc; mechanical humming. |

---

## 3. Placement Scoring & NavMesh Geometry Algorithm

The Trap Planner evaluates the level NavMesh and layout geometry during generation:

$$\text{HazardScore}(P) = W_{\text{Choke}} \cdot C(P) + W_{\text{Route}} \cdot R(P) + W_{\text{Blind}} \cdot B(P) - \text{Penalty}(P)$$

### Scoring Factors
1. **Chokepoint Metric $C(P)$:** Inverse local corridor width: $\frac{1}{\text{WidthCm}}$. Narrow alleys (1.2–2.0 m) score highest.
2. **Route Traffic $R(P)$:** Probability of player route traversal calculated from A* bot route paths ($R_{\text{main}}$).
3. **Blind Corner Proximity $B(P)$:** Distance to preceding occluding wall edge. Traps immediately past a blind turn score higher.
4. **Loot Value $L(P)$:** Proximity to high-tier medical or weapon caches.

```
       [Wall A]
──────────┐
          │  ◄── [Tripwire Peg]
 (Path)   │======= [Tripwire Wire (20 cm)]
          │  ◄── [Tripwire Peg + Grenade Trap]
──────────┘
       [Wall B]
```

---

## 4. Strict Fairness Rules & Counter-Play Mandates

To prevent frustration, the generator enforces strict topological fairness constraints:

1. **No-Spawn Safe Zone Around Player Start:**
   - Absolute minimum safe distance: **35.0 meters** from `PlayerStart`. The opening area is 100% hazard-free.
2. **Hazard Spacing (Density Ceiling):**
   - Minimum distance between any two traps: **15.0 meters**. No "minefield clusters" that cannot be stepped through.
   - Maximum traps per primary route: $\le 4$ traps along any 200 m segment.
3. **Guaranteed Bypass or Disarm:**
   - For every trapped passage, there must exist either:
     a) A clear navigable bypass route (e.g. adjacent room, climbable ladder to roof), OR
     b) Sufficient clearance (>= 80 cm) to carefully walk past the trigger radius, OR
     c) Guaranteed tools in squad inventory (Roger/Babai) to safely disarm.
4. **Bot Feasibility Gate (`LevelGenRouteSmoke`):**
   - Every generated layout must be validated by the automated playtest bot. If the bot cannot complete the mission without unavoidable detonation, the layout seed is rejected.

---

## 5. Numerical Parameters (Proposal)

| Parameter | Proposal Value | Justification |
|---|---|---|
| **Start Safe Radius (`StartClearanceCm`)** | `3500.f` cm (35 m) | Ensures player can orient squad without immediate death. |
| **Minimum Trap Spacing (`MinTrapSpacingCm`)** | `1500.f` cm (15 m) | Prevents chain explosions and unnavigable choke points. |
| **Max Trapped Crates Ratio (`TrappedCrateRatio`)** | `0.20` (20%) | Rewards cautious players without making every crate a chore. |
| **Mine Detection Range: Normal (`MineDetectBaseCm`)** | `450.f` cm | Visual awareness threshold for attentive operatives. |
| **Mine Detection Range: Peek Cone (`MineDetectPeekCm`)** | `1200.f` cm | `FTrapPeekSpotConfig` reward for careful corner peeking. |
| **Tripwire Height (`TripwireHeightCm`)** | `22.f` cm | Standard ankle-height tripwire. |

---

## 6. Training Ground Heat Map & Verification

### Generator Layout Visualizer
In `Tools/TrainingGround/`:
- Add a **Hazard Heat Map Layer**:
  - Yellow circles: Proximity mines.
  - Orange lines: Tripwires.
  - Purple boxes: Trapped loot crates.
  - Red cones: Sentry turret arcs.
- Displays a clearance verification check: `[PASS: 0 Traps in Start Zone, All Routes Solvable]`.

### Automated Smoke Tests (`Scripts/test_map.json`)
- **`TrapSoloSmoke`**: Verifies squad immediately transitions to solo hold upon spotting a trap.
- **`PMCTripwireSmoke`**: Validates tripwire placement, reveal, and disarm animations.
- **`PeekMinesSmoke`**: Validates sweeping for concealed mines via corner peek cones.
- **New Proposed Smoke:** `LevelGenTrapPlacementSmoke`
  - Validates seeded procedural trap generation on `L_Gen_Depot_1`.
  - Asserts safe start distances, minimum spacing, and successful bot route bypass.

---

## 7. Open Questions for Lead Designer

1. *Mine Types:* Should Depot feature standard HE blast mines only, or also frostbite cryogenic mines that induce severe hypothermia?
2. *Enemy Hazard Awareness:* Should hostile PMCs know where their own mines are located (using `UNavFilter_NoVault` / hazard bypass cost), or can they be lured into their own tripwires? *(Recommended: PMCs know their own mines, but panic/blind charge into them if suppressed).*
3. *Disarm Failure Consequences:* If an operative with low engineering skill attempts disarm and fails, does it immediately detonate, or start a 2-second fuse timer allowing a desperate dive?
