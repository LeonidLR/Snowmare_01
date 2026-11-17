# Plan 080 - Enemies (marksmen above all) jitter left-right on seeing the squad (Gemini, 2026-11-17)

## 1. Диагностика и первопричины (с точными цитатами file:line)

Дизайнерский баг B17-5 (плейтест main `0cc08c4b`):
> "some enemies, the marksmen above all, on seeing my operatives start to vibrate hard - microscopic turns right-left, left-right, as if they got stuck and keep turning."

Анализ кода выявил три взаимосвязанных фактора, вызывающих высокочастотную осцилляцию угла поворота (Yaw) противников и бойцов ЧВК при обнаружении отряда:

### Причина 1: Флаппирование выбора цели без гистерезиса дистанции
- **Где**:
  - `Source/CodexTactics/Private/Characters/MarksmanEnemyCharacter.cpp:817-839`: `FindClosestOperative` вызывается каждый кадр в `TickBehavior`. Простой перебор `Distance < OutDistance` без гистерезиса приводит к тому, что при приближении отряда в строю (где 2 оперативника находятся на схожей дистанции, например 22.0 м и 22.05 м) малейшие колебания анимации шага или дыхания переключают `Target` каждый кадр между левым и правым оперативником.
  - `Source/CodexTactics/Private/Characters/EnemyAIRules.cpp:125-130`: `EnemyAIRules::SelectTarget` использует строгое сравнение `Effective < MinDistance`, переключая цели для обычных противников (spitters и др.) при равных расстояниях.
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:1286-1298`: `UPmcSquadSubsystem::BuildFighterView` ищет ближайшую угрозу через `Distance < BestDistance`. При переключении вызывается `Fighter.SetManualPriorityTarget(ThreatActor)` (`Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:1688`), заставляя бойца ЧВК метаться между целями.

### Причина 2: Отсутствие мертвой зоны (Dead Band) при довороте
- **Где**:
  - `Source/CodexTactics/Private/Characters/EnemyCharacter.cpp:137-141`:
    ```cpp
    void AEnemyCharacter::FaceYaw(float Yaw, float DeltaTime, float InterpSpeed)
    {
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Yaw, 0.f), DeltaTime, InterpSpeed));
        bFacedThisTick = true;
    }
    ```
  - `Source/CodexTactics/Private/Characters/MarksmanEnemyCharacter.cpp:1066`:
    ```cpp
    FaceYaw((Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw, DeltaTime, 6.f);
    ```
  - `FMath::RInterpTo` выполняет интерполяцию без угловой зоны нечувствительности (`DeadBand`). Когда угол отклонения составляет доли градуса (< 0.5°), из-за погрешностей вычислений с плавающей запятой или микросмещений актора шаг поворота начинает менять знак каждый кадр (рыскание вокруг целевого угла).

### Причина 3: Конфликт владельцев ориентации (Movement Facing vs Aim Facing)
- **Где**:
  - `Source/CodexTactics/Private/Characters/EnemyCharacter.cpp:143-159`: `UpdateMovementFacing` в `AEnemyCharacter::Tick` вызывается после `TickBehavior`. Если в текущем состоянии `FaceYaw` не был вызван (например, в `EMarksmanMove::Approach` в `MarksmanEnemyCharacter.cpp:997-1040`), `bFacedThisTick` остается `false`, и ориентацию начинает задавать сглаженная скорость движения (`SmoothedVelocity`), которая на старте/остановке или обходе препятствий колеблется по направлению.

---

## 2. План исправлений

### 2.1. Гистерезис и минимальное время удержания цели (Target Switch Hysteresis)
1. **В `AMarksmanEnemyCharacter`**:
   - Сохранять `CurrentTarget`. Новый кандидат становится целью только если:
     - Текущая цель невалидна/мертва/потеряна из виду, ИЛИ
     - Дистанция до нового кандидата меньше дистанции до текущей цели минимум на `TargetSwitchHysteresisCm` (250 см по умолчанию), ИЛИ
     - Истек таймер удержания текущей цели `TargetHoldSeconds` (не менее 1.5 с).
2. **В `EnemyAIRules::SelectTarget`**:
   - Давать текущей цели приоритетный бонус расстояния (умножение на коэффициент гистерезиса 0.85f), чтобы исключить переключение при разнице дистанций менее 15%.
3. **В `UPmcSquadSubsystem`**:
   - Добавить гистерезис переключения приоритетной угрозы для бойцов ЧВК в `BuildFighterView` (150 см).

### 2.2. Угловая мертвая зона (Angular Dead Band)
1. **В `AEnemyCharacter::FaceYaw`**:
   - Ввести проверку: если `FMath::Abs(FRotator::NormalizeAxis(WantedYaw - CurrentYaw)) < FacingDeadBandDeg` (1.0°), вращение не прикладывать, фиксируя текущую ориентацию.
2. **В `FacingRules`**:
   - Добавить хелпер `FacingRules::HasDeadBandExceeded(CurrentYaw, TargetYaw, DeadBandDeg)`.

### 2.3. Конфигурация параметров (без магических чисел в C++)
- Добавить параметры в `FMarksmanConfig` (`Source/CodexTactics/Public/Characters/MarksmanAIRules.h`):
  - `TargetSwitchHysteresisCm = 250.0f`
  - `FacingDeadBandDeg = 1.0f`
  - `TargetMinHoldSeconds = 1.5f`
- Добавить параметры в `Content/Data/AI/pmc_commander.json`:
  - `target_switch_hysteresis_cm: 200`
  - `facing_dead_band_deg: 1.0`
- Добавить соответствующие границы в `pmc_commander.bounds.json`.

---

## 3. Верификация и смоук-тесты

1. **Модульные тесты**:
   - Добавить тесты на правила гистерезиса целей в `Source/CodexTacticsTests/Private/Characters/MarksmanAIRulesTest.cpp` и `PmcCommanderRulesTest.cpp`.
2. **Смоук-проверка на дрожание (Yaw Sign Flips)**:
   - Внедрить проверку частоты смены знака шага Yaw (`Sign(DeltaYaw) != Sign(PrevDeltaYaw)` с порогом амплитуды > 0.2°) за 3 секунды после обнаружения отряда.
   - Критерий прохождения: не более 2 смен знака в секунду (вместо > 15-30/сек в baseline).
3. **Полный регрессионный прогон смоуков**:
   - `PmcStalemateSmoke` (3/3)
   - `PmcEncounterSmoke` (3/3)
   - `EnemyArchetypeSmoke` (3/3)
   - `EnemyPrioritySmoke` (3/3)
4. **Валидационный гейт**:
   - `Scripts/Tools/gemini_gate.ps1 -Task 080 -SonnetReview` (PASS).
