# 011 - C++ Bug Review of 2026-11-10 Merges

**Scope:** Inspection of all C++ merges on `main` from 2026-11-10 (`git log --merges --since=2026-11-10 --until=2026-11-11 main`):
- `63922ad6`: `feat/injured-tiers-fullbody`
- `d9f897ee`: `fix/injured-layer-visible`
- `bce31678`: `fix/fast-regroup-on-b`
- `848c4cc9`: `tools/levelgen-props-c3`
- `8a29df13`: `tools/levelgen-props-c2`
- `111cdb17`: `feat/injured-locomotion`
- `5149d1e9`: `tools/levelgen-props-c1`
- `28ea4d26`: `fix/rpg-legs-m16`
- `847eb19b`: `fix/arena-rpg-contact-roof`
- `0d2adfe5`: `feat/tg-pass1-cpp`
- `afabefd5`: `fix/analyzer-null-derefs`
- `079a5759`: `fix/arena-no-outer-strip`

---

## Findings (Ranked by Severity / Danger)

### 1. `ComputeFlow` ignores quiet spell from round start to first contact
- **File:Line:** `Source/CodexTactics/Private/AI/PmcArenaRoundRules.cpp:34`
- **Concrete Failing State:** In `PmcArenaRoundRules::ComputeFlow(ContactTimes, DurationS)`, `Longest` is initialized to `FMath::Max(0.f, Duration - ContactTimes.Last())`. The loop then only checks gaps between successive events: `ContactTimes[Index] - ContactTimes[Index - 1]`. The quiet window from round start `0.0f` to the first contact `ContactTimes[0]` is completely ignored. For example, if a 120s round has contacts at `[80.0, 85.0]`, the 80-second quiet spell at the start is missed, and `LongestQuietS` erroneously reports `35.0s` (`120 - 85`). In `FPmcArenaContactFlowTest`, tests only passed because test fixtures used contact times where the tail `Duration - Last()` happened to exceed the head `ContactTimes[0]`.
- **Fix Idea:** Initialize `Longest` with `FMath::Max(ContactTimes[0], Duration - ContactTimes.Last())`.
- **Confidence:** 99% (Definite logic defect)

### 2. Unchecked `GetWorld()` dereference in `UpdateFastRegroup`
- **File:Line:** `Source/CodexTactics/Private/Characters/SquadSubsystemRegroup.cpp:122`
- **Concrete Failing State:** `const double Now = GetWorld()->GetTimeSeconds();` dereferences `GetWorld()` directly without a null check. In contrast, `ArmFastRegroup` (line 58: `GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0`) and `IsRegroupCombat` (line 84) guard `GetWorld()`. In automated unit tests or during world transitions/teardown where a subsystem may tick without an active world, this causes an immediate null pointer crash.
- **Fix Idea:** `const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;`
- **Confidence:** 95% (High)

### 3. Premature fire hold metric increment in `HoldShotForInjuredRaise`
- **File:Line:** `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp:804` and `926`
- **Concrete Failing State:** `HoldShotForInjuredRaise()` is called at line 804 prior to evaluating other early fire refusals (such as `bCrawlHold` line 807, sniper weapon move hold line 824, or aim cone alignment). Inside `HoldShotForInjuredRaise()`, `++InjuredRaiseHolds` is incremented and `LastInjuredRaiseHoldTime` is set. When an operative is crawling, line 814 immediately returns `TEXT("crawling: fire held until he stops")`, but `InjuredRaiseHolds` was already incremented as though the shot was held by the injured raise. This corrupts telemetry and smoke test assertions.
- **Fix Idea:** Move `HoldShotForInjuredRaise()` invocation after crawl, sniper, and aim checks (immediately before line 892 `if (bRaiseHold)`).
- **Confidence:** 90% (High)

### 4. Invalid state transition: Critical wound stance sync to leader on B regroup
- **File:Line:** `Source/CodexTactics/Private/Characters/SquadSubsystem.cpp:1086-1091`
- **Concrete Failing State:** When `ExitSoloMode` is triggered (pressing B):
  ```cpp
  const bool bFastRegroup = ArmFastRegroup(*Operative, *LeaderRef);
  const FFastRegroupState* Regroup = bFastRegroup ? FastRegroups.Find(Operative) : nullptr;
  Operative->SetStance(Regroup && Regroup->bStancePending ? EOperativeStance::Standing : LeaderRef->GetStance());
  Operative->MarkStanceFromSquad();
  ```
  If `Operative` is critically wounded, `ArmFastRegroup` returns `false` (`Regroup == nullptr`). Then line 1088 forces `Operative->SetStance(LeaderRef->GetStance())`. If the leader is standing, `Operative->SetStance(Standing)` is rejected by `CriticalWoundRules::AllowsStance`, producing verbose warning logs. More importantly, line 1091 unconditionally executes `Operative->MarkStanceFromSquad()`, stamping `SquadStanceSerial = StanceSerial` as if the squad stance was successfully applied.
- **Fix Idea:** Guard the stance change with `if (!Operative->IsCriticallyWounded()) { Operative->SetStance(...); Operative->MarkStanceFromSquad(); }`.
- **Confidence:** 85% (High)

### 5. Doorway route turn detection misses 90-degree corner turns
- **File:Line:** `Source/CodexTactics/Private/LevelGen/PropsLayoutView.cpp:892-895`
- **Concrete Failing State:** In `FPropsLayoutView::BuildDoorways()`:
  ```cpp
  const int64 Dot = (static_cast<int64>(V.X) - P.X) * (static_cast<int64>(Q.X) - V.X)
      + (static_cast<int64>(V.Y) - P.Y) * (static_cast<int64>(Q.Y) - V.Y);
  bTurn = Dot < 0;
  ```
  For a mandatory route taking a 90-degree corner in front of a doorway opening, the incoming vector `(V - P)` and outgoing vector `(Q - V)` are perpendicular, yielding `Dot == 0`. Because the condition requires `Dot < 0`, 90-degree turns are evaluated as `bTurn = false`. Only acute turn-around reversals (> 90 degrees) are counted, leaving 90-degree doorway turns unflagged in `bRouteTurnsAt`.
- **Fix Idea:** Change condition to `bTurn = Dot <= 0;` (or check direction change angle).
- **Confidence:** 85% (Medium-High)

### 6. Unchecked `Member->GetCapsuleComponent()` in HUD operative labels
- **File:Line:** `Source/CodexTactics/Private/UI/CodexTacticsHUD.cpp:1593`
- **Concrete Failing State:**
  ```cpp
  for (const AOperativeCharacter* Member : Squad->GetMembers())
  {
      const float Top = Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 40.f;
  ```
  While `Squad->GetMembers()` checks `Member.IsValid()`, `Member->GetCapsuleComponent()` is dereferenced without a null check. During character destruction, despawning, or in specialized actor mock tests where the capsule component is missing, `DrawOperativeLabels()` causes a fatal crash.
- **Fix Idea:** Check `if (const UCapsuleComponent* Capsule = Member->GetCapsuleComponent()) { ... }`.
- **Confidence:** 85% (Medium-High)

### 7. Inconsistent sprint speed cap for crouching/prone wounded operatives
- **File:Line:** `Source/CodexTactics/Private/Characters/OperativeCharacterFall.cpp:34`
- **Concrete Failing State:**
  ```cpp
  float AOperativeCharacter::GetSprintSpeedCap() const
  {
      return OperativeMovementRules::ComputeMaxSpeed(MovementConfig, EOperativeStance::Standing, CanSprint(), IsWounded(), bCarrying) * ColdSpeedMultiplier
          * (Stance == EOperativeStance::Standing ? GetWoundTierSpeedScale() : 1.f);
  }
  ```
  `GetSprintSpeedCap()` computes the potential standing sprint speed cap. However, the wound tier speed scale is conditioned on the operative's *current* stance `(Stance == EOperativeStance::Standing)`. If a heavily wounded operative is currently crouched or prone, `GetSprintSpeedCap()` returns the unscaled speed (`x1.0` instead of `x0.45/0.65`), giving an inaccurate sprint speed cap to callers.
- **Fix Idea:** Remove the stance condition: `* GetWoundTierSpeedScale();` (or pass `EOperativeStance::Standing` to evaluate the tier scale).
- **Confidence:** 85% (Medium-High)

### 8. Play area `SampleBounds` silently discards non-intersecting include bounds
- **File:Line:** `Source/CodexTactics/Private/AI/PmcArenaPlayArea.cpp:89-92`
- **Concrete Failing State:** In `PmcArenaPlayAreaRules::SampleBounds(Area, NavBounds)`:
  ```cpp
  if (MinX > MaxX || MinY > MaxY)
  {
      return NavBounds;
  }
  ```
  If a map's `include_box_cm` does not overlap with `NavBounds` at all (e.g. invalid bounds or coordinate mismatch), the function falls back to returning the full `NavBounds`. This completely ignores the restriction without error or warning, causing arena units to spawn and wander across the entire unclipped level.
- **Fix Idea:** Log a warning and return an empty/invalid box (`FBox(ForceInit)`) so callers can detect the invalid setup.
- **Confidence:** 80% (Medium)

### 9. Environment `override_carry` cannot set seed to 0
- **File:Line:** `Source/CodexTactics/Private/Environment/EnvironmentSubsystem.cpp:196`
- **Concrete Failing State:** In `UEnvironmentSubsystem::ResolveInitialState`:
  ```cpp
  if (Level.bOverrideCarry)
  {
      ...
      if (Level.Seed != 0)
      {
          State.RandomSeed = Level.Seed;
      }
  ```
  Because `Level.Seed` defaults to 0 and lacks a presence flag, setting `"seed": 0` explicitly in a level JSON with `"override_carry": true` will be ignored, retaining the carryover seed from the previous level.
- **Fix Idea:** Add `bool bHasSeed` to `FEnvironmentLevelSettings` or check JSON key presence.
- **Confidence:** 80% (Medium)

### 10. `HudLabelRulesTest` lacks test case for empty status formatting
- **File:Line:** `Source/CodexTacticsTests/Private/UI/HudLabelRulesTest.cpp:115`
- **Concrete Failing State:** `HudLabelRulesTest` validates enemy plate composition only with non-empty status strings (`TEXT("BURNING")`), leaving the empty status branch untested in automated test passes.
- **Fix Idea:** Add test verification with empty status string: `TestEqual(TEXT("empty status"), ComposeEnemyLabel(Config, TEXT("Brute"), TEXT(""), 100, 100), ...);`.
- **Confidence:** 75% (Medium)
