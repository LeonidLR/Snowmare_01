# Implementation Map #4 — Wall-to-Wall Cover Dash (Group Order)

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Target:** Task #4 (`pl-wall-cross`) — Переход от стены к стене под прикрытием (Bounding Overwatch)  
**Target Assignee:** Codex / Claude  
**Governing Documents:** `docs/systems/squad-and-orders.md`, `docs/systems/cover-and-peek.md`, `docs/port/TASK_BOARD.md` §4

---

## 1. Tactical Concept & Operational Goal

When the squad is positioned along Wall A and the commander orders them to take cover along Wall B across an exposed open corridor (street, alley, or courtyard), charging across all at once ("human wave") is fatal against prepared enemy fire.

**Task #4** implements coordinated **bounding overwatch (cover dash)**:
1. **Foothold Sprint (Bound 1):** The point operative breaks cover and sprints to the designated primary slot on Wall B.
2. **Covering Overwatch:** The remaining 2 operatives remain snug against Wall A in high readiness, aiming into the open crossing zone to suppress or engage any hostiles that reveal themselves.
3. **Subsequent Bounds (Bounds 2 & 3):** Once the point operative reaches Wall B and turns to cover the street, the second operative dashes across, followed in staggered sequence by the rear operative.

---

## 2. Existing Classes & Functions to Extend vs. New Code

### 2.1. Reusable Engine Components (~80% Reuse)

| Component | File & Line | Reusable Function / Capability |
| :--- | :--- | :--- |
| **Group Cover Dispatch** | `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerCover.cpp:897-967` | `ACodexTacticsPlayerController::OrderGroupCover(Direct, Slot, bPlan, Radius)` handles slot allocation and dispatch. |
| **Wall Stacking & Layouts** | `Source/CodexTactics/Private/Characters/SquadSubsystemGroupCover.cpp:123-203` | `USquadSubsystem::OrderGroupCoverWait` and `OrderGroupCoverProne` manage waiting for squad mates to settle. |
| **Wall Leadership Rules** | `Source/CodexTactics/Private/Formation/FormationWallLeadershipRules.cpp:1-120` | `FormationWallLeadership::PlanGroupCornerPeek` determines slot assignment priority. |
| **Cover Entry & Corner Aim** | `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp:416-465` | `AOperativeCharacter::OrderTakeCoverForPeek` snaps operatives into cover slots. |
| **Run-to-Cover Mechanics** | `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerRunPeek.cpp:454-647` | Path planning and corner peek transitions during movement. |
| **Suppression Fire** | `Source/CodexTactics/Public/Combat/SuppressionSubsystem.h` | `USuppressionSubsystem::ApplyAreaSuppression` provides overwatch suppression cone. |

### 2.2. New Code Required (~20%)

1. **`CoverDashRules` (Pure State & Trajectory Logic):**
   - Files: `Source/CodexTactics/Public/Tactics/CoverDashRules.h`, `Private/Tactics/CoverDashRules.cpp`.
   - Evaluates whether Wall A and Wall B form a valid dash pair across open terrain ($\text{Distance} \in [4\text{ m}, 35\text{ m}]$).
   - Computes sequential bounding order $[O_1, O_2, O_3]$ and timing offsets.
2. **`ACoverDashGhostActor` (Visualization):**
   - Translucent arrow ribbons connecting Wall A slots to Wall B slots with numbered sequence badges (1, 2, 3).
3. **Controller Seam:**
   - Detects when cursor targets a new wall while squad is in `bInCover` on an existing wall.

---

## 3. Data Keys & Tuning

Configured in `Content/Data/Balance/tactical_balance.json` under `"cover_dash"`:

```json
"cover_dash": {
  "min_crossing_distance_cm": 400.0,
  "max_crossing_distance_cm": 3500.0,
  "bound_stagger_delay_s": 0.75,
  "overwatch_aim_lead_cm": 200.0,
  "sprint_speed_bonus": 1.25,
  "suppression_coverage_range_cm": 6000.0,
  "abort_on_heavy_wound": true
}
```

---

## 4. Insertion Points in Existing Code

1. **Controller Click Intercept:**
   - In `ACodexTacticsPlayerControllerCover.cpp::UpdateCoverHover()`:
     ```cpp
     if (CurrentSquadState == ESubsystemCoverState::AllInCover && TargetWall != CurrentWall)
     {
         ShowCoverDashPreview(CurrentWall, TargetWall);
     }
     ```
2. **Order Dispatch:**
   - In `CodexTacticsPlayerControllerOrders.cpp::HandleWorldHit()`:
     - Dispatches `SquadSubsystem->OrderCoverDash(TargetSlot)`.
3. **Squad Subsystem Bounding State Machine:**
   - In `SquadSubsystemGroupCover.cpp`:
     - Sets Man 1 to `OrderMoveTo(Slot1, bSprint = true)`.
     - Sets Men 2 & 3 to `OrderGroupCoverWait(Slot, Man1, bOverwatchAim = true)`.
     - When Man 1 arrives, triggers Man 2, then Man 3.

---

## 5. Automated Verification & Smokes

* **Unit Test:** `Source/CodexTacticsTests/Private/Tactics/CoverDashRulesTest.cpp`
  - Validates bounding sequence calculation and distance limits.
* **Smoke Command:** `CoverDashSmokeCommand` (`Codex.Test.CoverDash`)
  - Stage 0: Sets up squad in cover at building wall in `L_FormationLabCombat`.
  - Stage 1: Issues cover dash to opposite wall 16 m away.
  - Stage 2: Asserts Man 1 sprints while Men 2 & 3 hold aim at street.
  - Stage 3: Asserts Man 1 arrives and enters cover; Man 2 starts sprint.
  - Stage 4: Asserts all 3 operatives successfully settled in new cover slots.

---

## 6. Open Design Questions (with Recommended Defaults)

1. **Вопрос 1: Способ вызова приказа перебежки (UX Trigger)**
   - *Варианты:* Двойной клик по новому укрытию, отдельная кнопка на панели, или стандартный двухкликовый выбор?
   - *Рекомендуемый дефолт (Recommended):* **Стандартный двухкликовый выбор**. Если отряд уже находится у стены, первый клик на противоположную стену рисует стрелки перебежки (1-2-3); второй клик в ту же точку подтверждает рывок.
2. **Вопрос 2: Определение очередности перебежки бойцов (Bounding Order)**
   - *Варианты:* Строго по роли (Командир первым) или по геометрии (ближайший к целевому слоту)?
   - *Рекомендуемый дефолт (Recommended):* **По кратчайшей траектории**. Боец, чей целевой слот на стене B ближе всего к его текущей позиции, бежит первым (минимизация времени на открытом пространстве).
3. **Вопрос 3: Реакция на шквальный огонь во время рывка (Abort on Suppression)**
   - *Варианты:* Бежать до конца несмотря на потери, или отменять рывок остальных?
   - *Рекомендуемый дефолт (Recommended):* **Отмена последующих перебежек при критическом ранении первого бойца**. Если боец №1 падает без сознания (Knockdown/Wounded) посреди улицы, оставшиеся бойцы остаются в укрытии на стене A и открывают подавляющий огонь.
