# Plan 071: Panic Flee Speed Respects Stance and Wounds

- **Task:** 071 (`panic-flee-speed`) / Codex 013 (board 062)
- **Governing Spec:** `docs/port/codex/inbox/013-panic-flee-speed.md`, `.gemini/task.md`
- **Base Commit:** `51ef0afba1ee4f6830642b5c2adf52c1246360fb` (`codex/013-panic-speed`)
- **Branch:** `gemini/071-panic-speed`
- **Allowed Paths:**
  - `Source/CodexTactics/Public/Characters/PanicRules.h`
  - `Source/CodexTactics/Private/Characters/PanicRules.cpp`
  - `Source/CodexTactics/Public/Characters/PanicComponent.h`
  - `Source/CodexTactics/Private/Characters/PanicComponent.cpp`
  - `Source/CodexTacticsTests/Private/Characters/PanicRulesTest.cpp`
  - `Source/CodexTacticsDev/Private/Debug/PanicSmokeCommand.cpp`
  - `docs/port/gemini/outbox/071-panic-flee-speed.plan.md`
  - `docs/port/gemini/outbox/071-panic-flee-speed.report.md`

---

## 1. Diagnosis of the Bug

1. **Forced Standing Discards Current Stance:**
   - In `Source/CodexTactics/Private/Characters/PanicComponent.cpp:218`:
     ```cpp
     // Drops the player's orders and the reload; stands up to run (Godot set_stance(STANDING, true)).
     Operative->StopOperative();
     Operative->SetStance(EOperativeStance::Standing);
     ```
     When panic begins in `TriggerPanic`, the operative is unconditionally forced to `Standing`. If the operative was prone or crouching, that stance is discarded.
   - In `Source/CodexTactics/Private/Characters/PanicComponent.cpp:243`:
     ```cpp
     void UPanicComponent::TransitionToCowering()
     {
         ...
         Operative->SetStance(EOperativeStance::Crouching);
     ```
     Transitions unconditionally to crouching, ignoring if an operative should stay prone.

2. **Config FleeSpeed Directly Overrides Movement Speed:**
   - In `Source/CodexTactics/Private/Characters/PanicComponent.cpp:304`:
     ```cpp
     if (Movement)
     {
         Movement->MaxWalkSpeed = Config.FleeSpeed > 0.f ? Config.FleeSpeed * 100.f : Movement->MaxWalkSpeed * Config.SpeedMultiplier;
     }
     ```
     `Config.FleeSpeed` is `6.f` m/s (`600.f` cm/s), which directly overwrites `Movement->MaxWalkSpeed` on every tick during `EPanicPhase::Fleeing`.
     This bypasses:
     - Stance-based speed (`OperativeMovementRules::ComputeMaxSpeed`): Prone crawl speed is `WalkSpeed * ProneSpeedMultiplier = 220 * 0.28 = 61.6` cm/s; crouching speed is `125` cm/s. Instead, a prone soldier was fleeing at `600` cm/s ("уползает как одержимый").
     - Wound multipliers: Light tier (`WoundedSpeedMultiplier = 0.65`), Heavy tier standing (`HeavySpeedMultiplier = 0.45` -> `99` cm/s), Critical tier (crawl only).
     - Cold speed scaling (`ColdSpeedMultiplier`).

---

## 2. Mathematical Model & Pure Rule (`PanicRules::PanicFleeSpeed`)

1. **Design Goals:**
   - Panic flight speed equals the normal speed of the operative's CURRENT state (prone = crawl speed, crouched = crouch speed, injured-tier multipliers applied), never above it.
   - Panic picks the fastest gait the state allows (e.g. standing healthy -> run, not sprint unless normal rules allow sprint for him), but never a speed the same state would not have outside panic.

2. **Numbers & Gait Matrix:**
   - `FOperativeMovementConfig`:
     - `WalkSpeed = 220.f` cm/s
     - `RunSpeed = 725.f` cm/s
     - `CrouchSpeed = 125.f` cm/s
     - `ProneSpeedMultiplier = 0.28f` (Crawl = `61.6` cm/s)
     - `WoundedSpeedMultiplier = 0.65f`
   - `FCriticalWoundConfig`:
     - `HeavyFrom = 0.35f`
     - `HeavySpeedMultiplier = 0.45f` (Standing heavy walk = `99.f` cm/s)
   - Calculated speeds:
     - **Prone Healthy:** `220 * 0.28 = 61.6` cm/s.
     - **Prone Wounded (Light / Heavy / Critical):** `61.6 * 0.65 = 40.04` cm/s (`<= crawl`, satisfies check 1).
     - **Crouch Healthy:** `125.0` cm/s.
     - **Crouch Wounded (Light / Heavy):** `125 * 0.65 = 81.25` cm/s (`<= crouch`, satisfies check 2).
     - **Standing Healthy:** `RunSpeed = 725.0` cm/s (`= run`, satisfies check 3).
     - **Standing Light Wound:** `RunSpeed * WoundedSpeedMultiplier = 725 * 0.65 = 471.25` cm/s (the wounded run).
     - **Standing Badly Wounded (Heavy):** Heavy wound has "no run" (`weapon_anim_sets.json`, `critical_wound.json`), standing speed is `WalkSpeed * HeavySpeedMultiplier = 220 * 0.45 = 99.0` cm/s (`<= wounded run`, satisfies check 4).
     - **Standing Critical:** Critical wound cannot stand; if in this state, crawl speed `40.04` cm/s.

3. **Function Signature:**
   ```cpp
   namespace PanicRules
   {
       CODEXTACTICS_API float PanicFleeSpeed(
           EOperativeStance Stance,
           CriticalWoundRules::EWoundTier WoundTier,
           const FOperativeMovementConfig& MovementConfig,
           const FCriticalWoundConfig& WoundConfig = FCriticalWoundConfig(),
           float ColdMultiplier = 1.f);
   }
   ```

---

## 3. Implementation Steps

1. **PanicRules Header & Implementation:**
   - Include `Characters/OperativeMovementRules.h` and `Characters/CriticalWoundRules.h` in `PanicRules.h`.
   - Declare `PanicFleeSpeed`.
   - Implement `PanicFleeSpeed` in `PanicRules.cpp`.

2. **Unit Tests in `PanicRulesTest.cpp`:**
   - Implement automation test `FPanicFleeSpeedRulesTest` in `CodexTactics.Panic.FleeSpeed`:
     - Prone wounded <= crawl speed (`40.04 <= 61.6`).
     - Crouch <= crouch speed (`81.25 <= 125`, `125 <= 125`).
     - Standing healthy == run speed (`725 == RunSpeed`).
     - Badly wounded standing <= wounded run (`99 <= 471.25`).
     - Cold scaling test (`ColdMultiplier = 0.5f`).

3. **PanicComponent Integration:**
   - In `TriggerPanic`: Remove `Operative->SetStance(EOperativeStance::Standing);`. Keep current stance.
   - In `TransitionToCowering`: Only transition to `Crouching` if `Stance != EOperativeStance::Prone`.
   - In `ProcessPanicState`: Set `Movement->MaxWalkSpeed` using `PanicRules::PanicFleeSpeed(Operative->GetStance(), Operative->GetWoundTier(), Operative->MovementConfig, CriticalWoundRules::GetWoundConfig(), Operative->GetColdSpeedMultiplier())`.

4. **PanicSmoke Extension:**
   - In `Source/CodexTacticsDev/Private/Debug/PanicSmokeCommand.cpp`:
     - Add a case (Stage 4 & 5) testing a prone, badly wounded operative panicking:
       - Set Susanin to `EOperativeStance::Prone`.
       - Set health to `30.f` (`EWoundTier::Heavy`).
       - Trigger panic.
       - Verify operative remains prone.
       - Sample velocity during fleeing: verify non-zero motion (`MaxSpeed > 10.f`).
       - Verify measured speed `<= CrawlSpeed * 1.05f` (`CrawlSpeed = OperativeMovementRules::ComputeMaxSpeed(Susanin->MovementConfig, EOperativeStance::Prone, false, Susanin->IsWounded(), false)`).

5. **Validation & Review:**
   - Build: `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1`
   - Tests: `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter CodexTactics.Panic`
   - Smokes: `powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands CodexTactics.PanicSmoke,CodexTactics.InjuredLocomotionSmoke,CodexTactics.ProneCrawlSmoke`
   - Triage: `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`
   - Gate: `powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_gate.ps1 -Task 071 -SonnetReview`
   - Write report: `docs/port/gemini/outbox/071-panic-flee-speed.report.md`
   - Final status chat message.
