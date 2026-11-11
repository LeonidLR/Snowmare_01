# Implementation Map #18 — APC (BTR-82) on Depot & The APC Fight

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-12  
**Target:** Task #18 (`pl-apc-depot`) — Бой на БТР, спавн на Depot, тактика «БТР + пехота снаружи»  
**Target Assignee:** Codex / Claude  
**Governing Documents:** `docs/vehicles/APC_00_DECISIONS.md`, `docs/vehicles/APC_01_TECHNICAL.md`, `docs/vehicles/APC_02_DESIGN.md`, `docs/port/TASK_BOARD.md` §18

---

## 1. Tactical Concept & Operational Goal

The BTR-82 Armored Personnel Carrier (APC) represents the heavy fire support and mechanized maneuver asset for the squad. While previously tested in isolation on `L_VehicleTest.umap`, **Task #18** integrates the APC into the primary production environment (`L_Gen_Depot_1` and procedural depot layouts) and implements **The APC Fight**:

1. **Mechanized Escort Formation («БТР + пехота снаружи»):**
   - Operatives advance alongside the moving or parked APC, using its armored flanks ($7.8 \times 3.1\text{ m}$) as dynamic, mobile high cover (`ETraceTypeQuery::TraceTypeQuery1` Visibility / Weapon block).
   - Cabin heater (`UHeatSourceComponent`) radiates warmth $2\text{ m}$ outward from the engine deck, mitigating blizzard hypothermia for walking infantry escorts.
2. **Asymmetric Heavy Combat (The APC Fight):**
   - Enemy PMC heavy weapons teams (specifically PMC RPG-7 gunners from `feat/rpg-ai-pmc`) prioritize the APC over infantry due to threat weighting (`EEnemyTargetKind::Vehicle`).
   - Melee hordes (Cutters, Hounds) assault the hull with reduced damage ($\times 0.1$ claw reduction), while Brutes charge to ram the front glacis.
   - Props C4 mines and tripwires across Depot roadway chokepoints detonate for 250 flat damage when run over by the 8x8 wheels.
3. **Manned Autocannon Support:**
   - Commander mans the BP-30 turret (30mm 2A42 autocannon with high explosive splash + PKTM coaxial MG with suppression). Targetable via Ctrl+click against explosive barrels and heavy structures.
4. **Disablement, Crew Ejection & Combat Repair:**
   - At $0\text{ HP}$, the vehicle becomes disabled (not obliterated). Crew is auto-ejected via screen fade to the safe flank opposite incoming fire. Engineer restores functionality via field repair ($5\text{ s}$ hold `Y`).

---

## 2. Existing Classes & Functions to Extend vs. New Code

### 2.1. Reusable Engine Components (~85% Reuse)

| Component | File & Line | Reusable Function / Capability |
| :--- | :--- | :--- |
| **`AVehiclePawn`** | `Source/CodexTactics/Public/Vehicles/VehiclePawn.h:58-200`<br>`Private/Vehicles/VehiclePawn.cpp:1-500` | Hull collision (`UBoxComponent`), skeletal mesh with wheel animations, engine audio, `CabinHeat`, `NavObstacle`, `CrushConfig`. |
| **`UVehicleTurretComponent`** | `Source/CodexTactics/Public/Vehicles/VehicleTurretComponent.h:32-150`<br>`Private/Vehicles/VehicleTurretComponent.cpp:1-400` | Autocannon 2-round burst (45 dmg + 20 splash), PKTM coaxial MG, turret yaw/pitch tracking, dead-zone check ($4\text{ m}$). |
| **`UVehicleSubsystem`** | `Source/CodexTactics/Public/Vehicles/VehicleSubsystem.h:29-180`<br>`Private/Vehicles/VehicleSubsystem.cpp:1-600` | Boarding/ejection logic (`BoardOperatives`, `EjectOperatives`), tactical pause refusal, fade-to-black sequence. |
| **`VehicleHullRules`** | `Source/CodexTactics/Public/Vehicles/VehicleHullRules.h:1-150`<br>`Private/Vehicles/VehicleHullRules.cpp:1-120` | Damage mitigation table: small claws $\times 0.1$, Brute $\times 1.0$, Spitter acid $\times 0.5$, Marksman $\times 0.25$, Mine detonation $250\text{ flat}$. |
| **`VehicleCombatRules`** | `Source/CodexTactics/Public/Vehicles/VehicleCombatRules.h:1-250`<br>`Private/Vehicles/VehicleCombatRules.cpp:1-180` | Threat evaluation, target scoring, vehicle ramming and small enemy knockdown. |
| **`EnemyAIRules` & Targeting** | `Source/CodexTactics/Private/Characters/EnemyAIRules.cpp:1-80` | Target candidate scoring: `FEnemyTargetCandidate` with `EEnemyTargetKind::Vehicle`, prioritizes manned hull. |
| **Dynamic Hull Cover** | `Source/CodexTactics/Private/Characters/SquadSubsystemGroupCover.cpp:120-250` | `USquadSubsystem` cover slot snapping adapts to dynamic moving bounds. |

### 2.2. New Code Required (~15%)

1. **`VehicleEscortFormationRules` (Escort Formation):**
   - Files: `Source/CodexTactics/Public/Vehicles/VehicleEscortFormationRules.h`, `Private/Vehicles/VehicleEscortFormationRules.cpp`.
   - Computes 4 mobile infantry escort slots relative to the vehicle transform:
     * Slot 0 (Port Flank): $[-100, -220, 0]$ (relative to hull center).
     * Slot 1 (Starboard Flank): $[-100, +220, 0]$.
     * Slot 2 (Port Rear Quarter): $[-320, -200, 0]$.
     * Slot 3 (Starboard Rear Quarter): $[-320, +200, 0]$.
   - Dynamically clamps infantry movement speed to match APC speed ($\text{Speed}_{\text{escort}} = \min(\text{PawnSprint}, \text{ApcSpeed})$).
2. **Depot Spawn Placement & Level Json Parser Seam:**
   - File: `Source/CodexTactics/Private/Data/LevelJsonRules.cpp`.
   - Reads `"vehicles": [ { "type": "btr82", "spawn": [X, Y, Z], "yaw": Yaw, "ammo_shells": 24, "fuel": 100 } ]` from Depot level definitions.
   - Spawns `BP_APC_BTR82` at level load, initializes navmesh obstacle carving.
3. **PMC Heavy Threat AI Behavior:**
   - File: `Source/CodexTactics/Private/AI/PmcCommanderRules.cpp`.
   - Adds rule `PmcShouldEngageVehicleWithRpg`: when a manned APC is detected within line of sight, RPG-7 PMC operator takes elevated position and fires rocket at vehicle center of mass.

---

## 3. Data Keys & Tuning

In `Content/Data/Balance/weapons_tuning.json` under `"vehicle_weapons"`:

```json
"vehicle_weapons": {
  "apc_hull": {
    "max_hp": 1200.0,
    "claw_damage_scale": 0.1,
    "brute_ram_damage_scale": 1.0,
    "spitter_acid_scale": 0.5,
    "marksman_bullet_scale": 0.25,
    "mine_flat_damage": 250.0,
    "repair_duration_s": 5.0,
    "repair_restore_pct": 0.50
  },
  "apc_30mm_cannon": {
    "direct_damage": 45.0,
    "splash_damage": 20.0,
    "splash_radius_cm": 150.0,
    "burst_count": 2,
    "burst_interval_s": 0.5,
    "burst_pause_s": 1.2,
    "max_range_cm": 6000.0,
    "dead_zone_radius_cm": 400.0,
    "shells_default": 30
  },
  "apc_pktm_coaxial": {
    "damage": 12.0,
    "fire_rate_rpm": 650.0,
    "heat_per_shot": 0.025,
    "cooldown_rate": 0.33,
    "suppression_per_hit": 18.0
  },
  "apc_escort": {
    "flank_offset_y_cm": 220.0,
    "flank_offset_x_cm": -100.0,
    "cabin_heat_radius_cm": 250.0,
    "escort_match_speed_tolerance_cm": 50.0
  }
}
```

---

## 4. Execution Sequence & State Flow

```
[Level Gen / Load] -> Read "vehicles" -> Spawn BP_APC_BTR82 -> Carve NavObstacle
      |
      v
[Boarding Order: Y / Alt+Y] -> Screen Fade (0.25s) -> Attach Operatives to Cabin
      |                                              (Engineer = Driver, Commander = Gunner)
      v
[Driving / Advance] -> Uncarve NavObstacle -> Dynamic Kinematic Move
      |                                    \-> Escort Operatives Hug Flanks in Mobile Cover
      v
[Combat Encounter]
   ├── Hostiles < 1.5 m/s: Shoved aside
   ├── Hostiles >= 1.5 m/s: Crushed (small enemies: hound, cutter, frostbitten)
   ├── Brute >= 4.0 m/s: Brute knocked down; APC stops abruptly; 30 ram damage
   ├── Turret Autocannon: 2-round 30mm bursts at PMC / Brute / Barrels
   └── Enemy PMC RPG: Rocket fires at BTR -> Massive direct hull hit
      |
      v
[Hull Damage reaches 0 HP]
   ├── Engine stalls, turret disabled, wreckage smoke activates
   ├── Screen Fade (0.25s) -> Eject Operatives to opposite side of incoming fire
   └── Engineer interacts (hold 'Y', 5s) -> Restores 50% HP (600 HP) -> Re-manned
```

---

## 5. Failure Modes & Edge Cases

1. **Infantry Crushing False Positives:**
   - *Risk:* Escorting operatives could be mistakenly run over by their own APC.
   - *Mitigation:* `FVehicleCrushConfig` strictly checks `!Actor->IsPlayerTeam()`. Friendly units never receive ram damage or knockdown; vehicle automatically applies brake deceleration if an operative blocks its forward path.
2. **NavMesh Carving Desynchronization:**
   - *Risk:* Parked APC leaves a ghost nav obstacle when it begins driving, causing pathfinding failure.
   - *Mitigation:* `UVehicleNavObstacleComponent` toggles dynamic avoidance vs obstacle carving: while moving ($v > 10\text{ cm/s}$), nav obstacle carving is dirty-flag disabled; upon halting for $> 1.0\text{ s}$, obstacle bounds are re-baked into the infantry tile.
3. **Ejection into Geometry / Walls:**
   - *Risk:* Crew ejection on hull disablement spawns operatives inside a depot wall or crate.
   - *Mitigation:* `UVehicleSubsystem::EjectOperatives` performs `NavigationSystem::ProjectPointToNavigation` with an escalating search radius $[150, 300, 500\text{ cm}]$ on the flank normal before finalizing placement.

---

## 6. Automated Tests & Acceptance Criteria

### 6.1. Unit & Rules Tests
- `FVehicleEscortSlotTest`: Verifies 4 escort slot offsets relative to vehicle orientation (0°, 90°, 180° yaw).
- `FVehicleDamageMitigationTest`: Validates claw ($\times 0.1$), acid ($\times 0.5$), bullet ($\times 0.25$), and mine ($250\text{ flat}$) damage formulas against `weapons_tuning.json`.
- `FVehiclePmcRpgPriorityTest`: Verifies that PMC AI target picker scores manned APC higher than individual operatives when within rocket launch cone.

### 6.2. Smoke Tests
- `CodexTactics.ApcDepotSpawnSmoke`:
  1. Loads `L_Gen_Depot_1` with vehicle config enabled.
  2. Asserts `BP_APC_BTR82` exists, has 1200 HP, and cabin heater active.
  3. Commands squad to board (Alt+Y), verifies screen fade and operative attachment.
  4. Commands drive across depot yard, verifies escort slot tracking and obstacle uncarve.
  5. Simulates mine explosion, verifies HP drops by 250.
  6. Forces 0 HP, verifies safe ejection and Engineer repair cycle.
