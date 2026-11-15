# Task 056: Death Ragdoll (Task 005 / Mechanics #16) Step 2 - Physics on Death & Standing Corpse Fixes

- **Branch:** `gemini/056-ragdoll-step2`
- **Status:** Complete (PASS)
- **Governing Specs:**
  - `docs/port/gemini/outbox/049-ragdoll-plan.md` (Step 2 Implementation)
  - `docs/port/codex/inbox/005-dead-enemies-standing-ragdoll.md`
  - Opus Review #375 / Task Board 056

---

## 1. Executive Summary & Deliverables

Implemented Step 2 of the Death Ragdoll system across `Combat/` and `Characters/`. This activates physical ragdoll simulation on death for character models with valid physics assets, eliminates standing corpse bugs through pelvic height verification and knockdown component death guards, prevents game thread disk hitches via world-level config caching, and ensures zero CPU overhead on settled bodies.

### 1.1 Data-Driven Balance & Zero-Hitch Caching
- **Config Schema (`Content/Data/Combat/death_ragdoll.json`):**
  - Added `upward_impulse_bias` (`0.25`), `min_damage_factor` (`0.5`), and `max_damage_factor` (`3.0`).
  - Added corresponding fields to `FDeathRagdollConfig` (`DeathRagdollTypes.h`).
- **Disk Hitch Prevention:**
  - `UCorpseSubsystem::OnWorldBeginPlay` loads and caches `death_ragdoll.json` into memory (`RagdollConfig`).
  - Fatal damage events query the cached configuration; no disk I/O or JSON parsing occurs on the game thread during combat.
  - Removed hardcoded default fallbacks (`SetBaseConfigDefaults`) per review #375; missing config logs an explicit error and falls back to `LayCorpseDown`.
- **Runtime Console Toggle:**
  - Added `Codex.DeathRagdoll.Enabled` (default `1`). Setting to `0` cleanly routes all deaths to animation/`LayCorpseDown` fallback.

### 1.2 Standing Corpse Root Cause & Fixes
- **Root Cause:**
  - When characters with death montages died, if the montage blended out before corpse freeze or lacked a held terminal frame, the animation blueprint reverted to the default locomotion state machine, causing `FreezeCorpse()` to pause animations while standing upright.
- **Fix 1 — Pelvic Floor Check:**
  - In `AEnemyCharacter::FreezeCorpse()` and `AOperativeCharacter::FreezeCorpse()`: if not active ragdoll, queries `pelvis` bone location against floor level. If `PelvisHeightAboveFloor > 45.0f` (standing/crouched upright), forces `LayCorpseDown()` (enemies) or `PlayDeathFall()` (operatives).
- **Fix 2 — Knockdown Component Death Guards:**
  - In `UKnockdownComponent::TickComponent()`, added immediate guard on `OwnerHealth && !OwnerHealth->IsAlive()`. Cancels ongoing get-up transitions, resets phase, and clears buffered orders on death to prevent post-mortem resurrection.
- **Fix 3 — Mesh Offset Decoupling:**
  - In `AOperativeCharacter::EnforceMeshBaseOffset()`, added guards `bIsRagdollActive || bKilledInAction` so skeletal meshes simulating physics or resting as corpses are never forcefully snapped back to capsule offsets.

### 1.3 Ragdoll Activation & Settlement Lifecycle
- **Activation (`UCorpseSubsystem::TryActivateDeathRagdoll`):**
  - Invoked from `AEnemyCharacter::HandleDied()` and `AOperativeCharacter::BecomeCorpse()`.
  - Checks config enablement, fallback archetypes (`Spitter`, `CRYO_DRONE`), concurrency budget cap (`<= 4`), and physics asset validity.
  - Disables capsule collision (`NoCollision`), enables mesh collision profile `Ragdoll`, wakes rigid bodies, enables physics simulation, and applies kinetic impulse to `pelvis` with `bVelChange = false` (mass-scaled impulse).
- **Settling & Freezing (`UCorpseSubsystem::UpdateSettling`):**
  - Ticks active ragdolls, monitoring root bone linear and angular velocities against `LinearSettleThresholdCmS` (15 cm/s) and `AngularSettleThresholdRadS` (0.25 rad/s) or timeout fallback (`MaxSettleTimeoutSeconds` = 3.5s).
  - On settle: puts all rigid bodies to sleep, disables physics simulation (`SetAllBodiesSimulatePhysics(false)`), sets collision profile to `NoCollision`, sets `bPauseAnims = true`, and calls `FreezeCorpse()`.
  - Result: Settled corpses consume **0 CPU cycles** (no ticks, no physics evaluation).

---

## 2. In-Game Smoke & Regression Verification

### 2.1 Headless Smoke Test (`CodexTactics.DeathRagdollSmoke`)
- Implemented `Source/CodexTacticsDev/Private/Debug/DeathRagdollSmokeCommand.cpp` and registered in `Scripts/test_map.json`.
- Spawns `EEnemyArchetype::Frostbitten` and hostile PMC operative on `L_MovementTest`.
- Applies lethal damage.
- Verifies:
  1. Enemy and PMC fighter activate death ragdoll: **PASS**
  2. Capsules have `NoCollision` / ignore pawns: **PASS**
  3. Corpses settle and freeze within 3.5s (Enemy: 2.2s, PMC: 2.2s): **PASS**
  4. Enemy pelvis height above floor: **16.1 cm < 45.0 cm** (flat on floor): **PASS**
  5. PMC fighter pelvis height above floor: **22.0 cm < 45.0 cm** (flat on floor): **PASS**
  6. Overall smoke result: **`Smoke RESULT: PASS`**

### 2.2 Regression Smokes
- **`CodexTactics.KnockdownSmoke`:** **PASS** (48.0s) — verified brute blow knockdown, get-up, recovery bar, death while down (`Death_Back` / `Death_Front`), turn-based AP costs, and mesh alignment.
- **`CodexTactics.EnemyDeathSmoke`:** **PASS** (43.2s) — verified fallback death clip playback on `OneShotSlot` holds final pose indefinitely without returning to graph.

### 2.3 Unit Test Suite
- `powershell .\Scripts\test.ps1 -Filter "CodexTactics.Combat.DeathRagdoll"`:
  - `CodexTactics.Combat.DeathRagdoll.BudgetCap`: PASS
  - `CodexTactics.Combat.DeathRagdoll.ConfigLoads`: PASS (asserts upward bias 0.25, min damage 0.5, max damage 3.0)
  - `CodexTactics.Combat.DeathRagdoll.FallbackArchetypes`: PASS
  - `CodexTactics.Combat.DeathRagdoll.ImpulseCalculation`: PASS
  - `CodexTactics.Combat.DeathRagdoll.PhysicsAssetValidation`: PASS
  - `CodexTactics.Combat.DeathRagdoll.SettleDetection`: PASS
  - **Result: 6 tests, 0 failed.**
- Full changed test suite: **0/560 unit tests failed (100% PASS)**.

### 2.4 Safety & Triage Audit
- Command: `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`
- **Boundary Breach:** PASS
- **Risk Rating:** 2.1 / 4 (PASS)
