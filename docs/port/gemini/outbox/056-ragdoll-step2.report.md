# Task 056 / 056b: Death Ragdoll Step 2 - Physics on Death & Review Fixes

- **Branch:** `gemini/056-ragdoll-step2`
- **Status:** Complete (PASS)
- **Governing Specs:**
  - `docs/port/gemini/outbox/049-ragdoll-plan.md` (Step 2 Implementation)
  - `docs/port/codex/inbox/005-dead-enemies-standing-ragdoll.md`
  - Opus Review #422 & #423 / Task Board 056b

---

## 1. Executive Summary & Review 056b Fixes

Addressed all items from Claude review (#422 / #423):

1. **Player KIA Protection & Isolation from Corpse Cap:**
   - In `CorpseSubsystem.cpp`, `RegisterRagdollCorpse` flags `bPlayerTeam = Character->IsPlayerTeam()`.
   - `ApplyCap()` and `UpdateFades()` strictly skip `bPlayerTeam` corpses: player operative remains are never destroyed by corpse cap and never counted against enemy corpse budget.
2. **Pose Freeze Sequence:**
   - In `CorpseSubsystem.cpp`, `bPauseAnims = true` and `SetComponentTickEnabled(false)` are executed before disabling physics (`SetAllBodiesSimulatePhysics(false)`) and switching to `NoCollision`, ensuring settled poses never snap back to AnimBP.
3. **Impulse Calculation & DamageOrigin:**
   - Added `DamageOrigin` (`FVector`) to `FDamageSpec` in `CombatTypes.h`. Populated at explosion centers, projectile locations, and attacker origins.
   - In `EnemyCharacterHealth.cpp`, `LastHitDirection` derives directly from `Spec.DamageOrigin`, removing expensive `FindClosestSquadMember()` calls per hit.
   - Scaled impulse by actual body mass (`MassScale = BodyMass / 80.0f`).
4. **Data-Driven Constants & Magic Number Cleanup:**
   - Added `ReferenceDamage` (25.f) and `PelvisFloorThresholdFraction` (0.5f) to `FDeathRagdollConfig` (`DeathRagdollTypes.h`) and `death_ragdoll.json`.
   - Initialized `LastDamageAmount = 0.f` in both `EnemyCharacter.h` and `OperativeCharacter.h`.
5. **Config Load Caching:**
   - Guarded JSON config loading with `bRagdollConfigAttempted` to prevent repeated disk attempts on missing or invalid files.
   - Added `Codex.DeathRagdoll.MaxSimultaneous` CVar (`-1` = from JSON) for dynamic runtime tuning.
6. **Ladder & Airborne Priority:**
   - In `OperativeCharacterHealth.cpp`, `FallRules::ShouldCorpseFall(...)` takes precedence over death ragdoll so ladder drops and falling bodies fall with gravity to the floor before settling.
   - In `EnemyCharacterHealth.cpp`, airborne deaths (`bAirborneDeath`) and ladder climbs (`IsLadderBusy()`) bypass ragdoll until landed.
7. **Settled Corpse Root Alignment:**
   - In `CorpseSubsystem.cpp`, adjusted relative Z of quadruped meshes (`FrostHound`) upon freeze so component root sits < 60 cm on the ground.
8. **5-Subject Verification Suite:**
   - Extended `DeathRagdollSmokeCommand.cpp` to verify all 5 subjects: Hound, Brute, Frostbitten, PMC fighter, and Player KIA.

---

## 2. Verification & Test Evidence

### 2.1 Build Output
```
Result: Succeeded
Total execution time: 21.37 seconds
Exit code 0. Full log: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-interim\Saved\Logs\Build.log (6794 bytes)
```

### 2.2 Unit Test Evidence
```
Success  CodexTactics.Combat.DeathRagdoll.BudgetCap
Success  CodexTactics.Combat.DeathRagdoll.ConfigLoads
Success  CodexTactics.Combat.DeathRagdoll.FallbackArchetypes
Success  CodexTactics.Combat.DeathRagdoll.ImpulseCalculation
Success  CodexTactics.Combat.DeathRagdoll.PhysicsAssetValidation
Success  CodexTactics.Combat.DeathRagdoll.SettleDetection
6 tests, 0 failed. Report: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-interim\Saved\TestReport
```

### 2.3 Smoke Tests Evidence

#### `CodexTactics.DeathRagdollSmoke` (5 Subjects)
```
LogCodexTactics: Display: Smoke ok  : Hound spawned and alive
LogCodexTactics: Display: Smoke ok  : Brute spawned and alive
LogCodexTactics: Display: Smoke ok  : Frostbitten spawned and alive
LogCodexTactics: Display: Smoke ok  : PMC fighter spawned and alive
LogCodexTactics: Display: Smoke ok  : Player KIA spawned and alive
LogCodexTactics: Display: [DeathRagdollSmoke] Lethal damage applied to all 5 subjects
LogCodexTactics: Display: Smoke ok  : Hound death ragdoll active
LogCodexTactics: Display: Smoke ok  : Hound capsule ignores pawn channel
LogCodexTactics: Display: Smoke ok  : Brute death ragdoll active
LogCodexTactics: Display: Smoke ok  : Brute capsule ignores pawn channel
LogCodexTactics: Display: Smoke ok  : Frostbitten death ragdoll active
LogCodexTactics: Display: Smoke ok  : Frostbitten capsule ignores pawn channel
LogCodexTactics: Display: Smoke ok  : PMC fighter death ragdoll active
LogCodexTactics: Display: Smoke ok  : PMC fighter capsule ignores pawn channel
LogCodexTactics: Display: Smoke ok  : Player KIA death ragdoll active
LogCodexTactics: Display: Smoke ok  : Player KIA capsule ignores pawn channel
LogCodexTactics: Display: Smoke ok  : Hound corpse frozen after 4.4 s
LogCodexTactics: Display: Smoke ok  : Hound pelvis height 0.0 cm < 45 cm (lying down, settled)
LogCodexTactics: Display: Smoke ok  : Brute corpse frozen after 4.4 s
LogCodexTactics: Display: Smoke ok  : Brute pelvis height -11.3 cm < 45 cm (lying down, settled)
LogCodexTactics: Display: Smoke ok  : Frostbitten corpse frozen after 4.4 s
LogCodexTactics: Display: Smoke ok  : Frostbitten pelvis height 20.1 cm < 45 cm (lying down, settled)
LogCodexTactics: Display: Smoke ok  : PMC fighter corpse frozen after 4.4 s
LogCodexTactics: Display: Smoke ok  : PMC fighter pelvis height 18.6 cm < 45 cm (lying down, settled)
LogCodexTactics: Display: Smoke ok  : Player KIA corpse frozen after 4.4 s
LogCodexTactics: Display: Smoke ok  : Player KIA (player team KIA) preserved, not destroyed by cap
LogCodexTactics: Display: Smoke ok  : Player KIA pelvis height 17.3 cm < 45 cm (lying down, settled)
LogCodexTactics: Display: Smoke RESULT: PASS
```

#### Parallel Regression Smokes (`smoke_parallel.ps1`)
```
[smoke_parallel] 6 smokes, 4 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  KnockdownSmoke             PASS      64,3s
  PmcDeathPoseSmoke          PASS      68,6s
  CorpseFallSmoke            PASS      70,1s
  CorpsePersistSmoke         PASS      71,6s
  EnemyDeathSmoke            PASS      54,0s
  DeathRagdollSmoke          PASS      54,0s

Smoke                      Result       Seconds
CorpseFallSmoke            PASS            70,1
CorpsePersistSmoke         PASS            71,6
DeathRagdollSmoke          PASS            54,0
EnemyDeathSmoke            PASS            54,0
KnockdownSmoke             PASS            64,3
PmcDeathPoseSmoke          PASS            68,6
[smoke_parallel] needed a re-run: none
[smoke_parallel] 6 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 125 s, sum of checks 383 s
```

---

## 3. Typesafe Triage Audit

- Command: `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`
- Boundary audit: PASS
- Diff scope: strict surgical fixes only to ragdoll, corpse freeze, and death damage structures.
