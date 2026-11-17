# Отчет 080: Устранение высокочастотного дрожания (jitter) прицеливания врагов и PMC при обнаружении отряда

**Ветка:** `gemini/080-enemy-jitter`  
**Базовый коммит:** `50e8e4a9` (main)  
**Коммит плана:** `5127d070 docs(ai): plan 080 enemy and marksman facing jitter fixes`  
**Коммит реализации:** `1868f3c8 fix(ai): add target switch hysteresis and facing dead band to prevent enemy jitter`  
**Задача:** `docs/port/gemini/inbox/080-enemy-facing-jitter.md` (баг дизайнера B17-5: враги, в особенности снайперы/стрелки, при обнаружении оперативников вибрируют влево-вправо).

---

## 1. Диагностика и воспроизведение (Pre-fix Reproduction)

### Причины бага:
1. **Отсутствие гистерезиса выбора цели:** В `AMarksmanEnemyCharacter::FindClosestOperative`, `EnemyAIRules::SelectTarget` и `UPmcSquadSubsystem::BuildFighterView` ближайшая цель выбиралась исключительно по минимальной евклидовой дистанции без порога переключения. При наличии двух оперативников на схожей дистанции микроколебания (дыхание, шаг, микро-смещение +/- 3-4 см) приводили к смене цели каждый кадр или тик (каждые ~0.15 с).
2. **Отсутствие угловой мертвой зоны (Facing Dead Band):** В `AEnemyCharacter::FaceYaw` поворот интерполировался через `FMath::RInterpTo` на каждом кадре без проверки минимального углового расхождения. Даже смещение на 0.1° вызывало изменение ориентации и смену знака угловой скорости (`DeltaYaw`), создавая визуальный эффект высокочастотной вибрации / микро-рывков.

### Воспроизведение до фикса:
Был разработан смоук-тест `EnemyFacingJitterSmoke` (`Source/CodexTacticsDev/Private/Debug/EnemyFacingJitterSmokeCommand.cpp`), размещающий стрелка на расстоянии 20 м перед двумя симметрично расположенными оперативниками с синусоидальным смещением +/- 4 см.
Результат прогона на исходном коде:
```
[JitterFlip] Marksman flip #1 at t=0.25: Yaw=-98.83, Delta=-2.03, Target=BP_Operative_C_1
[JitterFlip] Marksman flip #2 at t=0.40: Yaw=-99.85, Delta=1.54, Target=BP_Operative_C_0
...
[JitterFlip] Marksman flip #19 at t=3.05: Yaw=-99.77, Delta=-1.68, Target=BP_Operative_C_1
Smoke FAIL: Marksman facing jitter: 19 flips in 3.0s (6.23 flips/s, limit 2.50, max step 2.0 deg)
Smoke RESULT: FAIL
```
Частота смены направления поворота составила 6.23 флипа/с при лимите 2.50 флипа/с, что подтвердило баг B17-5.

---

## 2. Реализованные изменения

1. **Marksman (Снайпер / стрелок):**
   - В `Source/CodexTactics/Public/Characters/MarksmanAIRules.h` в `FMarksmanConfig` добавлены параметры:
     - `TargetSwitchHysteresisCm = 250.f` (кандидат должен быть ближе текущей цели минимум на 2.5 м).
     - `TargetMinHoldSeconds = 1.5f` (минимальное время удержания цели перед переключением).
     - `FacingDeadBandDeg = 1.5f` (угловая мертвая зона).
     - Объявлены и реализованы чистые функции `MarksmanAIRules::ShouldSwitchTarget` и `MarksmanAIRules::IsOutsideDeadBand` в `Source/CodexTactics/Private/Characters/MarksmanAIRules.cpp:88`.
   - В `AMarksmanEnemyCharacter`:
     - Добавлен таймер `TargetHoldTimer` в `Source/CodexTactics/Public/Characters/MarksmanEnemyCharacter.h`.
     - В `AMarksmanEnemyCharacter::FindClosestOperative` (`Source/CodexTactics/Private/Characters/MarksmanEnemyCharacter.cpp:465`) переключение цели разрешается только при соблюдении гистерезиса и минимального времени удержания.
     - В `BeginPlay` инициализирована угловая зона `FacingDeadBandDeg = MarksmanConfig.FacingDeadBandDeg`.

2. **Базовый Enemy (Frostbitten и др.):**
   - В `Source/CodexTactics/Public/Characters/EnemyCharacter.h` добавлено поле `FacingDeadBandDeg = 1.5f`.
   - В `AEnemyCharacter::FaceYaw` (`Source/CodexTactics/Private/Characters/EnemyCharacter.cpp:141`) добавлена проверка мертвой зоны: если расхождение угла `DeltaDeg < FacingDeadBandDeg`, поворот не выполняется, а флаг `bFacedThisTick = true` выставляется, предотвращая конфликт между логикой движения и прицеливания.
   - В `FEnemyAIConfig` (`Source/CodexTactics/Public/Characters/EnemyAIRules.h`) добавлены `TargetHysteresisFactor = 0.85f` и `FacingDeadBandDeg = 1.5f`.
   - В `EnemyAIRules::SelectTarget` (`Source/CodexTactics/Private/Characters/EnemyAIRules.cpp:126`) текущая цель получает бонус 15% к эффективной дистанции (`Effective *= Config.TargetHysteresisFactor`), предотвращая флаппинг между равноудаленными целями.

3. **PMC бойцы (`PmcCommanderRules` / `PmcSquadSubsystem`):**
   - В `FPmcWeights` (`Source/CodexTactics/Public/AI/PmcCommanderRules.h:250`) добавлены `TargetSwitchHysteresisCm = 200.f` и `FacingDeadBandDeg = 1.5f`.
   - Добавлена чистая функция `PmcCommanderRules::ShouldSwitchThreat` (`Source/CodexTactics/Private/AI/PmcCommanderRules.cpp:993`).
   - В `Content/Data/AI/pmc_commander.json:72` добавлены ключи `"target_switch_hysteresis_cm": 200` и `"facing_dead_band_deg": 1.5`.
   - В `Content/Data/AI/pmc_commander.bounds.json:168` добавлены диапазоны валидации для тюнера (`[50, 500]` и `[0.5, 5.0]`).
   - В `UPmcSquadSubsystem::BuildFighterView` (`Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:1289`) для текущей приоритетной цели применяется вычитание гистерезиса `EffectiveDistance = RealDistance - ThreatHysteresis`.

---

## 3. Таблица утверждений и доказательств (Claim | Evidence)

| Утверждение (Claim) | Доказательство (Evidence) |
| :--- | :--- |
| Воспроизведен баг B17-5 до внесения правок: Marksman совершает 19 смен знака угловой скорости за 3.0 с (6.23 флипа/с при лимите 2.50) | Лог запуска до фикса: `Saved/Logs/Smoke-EnemyFacingJitterSmoke.log` (FAIL, 19 flips in 3.0s, 6.23 flips/s) |
| Все параметры гистерезиса и dead band вынесены в структуры настроек и JSON-конфиги без магических чисел | `Content/Data/AI/pmc_commander.json:72`, `Content/Data/AI/pmc_commander.bounds.json:168`, `Source/CodexTactics/Public/Characters/MarksmanAIRules.h:46`, `Source/CodexTactics/Public/AI/PmcCommanderRules.h:250`, коммит `1868f3c8` |
| Добавлен модульный тест логики гистерезиса и dead band Marksman | `Source/CodexTacticsTests/Private/Characters/MarksmanAITest.cpp:98`, `test.ps1 -Filter CodexTactics.Characters.Marksman`: Success (1 tests, 0 failed) |
| Добавлен модульный тест логики гистерезиса угроз и чтения JSON PMC | `Source/CodexTacticsTests/Private/AI/PmcCommanderRulesTest.cpp:484`, `test.ps1 -Filter CodexTactics.AI.PmcCommander`: Success (17 tests, 0 failed) |
| Модульные тесты Marksman проходят на 100% | `test.ps1 -Filter CodexTactics.Marksman`: Success (5 tests, 0 failed) |
| Смоук-тест `EnemyFacingJitterSmoke` полностью проходит (PASS) для Marksman, Frostbitten и PMC бойца | `Saved/Logs/Smoke-EnemyFacingJitterSmoke.log`: Marksman 0 flips (0.00 flips/s), Frostbitten 1 flip (0.33 flips/s), PMC Fighter 0 flips (0.00 flips/s), Smoke RESULT: PASS (52.6s) |
| Регрессионный пакет смоук-тестов проходит без падений | `EnemyArchetypeSmoke`: PASS (49.7s), `EnemyPrioritySmoke`: PASS (52.4s), `PmcEncounterSmoke`: PASS (74.8s), `PmcStalemateSmoke`: PASS (57.8s) |
| Компиляция C++ завершается с кодом 0 | `Scripts/build.ps1` exit code 0 |

---

## 4. Что НЕ делалось и почему

1. **Не изменялись файлы анимаций и ABPs (`Content/Animations/**`, `EnemyAnimInstance`):** Баг заключался исключительно в логике выбора цели и интерполяции ориентации актора в C++. Внесение изменений в слои анимации не требовалось и строго запрещено регламентом допустимых путей (`Allowed paths: NOT EnemyAnimInstance / ABPs`).
2. **Не изменялись карты и ассеты (`Content/Maps/**`, `.uasset`):** Все проверки проводились на тестовой карте `L_MovementTest` со спавном тестовых акторов через подсистемы `UWaveSubsystem` и `UPmcSquadSubsystem`.
3. **Не ослаблялись пороги прохождения смоук-тестов:** Порог 2.50 флипа/с был сохранен без изменений; итоговые показатели (0.00 - 0.33 флипа/с) с огромным запасом укладываются в лимит.
