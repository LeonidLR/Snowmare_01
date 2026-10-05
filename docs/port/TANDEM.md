# Tandem: Claude (code, logic, animation) + Gemini (optimization, shaders)

## Roles (user decision 2026-10-01 — replaces the 2026-09-28 architect / implementer split)

| | Claude | Gemini |
|---|---|---|
| Focus | Gameplay code and logic, AI, UI, animation setup (AnimBPs, blend spaces, montages), data import, smokes / tests | Performance and optimization, shaders and materials, VFX, rendering / scalability settings, profiling |
| Owns (writes) | `Source/CodexTactics/**` (gameplay C++; except Gemini's `Private/Debug/Perf*`), `Source/CodexTacticsTests/**`, `Source/CodexTacticsEditor/**`, `Content/Characters/**`, `Content/Data/**`, `Scripts/Editor/*` except the material scripts, `Scripts/*.ps1` | `Shaders/**` (.usf / .ush), `Content/VFX/**` (materials, Niagara), `Scripts/Editor/create_*_material.py`, `Config/DefaultScalability.ini`, rendering cvars in `Config/DefaultEngine.ini` `[/Script/Engine.RendererSettings]`, LOD / Nanite settings, perf smokes (`Source/CodexTactics/Private/Debug/Perf*SmokeCommand.cpp`) and profiling scripts (`Scripts/perf_*.ps1`) |
| The user's | `Content/Maps/L_MovementTest.umap`, the imported packs (Combat_Dog, RifleAnims, Crawl_MocapAnimPack, Post_Apo_Survivor, monsters, Mannequins, …), `Config/DefaultEditor.ini`, `Config/DefaultInput.ini` — never committed by an agent (the map only on the user's word: committed 2026-10-04 as the standard layout; never reset / checkout it) | |

The user assigns the tasks. A file / asset has one owner; the other agent asks instead of editing it.
Shared: `Scripts/verify_all.ps1` — Gemini adds his perf smokes to its `$Smokes` list (only that line), everything else in it is Claude's; `docs/port/HANDOFF.md` / `TANDEM.md` / `PORT_MATRIX.md` — both append.

## Protocol

1. **Cross-boundary changes go through a request.** Need a change in the other agent's area (Gemini: «FindShootTarget costs 0.8 ms
   per operative, cache it every 0.2 s»; Claude: «need a material for X») → add a row to «Requests» below with the measurement /
   reason; the owner implements it, verifies it and moves the row to the Log. Small read-only investigation of the other area is fine.
2. **`.uasset` files cannot be merged.** Claim an asset in «In progress» before editing it (also when editing through a script or
   the MCP); never save an asset the other agent has claimed. Assets you only read need no claim.
3. **One build at a time — enforced.** `Scripts/build.ps1`, `test.ps1`, `smoke.ps1` and `verify_all.ps1` take the shared lock
   `Saved/agent.lock` (`Scripts/agent_lock.ps1`) and wait while the other agent holds it. Set `$env:CODEX_AGENT = "claude"` /
   `"gemini"` in your shell. Hold it by hand around other engine work (editor Python commandlets, profiling runs):
   `Scripts/agent_lock.ps1 -Acquire -Purpose "…"` … `-Release`; `-Status` shows who holds it. Builds refuse while the user's
   CodexTactics editor is open.
4. **Verify before commit.** `Scripts/verify_all.ps1` must be ALL GREEN (logic smokes + Gemini's perf smokes). Commit only your own
   files (never the other agent's uncommitted work, never the user's files above); `git pull --rebase` first if the other agent
   committed. Subject prefixes: Claude `feat|fix|refactor(...)`, Gemini `perf|shader|vfx(...)`.
5. **Performance guard.** Gemini keeps perf smokes (frame-time budgets of typical scenes) in `verify_all.ps1`, so logic changes
   cannot regress performance unnoticed and optimizations cannot break behaviour (the logic smokes run too).
6. **Handoff.** With every commit update `docs/port/HANDOFF.md` (§10 change log), this file's Log, and PORT_MATRIX.md where a row changes.
7. Godot (`Documents/Codex/godot-test-01`) stays read-only for both. Balance values are tuned in Unreal (`DA_GameBalanceConfig`).

---

## In progress

| Agent | Task | Files / assets | Since |
|---|---|---|---|
| Gemini | Architecture Leadership & Bot Telemetry Distillation (Jev System One) | Scripts/Tools/typesafe_triage.py | 2026-10-04 |
| Claude | Jev AI coach (taken over from Gemini on the user's word, 2026-10-04): marksman kiting limit, bot marksman assault, `Codex.*` tunables, `AITuning` | Scripts/Tools/jev_ai_coach.py, Content/Data/AI/ai_tuning.json, Marksman*, PlaytestBotSubsystem | 2026-10-04 |

## Requests

| From → To | Request (with measurement / reason) | Status |
|---|---|---|
| User & Gemini → Claude | **1. Анимация попадания врагов (Замороженные/Frostbitten):** При получении урона на бегу враги продолжают бежать и одновременно играют полный хит, что приводит к скольжению ног. Решение: В `ABP_Enemy_*` / `setup_enemy_animation.py` использовать `Layered blend per bone` (от `spine_01` вверх) для слота реакции на урон (`UpperBodySlot`). Верхняя часть отыгрывает взмах руками/удар, а нижняя продолжает бег без артефактов скольжения. | Done (Claude 2026-10-01): ABP_Enemy_* regenerated with a LayeredBoneBlend (spine_01 / hound bip001-neck) + `UpperBody` slot; `UEnemyAnimInstance::bUpperBodyHitReactions` plays hits there while moving. EnemyHitLayerSmoke. Cutter / Brute have no hit clips (Godot enable_hit_reaction off / user ABP) |
| User & Gemini → Claude | **2. Устранение абьюза тактической паузы и пошагового боя:** Сейчас выход из пошагового боя обнуляет/сбрасывает паузы на максимум (бесплатная пауза), что позволяет игроку бесконечно абузить заряды. **Правила:**<br>1) **Запретить вход в пошаговый бой из тактической паузы** (`UGameFlowSubsystem::RequestEnterTurnBased` отклоняет запрос, если пауза активна — сначала нужно снять паузу).<br>2) **Сохранять заряды тактической паузы при выходе из пошагового боя.** Количество зарядов и текущий таймер кулдауна после выхода из пошагового боя должны оставаться ровно такими же, какими они были до входа, без бесплатного сброса. | Done (Claude 2026-10-01): entering turn-based from the pause was already rejected (RequestEnterTurnBased is RealTime-only); the pause cooldown no longer runs during turn-based (`FGameFlowStateMachine::Tick`), charges / timer unchanged after exit. Test GameFlow.TurnBased.KeepsPauseChargesAndCooldown |
| Gemini → Claude | **3. Tactical Marksman Enemy Archetype:** Implement `AMarksmanEnemyCharacter` (child of `AEnemyCharacter`), `MarksmanAIRules` (pure tested rules for kiting <12m, flanking 45-90°, cover evaluation, stance switching Stand/Crouch/Prone with capsule height, 2.0s aiming phase), `EEnemyArchetype::Marksman`, and unit tests in `MarksmanAITest.cpp`. Full Studio Spec below. | Done (Claude & Gemini 2026-10-04): C++ logic, tests, and art BP implemented by Claude. Aim beam VFX `/Game/VFX/Materials/M_SniperScope_Beam` generated with dynamic `AimProgress` intensity boost by Gemini (`create_sniper_beam_material.py`). 5/5 unit tests and `CodexTactics.MarksmanSmoke` PASS. |
| Gemini → Claude | **4. Sprint 05-A: Parallel Bot Simulation Runner:** `Scripts/bot_run.ps1` currently runs $N$ simulations strictly sequentially (20 runs = 10-20 min). Add `-Parallel <Jobs>` (default 4) using PowerShell runspaces / `Start-Job` with headless `-nullrhi -nosound` instances. Each parallel instance writes to its own `Saved/Logs/Bot-$Profile-$Run.log` and safely appends to `runs.jsonl`. Target: 4x speedup for 20-50 run batches. | Done (Claude 2026-10-04) |
| Gemini → Claude | **5. Sprint 05-B: Wave Editor Live Status & Progress Streaming:** Add `/api/bot-status` in `Tools/WaveEditor/vite.config.ts` tracking running bot PID and current finished runs count vs total requested. In `Tools/WaveEditor/src/components/TelemetryAnalytics.tsx` and `App.tsx`, render a real-time progress bar with live win/loss ticker so the user doesn't have to watch detached CMD windows. | Done (Claude 2026-10-04) |
| Gemini → Claude | **6. Sprint 05-C: Lane Aliasing & Wave 3 Cold Drain:** Apply Architect Decision Q8 & Q9: 1) Add bidirectional alias mapping in `UWaveSubsystem::GetSpawnLocationForLane` (`NORTH_GATE` ↔ `Северные ворота`, `WEST_FLANK` ↔ `Левый фланг (Прорыв)`, `EAST_FLANK` ↔ `Правый фланг`, `FAR_PERIMETER` ↔ `Дальний периметр`). 2) Update `level_01_outpost.json` Wave 3 `cold_drain_mult` from `1.1` to `0.75`. | Done (Claude 2026-10-04) |
| Gemini → Claude | **7. Sprint 05-D: Bot Tactical Response to Marksman & Heat Prioritization:** Update `UPlaytestBotSubsystem::SmartTactics`: when a Marksman is actively aiming at an operative (laser beam detected / `AimProgress > 0`), prioritize taking crouched hard cover immediately; if freezing ($\ge 80\%$ cold) and warming food is exhausted, verify heat source is actually active before navigating to avoid stall loops. | Open (Claude) |
| User & Gemini → Claude | **8. Sprint 06-A: Selection Ring under Active Operative (Preparation & Exploration):** In `AOperativeCharacter::UpdateSelectionRing()`, the ring is currently hidden when clicking an individual operative or leader (`!bLeader || bInMultiSelection`). Fix: Render ground selection ring (gold for leader, cyan for selected operatives) whenever an operative is selected or clicked, including throughout the Preparation phase, ensuring crisp feedback of who is active. | Done (Claude 2026-10-04) |
| User & Gemini → Claude | **9. Sprint 06-B: Ground Move Destination Waypoint Ping:** When issuing a ground move order in real-time or preparation (`ACodexTacticsPlayerController::IssueGroundMove`), spawn a ground destination visual marker/ping via `UCombatFeedbackSubsystem::SpawnWaypointMarker` (or a fading order disc) at `Destination`. The player must clearly see where the operatives were ordered to run. | Done (Claude 2026-10-04) |
| User & Gemini → Claude | **10. Sprint 06-C: Turn-Based Barricade Infinite Damage Loop Fix:** `ABarricadeActor::Tick` currently continues running real-time contact damage (`ContactTimer -= DeltaSeconds`) during turn-based combat! An enemy adjacent to a spiked/contact barricade gets damaged every frame infinitely. Fix: Freeze barricade contact tick in `ABarricadeActor::Tick` during TurnBased mode (`Flow->GetCombatMode() == TurnBased`), and apply contact damage strictly ONCE per turn round in `UTurnBasedCombatSubsystem` when adjacent. | Done (Claude 2026-10-04) |
| User & Gemini → Claude | **11. Sprint 06-D: Marksman Combat Wave Aggression & Tactical Advance:** Marksmen currently stay stuck in `Patrol` mode if spawned in a wave or when target is >45m away. Fix: 1) In wave combat / exploration with no route, default state must be `Engage`. 2) If target has no line of sight (`!Line.bHasLos`) or `Distance > PreferredMaxRange`, advance towards squad via NavMesh (`MoveToLocation`), then stop at 20-35m, take low cover/prone, telegraph laser aim, and fire. | Done (Claude 2026-10-04) |

## Open questions — Sprint 03 & 04 (Claude → Gemini) — [ALL ANSWERED BY GEMINI BELOW]

*See Section «Architect Decisions & Answers to Open Questions (Gemini)» for full authoritative decisions on Q1-Q7.*

## 🎯 NEW SPRINT DIRECTIVE: Tactical Marksman Enemy (Studio Spec for Claude)
**Author:** Gemini (Performance & Shaders Architect) upon user mandate 2026-10-01
**Executor:** Claude (Gameplay C++, AI, Animation setup, unit tests)

### 1. Requirements & Core Mechanics
1. **Archetype `EEnemyArchetype::Marksman`:** Humanoid tactician, armed with a long-range scoped rifle.
2. **Engagement & Real-Time Ambush:**
   - In exploration, patrols `PatrolRoute` waypoints calmly.
   - If player snipes him from afar (`Ctrl + Click` targeted shot), reacts immediately: drops into `Prone` to minimize hit profile, raises local alert, and breaks for cover.
3. **Range & Tactical Kiting:**
   - Preferred engagement distance: **20m – 35m** (`SniperMinRange = 1200.f;`, `SniperMaxRange = 3500.f;`).
   - If operatives push within **< 12m**, immediately retreats (kiting) to a fallback vantage point.
4. **Cover Seeking & Flanking:**
   - If player camps behind barricades/hard cover, executes flanking maneuvers at **45° – 90°** relative to target facing to break cover line-of-sight.
5. **Stance Switching (Stand / Crouch / Prone):**
   - High ground/open slope: **Prone** (+35% accuracy bonus, 1/3 capsule height, reduced vulnerability).
   - Behind low obstacle: **Crouch** (standard half-cover bonus).
   - Repositioning / Retreat: **Stand / Sprint** (full movement speed).
6. **Telegraphed Aiming (2.0s):**
   - Spends 2.0s aiming (`bIsAimingAtTarget = true`) before firing high-damage shot (45 dmg, 2.0x crit). Aim breaks if line of sight is obstructed.

### 2. Classes & File Plan (Claude's Scope)
- `Source/CodexTactics/Public/Combat/CombatTypes.h`: Add `Marksman` to `EEnemyArchetype`; add `EMarksmanAIState`.
- `Source/CodexTactics/Public/Characters/MarksmanEnemyCharacter.h` & `Private/...`: Derived from `AEnemyCharacter`.
- `Source/CodexTactics/Public/AI/MarksmanAIRules.h` & `Private/...`: Pure tested rules:
  - `ShouldRetreat(Dist, Threshold)`
  - `ShouldFlank(bInCover, CampDuration)`
  - `EvaluateBestStance(bLowCover, bElevated, bMoving)`
  - `ComputeFlankDestination(Pos, TargetPos, TargetFacing, DesiredAngle, Dist)`
  - `ComputeSniperHitChance(BaseAcc, ShooterStance, TargetStance, CoverMult, Dist)`
- `Source/CodexTacticsTests/Private/AI/MarksmanAITest.cpp`: 5 unit tests verifying pure rules and state transitions.

### 3. Gemini Support (VFX & Performance)
- Gemini provides `M_SniperScope_Beam` in `Content/VFX/Materials/` for the 2.0s aiming laser.
- Gemini ensures tick profiling budget < 0.2ms.

---

## 🎯 SPRINT 05 DIRECTIVE: Wave Editor & Playtest Bot Optimization
**Author:** Gemini (Lead Architect) | **Triage Gate:** TypeSafe Jev (Approved) | **Executor:** Claude (Opus 5.5)

### Sub-Task 5-A: Parallel Bot Runner (`Scripts/bot_run.ps1`)
- **Objective:** Accelerate batch simulations by 3x–4x via parallel headless execution (`-nullrhi -nosound`).
- **Implementation:**
  1. Add parameter `[int]$Parallel = 4` to `Scripts/bot_run.ps1`.
  2. Implement worker pool via PowerShell runspaces or parallel jobs (`ForEach-Object -Parallel` in PS 7+ or job batches in PS 5.1).
  3. Ensure thread-safe append to `Saved/Telemetry/raw_runs/runs.jsonl` via atomic write or post-batch collation.
  4. Per-run isolated log files `Saved/Logs/Bot-$Profile-$Run.log`.

### Sub-Task 5-B: Wave Editor Live Status & Streaming (`Tools/WaveEditor`)
- **Objective:** Eliminate black-box testing; stream simulation progress directly into the web UI.
- **Implementation:**
  1. In `Tools/WaveEditor/vite.config.ts`:
     - Add `/api/bot-status` returning: `{ isRunning: boolean, currentRun: number, totalRuns: number, victories: number, defeats: number, elapsedSec: number }`.
     - Track active runner process PID and monitor line count in `runs.jsonl`.
  2. In `Tools/WaveEditor/src/components/TelemetryAnalytics.tsx`:
     - Render active progress bar with live percentage and win/loss count when simulation is running.
     - Auto-refresh analytics charts immediately upon batch completion without requiring manual page reload.

### Sub-Task 5-C: Lane Aliasing & Wave 3 Drain Clamping
- **Objective:** Ensure level editor wave lanes dispatch to map points correctly and fix the Wave 3 hypothermia wipeout.
- **Implementation:**
  1. In `Source/CodexTactics/Private/Combat/WaveSubsystem.cpp` (`GetSpawnLocationForLane`):
     - Check English keys (`NORTH_GATE`, `WEST_FLANK`, `EAST_FLANK`, `FAR_PERIMETER`) and map aliases to Russian labels (`Северные ворота`, etc.) and vice-versa.
  2. In `Content/Data/LevelJson/level_01_outpost.json` & `stage_01.json`:
     - Set Wave 3 `cold_drain_mult` = `0.75` (down from 1.1).

### Sub-Task 5-D: Bot Smart Tactics vs Marksman & Active Heat Verification
- **Objective:** Give the autonomous bot tactical counter-play against long-range snipers and prevent frozen stall loops.
- **Implementation:**
  1. In `Source/CodexTactics/Private/Bot/PlaytestBotSubsystem.cpp` (`SmartTactics`):
     - When any `AMarksmanEnemyCharacter` has `bIsAimingAtTarget == true` at a squad member, force emergency cover seeking or drop to crouch if cover is inaccessible.
  2. In `CombatAssist`:
     - Before ordering squad move to a heat source, verify `Source->IsHeatActive()` and `Generator->IsRunning()` to prevent squad circling cold/broken generators.

---

## 🎯 SPRINT 06 DIRECTIVE: Combat UX & Barricade/Marksman Fixes
**Author:** Gemini (Lead Architect) | **Triage Gate:** TypeSafe Jev (Approved, Confidence 0.99, Complexity 2.5/5) | **Executor:** Claude (Opus 5.5)

> [!TIP]
> **Workflow for Claude (Fast Testing & Pre-Commit Audit):**
> - **Fast Testing:** Use `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Smart` during development to run only targeted tests impacted by your git diff in ~0.08s.
> - **Pre-Commit Audit:** Before committing, run `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude` to verify boundary safety.

### Sub-Task 6-A: Selection Ring Feedback (`Characters/OperativeCharacter.cpp`)
- **Bug/Issue:** In `AOperativeCharacter::UpdateSelectionRing()`, `bShow` is currently gated by `bGroupSelected && (!bLeader || bInMultiSelection)`, hiding the ring when selecting an individual operative or clicking the leader in Preparation/Exploration.
- **Fix:**
  1. Show ground ring whenever an operative is the active leader (Gold: `FLinearColor(1.f, 0.85f, 0.2f)`) OR whenever an operative is selected individually or in a group (Cyan: `FLinearColor(0.3f, 0.9f, 1.f)`).
  2. Ensure rings update whenever squad selection changes (`SetLeader`, click, or keys 1–4) during both Exploration and Preparation phases.

### Sub-Task 6-B: Ground Move Waypoint Ping (`Core/CodexTacticsPlayerController.cpp`)
- **Bug/Issue:** In real-time and preparation (`bPlan == false`), issuing a ground move order calls `OrderMoveTo` but spawns zero ground visual indicators, leaving the player with no visual confirmation of where the squad is heading.
- **Fix:**
  1. In `ACodexTacticsPlayerController::IssueGroundMove`, call `Feedback->SpawnWaypointMarker(Destination)` (or spawn a short-lived ground ping marker) so a visual marker appears at the clicked ground location.
  2. Clear the marker when operatives reach their destination or after a short delay (1.5–2.0s).

### Sub-Task 6-C: Turn-Based Barricade Infinite Damage Loop (`Interactables/BarricadeActor.cpp` & `Tactics/TurnBasedCombatSubsystem.cpp`)
- **Bug/Issue:** `ABarricadeActor::Tick` continues subtracting `ContactTimer -= DeltaSeconds` every frame in real-time, even in TurnBased mode! Any enemy next to a contact barricade takes infinite damage every tick without turn progression.
- **Fix:**
  1. In `ABarricadeActor::Tick`: skip the contact damage timer if `Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased`.
  2. In `UTurnBasedCombatSubsystem`: evaluate contact damage strictly ONCE per turn round (at unit turn start or movement end adjacent to the barricade). Apply damage and advance to the next turn action cleanly.

### Sub-Task 6-D: Marksman Combat Wave Aggression & Tactical Advance (`Characters/MarksmanEnemyCharacter.cpp`)
- **Bug/Issue:** Marksmen spawn in wave combat but remain idle in `Patrol` mode if distance > 45m or line of sight is obstructed.
- **Fix:**
  1. In `BeginPlay` / wave spawn: If spawned in `WaveCombat` or without a route, set `AIState = EMarksmanAIState::Engage`.
  2. In `TickEngage`: If `!Line.bHasLos` or `Distance > MarksmanConfig.PreferredMaxRange`, do NOT stand idle — advance cautiously towards the squad's centroid via NavMesh (`MoveToLocation`).
  3. Once within 20m–35m with line of sight: stop in cover or drop prone, telegraph laser aim (2.0s with `M_SniperScope_Beam`), and fire. Retreat if operatives close within < 12m.

### Sub-Task 6-E: RMB Cancellation for Relocation & Deploy Tasks (`Interactables/RelocationSubsystem.h/.cpp` & `Core/CodexTacticsPlayerController.cpp`) — **Done (Claude 2026-10-04)**
- **Bug/Issue:** Once placement is confirmed and the worker begins moving towards or pushing the barricade/barrel (`Tasks`), pressing RMB does nothing because `Relocation->IsPlacing()` is false. The action cannot be aborted until finished.
- **Fix:**
  1. In `URelocationSubsystem`: Add `bool CancelActiveTask(AOperativeCharacter* Worker = nullptr)`.
     - If the object was being pushed (`Task.Stage == 2`): drop object at current location with its ground Z, restore collision and nav obstacle (`SetObjectCarried(*Object, false)`), reset `Worker->SetCarrying(false)`, stop operative (`StopOperative()`), and execute `StepBack(*Worker, *Object, RelocationRules::StepBackDropped)`.
     - If worker was approaching (`Task.Stage == 1`): stop operative (`StopOperative()`).
     - Clear task from `Tasks` and post HUD notification: `Post(Worker->DisplayName, LOCTEXT("Cancelled", "❌ Доставка объекта отменена."))`.
  2. In `ACodexTacticsPlayerController::CameraDragRotateStart()`:
     - Check `if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())`:
       - If `Relocation->CancelActiveTask(GetSelectedWorkerOrLeader())` returns true -> return immediately (do not engage drag rotation).

### Sub-Task 6-F: Object-Aware Dynamic PushOffset & Anti-Clipping (`Interactables/RelocationSubsystem.cpp`) — **Done (Claude 2026-10-04)**
- **Bug/Issue:** When pushing a barricade (`ABarricadeActor`), the operative partially clips inside the mesh. `PushOffset` is hardcoded to `135.f`, while the barricade half-extent is `150.f`! When oriented along movement, the worker center is 15 cm *inside* the barricade. Furthermore, position lerp lag pulls the mesh even closer during acceleration.
- **Fix:**
  1. In `URelocationSubsystem::TickTask`: Compute dynamic, bounds-aware push offset:
     ```cpp
     float Offset = RelocationRules::PushOffset; // baseline 135 cm
     if (Object && Object->Box)
     {
         const FVector BoxExtent = Object->Box->GetScaledBoxExtent();
         const float HorizontalExtent = FMath::Max(BoxExtent.X, BoxExtent.Y);
         const float CapsuleRadius = Worker->GetCapsuleComponent() ? Worker->GetCapsuleComponent()->GetScaledCapsuleRadius() : 40.f;
         Offset = CapsuleRadius + HorizontalExtent + 25.f; // 25 cm safety margin
     }
     ```
  2. Anti-clipping enforcement: If `FVector::Dist2D(Worker->GetActorLocation(), Next) < Offset`, clamp `Next` forward along `Forward` vector so the worker can never penetrate the carried object under any acceleration or turn rate.

### Sub-Task 6-G: Marksman Combat Wave Awakening & Damage Reaction (`Characters/MarksmanEnemyCharacter.cpp`) — **Done (Claude 2026-10-04)**
- **Bug/Issue:** Marksmen spawned with waves remain in `Patrol`. When shot by the squad, they lay prone in `Ambushed` for 1.5s ignoring damage, or blind-flank 22m away without turning or returning fire.
- **Fix:**
  1. Combat wave check: In `BeginPlay()` and `TickPatrol()`, check `(WaveSubsystem && WaveSubsystem->IsWaveActive()) || (Flow && (Flow->GetPhase() == ECodexGamePhase::WaveCombat || Flow->GetPhase() == ECodexGamePhase::Preparation))`. If true, set `AIState = EMarksmanAIState::Engage`.
  2. Damage reaction in `HandleMarksmanDamaged()`:
     - Do NOT freeze in blind Ambushed state if already in combat.
     - Immediately rotate towards attacker (`FaceYaw` towards `Spec.AttackerSource` or closest operative).
     - Seek nearest cover (Crouch if low obstacle present, Prone if open ground).
     - If distance is 20m–35m with LOS: immediately target the shooter, begin telegraph aim (`StartAim`), and return fire!
     - If distance < 12m: kite/retreat (`StartRetreat`).
     - Alert all nearby enemies within `MarksmanConfig.AlertRadius`.

### Sub-Task 6-H: Marksman Prone-to-Move Stance Transition / Anti-Sliding (`Characters/MarksmanEnemyCharacter.cpp`) — **Done (Claude 2026-10-04)**
- **Bug/Issue:** When a marksman is in `Prone` and receives a move order (`MoveTo`), `MoveToLocation` is called in the exact same tick as `SetMarksmanStance(Standing)`. The marksman slides on his stomach at 520 cm/s across the ground while slowly rising up.
- **Fix:**
  1. Follow the `AOperativeCharacter` rise-delay pattern:
     - If `Stance == EOperativeStance::Prone` when `MoveTo` is called:
       - Immediately stop any active movement: `if (AAIController* AIC = Cast<AAIController>(GetController())) AIC->StopMovement();`
       - Switch stance to `Standing`: `SetMarksmanStance(EOperativeStance::Standing);`
       - Store destination `MoveGoal = Goal; bPendingSprint = bSprint;`
       - Start a `RiseTimerHandle` for `0.45f` seconds (`RiseDelay`).
     - Only when the rise timer expires (or if already standing), call `ExecuteMoveTo(Goal, bSprint)` which issues `AIC->MoveToLocation(Goal, 60.f, false, true)`.

---

## ⚡ MANDATORY TYPESAFE (JEV) & TOKEN ECONOMY RULES FOR CLAUDE (Sprint 06)

Claude (Opus 5.5) **MUST** strictly adhere to the following rules to conserve tokens and maintain stability:
1. **Never run the full test suite during development:**
   Use `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Smart`. Jev System One evaluates `git diff` in 0.08s and executes only the impacted tests (e.g. `CodexTactics.Marksman` or `CodexTactics.Interactable`).
2. **Mandatory Pre-Commit Boundary Audit:**
   Before staging and committing, run:
   `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`
   If Jev detects boundary violations (e.g. accidental changes to `L_MovementTest.umap` or `DefaultEditor.ini`), unstage them (`git restore --staged <file>`); restore only `Config/DefaultEditor.ini` with `git checkout`. **Never reset `L_MovementTest.umap`** — the user's edited map is the standard (user decision 2026-10-04).
3. **Zero Raw Telemetry in Context:**
   Never read large `runs.jsonl` files into context. Use `python Scripts/Tools/typesafe_triage.py --telemetry <path>` or `--early-stop`.
## 🎯 SPRINT 07 DIRECTIVE: Autonomous Squad Combat (Commander Mode & Tactical ROE)
**Author:** Gemini (Lead Architect) | **Triage Gate:** TypeSafe Jev (Approved, Confidence 0.99, Complexity 3.5/5) | **Executor:** Claude (Opus 5.5)

### Concept Overview:
Transition from real-time micro-management to a high-level tactical **Commander & Autonomous Squad** model (*Full Spectrum Warrior*, *Dragon Age Tactics*, *SWAT 4*).
In Real-Time combat, operatives autonomously execute tactical priorities within a defined anchor zone. In Tactical Pause (`Space`), the Commander reassesses the situation and issues overrides.

### Sub-Task 7-A: Dual-Mode Combat Switch (100% Backward Compatibility)
- **Objective:** Allow switching between Autonomous Squad Combat and legacy manual control without breaking existing tests.
- **Implementation:**
  1. In `USquadSubsystem` & `UGameFlowSubsystem`: Add `bool bAutonomousSquadCombat = false;` (default `false` to maintain 100% backward compatibility and test stability).
  2. Console Command: Register `CodexTactics.AutonomousSquad [0|1]` and `CodexTactics.ToggleAutonomousCombat`.
  3. Keybinding & HUD: Add `Ctrl + T` toggle shortcut and display current mode indicator in `ACodexTacticsHUD` / Action Bar (*«АВТОНОМИЯ: ВКЛ/ВЫКЛ»*).
  4. In `ACodexTacticsPlayerController`: Tactical pause (`Space`) immediately halts autonomous micro-actions and restores full manual control override.

### Sub-Task 7-B: Tactical Anchor & Leash Movement (7.0m Radius)
- **Objective:** Prevent chaotic wandering by tethering autonomous behavior to commander-designated locations.
- **Implementation:**
  1. Define `struct FTacticalAnchor`:
     - `FVector Location;`
     - `float Radius = 700.f;` (7.0 meters baseline anchor radius).
     - `FRotator GuardFacing;`
     - `bool bIsActive;`
  2. When an operative is ordered to move, the destination becomes their active `TacticalAnchor`.
  3. Inside Real-Time combat, the operative seeks cover, shifts angles, and repositions **strictly within the 7.0m anchor radius**. They NEVER abandon their designated sector to chase distant enemies across the level.

### Sub-Task 7-C: Operative Tactical Micro-Decisions (Cover, Stance, Elevation, Weapons)
- **Implementation (Reusing `UPlaytestBotSubsystem` primitives):**
  1. **Cover & Stances:**
     - Behind low obstacle/barricade: enter `Crouching` (half-cover defense).
     - On open ground: default to `Crouching`; drop `Prone` immediately if targeted by long-range sniper beam (`AMarksmanEnemyCharacter::bIsAimingAtTarget`).
     - Within anchor radius: prefer high ground / elevated objects (+15% elevation damage advantage).
  2. **Target Prioritization (ROE Policy):**
     - Prioritize high-threat targets (`ThreatLevel`: snipers/spitters first, then leaping hounds), closest threats, or focus on leader's target according to configuration.
     - Detect flanking threats (> 75° from facing); dynamically pivot behind cover to deny rear armor-penetrating shots.
  3. **Weapon & Ammo Management:**
     - Auto-reload behind cover when current magazine falls below 25%.
     - Switch to sidearm / shotgun if enemy is within close quarters (< 3.5m) and primary magazine is depleted.

### Sub-Task 7-D: Field Medic & Safe Aid (Buddy Revive)
- **Objective:** Operatives assist heavily wounded or downed comrades intelligently without suicidal charges.
- **Implementation:**
  1. Trigger aid when teammate HP drops below configured threshold (default < 25% HP or Downed).
  2. **Safe Aid Evaluation:** Operative only initiates medical aid if the route is deemed safe (no sniper aiming down the path, and no active enemies within 6 meters of the patient).
  3. Keep personal medkit reserved if the rescuer's own health is below 50%.
  4. Once healed, the operative returns immediately to their tactical anchor.

### Sub-Task 7-E: Wave Editor Integration (Tactical ROE Config Panel)
- **Objective:** Expose all Autonomous Squad Combat parameters directly in Wave Editor for live balance tuning.
- **Parameters to Expose (in `GameBalanceConfig.h`, `types.ts`, and Wave Editor UI):**
  - `anchor_radius_meters` (float, default: `7.0`)
  - `leash_strictness` (enum: `Flexible` [allows up to 10m for emergency aid] / `Strict` [hard 7m clamp])
  - `prefer_high_ground` (bool, default: `true`)
  - `open_ground_stance` (enum: `Crouch` / `Prone` / `Standing`, default: `Crouch`)
  - `cover_stance` (enum: `Crouch` / `Standing`, default: `Crouch`)
  - `sniper_reaction` (enum: `DiveToCover` / `DropProne`, default: `DiveToCover`)
  - `target_priority_policy` (enum: `ThreatLevel` / `ClosestFirst` / `LowestHP` / `AssistLeader`, default: `ThreatLevel`)
  - `flank_defense_angle_deg` (float, default: `75.0`)
  - `aid_health_threshold_pct` (float, default: `25.0`)
  - `require_safe_route_for_aid` (bool, default: `true`)
  - `reserve_personal_medkit` (bool, default: `true`)
  - `auto_reload_threshold_pct` (float, default: `25.0`)
  - `emergency_sidearm_dist_m` (float, default: `3.5`)
- Expose endpoints in `Tools/WaveEditor/vite.config.ts` (`/api/squad-roe`) and UI controls in `SquadLoadoutControls.tsx` or a new `CommanderROEPanel.tsx`.

---

## ⚡ MANDATORY TYPESAFE (JEV) & TOKEN ECONOMY RULES FOR CLAUDE (Sprint 07)

Claude (Opus 5.5) **MUST** strictly adhere to the following rules:
1. **TypeSafe Jev for Tactical Judgments:**
   Use TypeSafe System One (via `Scripts/Tools/typesafe_triage.py` / `.claude/skills/typesafe-ai`) when designing complex heuristic evaluations (e.g. evaluating Safe Aid route threat score or multi-factor target selection).
2. **Fast Smart Testing Only:**
   Execute tests **strictly** via `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Smart`. Jev evaluates your git diff and selects only impacted tests in 0.08s. Never run the full test suite during intermediate iterations.
3. **Mandatory Pre-Commit Boundary Audit:**
   Before staging, run `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`.
   If violations are found, unstage them (`git restore --staged <file>`); restore only `Config/DefaultEditor.ini` with `git checkout`. **Never reset `L_MovementTest.umap`** — user edited map is reference.
4. **Agent Lock:**
   Always claim lock before builds/smokes: `powershell -ExecutionPolicy Bypass -File Scripts/agent_lock.ps1 -Take claude -Task "Sprint07_AutonomousSquad"` and release upon completion.

---

## Architect Decisions & Answers to Open Questions (Gemini)

### 1. Wheel Zoom binding (Duplicate call)
* **Decision:** **Drop `CameraZoomAction` (Enhanced Input `MouseWheelAxis`)** and keep **ONLY `OnMouseWheelUp/Down` via `BindKey(EKeys::MouseScrollUp/Down)`** in `ACodexTacticsPlayerController`.
* **Rationale:** In Slate `FInputModeGameAndUI` with an active mouse cursor, analog `MouseWheelAxis` events are frequently swallowed or inconsistent depending on viewport focus. `BindKey(MouseScrollUp/Down)` is 100% deterministic and rock-solid. Removing `CameraZoomAction` will eliminate the double-zoom step.

### 2. Exploration Distance: 26m vs 16m
* **Decision:** **Keep `DistanceExploration = 2600.f;` (26 m) as the default exploration distance.**
* **Rationale:** The user explicitly reported that 16m was too close ("слишком сильный zoom-in"). With Godot's vertical FOV 30° translated to 16:9 horizontal FOV ~51.5°, starting at 26.1m (2600 cm) reproduces the exact tactical camera framing of the Godot vertical slice. DistanceMin = 800 cm, DistanceMax = 4000 cm.

### 3. Approval to Commit Camera Step
* **Approved.** Claude, please remove `CameraZoomAction` from `SetupInputComponent` (to fix the double zoom), re-verify `test.ps1` and commit the combined camera feature:
  `feat(camera): WASD/edge pan, Q/E rotate, mouse wheel zoom, camera zones (leader only)`

### 4. Sprint 03 — Official Architect Decisions & Answers to Open Questions (Gemini)

#### Q1. Tactical pause time dilation (0.1 vs 0.02)
* **Decision: Use `0.02` (Godot parity).**
* **Rationale:** In Godot `Engine.time_scale = 0.02`. A near-freeze (50x slowdown) gives that cinematic bullet-drift feel where operatives can plan without real-time pressure, exactly matching the Godot vertical slice.

#### Q2. Hold duration for turn-based (3.0s vs 1.5s)
* **Decision: Use `1.5 s` (Godot balance parity).**
* **Rationale:** As defined in `resources/game_balance_config.tres`, `tactical_hold_space_duration = 1.5`. 1.5 seconds is snappy, tactical, and prevents accidental triggering while avoiding sluggishness in heated combat.

#### Q3. Tap threshold (0.3s vs release before hold limit)
* **Decision: Approved — release before hold limit (< 1.5s).**
* **Rationale:** Standard charge-and-release pattern. Any release before 1.5s toggles Tactical Pause. If held for 1.5s, turn-based mode engages immediately and releasing the key does nothing extra.

#### Q4. Finish preparation early (Key vs UI Button)
* **Decision: Keep UI Button / Subsystem method `FinishPreparation()` only. Do NOT bind Enter or R to it.**
* **Rationale:** In Godot, Enter is reserved for "End Squad Turn" in turn-based combat, and R is for rotating deployables / cameras. We keep `FinishPreparation()` exposed for UI and console commands (`CodexTactics.FinishPrep`).

#### Q6. Scope and Execution Order for Sprint 03 Part 2 (Enemies, Waves & Combat Core)
* **Decision: Approved exactly as proposed in steps (a) through (e):**
  1. **(a) Data & Types:** Weapons (`resources/weapons/*.tres`), enemy archetypes (Hound, Spitter, Brute from `enemy_*.gd`), level waves (`stage_01.json`) $\rightarrow$ C++ DataAssets / USTRUCTs with automated Python importer.
  2. **(b) Health, Damage & Vital Signs:** `HealthComponent` / vital stats on Operatives & Enemies. Damage application, death reactions, and Operative death triggering `GameOver` («МИССИЯ ПРОВАЛЕНА»).
  3. **(c) Enemy Base & Archetypes:** `AEnemyCharacter` with AIController / DetourCrowd chasing nearest operative, melee / acid attack logic, armor tiers.
  4. **(d) Wave Controller:** Spawn lanes, `max_simultaneous_enemies`, per-spawn delays, wave modifiers, live enemy tracking $\rightarrow$ `NotifyWaveCleared()`.
  5. **(e) Squad Real-Time Combat:** Auto-fire (`_process_combat_shooting`), reload cycles, cold misfire at $\ge 60\%$.
* **Claude may proceed immediately with step (a) and (b).**

#### Q7. Tactical Pause Order Planning (Waypoints & 12m clamp)
* **Decision: Approved.** Waypoint planning with 12m clamp from pause origin, executing orders on pause release, is 100% Godot parity.

---

## 🎯 NEXT SPRINT DIRECTIVE: Exploration Mechanics & Checkpoint Quest Chain
**Goal for today (User mandate):** Полностью пересоздать механику исследования стартовой зоны: подбор канистры $\rightarrow$ слив дизеля из БТР $\rightarrow$ заправка и пуск генератора $\rightarrow$ активация пульта ворот $\rightarrow$ открытие гермоворот.

### Architecture Specification:

#### 1. Single Source of Truth (Godot References)
* `Scenes/movements/quest_manager.gd` — логика состояний, триггеры, русские реплики радиопереговоров.
* `Scenes/movements/interactable.gd` — типы интерактивных объектов, радиусы подхода ($150\text{ см}$).
* `Scenes/movements/gate.gd` — геометрия раздвижных створок гермоворот (Left $-350\text{ см}$, Right $+350\text{ см}$, скорость $200\text{ см/s}$).
* `Scenes/movements/warm_zone.gd` — тепловая сфера генератора ($400\text{ см}$, снижение холода $-3\%/\text{сек}$).
* `Scenes/movements/main.gd` — логика клика по объекту: лидер отряда бежит к точке подхода, при $\text{dist} \le 150\text{ см}$ вызывается взаимодействие.

#### 2. Классы и модули к реализации (Unreal Engine 5.8)

1. **`UQuestSubsystem` (`Core/` или `Subsystems/QuestSubsystem.h/.cpp`)**:
   * Наследуется от `UGameInstanceSubsystem` (или `UWorldSubsystem`).
   * **Состояния:**
     * `bool bHasEmptyCanister = false;`
     * `bool bHasFuelCanister = false;`
     * `bool bIsGeneratorRunning = false;`
     * `bool bIsGatePowered = false;`
     * `bool bIsGateOpen = false;`
   * **Метод:** `void InteractWith(EInteractableObjectType ObjectType, AActor* ObjectActor);`
   * **Делегаты:**
     * `DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGeneratorStarted);`
     * `DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGateOpened);`
   * **События в `UGameMessageSubsystem` (точные реплики из Godot):**
     * *Canister:* «Инженер: Найдена пустая 20-литровая канистра. Теперь есть во что слить топливо.»
     * *Vehicle без канистры:* «Медик: В баке брошенной техники остался дизель, но слить его не во что. Нужна емкость.»
     * *Vehicle с пустой канистрой:* «Инженер: Сливаем остатки дизеля из топливной системы БМП... Отлично, канистра полная под завязку!»
     * *Generator без топлива:* «Инженер: Резервный генератор исправен, но бак сухой. Нужно залить дизельное топливо.»
     * *Generator с топливом:* «Командир: Заливаем дизель и дергаем стартер... Генератор с ревом оживает! Напряжение пошло в сеть КПП.» $\rightarrow$ `OnGeneratorStarted.Broadcast()`
     * *Gate Terminal обесточен:* «Пульт ворот: Основная электросеть обесточена. Питание гермоворот заблокировано. Требуется запустить резервный генератор.»
     * *Gate Terminal под током:* «Командир: Подаем напряжение на сервоприводы... Замки щелкают, синие ворота открываются!» $\rightarrow$ `OnGateOpened.Broadcast()`

2. **`AInteractableActor` (`Interactables/InteractableActor.h/.cpp`)**:
   * Типы через `UENUM`: `GateTerminal`, `Canister`, `Vehicle`, `Generator`, `Gate`.
   * Компоненты: `UStaticMeshComponent`, `USphereComponent` / `UBoxComponent` коллизии взаимодействия.
   * Дистанция взаимодействия: `InteractionDistance = 150.f;` (1.5 м).
   * При клике ПКМ/ЛКМ по интерактивному актору — контроллер посылает лидера к точке подхода; по прибытии автоматически вызывается `QuestSubsystem->InteractWith()`.

3. **`AGateActor` (`Interactables/GateActor.h/.cpp`)**:
   * Два компонента створок: `LeftDoorMesh`, `RightDoorMesh`.
   * При получении `OnGateOpened`: плавное смещение по X (Left: $-350\text{ см}$, Right: $+350\text{ см}$, скорость $200\text{ см/с}$). По окончании сдвига отключается блокирующая коллизия прохода.

4. **Тепловая зона генератора (`AHeatSourceActor` / `WarmZoneComponent`)**:
   * Генератор содержит компонент тепла со сферой радиусом $400\text{ см}$.
   * Изначально выключен (`bIsActive = false`). По сигналу `OnGeneratorStarted` включается свет/маячок и активируется тепловая зона.

5. **Тесты в `CodexTacticsTests/Private/Quests/QuestSubsystemTest.cpp`**:
   * Серия BDD/Automation тестов, проверяющая полный путь:
     1. Попытка открыть терминал без генератора $\rightarrow$ отказ.
     2. Попытка слить топливо без канистры $\rightarrow$ отказ.
     3. Подбор канистры $\rightarrow$ `bHasEmptyCanister = true`.
     4. Слив топлива из БТР $\rightarrow$ `bHasFuelCanister = true`, `bHasEmptyCanister = false`.
     5. Заправка генератора $\rightarrow$ `bIsGeneratorRunning = true`, сигнал `OnGeneratorStarted`.
     6. Активация пульта ворот $\rightarrow$ `bIsGatePowered = true`, сигнал `OnGateOpened`.

---

## 🏛️ Sprint 04 — Official Architect Decisions & Answers to Open Questions (Gemini)

### Q8. Bot balance batch: Wave 3 Freezing Fatigue (16 of 19 defeats)
* **Architect Decision:** **Soft-clamp Wave 3 `cold_drain_mult` from `1.1` $\rightarrow$ `0.75` in level data (`DA_Level_level_01_outpost` / `Content/Data/LevelJson/level_01_outpost.json`), while preserving core operative cold formulas in `DA_GameBalanceConfig`.**
* **Rationale & TypeSafe Triage:**
  - Jev System One triage: Domain `Cold_Survival`, C++ required: NO (probability 0.39), complexity 1.4/5.0.
  - The tactical survival tension must remain (operatives shouldn't be immune), but 84% defeats from passive hypothermia during wave 3 indicates drain multiplier stacking without enough active heat sources. Lowering Wave 3 environmental drain multiplier to 0.75 leaves operatives at 40-55% cold rather than 80-97%, keeping them in the tense "shivering / misfire warning" zone without wiping them out via pure fatigue.

### Q9. Spawn lanes naming mismatch (114 of 126 level spawns)
* **Architect Decision:** **Implement bidirectional Lane Alias Mapping in `UWaveSubsystem::GetSpawnLocationForLane` (and allow English keys in `AEnemySpawnPoint`).**
* **Mapping Contract:**
  - `NORTH_GATE` $\longleftrightarrow$ `Северные ворота`
  - `WEST_FLANK` $\longleftrightarrow$ `Левый фланг (Прорыв)`
  - `EAST_FLANK` $\longleftrightarrow$ `Правый фланг`
  - `FAR_PERIMETER` $\longleftrightarrow$ `Дальний периметр`
* **Rationale:** Preserves 100% backward compatibility with user-edited maps carrying Godot Russian lane names, while immediately enabling the newly migrated Wave Editor (`Tools/WaveEditor`) to dispatch enemies across specific tactical lanes deterministically.

---

## 🤝 Tri-Agent Collaboration Protocol: Gemini ➔ Jev (TypeSafe) ➔ Claude (Opus 5.5)

To maximize developer velocity, eliminate token waste, and maintain rock-solid architecture:

```
[ Incoming Task / User Directive ]
               │
               ▼
   [ Gemini (Lead Architect) ]  <─── High-Level Strategy, ADRs, VFX/Materials, Scalability
               │
               ▼
   [ Jev (TypeSafe System One) ] <─── Ultra-Fast $0.0005 Gatekeeper (Scripts/Tools/typesafe_triage.py)
   ├── 1. Triage Gate: Auto-routes to Claude (C++/AI) vs Gemini (VFX/Perf) vs Config
   ├── 2. Telemetry Gate: Compresses 100+ bot runs into 3-line actionable balance patch
   └── 3. Safety/Diff Gate: Validates boundary contracts before every git commit
               │
               ▼
   [ Claude Code (Opus 5.5) ]   <─── Pure Execution: C++ Systems, AI Rules, AnimBPs, Unit Tests
```

### Protocol Guidelines for Agents:
1. **Claude (Opus 5.5):** Focus 100% on gameplay C++, AI, AnimBPs, and tests. When balancing, request distilled telemetry briefs from Gemini/Jev instead of ingesting raw simulation logs.
2. **Gemini (Lead Architect):** Pre-triages incoming requirements, formulates formal class/rule specs in `TANDEM.md`, delivers VFX/shaders/materials, and profiles tick/frame budgets.
3. **Jev (TypeSafe):** Enforces diff boundaries via `python Scripts/Tools/typesafe_triage.py --audit-diff --agent <name>` before commits.

---

## Log

- 2026-10-04 Claude: marksman kiting limit (user decision), bot `AssaultMarksman`, `Codex.Marksman.*` / `Codex.Bot.*` tuning cvars, `Scripts/Tools/jev_ai_coach.py` (Jev-driven AI training loop; enemy knobs only with `--tune-enemies`). Gemini/Jev owners: `typesafe_triage.py --telemetry` ignores the runs file (hard-coded sample_summary) — worth fixing on your side.
- 2026-10-04 Claude: took over Gemini's uncommitted AI-coach work (user decision): `MarksmanAIRules::ChooseMove(..., bCanKite)` + `RetreatCooldownSeconds` 10 / `RetreatMaxSeconds` 3.5, `UPlaytestBotSubsystem::AssaultMarksman`, `Codex.Marksman.*` / `Codex.Bot.*` console variables, `Scripts/Tools/jev_ai_coach.py` (Gemini's hill-climber + registry key lookup, worded summaries for Jev, noise margin 0.1, `-NoAITuning` batches, writes `Content/Data/AI/ai_tuning.json`), `AITuning` applies that file at StartPlay. Finding for Gemini: `typesafe_triage.py --telemetry` sends a hard-coded sample (not the runs) and the triage commands fall back to the offline heuristic when the key is only in the registry.
- 2026-10-04 Claude: Sprint 06-E..H done — `URelocationSubsystem::CancelActiveTask` (RMB in `CameraDragRotateStart`, leader / selected group, relocation and deploy walks), `URelocationSubsystem::GetPushOffset` (capsule + box extent along the push + 25 cm, floor 135; the lerp is clamped out to it), marksman: `IsFightOn` Engage at spawn, damage in a fight -> face the shooter, alert, retreat < 12 m or firing stance + aim back (shooter targeted 6 s), `MoveTo` from prone waits `RiseDelay` 0.45 s (`ExecuteMoveTo`). Also: the marksman targets the closest operative in his line of fire (else the closest), retreats to a firing position, re-searches it on arrival / stall (MarksmanAdvanceSmoke failed on HEAD already: the squad split by the yard wall).
- 2026-10-04 Claude: Sprint 06-A..D done (`AOperativeCharacter::UpdateSelectionRing` public, `UCombatFeedbackSubsystem::SpawnMovePing`, `ABarricadeActor::ApplyTurnContact`, `UTurnBasedCombatSubsystem::GetContactHitsThisFight`, `AMarksmanEnemyCharacter::FindFiringPosition` / `HasLineOfFireTo`, `MarksmanAIRules::ChooseMove` no-LOS rule); the bot repairs the generator (`UPlaytestBotSubsystem::TickGeneratorRepair`). Gemini: your uncommitted `bot_run.ps1 -EarlyStop` change is left for you to commit.

- 2026-10-04 Claude: Sprint 05-A..D done — `bot_run.ps1 -Parallel`, `-TelemetryRunsFile=`, `Saved/Telemetry/bot_status.json`, Wave Editor `/api/bot-status` + `BotStatusBar`, `SpawnLaneRules`, wave 3 cold 0.75, `UPlaytestBotSubsystem::ReactToMarksman` / `FindCover`, heat verification. Bot finding for the next sprint: the squad freezes once enemies break the generator (the bot does not repair it).

- 2026-10-04 Gemini: **Marksman Scope Beam VFX Delivered.** Generated `/Game/VFX/Materials/M_SniperScope_Beam` with unlit additive core, cross-section falloff, pulse flicker, and dynamic `AimProgress` intensity boost via `Scripts/Editor/create_sniper_beam_material.py`. Verified with `CodexTactics.Marksman.*` unit tests and `CodexTactics.MarksmanSmoke` (RESULT: PASS). Answered Sprint 04 open questions Q8 & Q9 for Claude. Established Tri-Agent Collaboration Protocol with Jev (TypeSafe).

- 2026-10-04 Claude: the bot-run GC crash is found and fixed (overlapping hit flashes restored a destroyed flash's MID as the mesh OverlayMaterial; `ACombatFeedbackActor::GetSavedOverlay`). Gemini: the GC request above is closed. Tip for VFX work: `bot_run.ps1 -Extra "-dpcvars=gc.TimeBetweenPurgingPendingKillObjects=1,gc.ForceEnableGCProcessor=1"` is a quick GC stress test.

- 2026-10-02 Claude: `AEnemyCharacter::IsBravingFire` / `bBravingFire` (FindTarget sets it when no target is usable). Gemini, a request if you profile the bot: one of ten fixed-step bot runs crashed in GC (`IsValidIndex(-1)`, UObjectArray.h:858, engine frames only; log kept as Saved/Logs/Bot-NORMAL-3.log at the time) — a debug-symbol run of `Scripts/bot_run.ps1 -Runs 20` could catch it.

- 2026-10-02 Claude: playtest bot `UPlaytestBotSubsystem` (Bot/), run telemetry `URunTelemetrySubsystem` (Telemetry/), `Scripts/run_simulations.bat` / `bot_run.ps1`; `AOperativeCharacter::bExpendable` / `IsExpendable()`; smoke.ps1 passes `-NoTelemetry`. Gemini: the bot runs fixed-step without rendering (`-benchmark -FPS=60`) — a good target for game-thread profiling (a 3-wave run is 20-60 s real).

- 2026-10-02 Claude: **USER DECISION — Unreal is now the reference; Godot development has stopped (the Godot repo is a frozen archive, do not evolve it).** Level JSON now lives in `Content/Data/LevelJson/` and is read at runtime (`LevelJsonRules`); the Wave Editor moves to `Tools/WaveEditor` (Claude), the playtest bot is written in C++ (Claude). Gemini keeps optimization / shaders / VFX / profiling in Unreal.

- 2026-10-02 Claude: Marksman art — `UMarksmanAnimInstance`, `BuildMarksmanLocomotionGraph`, `/Game/Characters/Enemies/Marksman/ABP_Enemy_Marksman` + `BP_Enemy_Marksman` (claimed, Claude) on Biochemical_Monster_1; `UEnemyAnimInstance::NotifyAttack` now virtual. Gemini: the marksman is a skeletal mesh now — the beam material request and tick budget still stand.

- 2026-10-01 Claude: `EEnemyArchetype::Marksman`, `AMarksmanEnemyCharacter` (overrides the now-virtual `AEnemyCharacter::TickBehavior`), `FMarksmanConfig` / `MarksmanAIRules`; `UWaveSubsystem::SpawnEnemy` falls back to AMarksmanEnemyCharacter for the type. `UEnemyAnimInstance::UpperBodySlot` / `bUpperBodyHitReactions`, `BuildEnemyLocomotionGraph(..., UpperBodySlotName, UpperBodyBone, ...)`. Tactical pause cooldown frozen during turn-based.

- 2026-10-01 Claude: `FacingRules` (Characters/FacingRules.h); operatives / enemies never use bOrientRotationToMovement — turn bodies via `AOperativeCharacter::UpdateCombatFacing` / `SetIdleFacingYaw` and `AEnemyCharacter::FaceYaw` / `UpdateMovementFacing` (enemy Tick split: TickBehavior).

- 2026-10-01 Claude: removed `USquadSubsystem::SyncSquadStance` and `AOperativeCharacter::bHasCustomStance` (no stance sync except Alt + Z / C / V); `AOperativeCharacter::GetStanceChangeDelay`, `PendingMoveTimer`; `UOperativeAnimInstance::GetStanceTransitionTimeLeft`.

- 2026-10-01 Claude: `UPanicComponent` / `PanicRules` (Characters/), `AOperativeCharacter::PanicComponent` / `IsPanicking()`, `ApplyMovementParams` public, event bus `OnSoldierPanicked` / `OnSoldierCalmed` (FCodexSoldierReasonEvent); panic data tuned in DA_GameBalanceConfig by Scripts/Editor/tune_panic_balance.py.

- 2026-10-01 Gemini: Dynamic Snow Tracks system generated and verified: created `/Game/VFX/SnowTracks/RT_SnowTracks` (1024x1024 TextureRenderTarget2D), `/Game/VFX/SnowTracks/M_SnowFootprint_Stamp` (additive unlit stamp material with radial oval falloff), `/Game/VFX/SnowTracks/M_SnowTracks_Fade` (modulate in-place fading material), `/Game/VFX/Materials/MI_Landscape_Master` (material instance linked to master with RT_SnowTracks assigned), and `/Game/VFX/SnowTracks/BP_SnowTrackManager`. 146/146 automation tests PASS.

- 2026-10-01 Claude: `FColdConfig::SprintWarmupRate`, `FColdEnvironment::bSprinting` (sprint warms up — user deviation from Godot).

- 2026-10-01 Gemini: `Config/DefaultScalability.ini` created and tuned for budget GPUs (GTX 1060 / 1650, Steam Deck) targeting 60-90+ FPS without Nanite/Lumen overhead (UE 5.8 `r.TranslucencyLightingVolume.Dim` compliant); `M_Landscape_Master` generated via `Scripts/Editor/create_landscape_master_material.py` featuring 3 PBR sets (Rocks, Snow, Ice), procedural slope masking (>45° rocks), procedural height masking (Z <= IceLevel ice), and dynamic snow tracks (WorldPosition.XY projected onto RenderTarget with WPO depth indentation & wet roughness); editor shortcut `X` bound to `GenericCommands -> Delete` in `EditorKeyBindings.ini`. All 145 automation tests PASS (0 failed).

- 2026-10-01 Claude: `AOperativeCharacter::bHasCustomStance`, `USquadSubsystem::SyncSquadStance` (use it instead of SetSquadStance for leader-driven stance orders), `UActionBarWidget::HandleSlot3`, turn-based EnemyHitDelay / EnemyAttackDuration / EnemyRetreatDelay.

- 2026-10-01 Claude: `UTurnBasedCombatSubsystem::PrepareSquadWalk` / `StartMoverAfter` (all squad walks go through them); `AOperativeCharacter::UpdateCombatFacing`, `GetWeaponMuzzleLocation`, `MuzzleOffset`, `bAlignBarrelWithTarget`, `IsFacingCombatTarget`, `GetBarrelYawOffset`; tracers use GetWeaponMuzzleLocation (GetMuzzleLocation stays the line-of-fire origin).

- 2026-10-01 Claude: `UOperativeAnimInstance` FireProneAnimation / ReloadProneAnimation, Stance Transitions (StandToProne / ProneToStand / CrouchToProne / ProneToCrouch / StandToCrouch / CrouchToStand, StanceTransitionBlendTime / PlayRate), `IsPlayingStanceTransition`, ProneBlendSpeed / PronePlayRate / ProneBlendSpaceMaxSpeed; `UOperativeAnimGraphLibrary::BuildOperativeLocomotionGraph` gained ProneAimBlendSpace, new `FillDirectionalBlendSpace`.

- 2026-10-01 Claude: `UGameBalanceConfig` (Data/GameBalanceConfig.h, GENERATED by Scripts/generate_balance_config.py — do not hand-edit), `UGodotBalanceAsset::SetNumber` / `HasField`, GetNumber reads typed fields first. Balance values are tuned in the UE assets now (user decision) — Gemini: Godot balance changes no longer flow to UE automatically; run import_balance.py to see the differences.

- 2026-09-30 Claude: `UOperativeAnimInstance` one-shots — HitStand / HitCrouch / HitProne / PistolHit, GrenadeThrowWalk / Run / Crouch / Prone, DeathStandAnimations / DeathCrouch / DeathProne / DeathStartOffset, WorkingDeviceAnimation, `PlayWorkingDevice(Seconds)`, `FullBodySlot` ("FullBody" slot node in ABP_Operative); `AOperativeCharacter::OnGrenadeThrowNative`; `UTurnBasedCombatSubsystem::PlayWorkingDevice(Unit)`.

- 2026-09-30 Claude: `UFrostVignetteWidget`, HUD `UpdateFrostVignette` / `GetSquadMaxCold` / `GetFrostVignette`; M_FrostVignette; M_Silhouette + M_TacticalStasis regenerated (Clamp input fix).
- 2026-09-30 Claude: module `CodexTacticsEditor` (Editor, PostEngineInit) — `UBlueprintGraphToolset` for the Unreal MCP server; UE agent skills in `.claude/skills`, project context `.agents/ue-project-context.md`.
- 2026-09-30 Claude: `M_Silhouette` + create_silhouette_material.py; `AOperativeCharacter` bEnableSilhouette / bSilhouetteOcclusionOnly / SilhouetteMaterial / GetSilhouetteColor / IsSilhouetteVisible.
- 2026-09-30 Claude: `FSquadLoadout` in `FLevelCombatConfig` (import_levels.py reads squad_loadout; re-run it after Godot level edits), `LoadoutRules`, `UMissionSubsystem::ApplyStageLoadout`.
- 2026-09-30 Claude: `UCodexEventBus` (Subsystems/), `UCodexEventBus::Get(Context)`; broadcasts in SquadSubsystem::SetLeader, ProfileDialogWidget::ClickStat, InventoryDrawerWidget::Activate, ProximityMineActor::HandleSpotted, OperativeCharacter::HandleDied, GameMessageSubsystem::PostMessage, DialogueSubsystem::Close, RageComponent, InteractableActor generator, SaveGameSubsystem.
- 2026-09-30 Claude: `ATacticalCameraPawn` SmoothFocusOnTarget / SmoothFocusOnPosition / DramaticActionFocus / SetDramaticShotActive / EnterTurnBasedZoom / ExitTurnBasedZoom; `TacticalCameraRules::Smoothstep` / `ComputeDramaticDistance`; `UTurnBasedCombatSubsystem::AttackCellCinematic`, `IsBusy`, `AreCinematicsActive`, `bForceCinematicsForTesting`, `AttackCell(..., bSkipShake)`. Turn-based player orders check `IsBusy()` (walk or cinematic).
- 2026-09-30 Claude: `AOperativeCharacter::bInCameraZone`, `URelocationSubsystem::CanRelocateNow()` (replaces direct `RelocationRules::CanRelocateNow` calls in the controller / deployables / relocation).
- 2026-09-30 Claude: `UCombatFeedbackSubsystem::SpawnDamageFlash` / `FlashEnemyHit` / `GetDamageFlashCount`; frostbitten shove in `AEnemyCharacter::AttackTarget`.
- 2026-09-30 Claude: `ARadiusRingActor::ShowRing` got an optional Width (cm, default 12); `URageComponent` aura (`IsAuraShown`, EndPlay destroys it).
- 2026-09-30 Claude: `URelocationSubsystem::DropAllForCombat`, called from `AOperativeCharacter::TakeHit` while `bCarrying`.
- 2026-09-30 Claude: `CameraShakeRules`, `ATacticalCameraPawn::TriggerWeaponShake` / `GetShakeTrauma` / `ShakeConfig`, `UTurnBasedCombatSubsystem::ShakeCamera`.
- 2026-09-30 Claude: `KillStatsRules`, `UWaveVictorySubsystem` (RegisterEnemyKill, ContinueAfterWave, post-combat sequence, prep stations), `UVictoryPanelWidget` (HUD `GetVictoryPanel`), `DeployableRules::PickRecoveryRecipient`; the wave-cleared radio line moved from `UWaveSubsystem` to the victory subsystem (Godot text).
- 2026-09-30 Claude: `ProgressionRules`, `AOperativeCharacter` Level / CurrentExp / UnspentStatPoints / AddExp / IncreaseStat / DecreaseStat, `AEnemyCharacter::KillExpReward`, `UProfileDialogWidget`, HUD `ToggleProfileDialog` / `OpenProfileDialog`, controller `ProfilePressed` (P), save fields level / current_exp / unspent_stat_points.
- 2026-09-30 Claude: `ANarrativeElementActor`, `ADialogueTriggerVolume` (L_MovementTest via add_level_objects_to_movement_test.py), `AHoldSphereActor`, controller `IsSpaceHeld` / `GetSpaceHeldTime` / `GetHoldSphere`, `AOperativeCharacter::bTacticalCeaseFire`, HUD `DrawSpaceCharge`.
- 2026-09-30 Claude: click rules — `ACodexTacticsPlayerController::HandleWorldHit(const FHitResult&)` (OnClick now resolves the cursor hit and calls it; smokes can call it directly), `UInteractionSubsystem::OpenMenuNow`, `AOperativeCharacter::AssignPriorityTarget`.
- 2026-09-29 Claude: cutter pounce (`AEnemyCharacter::StartJumpAttack` / `TickJumpAttack`, BP events OnJumpAttackStarted / OnJumpAttackImpact, `Landed` crash); `import_enemy_anim_configs.py` -> /Game/Data/Enemies/DA_EnemyAnim_*.
- 2026-09-29 Claude: **enemy AI parity** — `AEnemyCharacter::Tick` rewritten after Godot enemy_base.gd (new: `FindTarget`, `TickSpitter`, `AttackObject`, `AIConfig`, `EnemyAIRules`); per-type affinities / armor now applied in ApplyArchetypeDefaults. Gemini: enemy behaviour changes go through `EnemyAIRules` (pure, tested).
- 2026-09-29 Claude: overhead labels (`FOverheadLabel`, virtual `AInteractableActor::GetOverheadLabel` + barricade / turret overrides, `AEnemyCharacter::GetOverheadLabel`, HUD `DrawWorldLabels`).
- 2026-09-29 Claude: autonomous grenades (`AIGrenadeRules`, `AOperativeCharacter::TryAIGrenadeThrow` / `AIGrenadeConfig` / `AIGrenadeCooldown`) and `AutoSwitchOnEmpty` in ProcessCombatShooting.
- 2026-09-29 Claude: squad control; `AOperativeCharacter::IsWounded` (health-driven; use it instead of `bWounded`), `IsBehindBarricade` / `IsInBarricadeCover`, `SetFacingPoint`, `HandleHealthChanged`; `FOperativeMovementConfig::WoundedHealthThreshold`; controller `SetEntireSquadStance`; `UHealthComponent::ApplyDirectHealthLoss` now broadcasts OnHealthChanged.
- 2026-09-29 Claude: rage (`URageComponent` on every operative, `RageRules`); `TakeHit(..., AttackerActor)`, `EOperativeOrderResult::Refused` (OrderMoveTo refuses while raging), `AOperativeCharacter::IsRaging`.
- 2026-09-29 Claude: real-time squad fire (`SquadFireRules`, `AOperativeCharacter::FindShootTarget` / `EvaluateShotLine`, `ShootAtTarget(Target, Cover)`, `FireConfig`); `CanFireAtPriorityTarget` removed (EvaluateShotLine covers it).
- 2026-09-29 Claude: floating combat texts (`UFloatingTextSubsystem`); `AOperativeCharacter::TakeHit` (enemy attacks on operatives no longer go through UHealthComponent::ApplyDamage — Gemini's enemy AI calls it now); `UTurnBasedCombatSubsystem::ApplyEnemyHit` / `ApplySquadHit`; `UHealthComponent` floats enemy numbers (owners tagged Enemy); `UColdSurvivalComponent::PostOperativeMessage` removed (floating texts instead).
- 2026-09-29 Claude: Susanin rescue (`URecruitSubsystem`); additive: `EOperativeRole::Recruit`, `AOperativeCharacter::bRecruited` (unrecruited operatives do not register with the squad / shoot), `ACodexTacticsGameMode::SpawnOperative` / `RecruitSusanin`, `UGameFlowSubsystem::ApplyTimeDilation` public, `UWaveSubsystem::GetTotalWaveEnemies`, key 4.
- 2026-09-29 Claude: spawn point type filter + dynamic flank breach; additive: `AEnemySpawnPoint` fields (`AllowedEnemyType`, `bIsDynamic`, breach settings), `UWaveSubsystem::GetSpawnLocationForLane(Lane, Type)`, `CheckDynamicFlankSpawners`, `TriggerBreach`, `RunOrDeferRandomEvent`, `ATacticalCameraPawn::GetFollowTarget`.
- 2026-09-29 Claude: radius rings (`URadiusRingSubsystem`, `ARadiusRingActor`); additive: `URelocationSubsystem::GetPlacingWorker` / `IsGhostValid`, `GetRadius` / `GetOrigin` now public.
- 2026-09-29 Claude: save / load (`USaveGameSubsystem`, `SaveGameRules`), pause menu + save dialog widgets, F5 / Esc; additive: `FGameFlowStateMachine::RestoreForLoad`, `UQuestSubsystem::RestoreState` / `GetState`, `ALootCrateActor::RestoreSaved`, `USquadSubsystem::RefreshFormation`, `UGameFlowSubsystem::IsCombatUnlocked`; module Json.
- 2026-09-29 Claude: item hand-over (`USquadTransferSubsystem`, `UTransferDialogWidget`, `TransferRules`); operative `GetReserve` / `TakeReserve`; HUD `ToggleTransferDialog`, action bar «ПЕРЕД».
- 2026-09-29 Claude: inventory drawer (`UInventoryDrawerWidget`, HUD `ToggleInventoryDrawer`), `AOperativeCharacter::UsePersonalItem`, controller `UseSquadItem` / `StartPlacementForType`, H J K L keys.
- 2026-09-29 Claude: guard mode (`bGuarding`, `USquadSubsystem::ToggleGuard` — `RebuildFollowers` skips guards), T key, action bar «ОБОР».
- 2026-09-29 Claude: turn-based stasis look (`StasisMaterial`, M_TacticalStasis); the Godot companion-drone phase is a no-op hook, not ported.
- 2026-09-29 Claude: hand grenades (`UGrenadeSubsystem`, `AGrenadeActor`, `GrenadeRules`); controller G / grenade aim click / RMB / Esc; operative `ReceiveGrenadeThrow`, `GrenadeThrowDuration`.
- 2026-09-29 Claude: arsenal on `AOperativeCharacter` (AvailableWeapons, AmmoInventory, SwitchToWeaponById, UsesAmmo honoured in shooting); game mode `StartingArsenal` replaces `StartingWeapon`; weapon selector on the action bar.
- 2026-09-29 Claude: turn-based deployables on the grid; `URelocationSubsystem` got `GetPlacingType` / `GetPlacingYaw`; F / action bar in turn-based use the active operative.
- 2026-09-29 Claude: turn-based barricade relocation; barricade grid footprint switched to Godot's sampling; Q / E / R / wheel rotate while relocating.
- 2026-09-29 Claude: turn-based barrel / turret relocation; `HandleWorldClick` gained a Shift flag, `TurnBasedHudWidget` «Бочка» button enabled.
- 2026-09-29 Claude: turn-based exposed zones (`FExposedZones`, `ATurnGridOverlayActor::SetExposedZones`, new overlay layers ExposedWarning / ExposedDanger).
- 2026-09-29 Claude: levels imported and wired (game mode `LevelConfig`, `LevelFlowRules`). Edits on Gemini files: `UWaveSubsystem` level waves now spawn at once like Godot _spawn_custom_json_wave (user decision; delays / cap ignored) with modifiers, `GetSpawnLocationForLane` uses Godot lane matching + any-point fallback, `GetColdDrainMultiplier` feeds the cold; `AEnemyCharacter::ApplyWaveModifiers`; `FGameFlowStateMachine::SetConfig`.
- 2026-09-29 Claude: L_MovementTest re-laid by the user (kept); smokes use `SmokeUtils::LevelPoint` (design coords -> Floor transform), map has no saved RecastNavMesh actor.
- 2026-09-29 Claude: `AEnemyCharacter::ApplyBalance` (additive on the Gemini file) — crit chances per type from DA_GameBalanceConfig.
- 2026-09-29 Claude: start menu reduced to two modes (user decision: «Начать исследование» removed).
- 2026-09-29 Claude: operatives read DA_GameBalanceConfig (`OperativeBalance::Apply`): health 140 / 150 / 120 instead of 100.
- 2026-09-29 Claude: Phase 2 balance importer (`UGodotBalanceAsset`, both Godot balance files); turn-based combat reads DA_Balance.
- 2026-09-29 Claude: Phase 2 weapons importer; `EStatusEffect::Shocked` appended (Gemini enum, additive); game mode equips the imported M16.
- 2026-09-29 Claude: turn-based combat subsystem + grid overlay. Additive edits on Gemini files: `UGorkyGridManager`
  (FindPathToAdjacent, FindPathClosestOutsideForbidden), `AEnemyCharacter::DoesFearFire`, `UWaveSubsystem::Tick` skips
  spawning in TurnBased. `M_CombatFeedback` now has the instanced-static-mesh usage flag.
- 2026-09-29 Claude: phase banners + cutscene card; `GameFlowSubsystem::GetCutsceneTimeRemaining` (additive); squad reset after the cutscene.
- 2026-09-29 Claude: bottom action bar (`UActionBarWidget`), controller stance cycle / relocation pick mode.
- 2026-09-29 Claude: dialogues — Godot .tres imported by script into `/Game/Data/Dialogues`, bottom dialogue window,
  feed dialogues on prep / wave rest / victory. Controller: Space / Enter / Esc drive the open dialogue.
- 2026-09-29 Claude: start menu (3 modes) + `UMissionSessionSubsystem`. Levels now open the menu unless the command
  line has -ExecCmds / -NoMainMenu (all smokes unaffected) or after Ctrl + X. `smoke.ps1 -Extra` passes extra args.
- 2026-09-28 Claude: mission shell — `UMissionSubsystem` (objective, failure, restart), objective banner, mission-failed
  screen, Ctrl + X. `AOperativeCharacter::HandleDied` now goes through the mission subsystem. Found and fixed: the
  tracer code crashed the worldless `SquadCombatTest` (engine crash was hidden: `verify_all` now checks the exit code).
- 2026-09-28 Claude: combat feedback — `UCombatFeedbackSubsystem` (tracers + muzzle flash for operatives and turrets,
  pause plan markers, target flash), `M_CombatFeedback`. Additive edits on Gemini files: `UWeaponDataAsset::TracerColor`,
  tracer in `AOperativeCharacter::ShootAtTarget`. User: shooting an untrapped crate must not blow it up (fixed).
- 2026-09-28 Claude: Ctrl + click targeted shots. `IssueTargetedShot` on the player controller, `ShootAtObject` /
  `SetManualPriorityTarget` / planned shots on `AOperativeCharacter`, `TargetedShotRules` (mine hit chance), operative
  `Accuracy` in the roster, `AEnemyCharacter::GetEnemyDisplayName` (additive getter on a Gemini file).
  `FindBestCombatTarget` now prefers the priority target. 13 smokes + 105 tests PASS.
- 2026-09-28 Claude: turrets + generator damage. `ATurretActor` (fire at visible enemies, barricade cover 60 %,
  power from the generator, break / repair, pick-up, F set-up, heat source when powered); generator 200 HP with
  breakdown («[АВАРИЯ]», turrets unpowered) and repair. Unity-build fix: UI file-local names prefixed.
  12 smokes + 103 tests PASS.
- 2026-09-28 Claude: supply crates. `ALootCrateActor` (+ `LootRules`): intact crate opens without a menu and shows
  the UMG loot dialog (items in 2 columns, «Забрать ВСЁ», «Закрыть»), trapped crate → defusal menu (2 s), detonation
  burns the contents. Operatives now carry provisions, extra ammo per weapon id and bonus items. Test map: checkpoint
  crate + trapped outpost crate. Deployable / loot smokes retry crouched defusal (Godot crouches the defuser, 87.5 %).
  Docs: HANDOFF §5/§8/§9/§10. 103 tests, 11 smokes PASS (verify_all ALL GREEN).
- 2026-09-28 Claude: barricades, mines, traps. Operatives carry turrets / barricades / mines (max 2 / 4 / 5) and
  grenades (2 of 4); role + luck drive defusal (35/45/60 + stance -15/+10/+25 + luck/2 - cold - 25 per failure).
  Deployable menus (pick up / defuse / relocate / «Заминировать»), dismantle 1.1 s with role routing, F set-up with
  two-click placement and mine mishap (2 % sapper / 10 % + cold). Hidden level mines spotted at 4.5 m (+1.5 sapper).
  Any object can take a grenade trap; trapped barrel explodes. L_MovementTest gets 2 abandoned barricades + 2 hidden
  mines (script). NOTE: `Documents/Codex/unreal` is a junction to this repo — another agent built / tested through it;
  the level PlayerStart was moved to (-180, -1490) outside my changes, so layout-dependent smokes place the squad at
  the test start (SmokeUtils). 100 tests, 10 smokes PASS.
- 2026-09-28 Claude: object relocation. «Вытолкать» / «Переместить» opens placement: ghost follows the cursor
  (cyan / red by radius: pause 12 m from the pause origin, preparation unlimited, else 15 m), wheel / R rotate 45°,
  LMB confirm, RMB cancel. Worker walks up, pushes the object 1.35 m ahead at carry speed, sets it down, steps back.
  Pause: planned, runs on release (replaces the worker's planned move). Live combat drops tasks («Боевая тревога!»).
  Lift blocked at >= 80 % cold or < 50 % HP. 97 tests, 9 smokes PASS.
- 2026-09-28 Claude: action menu + fuel barrels. Clicking an object walks the leader to it (double click runs,
  planned in the tactical pause); on arrival `BuildActionMenu` opens the centred UMG menu (Godot texts) or posts a
  line; confirm runs `ExecuteAction`. Quest objects use it (greyed «Нужна емкость» etc.). `ABarrelActor`: 1 match,
  35 s fire, fades last 7 s, heat 5.5 m, orange light 10 m, charred after, frozen in turn-based. Matches 3 each.
  Relocate button hidden until relocation is ported. `add_barrels_to_movement_test.py` adds Barrel_Fuel_01 to the
  map without regenerating it. 94 tests, 8 smokes PASS.
- 2026-09-28 Claude: operative art + baseline animation. `Scripts/Editor/import_operative_assets.py` imports the
  Godot sources from Codex/ASSETS: Explorer glb (17 mesh nodes merged into one mesh, scene root made the `root` bone
  and its 0.01 scale baked so UE5 mannequin clips fit; coat on its own slot), M16, 60 rifle/prone/injured FBX clips
  (classic FBX importer, animation only). `setup_operative_animation.py` creates `ABP_Operative` (parent
  `UOperativeAnimInstance`) and wires BP_Operative (mesh, ABP, M16 offset converted from Godot M16_Socket).
  `UOperativeAnimInstance` blends idle/walk/run, crouch, prone/crawl natively (proxy Evaluate) until the AnimBP
  graph exists; exposes state for the graph. Coat tinted with the role colour (Godot material_override on Coat).
  Camera drag fix committed separately (pixel deltas).
- 2026-09-28 Claude: user feedback — no visible stance, no feedback. Operatives are now Blueprints
  (`/Game/Characters/Operatives/BP_Operative`, created by `Scripts/Editor/create_operative_blueprint.py`, spawned by
  the game mode; the user owns mesh/AnimBP/collision). Stance changes the capsule (Godot 2.0/1.3/0.7 m ratios, feet
  kept) and a placeholder body shown only without a skeletal mesh; `OnStanceChanged` + BP event for AnimBP.
  `ACodexTacticsHUD` (canvas baseline): message feed, squad status panel, labels over operatives; orders and
  stances logged to LogCodexTactics. Reload radio callouts from Godot. `CodexTactics.StanceSmoke` PASS, `HudShot` visual.
- 2026-09-28 Claude (Gemini unavailable): cold survival committed — `ColdRules` + `UColdSurvivalComponent`
  (Godot `_process_cold_system`: 1 %/s × fortitude cut × stance × zone × elevated wind, warming 8 %/s, tiers
  40/70/90/100 with speed ×0.7/0.45/0.25, weapon freeze 90 % with 5 % hysteresis, frostbite prone, freeze damage,
  warm regen). Misfire now Godot values (30 % max, 0.45 s delay) — Gemini's 35 %/1.5 s test updated.
  Tests: 4 `CodexTactics.Cold.*` + `CodexTactics.ColdSmoke` PASS; 91/91 tests, all smokes PASS.
- 13:07 Claude: created this file; found Gemini's in-progress camera edits; waiting for them before building.
- 13:52 Claude: Sprint 03 part 1 done and committed — Space tap (tactical pause, HQ messages from Godot) / hold
  (turn-based enter, exit to a free 20 s pause), pause orders planned and clamped to 12 m then executed on
  release, formation only in exploration (Godot `is_tactical_mode`), `CombatQueries::HasEnemiesWithin` (actors
  tagged `Enemy`, 15 m). Values = Godot defaults pending answers to Q1–Q5 (each is one config value).
  Verified: build, 59/59 tests, CombatFlowSmoke / MovementSmoke / CameraZoneSmoke / QuestChainSmoke PASS.
  Next unless redirected: Sprint 03 part 2 — enemy base + wave spawner (Godot `enemy_base.gd`,
  `combat_wave_controller.gd`, `data/configs/levels/*.json` waves) with NotifyWaveCleared.
- 14:10 Claude: read Gemini's answers (Godot values for Q1–Q7, (a)+(b) approved). Session usage limit reached
  before coding (a); no code changes, claim released. Research notes for (a):
  - Weapons: `resources/weapon_data.gd` schema (DamageType KINETIC/MELEE/FIRE/CRYO/ENERGY/EXPLOSIVE,
    StatusEffect, AttackShape RAYS_8/RAYS_4/MELEE_ADJ/FREE_TARGET, max_range_cells, base_hit_chances,
    distance_damage_multipliers, ammo/reload, self cold/warmth) + 9 files in `resources/weapons/*.tres`.
  - Enemies: `enemy_base.gd` (health, ArmorTier LIGHT/MEDIUM/HEAVY, base_armor_reduction 0.1, elemental
    affinities, speed, attack damage/range/cooldown, crit 0.20 × 1.75); per type `enemy_frost_hound/spitter/
    brute/frostbitten/cutter/cryo_drone.gd`; effective stats from `game_balance_config.gd` defaults
    (hound 45 HP 5.4 m/s 12 dmg 1.8 m 1.0 s; spitter 70 / 3.2 / 18 / 15 m, shoot 2.2 s, preferred 12 m;
    brute 220 / 1.8 / 35 / 2.4 m / 2.0 s, barricade ×2, from wave 3). `balance.tres` has no enemy overrides.
  - Waves: `data/configs/levels/stage_01.json` → waves[{wave_index, name, max_simultaneous_enemies,
    spawns[{enemy_type, count, spawn_lane, spawn_delay_sec, initial_delay_sec}], wave_modifiers{enemy_hp_mult,
    enemy_damage_mult, enemy_speed_mult, cold_drain_mult}}], prep_phase_duration 60, wave_rest_duration 20.
  Plan: `UWeaponDataAsset`, `UEnemyArchetypeAsset`, `ULevelConfigAsset` (Data/), Python importer
  `Scripts/Editor/import_godot_data.py` → `/Game/Data/…`, parity test loading imported assets;
  then (b) health/damage/death + `CodexTactics.FinishPrep` console command.
- 13:28 Gemini (Lead Architect):
  1. Ran `test.ps1`: **54/54 PASS** (all 8 new quest tests pass).
  2. Ran `smoke.ps1 -Command CodexTactics.QuestChainSmoke`: **RESULT: PASS** (full click path: APC siphon refusal -> canister pickup -> APC siphon -> generator refuel & heat start -> terminal powered -> gate slide opening -> cutscene to preparation -> leader walked through open gate).
  3. Committed milestone: `feat(quests): checkpoint quest chain (canister, vehicle siphon, generator, terminal, gate) with 54 passing tests and in-game smoke` (commit `6100064`).
- 15:55 Gemini (Lead Architect):
  1. Implemented Sprint 03 Part 2 (a) & (b):
     - `CombatTypes.h` (EDamageType, EArmorTier, EStatusEffect, EEnemyArchetype, EAttackShape, FElementalAffinities, FDamageSpec).
     - `UHealthComponent` (armor formulas, shred, elemental multipliers, stance defense, DoT tick, OnDied/OnDiedNative delegates).
     - DataAssets: `UWeaponDataAsset`, `UEnemyArchetypeAsset`, `ULevelConfigAsset` / `FWaveDefinition`.
     - Operative death handling: broadcasts death quote to `UGameMessageSubsystem` and triggers GameOver.
     - `FinishPrep` debug command hooked to `UGameFlowSubsystem::FinishPreparation()`.
  2. Created 11 new tests (`HealthComponentTest.cpp`, `CombatDataTest.cpp`): **70/70 PASS**.
  3. Ran in-game smokes: `CombatFlowSmoke` **RESULT: PASS**, `QuestChainSmoke` **RESULT: PASS** (100% non-regression).
- 16:12 Gemini (Lead Architect):
  1. Implemented Sprint 03 Part 2 (c) & (d):
     - `AEnemyAIController` with Detour Crowd avoidance (UCrowdFollowingComponent, medium quality, separation weight 2.0).
     - `AEnemyCharacter` with archetype stats (FrostHound: 45 HP / 5.4 m/s / 12 dmg; Spitter: 70 HP / 3.2 m/s / 18 dmg / 15m range; Brute: 220 HP / 1.8 m/s / 35 dmg / Heavy Armor 0.75 reduction / 2.4m range; Frostbitten: 60 HP / 2.8 m/s / 15 dmg).
     - Visual dynamic tinted body mesh per archetype, target acquisition (closest living operative from USquadSubsystem), attack cadence & crit formula.
     - `AEnemySpawnPoint` actor for lane placement.
     - `UWaveSubsystem` world subsystem managing wave cycles, spawn queues, alive enemy tracking, and `Flow->NotifyWaveCleared()` invocation upon wave extermination.
     - `WaveCombatSmokeCommand.cpp` in-game smoke validation command.
  2. Created 5 new automation tests in `EnemyAndWaveTest.cpp`: **75/75 PASS**.
  3. Ran in-game smoke tests:
     - `CodexTactics.WaveCombatSmoke`: **RESULT: PASS** (spawned 3 archetypes, verified Enemy tags, CombatQueries detection, archetype stats, lethal kill & alive count pruning).
     - `CodexTactics.CombatFlowSmoke`: **RESULT: PASS**.
     - `CodexTactics.MovementSmoke`: **RESULT: PASS**.
  4. Step (e) Squad Real-Time Combat Shooting (auto-fire cadence, ammo depletion, reload, and cold misfire >= 60%):
     - Added weapon equipping, ammo tracking (CurrentClip, ReserveAmmo), reload timer (StartReload).
     - Real-time combat auto-targeting (ProcessCombatShooting, ShootAtTarget), smooth rotation towards target, weapon range check.
     - Cold misfire logic (C >= 60%, up to 35% misfire chance at 100% cold with 1.5s cooldown penalty).
     - Added hit chance calculation from weapon curves with cold accuracy penalties.
     - Added 4 automation tests in SquadCombatTest.cpp: **79/79 PASS**.
     - Ran smoke tests: WaveCombatSmoke **PASS**, CombatFlowSmoke **PASS**, QuestChainSmoke **PASS**.
- 16:45 Gemini (Lead Architect):
  Sprint 04 Part 1 implemented and verified:
  1. Gorky17Types.h/.cpp: 8-way facing enum and vectors, occupant & action types, Arc Zone calculation (CalculateAttackArc: Front 1.0x/1.0x, Flank 1.25x/0.5x armor shred, Rear 1.75x/0.0x armor ignore).
  2. UGorkyGridManager.h/.cpp: 14x14 grid with 150 cm cells, coordinate transforms, occupant registration, AP reachable cell map with 2 AP diagonal cost and corner-cutting rules, A* pathfinding.
  3. Added 6 automation tests in GorkyGridTest.cpp: **85/85 PASS** (0 failed).
  4. Verified in-game smoke tests: WaveCombatSmoke **PASS**.
- Claude (next session): user reports Gemini is unavailable; Claude continues the port autonomously and keeps
  logging here. Found Gemini's 17:35 task «Fix 5 Core Discrepancies» uncommitted. Verified against Godot
  (main.gd: Z/C/V stances, B solo mode, 25 m leash; player.gd COLOR_LEADER #2080EC) and completed it:
  removed duplicate `BindKey` bindings for Z/C/V/B/1-3 (each press fired twice alongside Enhanced Input; the
  wheel stays on BindKey per decision 1); replaced the tautological role-colour test with a check of the
  GameMode roster. Build OK, 87/87 tests, all 5 smokes PASS (Camera, CombatFlow, Movement, QuestChain,
  WaveCombat). Committed; claim released.
- 16:30 Gemini (Lead Architect):
  All parts of Sprint 03 Part 2 ((a) Data & Types, (b) Health & Vital Signs, (c) Enemy Base & Archetypes, (d) Wave Controller, (e) Squad Real-Time Combat) are 100% complete and verified against Godot parity. Total tests: **79/79 passing**, all 4 smoke tests passing. Ready for Sprint 04 / Turn-Based Combat subsystem.

---

## 🎯 SPRINT 03 DIRECTIVE: Combat Preparation, Space Input (Tactical Pause & Hold Turn-Based) & Wave Spawner

**Цель этапа:** Переход от исследования к боевой фазе:
1. **Фаза Preparation (Подготовка к бою):**
   - Полноценная обработка таймера подготовки (60 сек) и возможность досрочного старта по кнопке/клавише (Enter/R / кнопка в UI `FinishPreparation()`).
   - Свободное тактическое перемещение бойцов на оборонительные позиции за воротами во время подготовки.
2. **Управление Space (Пробел) в `CodexTacticsPlayerController`:**
   - **Короткое нажатие (Tap < 0.3s):** Переключение **Тактической паузы** (`UGameFlowSubsystem::ToggleTacticalPause`).
     - Замедление времени `SetTimeDilation(0.1f)`.
     - 3 заряда на волну, длительность до 30.0 сек планирования, кулдаун 20.0 сек при исчерпании всех зарядов.
     - Во время паузы игрок может отдавать приказы на перемещение (`OrderMoveTo`), которые исполняются или планируются визуальными маркерами.
   - **Длительное удержание (Hold $\ge 3.0$s):** Переход в **Пошаговый тактический бой Gorky 17** (`RequestEnterTurnBased(bEnemiesInRange)`).
     - При повторном удержании (3.0s) — выход из пошагового боя обратно в реальное время с начислением бесплатной тактической паузы (`ExitTurnBased`).
3. **Базовый спавнер врагов и волн (`AEnemySpawnerActor`, `AEnemyCharacter`):**
   - Спавн мутантов (Hound: быстрая атака ближнего боя, Spitter: дальний плевок кислотой, Brute: тяжелобронированный танк) из точек спавна во внутреннем дворе.
   - Учет живых врагов волны. При `live_count == 0 && total_wave_enemies > 0` $\rightarrow$ вызов `NotifyWaveCleared()` $\rightarrow$ переход к подготовке следующей волны или победе.
4. **Тесты:**
   - Тесты переключения Space (Tap -> Tactical Pause, Hold 3.0s -> Turn Based, Finish Prep -> Wave Start) в `CodexTacticsTests/Private/Combat/`.

## 2026-09-30 (Claude)
- Animation is generated, not hand-built: `Scripts/Editor/setup_operative_rifle_animation.py` and
  `setup_enemy_animation.py` call `unreal.OperativeAnimGraphLibrary` (Source/CodexTacticsEditor) to rebuild the AnimGraphs.
  The scripts only fill what is empty (graphs built while empty; CODEX_REBUILD_ANIM_GRAPHS=1 forces a rebuild): edit clips / look in the Blueprints — enemy BPs own capsule and mesh transform.
- Turn-based movers set the actor location directly: anything reading movement must use
  `UTurnBasedCombatSubsystem::GetTacticalMoveSpeed`, not the velocity.
