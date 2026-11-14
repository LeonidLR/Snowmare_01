# Implementation Map #1 + #49 — Kill Zone Ambush from Roofs (Enfilade Crossfire)

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Target:** Task #1 & #49 (`pl-kill-zone-ambush`) — Засада на открытый сектор / Kill Zone с крыш  
**Target Assignee:** Codex / Claude  
**Governing Documents:** `docs/design/KILL_ZONE_AMBUSH.md`, `docs/systems/pmc-squad-ai.md` §7, `docs/port/TASK_BOARD.md` §1 & §49

---

## 1. Tactical Concept & Operational Goal

### 1.1. Tactical Vision
In urban/industrial sectors like Depot (`L_Gen_Depot_1`), hostile PMC units must operate as disciplined military veterans rather than wandering targets. A veteran PMC fireteam establishes an asymmetrical **Crossfire Kill Zone Ambush**:
1. **Geometric Funnel (Fatal Funnel):** Exploits narrow urban cuts and corridors (width $1.0\text{ m} - 3.0\text{ m}$) flanked by high industrial roofs ($Z \ge +300\text{ cm}$).
2. **Elevated Enfilade Positions:** Shooters deploy in prone/low-cover holds along opposing roof parapets, establishing intersecting crossfire angles.
3. **Trigger Discipline (Holding Fire):** Ambushers withhold fire while the player squad approaches, springing the trap only when the squad reaches the **Commitment Threshold** ($\ge 50\%$ along corridor depth) AND their facing direction is oriented away from the perches (target attention cosine $\le 0.3$).
4. **Fairness & Atmospheric Tells:** Subtle tells allow observant players to anticipate, avoid, or counter-ambush:
   - Specular scope glint Niagara flash along roof parapet every $5.0 \pm 1.5\text{ s}$.
   - Military dog growls toward the high perimeter (`HoundScentSubsystem`).
   - HQ radio intercept crackles if the squad pauses near the cut entrance.
5. **Alpha Strike Execution:** When triggered, a cut-off grenade/flashbang is thrown behind the rear guard, followed by simultaneous crossfire into flanks. Smoke grenades deployed by the player blind elevated line-of-sight for $15\text{ s}$, creating a viable escape window.

---

## 2. Existing Classes to Extend vs. New Code

### 2.1. Reusable Engine Components (~80% Reuse)

| Component | File & Line | Reusable Function / Capability |
| :--- | :--- | :--- |
| **PMC Squad Subsystem** | `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:1-400` | Squad coordination, tactical roles (Gunner, Marksman, Leader), order dispatch. |
| **Squad Order Catalog** | `Source/CodexTactics/Public/AI/SquadOrderCatalog.h` | `FPmcOrder` for synchronized fire, grenade tosses, and stance assignments. |
| **Perception & Visibility** | `Source/CodexTactics/Private/AI/PerceptionRules.cpp:1-180` | Sight cone math, elevation bonuses, facing angle evaluations. |
| **Scent & Dog Alerts** | `Source/CodexTactics/Private/Characters/EnemyCharacterScent.cpp:50-120` | `HoundScentSubsystem` emits growl and warning text when sensing elevated hostiles. |
| **Radio Comms & Warnings** | `Source/CodexTactics/Private/UI/GameMessageSubsystem.cpp:1-150` | `UGameMessageSubsystem::PostMessage` displays atmospheric HQ warnings. |
| **Smoke Screen Cover** | `Source/CodexTactics/Private/Combat/SmokeGrenadeSubsystem.cpp:1-200` | Obscures line of sight between elevated shooters and ground corridor. |

### 2.2. New Code Required (~20%)

1. **`KillZoneAmbushRules` (Pure State & Geometric Evaluation):**
   - Header: `Source/CodexTactics/Public/AI/KillZoneAmbushRules.h`
   - Source: `Source/CodexTactics/Private/AI/KillZoneAmbushRules.cpp`
   - `EvaluateCorridorGeometry`: Identifies valid choke segments ($W \in [100, 300]\text{ cm}$, $H \ge 300\text{ cm}$).
   - `IsCommitmentThresholdReached`: Calculates squad progression percentage along corridor vector.
   - `IsTargetAttentionDiverted`: Evaluates whether lead operative's look vector faces away from shooters ($\cos \theta \le 0.3$).
   - `ShouldSpringAmbush`: Evaluates triggers (Commitment reached, Patience timer expired, or Trap compromised).
2. **`UKillZoneAmbushSubsystem` (`UWorldSubsystem`):**
   - Header: `Source/CodexTactics/Public/AI/KillZoneAmbushSubsystem.h`
   - Source: `Source/CodexTactics/Private/AI/KillZoneAmbushSubsystem.cpp`
   - Scans level geometry on map load (or reads from level JSON `ambush_zones`).
   - Ticks active ambushers at low frequency ($4\text{ Hz}$, $0.25\text{ s}$) to evaluate player squad entry.
   - Controls Niagara scope glint effect spawns on roof edges.

---

## 3. Data Keys & Tuning

Configured in `Content/Data/AI/kill_zone_ambush.json`:

```json
{
  "corridor_width_min_cm": 100.0,
  "corridor_width_max_cm": 300.0,
  "min_flank_elevation_cm": 300.0,
  "commitment_depth_ratio": 0.50,
  "max_facing_cos": 0.30,
  "patience_seconds": 45.0,
  "scope_glint_interval_s": 5.0,
  "scope_glint_jitter_s": 1.5,
  "first_volley_damage_scale": 0.65,
  "smoke_break_duration_s": 15.0
}
```

---

## 4. Insertion Points in Existing Code

### 4.1. Ambush Evaluation Loop in `UKillZoneAmbushSubsystem`
```cpp
void UKillZoneAmbushSubsystem::TickAmbushEvaluation(float DeltaSeconds)
{
    for (FKillZoneAmbushInstance& Ambush : ActiveAmbushes)
    {
        if (Ambush.bSprung) continue;

        // Check if player squad has entered corridor
        const FVector SquadLeadPos = SquadSubsystem->GetLeader()->GetActorLocation();
        const FVector SquadFacing = SquadSubsystem->GetLeader()->GetActorForwardVector();

        const bool bDeepInZone = KillZoneAmbushRules::IsCommitmentThresholdReached(
            SquadLeadPos, Ambush.CorridorStart, Ambush.CorridorEnd, AmbushConfig.CommitmentDepthRatio);
        
        const bool bFacingAway = KillZoneAmbushRules::IsTargetAttentionDiverted(
            SquadFacing, Ambush.CorridorDir, AmbushConfig.MaxFacingCos);

        Ambush.PatienceTimer -= DeltaSeconds;

        if ((bDeepInZone && bFacingAway) || Ambush.PatienceTimer <= 0.0f || Ambush.bCompromised)
        {
            SpringAmbush(Ambush);
        }
    }
}
```

### 4.2. Springing the Trap (Alpha Strike)
```cpp
void UKillZoneAmbushSubsystem::SpringAmbush(FKillZoneAmbushInstance& Ambush)
{
    Ambush.bSprung = true;
    
    // 1. Rear Cut-off Grenade
    if (AOperativeCharacter* Sapper = Ambush.RearGuard.Get())
    {
        const FVector CutoffPoint = Ambush.CorridorStart + FVector(0, 0, 10);
        GrenadeSubsystem->ThrowGrenade(Sapper, CutoffPoint, EGrenadeType::Flashbang);
    }

    // 2. Synchronized Crossfire from Roofs
    for (TWeakObjectPtr<AOperativeCharacter>& Shooter : Ambush.Shooters)
    {
        if (Shooter.IsValid())
        {
            Shooter->SetStance(EOperativeStance::Standing);
            Shooter->OrderQuickFireAt(SquadSubsystem->GetLeader()->GetActorLocation());
        }
    }
}
```

---

## 5. Automated Verification & Smokes

* **Unit Test:** `Source/CodexTacticsTests/Private/AI/KillZoneAmbushRulesTest.cpp`
  - Validates commitment ratio calculations along straight and angled corridors.
  - Validates cosine threshold for diverted attention (perpendicular or forward vs facing roof).
  - Asserts patience timer expiry triggers fallback engagement.
* **Smoke Command:** `KillZoneAmbushSmokeCommand` (`Codex.Test.KillZoneAmbush`):
  1. Sets up 3-man PMC team in high parapet cover on Depot roofs over `Z_cut`.
  2. Spawns player bot team walking along `R_main`.
  3. Asserts PMCs hold fire while bot is in first $30\%$ of corridor.
  4. Asserts PMCs open coordinated fire simultaneously when bot reaches $50\%$ depth.
  5. Asserts bot deploying smoke grenade immediately breaks elevated line of sight.

---

## 6. Open Design Questions (with Recommended Defaults)

1. **First Volley Lethality:**
   - *Question:* Should the initial elevated volley deal full damage (risk of instant team wipe) or be scaled down?
   - *Recommended Default:* Scale first volley damage to $65\%$ (`first_volley_damage_scale: 0.65`). This preserves intense danger and armor shredding while giving the player tactical agency to dive for low barriers.
2. **Player Ambush Symmetry:**
   - *Question:* Can the player squad set an identical "Ambush Hold" order at doorways or roof edges?
   - *Recommended Default:* Yes. When the player sets an overwatch cone while in high cover, add a toggle `"Hold until entered"` that delays overwatch fire until the enemy target is fully inside the sector.
3. **Interior Window Shooting:**
   - *Question:* Should shooters only be placed on exterior open roofs, or inside warehouse window frames?
   - *Recommended Default:* Limit Milestone 1 ambushes strictly to open roofs with parapets. Interior window shooting requires destructible glass and occlusion geometry, which can be deferred to Milestone 2.
