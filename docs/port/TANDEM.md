# Tandem: Gemini (architect) + Claude (implementer)

User decision 2026-09-28: Gemini leads and directs the UE 5.8 port as Lead Systems Architect; Claude executes C++ implementation and automation tests.

## Protocol

1. **Claim before editing.** Add a row to «In progress» with your name and the files you will touch; remove it when done. Do not edit files claimed by the other agent.
2. **One builder at a time.** Build / test / smoke / commit only when «In progress» is empty.
3. **Verification before commit.** `Scripts/build.ps1`, `Scripts/test.ps1`, relevant `Scripts/smoke.ps1 -Command …` must pass. Claude commits verified steps.
4. **Architect Directives & Questions:** Architectural decisions, formulas and task assignments are dictated by Gemini. Claude implements.
5. Close Unreal Editor before building or regenerating test maps.

---

## In progress

*Currently empty. Checkpoint quest chain completed, verified by 54/54 unit tests and in-game smoke test.*

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

- 13:07 Claude: created this file; found Gemini's in-progress camera edits; waiting for them before building.
- 13:12 Claude: user appointed Gemini as lead; Claude follows Gemini's plan and commands. Verified build OK, 46/46 tests. Confirmed wheel double zoom.
- 13:15 Gemini: Lead Architect instructions issued:
  1. Resolved questions (drop `CameraZoomAction` to keep single clean zoom, retain 26m distance).
  2. Approved camera milestone commit.
  3. Assigned sprint: **Exploration Quest Chain (Canister, Vehicle, Generator, Gate)** with exact C++ specs and parity tests.
- 13:24 Claude: removed `CameraZoomAction` (wheel only via `BindKey`), kept 26 m. Build OK, 46/46 tests,
  CameraZoneSmoke PASS, MovementSmoke PASS. Committed camera step. Starting quest chain sprint: reading Godot
  references first; numeric conflicts with Godot code will be listed under «Open questions» before coding.
