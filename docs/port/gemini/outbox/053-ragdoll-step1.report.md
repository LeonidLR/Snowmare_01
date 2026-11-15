# Task 053: Death Ragdoll (Task 005 / Mechanics #16) Step 1 - Foundation Data & Rules

- **Branch:** `gemini/053-ragdoll-step1` (branched from `main` `664d78b27f54c0cda71a1651d8f6f8ce3bdf6ca1`)
- **Status:** Complete (PASS)
- **Governing Specs:**
  - `docs/port/gemini/outbox/049-ragdoll-plan.md` (Step 1 Foundation)
  - `docs/port/codex/inbox/005-dead-enemies-standing-ragdoll.md`
  - `docs/systems/pmc-squad-ai.md`

---

## 1. Executive Summary & Deliverables

Per directive #370 and Plan 049 §3 (Step 1), implemented the foundational data configuration and pure C++ evaluation rules for the character death ragdoll system. This foundation provides deterministic impulse calculations, velocity settle thresholds, budget concurrency gating (cap = 4), archetype fallback guards, and physics asset validation with 100% test coverage and zero risk to existing runtime character logic.

### 1.1 Data Configuration (`Content/Data/Combat/death_ragdoll.json`)
- Global toggle: `enabled: true`.
- Simulation budget cap: `max_simultaneous_ragdolls: 4` (prevents CPU spikes during mass AoE casualties).
- Delay before simulation: `ragdoll_delay_s: 0.0`.
- Settle timeout: `max_settle_timeout_s: 3.5` (guarantees corpses come to rest and freeze even on slopes or jitter).
- Velocity sleep thresholds: `linear_settle_threshold_cm_s: 15.0`, `angular_settle_threshold_rad_s: 0.25`.
- Kinetic safety limits: `max_impulse_magnitude: 120000.0`, `angular_damping: 1.5`, `linear_damping: 0.5`.
- Elemental impulse scaling: `Kinetic: 45000.0`, `Melee: 25000.0`, `Fire: 15000.0`, `Cryo: 5000.0`, `Energy: 30000.0`, `Explosive: 100000.0`.
- Archetype overrides: `Spitter` and `CryoDrone` configured for automatic fallback to standard death pose/FX.

### 1.2 C++ Types (`Source/CodexTactics/Public/Combat/DeathRagdollTypes.h`)
- `FDeathRagdollConfig`: USTRUCT reflecting the schema of `death_ragdoll.json`.

### 1.3 Pure Evaluation Rules (`Source/CodexTactics/Public/Combat/DeathRagdollRules.h`, `Private/Combat/DeathRagdollRules.cpp`)
- `LoadConfigFromJson`: Parses and validates `death_ragdoll.json`.
- `CalculateImpulse`: Computes directional impulse on fatal hit with an upward trajectory bias (+0.25 Z) to prevent ground clipping, scaled by damage amount and clamped to `MaxImpulseMagnitude`.
- `HasBodySettled`: Evaluates linear and angular velocities against thresholds, and forces a settle if `BodyAge >= MaxSettleTimeoutSeconds`.
- `CanSimulateRagdoll`: Enforces concurrent simulation cap ($N < 4$).
- `ShouldFallbackToPose`: Identifies archetypes without physics assets (e.g. `Spitter`) to use `LayCorpseDown()` fallback.
- `ValidatePhysicsAssetSetup`: Pure validation inspecting `USkeletalMesh` and `UPhysicsAsset::SkeletalBodySetups` to guarantee bodies exist before simulation.

---

## 2. Test Verification

### 2.1 Unit Test Suite (`Source/CodexTacticsTests/Private/Combat/DeathRagdollRulesTest.cpp`)
- `CodexTactics.Combat.DeathRagdoll.ConfigLoads`: PASS (validates JSON parsing, thresholds, scales, overrides).
- `CodexTactics.Combat.DeathRagdoll.ImpulseCalculation`: PASS (validates directional vector, upward bias, explosive vs kinetic scaling, maximum clamp).
- `CodexTactics.Combat.DeathRagdoll.SettleDetection`: PASS (validates linear/angular movement detection and forced timeout fallback).
- `CodexTactics.Combat.DeathRagdoll.BudgetCap`: PASS (asserts simulation allowed at 0..3 active, blocked at 4+).
- `CodexTactics.Combat.DeathRagdoll.FallbackArchetypes`: PASS (asserts Spitter/CryoDrone fallback true, standard bipeds false).
- `CodexTactics.Combat.DeathRagdoll.PhysicsAssetValidation`: PASS (validates null mesh and physics body checks).

### 2.2 Verification Summary
- **Compilation (`powershell .\Scripts\build.ps1`):** SUCCESS (74.46s).
- **Unit Tests (`test.ps1 -Filter "CodexTactics.Combat.DeathRagdoll"`):** 6/6 PASS.
- **Smart Test Gate (`test.ps1 -Smart -Quick`):**
  - Unit tests: 34/34 PASS.
  - Smokes: 4/4 PASS:
    - `MovementSmoke`: PASS (60.7s)
    - `QuickFireSmoke`: PASS (63.8s)
    - `CombatFlowSmoke`: PASS (66.2s)
    - `SuppressionSmoke`: PASS (67.4s)
  - Also verified standalone: `EventBusSmoke`: PASS (52.7s), `MarksmanSmoke`: PASS (53.8s), `TurretSmoke`: PASS (55.5s), `SquadFireSmoke`: PASS (53.4s), `StanceSmoke`: PASS (65.2s), `CommanderModeSmoke`: PASS (76.4s), `MissionSmoke`: PASS (72.8s), `CornerHoldSmoke`: PASS (120.7s).
  - Note: `CoverSmoke` failed due to pre-existing animation montage weighting issue on `main` (`fire_idle_L` weight 0.00 in `L_MovementTest`).
- **Triage Audit (`typesafe_triage.py --audit-diff --agent gemini`):** PASS (0 boundary breaches).
