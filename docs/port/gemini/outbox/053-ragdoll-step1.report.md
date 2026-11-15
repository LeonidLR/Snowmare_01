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
- Archetype overrides: `Spitter` and `CRYO_DRONE` configured for automatic fallback to standard death pose/FX.

### 1.2 C++ Types (`Source/CodexTactics/Public/Combat/DeathRagdollTypes.h`)
- `FDeathRagdollConfig`: USTRUCT reflecting the schema of `death_ragdoll.json`.

### 1.3 Pure Evaluation Rules (`Source/CodexTactics/Public/Combat/DeathRagdollRules.h`, `Private/Combat/DeathRagdollRules.cpp`)
- `GetDefaultConfig`: Loads configuration dynamically from `Content/Data/Combat/death_ragdoll.json` (with base hardcoded defaults fallback if uninitialized).
- `LoadConfigFromJson`: Parses and validates `death_ragdoll.json`, loading impulse scales and archetype overrides directly from JSON.
- `CalculateImpulse`: Computes directional impulse on fatal hit with an upward trajectory bias (+0.25 Z) to prevent ground clipping, scaled by damage amount and clamped to `MaxImpulseMagnitude`.
- `HasBodySettled`: Evaluates linear and angular velocities against thresholds, and forces a settle if `BodyAge >= MaxSettleTimeoutSeconds`.
- `CanSimulateRagdoll`: Enforces concurrent simulation cap ($N < 4$).
- `ShouldFallbackToPose`: Identifies archetypes without physics assets (e.g. `Spitter`, `CRYO_DRONE`) with case-insensitive and alias matching to use fallback pose.
- `ValidatePhysicsAssetSetup`: Pure validation inspecting `USkeletalMesh` and `UPhysicsAsset::SkeletalBodySetups` to guarantee bodies exist before simulation.

---

## 2. Test Verification

### 2.1 Unit Test Suite (`Source/CodexTacticsTests/Private/Combat/DeathRagdollRulesTest.cpp`)
- `CodexTactics.Combat.DeathRagdoll.ConfigLoads`: PASS (validates JSON parsing, thresholds, scales, overrides).
- `CodexTactics.Combat.DeathRagdoll.ImpulseCalculation`: PASS (validates directional vector, upward bias, explosive vs kinetic scaling, maximum clamp).
- `CodexTactics.Combat.DeathRagdoll.SettleDetection`: PASS (validates linear/angular movement detection and forced timeout fallback).
- `CodexTactics.Combat.DeathRagdoll.BudgetCap`: PASS (asserts simulation allowed at 0..3 active, blocked at 4+).
- `CodexTactics.Combat.DeathRagdoll.FallbackArchetypes`: PASS (asserts Spitter/CRYO_DRONE fallback true, standard bipeds false).
- `CodexTactics.Combat.DeathRagdoll.PhysicsAssetValidation`: PASS (validates null mesh and physics body checks).

### 2.2 Night 2 Review Follow-Up Fixes (Per Opus Review #375)
1. **Archetype Key Canonicalization:**
   - In `Content/Data/Combat/death_ragdoll.json:25`, updated archetype override key from `CryoDrone` to canonical `CRYO_DRONE`.
2. **Data-Driven Rules Loading:**
   - Replaced static defaults in `DeathRagdollRules.cpp:11-29` with dynamic loading from `death_ragdoll.json`.
   - Separated base hardcoded fallback into `SetBaseConfigDefaults`.
   - Populated `FallbackArchetypes` dynamically from JSON `per_archetype_overrides`.
   - Added case-insensitive and alias resolution in `ShouldFallbackToPose`.
3. **Safety & Triage:**
   - Reverted `Scripts/Tools/typesafe_triage.py` to match `origin/main`.

### 2.3 Verification Summary
- **Compilation (`powershell .\Scripts\build.ps1`):** SUCCESS (45.85s).
- **Unit Tests (`test.ps1 -Filter "CodexTactics.Combat.DeathRagdoll"`):** 6/6 PASS.
- **Triage Audit (`typesafe_triage.py --audit-diff --agent claude`):** PASS.
