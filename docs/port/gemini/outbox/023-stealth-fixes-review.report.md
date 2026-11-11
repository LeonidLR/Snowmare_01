# Отчёт ревью незакоммиченных правок стелса Codex (Queue 023, Item 1)

**Дата:** 2026-11-11 ~19:00  
**Автор:** Gemini (read-only review)  
**Контекст задачи:** Оценка незакоммиченных правок Codex по обратной связи дизайнера (#184–#186).  
**Источники данных (READ-ONLY):**
- Бэкап: `Saved/Orchestrator/backup/interim-wip-1111-1703/wip.patch` (39 KB, 12 изменённых файлов)
- Архив: `Saved/Orchestrator/backup/interim-wip-1111-1703/new.tar` (`Source/CodexTacticsDev/Private/Debug/StealthDetectionSmokeCommand.cpp`)
- Базовая ветка сравнения: `interim/2026-11-12` (коммит `d12ffdbd803a` и ранее).

---

## 1. Сводная таблица по 6 требованиям дизайнера (#184–#186)

| № | Требование дизайнера | Статус | Основные затронутые файлы:строки | Вердикт / Замечания |
|---|----------------------|--------|----------------------------------|---------------------|
| **1** | Вход автоматически ставит PASSIVE | **PARTIAL** | `OperativeCharacterStealth.cpp:58-59` | На входе выставляется `Passive`, но **критический баг при выходе**: оверрайд не сбрасывается, боец навсегда остаётся в `Passive`. |
| **2** | Разрешить ползать в стелсе (Crawl allowed) | **DONE** | `OperativeCharacterStance.cpp:42`, `OperativeCharacterStealth.cpp:60-63, 151`, `OperativeAnimInstanceStealth.cpp:43, 51` | Ползание сохраняет режим стелса и шейдер; анимационный слой безопасно передаёт управление `Crawl_MocapAnimPack`. |
| **3** | Подтверждённое обнаружение отменяет режим и шейдер | **DONE** | `EnemyCharacterPerception.cpp:188-194`, `TacticalSightSubsystem.cpp:897-902`, `PmcSquadSubsystem.cpp:653-656`, `OperativeCharacterStealth.cpp:65-138` | Зрительное обнаружение сбрасывает режим и восстанавливает исходные материалы. Слух/шум режим не сбрасывает. |
| **4** | Работа против ЧВК (PMC) | **DONE** | `PmcSquadSubsystem.cpp:644-657, 692-696`, `StealthDetectionSmokeCommand.cpp:140-155` | Внедрено масштабирование радиуса зрения (`DetectionRangeScale` = 0.35) и проверка укрытия (`IsHiddenInCoverFrom`). Покрыто смоуком. |
| **5** | Огонь врагов только по последней видимой точке | **DONE** | `PmcSquadSubsystem.h:188-196`, `PmcSquadSubsystem.cpp:680-690, 740-779, 1090, 1980-1991`, `PmcCommanderRules.cpp:171`, `TacticalSightSubsystem.cpp:906-908` | Координаты замораживаются в `LastKnown`; ЧВК переключаются на площадной огонь на подавление (`OrderSuppress`) точки памяти. Unit-тест PASS. |
| **6** | Реальные позы прижимания к стене и выглядывания (`StealthLocomotion` LeftWall/RightWall/Lean) | **DONE** | `OperativeAnimInstance.cpp:1874, 1907-1930, 2362`, `OperativeCharacterCover.cpp:144, 224`, `OperativeCharacterCoverEntry.cpp:24, 55`, `StealthModeSmokeCommand.cpp:89-130, 720-745` | Устранено подавление стелс-слоя монтажами M4 Cover Pack; стелс-клипы получили полный контроль над телом у стены. Проверено костным семплером. |

---

## 2. Детальный разбор каждого пункта

### Пункт 1: Вход автоматически ставит PASSIVE
- **Статус:** **PARTIAL / СЕРЬЁЗНЫЙ ДЕФЕКТ ПРИ ВЫХОДЕ**
- **Файлы:**
  - `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp:58-59` (коммит `d12ffdbd803a` в `interim`):
    ```cpp
    bStealthMode = true;
    bHasPostureOverride = true;
    PostureOverride = ESquadFirePosture::Passive;
    if (Stance != EOperativeStance::Prone)
    {
        SetStance(EOperativeStance::Crouching);
    }
    ```
- **Анализ реализации:**
  - При вызове `SetStealthMode(true)` боец гарантированно переводится в режим огня `ESquadFirePosture::Passive` (`bHasPostureOverride = true`). Это предотвращает срыв скрытности случайным открытием огня по проходящему патрулю.
- **Обнаруженный баг (Bug 1-A):**
  - **Триггер:** Любой выход из стелса (`SetStealthMode(false)`) — по нажатию хоткея игроком, по подтверждённому обнаружению врагом, по выстрелу, по спринту или при получении ранения/нокдауна.
  - **Ожидаемое поведение:** При выходе из стелса боец должен либо восстановить прежний режим огня, либо сбросить оверрайд (`bHasPostureOverride = false`), возвращаясь к общему режиму отряда (Aggressive / Defensive), чтобы иметь возможность открыть ответный огонь при обнаружении.
  - **Фактическое поведение:** В блоке `else` функции `SetStealthMode` (`OperativeCharacterStealth.cpp:65-68`):
    ```cpp
    else
    {
        bStealthMode = false;
    }
    ```
    Поле `bHasPostureOverride` **не сбрасывается**, а `PostureOverride` остаётся равным `Passive`.
  - **Последствия для геймплея:** Раскрытый боец остаётся в стойке `Passive` навсегда. Враги (ЧВК или мутанты) расстреливают его в упор, а он даже не пытается отстреливаться, пока игрок вручную не прожмёт хоткей смены стойки (`,`, `.`, `/`) или не отдаст прямой приказ на атаку.
  - **Рекомендация для исправления:** 
    В `AOperativeCharacter` сохранять `bPreStealthPostureOverride` и `PreStealthPosture` при входе в стелс, либо в `SetStealthMode(false)` выполнять:
    ```cpp
    bHasPostureOverride = false;
    PostureOverride = FirePostureRules::DefaultPosture;
    ```

---

### Пункт 2: Разрешить ползать в стелсе (Crawl allowed)
- **Статус:** **DONE**
- **Файлы:**
  - `Source/CodexTactics/Private/Characters/OperativeCharacterStance.cpp:42` (коммит `d12ffdbd803a`):
    ```cpp
    if (IsInStealthMode() && NewStance == EOperativeStance::Standing)
    {
        NewStance = EOperativeStance::Crouching; // cover entry must not stand a stealthed operative up
    }
    ```
    (Удалена ранее существовавшая ветка `if (NewStance == EOperativeStance::Prone) SetStealthMode(false);`).
  - `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp:60-63, 151`:
    - В `SetStealthMode(true)` стойка меняется на `Crouching` только если боец уже не лежал: `if (Stance != EOperativeStance::Prone) SetStance(EOperativeStance::Crouching);`.
    - В `UpdateStealthMode()` удалена проверка `Stance == EOperativeStance::Prone`, которая ранее принудительно сбрасывала стелс.
  - `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:43, 51`:
    - В анимационном слое: `const bool bActive = ... && !bIsProne;` и `if (... || bIsProne) { StealthWeight = 0.f; }`.
- **Анализ реализации:**
  - Поскольку в паке `StealthLocomotion` нет ассетов ползания по-пластунски, стелс-слой обнуляет свой вес при `bIsProne == true`. При этом персонаж полностью сохраняет игровой статус `bStealthMode == true`, полупрозрачный шейдер-призрак (`RefreshStealthVisuals()`) и сниженный радиус обнаружения/шума. Анимацию тела бесшовно подхватывает мокап-пак ползания `Crawl_MocapAnimPack`.
- **Баги/замечания:** Отсутствуют. Вход в укрытие лёжа (`OperativeCharacterCover.cpp:305-310`) корректно сохраняет положение лежачего бойца.

---

### Пункт 3: Подтверждённое обнаружение отменяет режим и шейдер
- **Статус:** **DONE**
- **Файлы:**
  - `Source/CodexTactics/Private/Characters/EnemyCharacterPerception.cpp:188-194` (`wip.patch`):
    ```cpp
    FPatrolAlertInput Input;
    Input.bSeesOperative = Seen != nullptr && PatrolSuspicion >= 1.f;
    if (Input.bSeesOperative && SeenOperative)
    {
        SeenOperative->SetStealthMode(false);
    }
    ```
  - `Source/CodexTactics/Private/Combat/TacticalSightSubsystem.cpp:897-902` (`wip.patch`):
    ```cpp
    const bool bVisual = !bCoverHidden && Distance <= EnemySightCm * RangeScale && HasClearSight(Eye, ProfilePoint(*Operative));
    const bool bPerceives = bVisual || IsDemasked(Operative) || Distance <= SightRules::EnemyHearingRadius(Operative->GetStance()) * NoiseScale;
    if (bVisual)
    {
        Operative->SetStealthMode(false);
    }
    ```
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:653-656` (`wip.patch`):
    ```cpp
    if (bEye && TargetOperative)
    {
        TargetOperative->SetStealthMode(false);
    }
    ```
  - `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp:65-71, 73-138`:
    - `SetStealthMode(false)` вызывает `RefreshStealthVisuals()`, который восстанавливает оригинальные материалы из `StealthOriginalMaterials` и очищает оверлейный слот сетки (`SetOverlayMaterial(nullptr)`).
- **Анализ реализации:**
  - Разделено обнаружение по зрению и по слуху: шаги/звуки настораживают патруль, но **не сбрасывают** стелс до тех пор, пока подозрение не заполнится до 100% (`PatrolSuspicion >= 1.f`) при прямом зрительном контакте.
  - Успешно восстанавливаются все материалы на скелетной сетке бойца и на оружии (`BodyMesh`, `WeaponMesh`, `WeaponMeshParts`).
- **Обнаруженные баги/замечания:**
  - **Bug 3-A (Следствие бага 1-A):** При сбросе стелса боец остаётся в пассивной стойке огня и не реагирует на открывшего огонь противника.
  - **Bug 3-B (Всеракурсный сброс в бою у монстров):** В `TacticalSightSubsystem.cpp:897` проверка `HasClearSight(Eye, ProfilePoint(*Operative))` не учитывает сектор обзора (FOV), поэтому монстр, уже находящийся в состоянии боя (`bBelief == true`), мгновенно срывает стелс со стоящего у него за спиной оперативника на дистанции `EnemySightCm * RangeScale`. (В режиме исследования `TickPatrolPerception` это не проявляется, так как там проверяется конус взгляда через `FindVisibleOperative`).

---

### Пункт 4: Работа против ЧВК (PMC)
- **Статус:** **DONE**
- **Файлы:**
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:644-657` (`wip.patch`):
    ```cpp
    AOperativeCharacter* TargetOperative = Cast<AOperativeCharacter>(Target);
    const float Distance = FVector::Dist2D(Observer->GetActorLocation(), Target->GetActorLocation());
    const bool bStealth = TargetOperative && TargetOperative->IsInStealthMode();
    const float Range = Sight->GetSightRangeCm(*Observer) * StealthModeRules::DetectionRangeScale(bStealth, StealthModeRules::GetConfig());
    const bool bHidden = TargetOperative && TargetOperative->IsHiddenInCoverFrom(Observer->GetActorLocation());
    bEye = !bHidden && (!bStealth || Distance <= Range)
        && ((SightRules::IsWithinTouchReveal(Distance) && Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Observer), Profile))
            || (bAllRound ? Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Observer), Profile) : Sight->CanOperativeSee(*Observer, Profile)));
    if (bEye && TargetOperative)
    {
        TargetOperative->SetStealthMode(false);
    }
    ```
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:692-696` (`wip.patch`):
    ```cpp
    const AOperativeCharacter* TargetOperative = Cast<AOperativeCharacter>(Target);
    Known.bSeenNow = bEye || (Contact == SightRules::EContact::Seen && (!TargetOperative || !TargetOperative->IsInStealthMode()));
    ```
  - `Source/CodexTacticsDev/Private/Debug/StealthDetectionSmokeCommand.cpp:140-155` (`new.tar`):
    - Добавлен автоматический смоук-тест, тестирующий именно PMC: боец ставится на дистанции `0.45 * Range` (между радиусом стелса 0.35 и обычным 1.0) и проверяется, что без стелса ЧВК его видит, а при активации стелса цель становится скрытой.
- **Анализ реализации:**
  - Причина исходной проблемы: ранее ЧВК вызывали `Sight->CanOperativeSee(*Observer, Profile)`, который ничего не знал о `StealthModeRules` и проверял дальность по обычной шкале (~25-30 метров). Поэтому для бойцов ЧВК стелс игрока вообще не действовал.
  - Codex корректно внедрил `DetectionRangeScale` и проверку скрытности в укрытии `IsHiddenInCoverFrom`. Теперь ЧВК замечают крадущегося бойца только на сокращённой дистанции либо при тактильном контакте (дистанция до 2 метров).

---

### Пункт 5: Огонь врагов только по последней видимой точке
- **Статус:** **DONE**
- **Файлы:**
  - `Source/CodexTactics/Public/AI/PmcSquadSubsystem.h:188-196` (`wip.patch`):
    - Добавлены поля состояния бойца ЧВК: `RecentFireTarget`, `LostFireTarget`, `LostFirePoint`, `LostFireUntil`.
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:680-690` (`wip.patch`):
    - Фиксация координат последнего контакта:
      ```cpp
      const FVector ContactLocation = bEye ? Target->GetActorLocation() : (bNewFlash ? *Flash : (Previous ? Previous->LastKnown : Target->GetActorLocation()));
      Known.LastKnown = ContactLocation;
      ```
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:740-779` (`wip.patch`):
    - Логика автоматического подавления точки потери видимости:
      ```cpp
      AActor* Recent = FighterState.RecentFireTarget.Get();
      const FKnownTarget* Known = Recent ? Team.Known.Find(Recent) : nullptr;
      if (Known && !Known->bSeenNow && FighterState.LostFireUntil < 0.0)
      {
          const FVector Point = Known->LastKnown;
          const bool bRecent = Now - Observer->GetLastShotTime() <= Team.Weights.ContactMemorySeconds
              && Now - Known->LastSeenTime <= Team.Weights.ContactMemorySeconds;
          if (bRecent && !Observer->HasSuppressOrder()
              && Observer->HasUsefulFireLine(Point + FVector(0.f, 0.f, SuppressionRules::GetConfig().OrderAimHeightCm))
              && Observer->OrderSuppress(nullptr, Point))
          {
              FighterState.Order = EPmcOrder::Suppress;
              FighterState.LostFireTarget = Recent;
              FighterState.LostFirePoint = Point;
              FighterState.LostFireUntil = Known->LastSeenTime + Team.Weights.ContactMemorySeconds;
          }
          FighterState.RecentFireTarget.Reset();
      }
      ```
  - `Source/CodexTactics/Private/AI/PmcCommanderRules.cpp:171` (`wip.patch`):
    - Правило скоринга командира ЧВК разрешает продолжать приказ `Suppress` по запомненной цели без прямой видимости: `(F.RunningOrder == EPmcOrder::Suppress && F.Threat.SecondsSince <= W.ContactMemorySeconds ? Step(F.bHasThreat) : SeenNow)`.
  - `Source/CodexTacticsTests/Private/AI/PmcCommanderRulesTest.cpp:74-95` (`wip.patch`):
    - Unit-тест `FPmcCommanderRememberedSuppressTest` проверяет удержание подавления по памяти, блокировку лучом через стену и истечение таймера памяти.
- **Обнаруженные баги/замечания:**
  - **Bug 5-A (Потеря подавления из-за геометрии угла):** Если оперативник забегает за угол, то точка `LastKnown` может оказаться впритирку к грани стены. Метод `HasUsefulFireLine(Point + FVector(0, 0, AimHeight))` стреляет лучом из дула оружия ЧВК в эту точку. Если край укрытия срезает луч, приказ `OrderSuppress` не активируется, и ЧВК просто прекращает стрельбу, не стреляя в угол.
  - **Поведение мутантов:** Механика стрельбы на подавление распространяется только на вооружённых бойцов (ЧВК). Мутанты в `TacticalSightSubsystem:906` просто фиксируют точку `Location = ContactLocation`, прекращая преследование смещающегося скрытого персонажа и направляясь к месту, где его видели последний раз. Это соответствует дизайну.

---

### Пункт 6: Реальные позы прижимания к стене и выглядывания (`StealthLocomotion`)
- **Статус:** **DONE**
- **Файлы:**
  - `Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp:1874, 1907-1930, 2362` (`wip.patch`):
    - В `PlayCoverMontage` и `PlayCoverOneShot`:
      `if (!Clip || (BoundOperative.IsValid() && BoundOperative->IsInStealthMode())) return nullptr;`
    - В `UpdateCoverLayer`:
      ```cpp
      if (Operative.IsInStealthMode())
      {
          if (UAnimMontage* Loop = CoverLoopMontage.Get()) { Montage_Stop(0.f, Loop); }
          if (UAnimMontage* OneShot = CoverOneShotMontage.Get()) { Montage_Stop(0.f, OneShot); }
          CoverLoopMontage.Reset();
          CoverLoopClip.Reset();
          CoverOneShotMontage.Reset();
          CoverPendingFireClip.Reset();
          CoverPeekStanceMontage.Reset();
          bCoverEnterPlaying = false;
          bCoverPeekStanceEntry = false;
          bCoverInFirePose = false;
          ReleasePeekStanceYawLock();
          ReleasePeekApproachYawLock();
          return;
      }
      ```
  - `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp:505` (`wip.patch`):
    - `WantsRunCornerPeekPreTurn()` отключается в стелсе (`!IsInStealthMode()`).
  - `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp:144, 224` (`wip.patch`):
    - `bPeekStanceArrival` и `bHasEnterClip` выключаются в режиме стелса.
  - `Source/CodexTactics/Private/Characters/OperativeCharacterCoverEntry.cpp:24, 55, 57` (`wip.patch`):
    - `bCoverSwitchPending` сбрасывается для стелса: вход в укрытие передаётся родным клипам `StealthLocomotion`.
  - `Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:89-130, 720-745` (`wip.patch`):
    - Внедрена валидация костей (`SampleStealthHead`): проверяется выдвижение головы за угол укрытия (`HeadPastEdge > 0.f`, `EdgeExtension > 3.f`) и отсутствие запущенных монтажей из M4 Cover Pack (`State.WallCoverMontageFrames == 0`).
- **Анализ реализации:**
  - Точный корень проблемы, обнаруженный Codex: базовая система укрытий M4 Cover Pack при подходе к стене запускала свои тяжеловесные монтажи входа (`CoverStandEnter` / `CoverCrouchEnter`). Они занимали слот `FullBody`, из-за чего флаг `bBodyBusy` сбрасывал вес стелс-слоя в 0, либо анимации M4 накладывались поверх.
  - Решение Codex полностью изолирует стелс от монтажей укрытий M4: все данные о слотах и геометрии укрытий берутся штатно, но визуал и позирование костей на 100% отдаются клипам `Stealth_LeftWall_Idle_IP`, `Stealth_RightWall_Idle_IP`, `Stealth_LeftWall_Lean_IP` и `Stealth_RightWall_Lean_IP`.
- **Замечания / Ограничения:**
  - В паке `StealthLocomotion` нет отдельного клипа скользящего «входа» в укрытие («slide into wall»), поэтому при подходе персонаж доходит шагом/рысцой стелса и начинает анимацию остановки у стены `Stop_IP` / `Idle_IP`. Блендинг костей в `BlendStealthLayer` сглаживает это, но кинематографического скольжения в укрытие (как в M4) здесь нет по составу самого ассет-пака.

---

## 3. Резюме дефектов и рекомендации для Codex

1. **Критический дефект (Блокер для боя):** Сброс режима `Passive` при выходе из стелса.
   - **Файл:** `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp:65-68`.
   - **Решение:** В ветке `else` функции `SetStealthMode` обязательно добавить:
     ```cpp
     bHasPostureOverride = false;
     ```
     (или восстанавливать сохранённый до входа в стелс режим огня). Без этого бойцы игрока после раскрытия стоят под огнём как мишени.

2. **Состояние сборки и тестов:**
   - Патч `wip.patch` и новый файл `StealthDetectionSmokeCommand.cpp` синтаксически и архитектурно согласованы с `interim/2026-11-12`.
   - В `Scripts/test_map.json` новый смоук зарегистрирован во всех необходимых списках и группах.
   - Юнит-тест `FPmcCommanderRememberedSuppressTest` в `PmcCommanderRulesTest.cpp` написан чисто и следует стандартам тестового фреймворка проекта.

3. **Готовность к фиксации:**
   - После исправления сброса `bHasPostureOverride` в `OperativeCharacterStealth.cpp` ветку можно безопасно коммитить в `interim`.
