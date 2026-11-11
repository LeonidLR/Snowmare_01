# Implementation Map #46a — Procedural Trap & Hazard Planner

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Target:** Task #46a (`pl-trap-planner`) — Процедурный генератор ловушек и тактических угроз  
**Target Assignee:** Codex / Claude  
**Governing Documents:** `docs/design/TRAP_PLANNER.md`, `docs/systems/interactables-traps-loot.md` §5, `docs/design/levelgen/PROPS_LAYERS.md`

---

## 1. Tactical Concept & Operational Goal

### 1.1. Purpose & Requirements
In fortified combat zones like Depot (`L_Gen_Depot_1`), tactical hazards prevent straightforward run-and-gun rushes and reward methodical reconnaissance:
1. **Procedural Seed-Based Placement:** Automatically places concealed mines, tripwires, booby-trapped loot containers, and autonomous sentries into generated level geometry.
2. **Topological Scoring:** Positions hazards at critical chokepoints: narrow door thresholds, bases/tops of ladders, ramp landings, blind corners, and high-value loot caches.
3. **Strict Non-Cheating Fairness Constraints:**
   - **Start Safe Zone:** Absolute minimum radius of **$35.0\text{ m}$** around `PlayerStart` is $100\%$ hazard-free.
   - **Minimum Spacing:** Minimum distance between any two hazards is **$15.0\text{ m}$** (no impassable chain-blast clusters).
   - **Density Cap:** Maximum of $\le 4$ hazards per $200\text{ m}$ of primary route.
   - **Guaranteed Solvability:** Every trapped segment must offer either: (a) a navigable bypass flank (e.g. side room, roof ladder), (b) safe walk clearance ($\ge 80\text{ cm}$), or (c) disarm capability via Sapper (Roger/Babai).
4. **Physical Tells Over UI Cheats:** Trapped crates and mines **never have artificial red outlines**. Players detect them through physical observation: taut wires, disturbed snow mounds, mechanical servo hums, or corner-peek sweeps.

---

## 2. Existing Classes to Extend vs. New Code

### 2.1. Reusable Engine Components (~85% Reuse)

| Component | File & Line | Reusable Function / Capability |
| :--- | :--- | :--- |
| **Proximity Mines** | `Source/CodexTactics/Public/Interactables/ProximityMineActor.h` | `AProximityMineActor` provides trigger radius, arming timer, and explosive yield. |
| **Tripwires** | `Source/CodexTactics/Public/Interactables/TripwireActor.h` | `ATripwireActor` provides line trace sensor, peg attachments, and wire rendering. |
| **Loot Crates** | `Source/CodexTactics/Public/Interactables/LootCrateActor.h` | `ALootCrateActor` supports `bTrapped` state and disarm interaction. |
| **Autonomous Turrets** | `Source/CodexTactics/Public/Interactables/TurretActor.h` | `ATurretActor` provides scanning arc, optical sensor, and EMP vulnerability. |
| **Trap Detection Rules** | `Source/CodexTactics/Public/Interactables/TrapSpotRules.h` | Evaluates spot distance based on operative role, corner peek cone, and dog scent. |
| **Sapper Disarm Logic** | `Source/CodexTactics/Private/Interactables/InteractionSubsystem.cpp:120-220` | Handles disarm channeling, tool checks, and salvage drop generation. |

### 2.2. New Code Required (~15%)

1. **`TrapPlannerRules` (Pure Placement & Solvability Logic):**
   - Header: `Source/CodexTactics/Public/LevelGen/TrapPlannerRules.h`
   - Source: `Source/CodexTactics/Private/LevelGen/TrapPlannerRules.cpp`
   - Evaluates placement score:
     $$\text{Score}(P) = W_{\text{Choke}} \cdot C(P) + W_{\text{Route}} \cdot R(P) + W_{\text{Blind}} \cdot B(P)$$
   - Filters candidate points against fairness constraints (Start radius, min spacing, density cap).
   - Validates that bot navigation graph remains solvable.
2. **TypeScript Training Ground Integration (`Tools/TrainingGround/`):**
   - Module: `src/levelgen/traps.ts` for browser-based level generation and preview.
   - Hazard heat map rendering layer in `src/renderer/`.

---

## 3. Data Keys & Tuning

Configured in `Content/Data/LevelGen/trap_planner_rules.json`:

```json
{
  "start_safe_radius_cm": 3500.0,
  "min_hazard_spacing_cm": 1500.0,
  "max_hazards_per_200m": 4,
  "trapped_crate_chance": 0.20,
  "scoring_weights": {
    "chokepoint_weight": 0.40,
    "route_traffic_weight": 0.35,
    "blind_corner_weight": 0.25
  },
  "detection_radii": {
    "mine_base_cm": 450.0,
    "mine_peek_bonus_cm": 750.0,
    "tripwire_base_cm": 600.0,
    "crate_inspection_time_s": 2.0
  },
  "disarm_success_salvage_yield": {
    "ProximityMine": "component_explosive_tier1",
    "Tripwire": "scrap_wire_and_fuse"
  }
}
```

---

## 4. Insertion Points in Existing Code

### 4.1. Level Generation Hazard Pass
In `Source/CodexTactics/Private/LevelGen/LevelGenerator.cpp`:
```cpp
void ULevelGenerator::GenerateTacticalHazards(FLevelLayout& Layout, FRandomStream& Rng)
{
    // 1. Identify Candidate Chokepoints from NavMesh & Corridors
    TArray<FTrapCandidate> Candidates = TrapPlannerRules::FindChokepoints(Layout);

    // 2. Score Candidates
    for (FTrapCandidate& Candidate : Candidates)
    {
        Candidate.Score = TrapPlannerRules::CalculateHazardScore(Candidate, Layout);
    }
    Candidates.Sort([](const FTrapCandidate& A, const FTrapCandidate& B) { return A.Score > B.Score; });

    // 3. Filter Candidates against Fairness Rules
    TArray<FPlacedHazard> PlacedHazards;
    for (const FTrapCandidate& Candidate : Candidates)
    {
        if (TrapPlannerRules::IsValidPlacement(Candidate, PlacedHazards, Layout.PlayerStartLocation, TrapConfig))
        {
            FPlacedHazard Hazard = TrapPlannerRules::CreateHazard(Candidate, Rng);
            PlacedHazards.Add(Hazard);
        }
    }

    // 4. Assert Layout Remains Solvable
    check(TrapPlannerRules::ValidateRouteSolvability(Layout, PlacedHazards));
}
```

### 4.2. Detection Sweep in `TrapSpotRules.cpp`
```cpp
bool TrapSpotRules::CheckOperativeSpotting(const AOperativeCharacter& Operative, const AActor& Hazard)
{
    const float Dist = FVector::Dist(Operative.GetActorLocation(), Hazard.GetActorLocation());
    const bool bIsSapper = Operative.SquadRole == EOperativeRole::MedicSapper;
    const float BaseRange = bIsSapper ? 650.0f : 450.0f;
    const float EffectiveRange = Operative.bCornerAimActive ? BaseRange + 750.0f : BaseRange;

    return Dist <= EffectiveRange && Operative.HasLineOfSightTo(&Hazard);
}
```

---

## 5. Automated Verification & Smokes

* **Unit Test:** `Source/CodexTacticsTests/Private/LevelGen/TrapPlannerRulesTest.cpp`
  - Validates rejection of any candidate within $35\text{ m}$ of `PlayerStart`.
  - Asserts minimum $15\text{ m}$ spacing between hazards.
  - Verifies solvability validator rejects dead-end blocked paths.
* **Smoke Command:** `TrapSoloSmokeCommand` (`Codex.Test.TrapSolo`):
  1. Spawns squad moving in file formation towards chokepoint.
  2. Spawns concealed tripwire $10\text{ m}$ ahead.
  3. Asserts squad spots tripwire, halts advance, and transitions to Solo mode.
  4. Sapper advances and executes disarm channel without detonation.
  5. Asserts salvage item added to inventory.

---

## 6. Open Design Questions (with Recommended Defaults)

1. **Non-Sapper Disarm Penalty:**
   - *Question:* If a non-sapper (e.g. Commander or Rifleman) attempts to disarm a mine or tripwire without specialized tools, should it detonate?
   - *Recommended Default:* Non-sappers have a $50\%$ detonation chance on mines and $30\%$ on tripwires. Sapper (Roger/Babai) has $0\%$ detonation chance when unhurried.
2. **Hostile NPC Trap Awareness:**
   - *Question:* Can enemy monsters (Hounds, Brutes) or hostile PMCs trigger PMC-placed traps?
   - *Recommended Default:* Hostile PMCs know their own trap locations and avoid them; feral beasts (Frost Hounds, Brutes, Cutters) lack trap awareness and trigger them upon stepping into radius, allowing creative luring strategies.
3. **Disarm Salvage Yield:**
   - *Question:* What resource rewards should disarming provide?
   - *Recommended Default:* Disarming a mine yields $1\times$ `Explosive Component` (usable for crafting field charges); disarming a tripwire yields $1\times$ `Frag Grenade`.
