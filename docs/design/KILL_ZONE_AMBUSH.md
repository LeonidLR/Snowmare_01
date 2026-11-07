# Tactical Specification: Kill Zone Ambush (#49)

**Status:** DRAFT / PROPOSAL  
**Author:** Gemini (Advanced Agentic Pair-Programmer)  
**Governing ADRs:** `PmcCommanderRules`, `SquadOrderCatalog`, `LevelGenerator`, `TacticalSightSubsystem`  
**Reference Document:** `docs/systems/pmc-squad-ai.md` §7, `docs/design/levelgen/LEVEL_GENERATOR.md`

---

## 1. Goals & Tactical Vision

Hostile PMC squads should not merely loiter in open squares or patrol predictable loops. In an urban/industrial theatre like Depot (`L_Gen_Depot_1`), a veteran PMC unit sets up an asymmetrical **Crossfire Kill Zone Ambush**:
1. **Chokepoint Exploitation:** Identify narrow geometric passages (alleys, cuts, gaps between warehouses) that funnel the player squad.
2. **Elevated Crossfire (Enfilade):** Position fireteams on elevated perches (roofs, walkways, interior window sills) on both sides of the passage.
3. **Trigger Discipline & Target Attention (#54):** Withhold fire while the enemy approaches, springing the trap only when the target is deep inside the kill bag and their weapons/attention are facing away.
4. **Fairness & Atmospheric Tells:** Provide subtle environmental and audio/visual cues allowing observant players to detect, counter-ambush, flank, or bypass the trap.

---

## 2. Player-Facing Behaviour

### Phase 1: The Trap Is Set (Silent Overwatch)
- As the player squad approaches a narrow corridor between two 3+ meter concrete buildings, the area appears eerily quiet.
- PMC operators are in prone or crouched high-cover holds along the roof parapets on both sides.
- No red contact pings, no voice chatter, no active movement until triggered.

### Phase 2: Player Tells & Forewarning
Before stepping into the fatal funnel, an observant player can detect the ambush through distinct tells:
- **Scope Glint:** An intermittent specular glint on the roof edge (Niagara flash) every 4–7 seconds if looking towards the roofline.
- **Snow Disturbance:** Fresh boot tracks leading to the base of access ladders or snow falling from a roof edge where a sniper shifted position.
- **HQ Radio Warning:** If the squad stops before the entrance, HQ crackles over the radio: *"Comms indicate chatter in grid B-4... check those rooftops before stepping into the cut."*
- **Dog Scent / Scout Radar:** A military combat dog stops and growls towards the high perimeter (`HoundScentSmoke`).

### Phase 3: The Spring of the Trap
- The trap springs when the lead operative crosses the **Commitment Threshold** (halfway through the corridor) AND their vision cone is oriented away from the shooters (e.g. looking straight ahead down the alley).
- The PMC commander issues a synchronized fire command:
  1. A flashbang or fragmentation grenade lands behind the rear guard, cutting off egress.
  2. Simultaneous crossfire erupts from both rooftops into the flank and rear of the trapped squad.

### Phase 4: Fallbacks & Counter-Play
- If the player throws smoke into the alley, snipes a guard, ascends an exterior flank ladder, or reverses course, the PMC patience breaks:
  - Rooftop PMCs switch to suppressive fire and displace along roof parapets (`PmcRelocate`).

---

## 3. The Algorithm & Data Flow

```
[Chokepoint Identification]
   └── Corridor Width: 100 - 300 cm
   └── Flanking Heights: >= 300 cm (Level 1+)
   └── Line of Sight: Longitudinal sweep clear
         │
[Position Allocation]
   ├── Split Squad: 1-2 Marksmen / Riflemen on Left Roof
   ├── 1-2 Riflemen / Automatic Gunner on Right Roof
   └── Cover Stance: Low cover lean / Prone edge peek
         │
[Trigger Evaluation Loop (every 0.25 s)]
   ├── Condition A: Target Depth in Zone >= 50%
   ├── Condition B: Target Attention Facing away (Cos >= 0.5)
   ├── Condition C: Fallback Trigger (Spotted / Grenade / Timer Expired)
         │
[Execution: Coordinated Alpha Strike]
   └── Synchronized FireOrder via SquadOrderCatalog
```

### Data Structures (`KillZoneAmbushTypes.h`)
```cpp
struct FKillZone
{
    FVector2D ZoneStart;
    FVector2D ZoneEnd;
    float CorridorWidthCm;
    TArray<FVector> LeftPerches;
    TArray<FVector> RightPerches;
    TArray<TWeakObjectPtr<AOperativeCharacter>> Ambushers;
    float PatienceTimerSeconds;
    bool bSprung;
};
```

---

## 4. Key Parameters & Numerical Thresholds (Proposal)

| Parameter | Proposal Value | Rationale |
|---|---|---|
| **Chokepoint Width (`CorridorWidthMin/Max`)** | `100.f – 300.f` cm | Corresponds to standard narrow streets and cuts in `gen_ref_01_02_depot_1.json`. |
| **Flanking Elevation (`MinFlankElevationCm`)** | `300.f` cm | Minimum roof height (Level 1 = 3 m) to prevent ground-level melee bypass. |
| **Commitment Depth Threshold (`CommitmentPercent`)** | `50 %` | Ensures the entire squad is inside the fatal corridor before firing. |
| **Target Attention Cosine Threshold (`MaxFacingCos`)** | `0.3` (>= 70° angle) | Target must be looking roughly perpendicular or away from the ambushers. |
| **Patience Timer (`PatienceSeconds`)** | `45.f` s | If player camps outside the entrance, PMCs relocate or deploy a probe scout. |
| **Glint Periodicity (`ScopeGlintInterval`)** | `5.0 ± 1.5` s | Gives a clear visual window to reward careful reconnaissance. |
| **Engagement Standoff (`AmbushEngagementBand`)** | `15.f – 35.f` m | Matches M16 effective combat band from elevated perch to ground. |

---

## 5. Fairness & Counter-Play

1. **Never a Blind Death:** Ambush fire from elevation begins with a single warning burst or near-miss grenade before lethal alpha focus.
2. **Defilade Opportunities:** Every generated corridor must feature at least one set of low concrete barriers or recessed doorways allowing immediate cover.
3. **Flank Routes:** The level generator must always guarantee at least one bypass ladder or interior stairs allowing player scouts to climb the roofs *behind* the ambushers.
4. **Smoke Screen Counter:** Smoke grenades deployed inside the corridor break elevated line of sight for 15 seconds, allowing the squad to withdraw or breach through.

---

## 6. Smokes & Automated Verification

- **`CombatLabAmbushSmoke`**: Verifies ambush trigger conditions, delayed opening of fire, and target acquisition.
- **`PmcStandoffSmoke`**: Validates elevated rifle engagement bands against ground targets.
- **`RoofCutawaySmoke`**: Validates camera readability and sight lines when characters fight on roof edges over street corridors.
- **New Proposed Smoke:** `KillZoneAmbushSmoke`
  - Spawns a 3-man PMC team on Depot roofs above `Z_cut`.
  - Bot squad walks along `R_main`.
  - Asserts that PMCs hold fire until bot reaches 50% corridor length, then open fire simultaneously.

---

## 7. Open Questions for Director & Team

1. *Interior Windows vs. Roofs:* In Milestone 1, should ambushes be restricted strictly to open roofs with parapets, or should interior window shooting be supported (requires window frame destruction/glass shatter)?
2. *Lethality on Normal Difficulty:* Should the first volley deal 50% damage to prevent instant wipeouts, or deal full realistic damage to reinforce punishing tactical discipline?
3. *Player Ambush Symmetry:* Can the player squad issue a "Wait for Ambush" order to hold fire at a doorway until enemies enter the room?
