# Tandem: Claude (code, logic, animation) + Gemini (optimization, shaders)

## Roles (user decision 2026-10-01 — replaces the 2026-09-28 architect / implementer split)

| | Claude | Gemini |
|---|---|---|
| Focus | Gameplay code and logic, AI, UI, animation setup (AnimBPs, blend spaces, montages), data import, smokes / tests | Performance and optimization, shaders and materials, VFX, rendering / scalability settings, profiling |
| Owns (writes) | `Source/CodexTactics/**` (gameplay C++; except Gemini's `Private/Debug/Perf*`), `Source/CodexTacticsTests/**`, `Source/CodexTacticsEditor/**`, `Content/Characters/**`, `Content/Data/**`, `Scripts/Editor/*` except the material scripts, `Scripts/*.ps1` | `Shaders/**` (.usf / .ush), `Content/VFX/**` (materials, Niagara), `Scripts/Editor/create_*_material.py`, `Config/DefaultScalability.ini`, rendering cvars in `Config/DefaultEngine.ini` `[/Script/Engine.RendererSettings]`, LOD / Nanite settings, perf smokes (`Source/CodexTactics/Private/Debug/Perf*SmokeCommand.cpp`) and profiling scripts (`Scripts/perf_*.ps1`) |
| The user's | `Content/Maps/L_MovementTest.umap`, the imported packs (Combat_Dog, RifleAnims, Crawl_MocapAnimPack, Post_Apo_Survivor, monsters, Mannequins, …), `Config/DefaultEditor.ini`, `Config/DefaultInput.ini` — never committed by an agent | |

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
| Gemini | Marksman VFX: M_SniperScope_Beam (aim telegraph laser beam material) | Content/VFX/Materials/M_SniperScope_Beam.uasset, Scripts/Editor/create_sniper_beam_material.py | 2026-10-01 |

## Requests

| From → To | Request (with measurement / reason) | Status |
|---|---|---|
| User & Gemini → Claude | **1. Анимация попадания врагов (Замороженные/Frostbitten):** При получении урона на бегу враги продолжают бежать и одновременно играют полный хит, что приводит к скольжению ног. Решение: В `ABP_Enemy_*` / `setup_enemy_animation.py` использовать `Layered blend per bone` (от `spine_01` вверх) для слота реакции на урон (`UpperBodySlot`). Верхняя часть отыгрывает взмах руками/удар, а нижняя продолжает бег без артефактов скольжения. | Done (Claude 2026-10-01): ABP_Enemy_* regenerated with a LayeredBoneBlend (spine_01 / hound bip001-neck) + `UpperBody` slot; `UEnemyAnimInstance::bUpperBodyHitReactions` plays hits there while moving. EnemyHitLayerSmoke. Cutter / Brute have no hit clips (Godot enable_hit_reaction off / user ABP) |
| User & Gemini → Claude | **2. Устранение абьюза тактической паузы и пошагового боя:** Сейчас выход из пошагового боя обнуляет/сбрасывает паузы на максимум (бесплатная пауза), что позволяет игроку бесконечно абузить заряды. **Правила:**<br>1) **Запретить вход в пошаговый бой из тактической паузы** (`UGameFlowSubsystem::RequestEnterTurnBased` отклоняет запрос, если пауза активна — сначала нужно снять паузу).<br>2) **Сохранять заряды тактической паузы при выходе из пошагового боя.** Количество зарядов и текущий таймер кулдауна после выхода из пошагового боя должны оставаться ровно такими же, какими они были до входа, без бесплатного сброса. | Done (Claude 2026-10-01): entering turn-based from the pause was already rejected (RequestEnterTurnBased is RealTime-only); the pause cooldown no longer runs during turn-based (`FGameFlowStateMachine::Tick`), charges / timer unchanged after exit. Test GameFlow.TurnBased.KeepsPauseChargesAndCooldown |
| Gemini → Claude | **3. Tactical Marksman Enemy Archetype:** Implement `AMarksmanEnemyCharacter` (child of `AEnemyCharacter`), `MarksmanAIRules` (pure tested rules for kiting <12m, flanking 45-90°, cover evaluation, stance switching Stand/Crouch/Prone with capsule height, 2.0s aiming phase), `EEnemyArchetype::Marksman`, and unit tests in `MarksmanAITest.cpp`. Full Studio Spec below. | Done (Claude 2026-10-01): `AMarksmanEnemyCharacter`, `MarksmanAIRules` (Characters/, repo convention instead of AI/), `EEnemyArchetype::Marksman`, 5 tests CodexTactics.Marksman.*, MarksmanSmoke. Beam = engine cylinder, uses `/Game/VFX/Materials/M_SniperScope_Beam` (scalar AimProgress) once it exists. Gemini: tick budget profiling + the beam material; no art Blueprint yet (placeholder body) |

## Open questions — Sprint 03 (Claude → Gemini) — [ALL ANSWERED BY GEMINI BELOW]

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

## Log

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
