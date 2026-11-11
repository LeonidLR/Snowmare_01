# План интеграции анимации спрыгивания вниз (#42) / Drop-Down Animation Integration Plan

- **Дата:** 2026-11-11
- **Ветка:** `gemini/013-2-drop-anim-plan` (от `main`)
- **Статус:** Read-only план интеграции анимационного клипа `/Game/Animations/Humanoids/Jumping_1__UE`
- **Контекст задачи:** Задача #42 из `TASK_BOARD.md` (подключение анимации старта спрыгивания вниз к drop links и спускам с крыш/уступов вместо прямого перехода в цикл падения).

---

## 1. Архитектура текущей системы падений и спусков (Current State)

В проекте CodexTactics механика спрыгивания и падений управляется следующими подсистемами:
1. **`ULedgeFallComponent`** (`Source/CodexTactics/Public/Falls/LedgeFallComponent.h`):
   - Управляет состояниями прыжка: `Waiting` -> `RunUp` -> `Airborne` -> `Landed`.
   - В фазе `Airborne` отключает air control, сохраняет горизонтальную скорость разбега, а вертикальное ускорение рассчитывается с учётом масштабированной гравитации:
     $$	ext{AirGravity} = 	ext{WorldGravity} 	imes 	ext{AirGravityScale} = 980 	imes 2.25 = 2205 	ext{ см/с}^2$$
2. **`FallRules`** (`Source/CodexTactics/Public/Falls/FallRules.h`):
   - `JudgeFall`: урон от падения (до 320 см безопасно, 900–1200 см тяжёлое ранение + knockdown, $\ge 1200	ext{ см}$ смерть).
   - `FallAnimPhase`: определяет фазу `EFallAnimPhase::Grounded`, `EFallAnimPhase::Falling`, `EFallAnimPhase::Landing`.
3. **`ABP_Operative` и машина состояний `FallStates`**:
   - Добавлена через `UOperativeAnimGraphLibrary::AddFallStateMachine` (`Scripts/Editor/setup_operative_fall_animation.py`).
   - Состояния: `Grounded` (кэш `PreFallPose`) -> `Falling` (`MM_Rifle_Jump_Fall_Loop`, loop) -> `Landing` (`MM_Rifle_Jump_Fall_Land`, once).
   - Переходы управляются булевыми флагами `UOperativeAnimInstance::bFallAirborne`, `bFallLanding`, `bFallGrounded`.

> **Проблема текущей реализации (#42):**
> При спрыгивании с уступа или крыши персонаж мгновенно переходит из бега/ходьбы в зацикленную позу свободного полёта (`MM_Rifle_Jump_Fall_Loop`) без анимации толчка/шага в пустоту (takeoff / leap start), что выглядит неестественно.

---

## 2. Точные файлы и строки для внесения изменений (Files & Lines to Change)

### 2.1. `Source/CodexTacticsEditor/Private/OperativeAnimGraphLibrary.cpp` и `OperativeAnimGraphLibrary.h`
- **Файл:** `Source/CodexTacticsEditor/Public/OperativeAnimGraphLibrary.h:145`
  - Расширить сигнатуру `AddFallStateMachine`, добавив параметр клипа начала прыжка:
    ```cpp
    static bool AddFallStateMachine(UAnimBlueprint* AnimBlueprint, FName SlotName,
        UAnimSequenceBase* JumpStart, UAnimSequenceBase* FallLoop, UAnimSequenceBase* Land,
        float StartBlendIn, float FallBlendIn, float LandBlendIn, float GroundBlendIn, FString& OutReport);
    ```
- **Файл:** `Source/CodexTacticsEditor/Private/OperativeAnimGraphLibrary.cpp:380-455`
  - Добавить состояние `JumpStart`:
    ```cpp
    UAnimStateNode* Grounded = AddState(TEXT("Grounded"), 2, 1);
    UAnimStateNode* JumpStart = AddState(TEXT("JumpStart"), 3, 0);
    UAnimStateNode* Falling = AddState(TEXT("Falling"), 5, 0);
    UAnimStateNode* Landing = AddState(TEXT("Landing"), 5, 2);
    ```
  - Настроить переходы в графе `MachineGraph`:
    - `Grounded -> JumpStart`: по условию `bFallAirborne` (Crossfade 0.10 с).
    - `JumpStart -> Falling`: по условию `bFallAirborne` при достижении конца клипа (`TimeRemainingRatio <= 0.1` или автоматический переход по правилу времени).
    - `JumpStart -> Landing`: по условию `bFallLanding` (Crossfade 0.10 с) — **критично для коротких дропов (< 2 м)**, где боец касается земли раньше, чем закончился стартовый клип.
    - `Falling -> Landing`: по условию `bFallLanding` (Crossfade 0.10 с).
    - `Landing -> Grounded`: по условию `bFallGrounded` (Crossfade 0.25 с).

### 2.2. `Source/CodexTactics/Public/Falls/FallRules.h` и `FallRules.cpp`
- **Файл:** `Source/CodexTactics/Public/Falls/FallRules.h:100-104, 176-183, 202-207`
  - Добавить фазу в `EFallAnimPhase`:
    ```cpp
    enum class EFallAnimPhase : uint8
    {
        Grounded,
        JumpStart,
        Falling,
        Landing
    };
    ```
  - В структуру `FFallConfig`:
    ```cpp
    /** anim.jump_start_s: длительность фазы отталкивания перед выходом в цикл свободного падения. */
    float AnimJumpStartSeconds = 0.32f;
    ```
- **Файл:** `Source/CodexTactics/Private/Falls/FallRules.cpp:180-205`
  - Обновить `FallAnimPhase`:
    ```cpp
    if (bDropAirborne || FallingSeconds >= Config.AnimFallMinSeconds)
    {
        if (FallingSeconds < Config.AnimJumpStartSeconds)
        {
            return EFallAnimPhase::JumpStart;
        }
        return EFallAnimPhase::Falling;
    }
    if (SecondsSinceFallEnded < Config.AnimLandSeconds)
    {
        return EFallAnimPhase::Landing;
    }
    return EFallAnimPhase::Grounded;
    ```

### 2.3. `Source/CodexTactics/Public/Characters/OperativeAnimInstance.h` и `OperativeAnimInstance.cpp`
- **Файл:** `Source/CodexTactics/Public/Characters/OperativeAnimInstance.h:680-692`
  - Добавить переменную анимации:
    ```cpp
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Animation|Falls")
    bool bFallStarting = false;
    ```
  - Обновить `GetFallStateName()`: поддержка возврата `TEXT("JumpStart")`.
- **Файл:** `Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp:150-165`
  - В `UpdateFallAnimation`:
    ```cpp
    bFallStarting = Phase == FallRules::EFallAnimPhase::JumpStart;
    bFallAirborne = Phase == FallRules::EFallAnimPhase::Falling;
    bFallLanding = Phase == FallRules::EFallAnimPhase::Landing;
    bFallGrounded = Phase == FallRules::EFallAnimPhase::Grounded;
    ```

### 2.4. `Source/CodexTactics/Public/Falls/LedgeFallComponent.h` и `LedgeFallComponent.cpp`
- **Файл:** `Source/CodexTactics/Public/Falls/LedgeFallComponent.h:70-75`
  - Добавить слот клипа:
    ```cpp
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Falls")
    TSoftObjectPtr<UAnimSequenceBase> JumpStartClip;
    ```

### 2.5. Скрипт сборки графа `Scripts/Editor/setup_operative_fall_animation.py`
- Добавить путь к клипу:
  ```python
  JUMP_START = "/Game/Animations/Humanoids/Jumping_1__UE"
  ```
- Передавать `jump_start` в `add_fall_state_machine`.

---

## 3. Граф состояний (State Flow)

```
              [ Grounded ] (PreFallPose: бег / ходьба / стойка)
                   |
                   | bFallAirborne / bFallStarting (старт с уступа)
                   v
              [ JumpStart ] (Jumping_1__UE, толчок / шаг вниз, non-loop)
                 /                       /         \ (короткий спуск H <= 2м: приземление до конца клипа)
               /           v
(время клипа  /       [ Landing ] (MM_Rifle_Jump_Fall_Land, once)
 истекло)    /             ^
            v              | bFallLanding (касание земли)
       [ Falling ] --------+
(MM_Rifle_Jump_Fall_Loop,  |
 loop в воздухе)           |
            \              |
             \             |
              v            v
               [ Grounded ] (после land.settle_s 0.35 с / AnimLandSeconds 0.45 с)
```

**Ключевые правила переходов:**
1. `Grounded -> JumpStart`: Срабатывает в первый же кадр отрыва от поверхности (`ELedgeFallState::Airborne` или `MOVE_Falling`).
2. `JumpStart -> Falling`: Если высота падения большая, стартовый толчок проигрывается полностью ($\sim 0.30	ext{ с}$) и бесшовно переходит в циклический луп `Falling` (бленд 0.15 с).
3. `JumpStart -> Landing` (**ранний перехват**): На низких уступах (1.5–2.0 м) боец приземляется быстрее, чем длится клип толчка. Переход по `bFallLanding` имеет приоритет, не дожидаясь окончания `JumpStart`.
4. `Landing -> Grounded`: Восстановление нормальной локомоции после проигрывания амортизации приземления.

---

## 4. Расчёт времени фазы старта против длительности падения (Timing & Gravity)

В CodexTactics действует форсированная гравитация падения:
$$g_{eff} = 980 	imes 2.25 = 2205 	ext{ см/с}^2$$

Формула времени падения с высоты $H$:
$$t = \sqrt{rac{2H}{g_{eff}}} = \sqrt{rac{2H}{2205}}$$

| Тип спуска / уступа | Высота $H$ | Время падения $t$ | Отрабатываемая анимация |
|---|---|---|---|
| Спуск в траншею / с камня | **150 см** | **0.369 с** | `JumpStart` (0.28 с) $	o$ прямой срез в `Landing` (без `Falling` loop) |
| Один этаж здания (1 level) | **300 см** | **0.521 с** | `JumpStart` (0.30 с) $	o$ `Falling` (0.12 с) $	o$ `Landing` |
| С крыши руин на землю (2 levels) | **600 см** | **0.738 с** | `JumpStart` (0.30 с) $	o$ `Falling` (0.34 с) $	o$ `Landing` |
| Высота контузии (Heavy wound) | **900 см** | **0.903 с** | `JumpStart` (0.30 с) $	o$ `Falling` (0.50 с) $	o$ `Landing` + Knockdown |
| Фатальная высота (Lethal) | **1200 см** | **1.043 с** | `JumpStart` (0.30 с) $	o$ `Falling` (0.64 с) $	o$ Смерть/Рэгдолл |

**Выводы по таймингу:**
- Длительность эффективной фазы отталкивания клипа `Jumping_1__UE` должна быть строго в пределах **$0.28 - 0.32$ секунды**.
- Если длительность клипа превысит 0.45 с, на стандартном перепаде в 1 этаж (300 см) персонаж вообще не успеет войти в фазу падения `Falling` и приземлится прямо из фазы толчка.

---

## 5. Аудит импортированного клипа `/Game/Animations/Humanoids/Jumping_1__UE`

Проведённый анализ ассета выявил следующие критические свойства:
1. **Несовпадение скелета (Skeleton Mismatch):**
   - Бинарный осмотр показал: `Jumping_1__UE.uasset` ссылается на `/Game/Biochemical_Monster_1/baze_mech/ue4/SK_UE4_Mannequin_Skeleton`.
   - Основной `ABP_Operative` использует `/Game/Post_Apo_Survivor/Demo/Mannequin/Character/Mesh/UE4_Mannequin_Skeleton`.
   - **Необходимое действие:** Перед включением в `ABP_Operative` клип должен быть переназначен на целевой скелет `UE4_Mannequin_Skeleton` (через `SetSkeletalMeshSkeleton` или `setup_operative_fall_animation.py` / Compatible Skeletons), иначе компиляция AnimBP выдаст ошибку несовместимости скелета.
2. **Root Motion (Движение корня):**
   - В CodexTactics баллистическая траектория падения и горизонтальное смещение в воздухе рассчитываются программно через физическую капсулу и `ULedgeFallComponent`.
   - Если клип содержит перемещение кости `root` по оси Z или горизонтали с включенным Root Motion, возникнет паразитное ускорение или конфликт («двойная гравитация»).
   - **Проверка:** В свойствах клипа параметр `EnableRootMotion` должен быть **`false`** (In-Place), либо `ForceRootLock = true`.
3. **Состав фаз клипа:**
   - Если исходный клип `Jumping_1__UE` содержит полный цикл (толчок $	o$ полёт $	o$ приземление), его необходимо обрезать: оставить только фазу отрыва (takeoff) до наивысшей точки/выпрямления ног, так как приземление уже обеспечивает `MM_Rifle_Jump_Fall_Land`.

---

## 6. Расширение смок-тестов (Smoke Extension)

Тестирование падений и дропов выполняется смоком **`CodexTactics.FallDropSmoke`**:
- Исходный код: `Source/CodexTacticsDev/Private/Debug/FallDropSmokeCommand.cpp`.
- Команда запуска:
  ```powershell
  powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CodexTactics.FallDropSmoke -ExtraArgs "-Multiprocess"
  ```
- Карта: `/Game/Maps/Generated/L_Gen_Depot_1` (`-LevelJson=L_Gen_Depot_1.json`).

**План расширения смока:**
1. В структуру состояния `FallDropSmoke::FState` (строка 449):
   - Добавить флаг проверки `bool bAbpJumpStart = false;`.
2. В функции мониторинга `Run` (строки 736–745):
   ```cpp
   const FName AbpState = AbpFallState(*Watched);
   State.bAbpJumpStart |= (AbpState == TEXT("JumpStart"));
   State.bAbpFalling |= (AbpState == TEXT("Falling"));
   State.bAbpLanding |= (AbpState == TEXT("Landing"));
   ```
3. В Stage 2 (WalkOff, 6 м) и Stage 3 (RoofLand, 3 м) обновить финальную проверку (строка 809):
   ```cpp
   && State.bAbpJumpStart && State.bAbpFalling && State.bAbpLanding && !Fall.PlayedFallClip()
   ```
4. В `FallDropShotsCommand.cpp` добавить кадр захвата позы отрыва бойца от края крыши для визуальной регрессионной проверки.
