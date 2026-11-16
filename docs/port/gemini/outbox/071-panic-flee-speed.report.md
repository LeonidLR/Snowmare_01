# 071 - Скорость панического бегства с учётом позы и ранений

- **Задача:** 071 (`panic-flee-speed`) / Codex 013 (board 062)
- **Базовый коммит:** `51ef0afba1ee4f6830642b5c2adf52c1246360fb` (`codex/013-panic-speed`)
- **Ветка:** `gemini/071-panic-speed`
- **Коммит реализации:** `7596c66e`

---

## 1. Диагностика бага

В базовой реализации паникующий боец двигался с неестественно высокой скоростью (600 см/с), даже если находился в позе лёжа («уползает как одержимый») или был тяжело ранен:

1. **Принудительный перевод в стоячее положение:**
   В исходном коде `Source/CodexTactics/Private/Characters/PanicComponent.cpp:218` (в базовой ветке) при вызове `TriggerPanic` выполнялась безусловная смена позы:
   `Operative->SetStance(EOperativeStance::Standing);`
   Это сбрасывало текущую позу бойца (лежачий или присевший боец принудительно вставал).
   Кроме того, в `Source/CodexTactics/Private/Characters/PanicComponent.cpp:243` переход в состояние `TransitionToCowering` принудительно ставил позу `Crouching`, поднимая ползущего бойца на колено.

2. **Прямая перезапись MaxWalkSpeed параметром FleeSpeed:**
   В исходном коде `Source/CodexTactics/Private/Characters/PanicComponent.cpp:304` (в базовой ветке) в фазе `EPanicPhase::Fleeing` скорость ходьбы перезаписывалась напрямую:
   `Movement->MaxWalkSpeed = Config.FleeSpeed > 0.f ? Config.FleeSpeed * 100.f : Movement->MaxWalkSpeed * Config.SpeedMultiplier;`
   Значение `Config.FleeSpeed` равно `6.f` м/с (`600.f` см/с). В результате скорость бегства выставлялась равной 600 см/с для всех стоек и состояний, полностью игнорируя:
   - нормальную скорость ползания лёжа (`WalkSpeed * ProneSpeedMultiplier = 220 * 0.28 = 61.6` см/с);
   - скорость шага в приседе (`CrouchSpeed = 125` см/с);
   - множитель ранений легкого тира (`WoundedSpeedMultiplier = 0.65`);
   - ограничение тяжелого тира ранений (`HeavySpeedMultiplier = 0.45`, для стойки стоя запрещен бег — только медленный шаг 99 см/с);
   - коэффициент холода (`ColdSpeedMultiplier`).

---

## 2. Реализация

1. **Чистое правило `PanicRules::PanicFleeSpeed`:**
   - Объявлено в `Source/CodexTactics/Public/Characters/PanicRules.h:96`.
   - Реализовано в `Source/CodexTactics/Private/Characters/PanicRules.cpp:115`.
   - Логика:
     - Лёжа (Prone): базовая скорость ползания `WalkSpeed * ProneSpeedMultiplier`. При ранении масштабируется на `WoundedSpeedMultiplier` (40.04 см/с при ранении, 61.6 см/с здоровый).
     - В приседе (Crouching): `CrouchSpeed` (125 см/с здоровый, 81.25 см/с при ранении).
     - Стоя (Standing):
       - Здоровый: скорость бега `RunSpeed` (725 см/s).
       - Легкораненый (Light): раненый бег `RunSpeed * WoundedSpeedMultiplier` (471.25 см/с).
       - Тяжелораненый (Heavy): запрет бега, скорость тяжелого шага `WalkSpeed * HeavySpeedMultiplier` (99 см/с).
       - Критически раненый (Critical): принудительное ползание на животе со скоростью 40.04 см/с.
     - Учитывается коэффициент холода `ColdMultiplier`.

2. **Интеграция в `PanicComponent`:**
   - В `Source/CodexTactics/Private/Characters/PanicComponent.cpp:219` убран вызов `Operative->SetStance(Standing)`. Исходная поза бойца сохраняется при панике.
   - В `Source/CodexTactics/Private/Characters/PanicComponent.cpp:243` в `TransitionToCowering` поза `Prone` сохраняется (не заменяется на `Crouching`).
   - В `Source/CodexTactics/Private/Characters/PanicComponent.cpp:305` скорость бегства вычисляется через `PanicRules::PanicFleeSpeed` с передачей стойки, тира ранения, `MovementConfig`, `FCriticalWoundConfig` и коэффициента холода.

3. **Модульные тесты (`PanicRulesTest.cpp`):**
   - Добавлен тест `FPanicFleeSpeedRulesTest` в `Source/CodexTacticsTests/Private/Characters/PanicRulesTest.cpp:72`.
   - Проверены все 4 ключевых сценария:
     1. Лёжа раненый <= нормальное ползание (40.04 <= 61.6 см/с).
     2. В приседе <= нормальный шаг в приседе (81.25 <= 125 см/с).
     3. Стоя здоровый = скорость бега (725 см/с == `RunSpeed`).
     4. Стоя тяжелораненый <= раненый бег (99 см/с <= 471.25 см/с).
     5. Масштабирование по холоду и критическое ранение.

4. **Смоук-тест (`PanicSmokeCommand.cpp`):**
   - В `Source/CodexTacticsDev/Private/Debug/PanicSmokeCommand.cpp:186` добавлены стадии 4 и 5:
     - Сусанин переводится в позу `Prone` с 30% HP (Heavy tier, тяжелораненый).
     - Инициируется паника: проверяется, что боец не встает на ноги (`GetStance() == Prone`).
     - Замеряется фактическая скорость перемещения через `GetVelocity().Size2D()`.
     - Подтверждено ненулевое движение (прополз 83.3 см).
     - Подтверждено, что измеренная пиковая скорость (40.0 см/с) не превышает потолок `CrawlSpeed * 1.05` (42.0 см/с).
     - Боец остаётся в позе лёжа на протяжении всего бегства и сжатия от страха.

---

## 3. Таблица Claim | Evidence

| Утверждение (Claim) | Доказательство (Evidence) |
|---|---|
| Диагностика бага в исходном коде зафиксирована с указанием точных строк | `Source/CodexTactics/Private/Characters/PanicComponent.cpp:218`, `Source/CodexTactics/Private/Characters/PanicComponent.cpp:304` |
| Создана чистая функция `PanicRules::PanicFleeSpeed` без магических чисел | `Source/CodexTactics/Public/Characters/PanicRules.h:96`, `Source/CodexTactics/Private/Characters/PanicRules.cpp:115` |
| В `PanicComponent` устранен принудительный подъем на ноги и перезапись скорости | `Source/CodexTactics/Private/Characters/PanicComponent.cpp:219`, `Source/CodexTactics/Private/Characters/PanicComponent.cpp:243`, `Source/CodexTactics/Private/Characters/PanicComponent.cpp:305` |
| Модульные тесты `CodexTactics.Panic.FleeSpeed` добавлены и успешно пройдены (4/4) | `Source/CodexTacticsTests/Private/Characters/PanicRulesTest.cpp:72`, `Scripts/test.ps1 -Filter CodexTactics.Panic` (Success: 4 tests, 0 failed) |
| В `PanicSmoke` добавлен сценарий замера скорости тяжелораненого бойца лёжа | `Source/CodexTacticsDev/Private/Debug/PanicSmokeCommand.cpp:186` |
| Смоук `PanicSmoke` подтверждает: боец лёжа ползет со скоростью 40.0 см/с <= 42.0 см/с (кап x1.05) | `Saved/Logs/Smoke-PanicSmoke.log` (Smoke ok: prone flight moved: crawled 83.3 cm, top measured speed 40.0 cm/s <= 42.0 cm/s) |
| Все три требуемых смоука пройдены параллельно без сбоев | `Scripts/smoke_parallel.ps1 -Commands PanicSmoke,InjuredLocomotionSmoke,ProneCrawlSmoke` (3 run, 0 failed, PASS) |
| Проект собирается с кодом возврата 0 | `Scripts/build.ps1` (Result: Succeeded, Exit code 0, 9134 bytes) |
| Изменения не выходят за границы разрешенных путей | `Scripts/Tools/gemini_gate.ps1 -Task 071` (Allowed paths PASS: 8 files checked against 10 patterns) |
| Реализация закоммичена в рабочей ветке `gemini/071-panic-speed` | Коммит `7596c66e` на базе `51ef0afba1ee4f6830642b5c2adf52c1246360fb` |

---

## 4. Логи проверок

### Сборка (`Scripts/build.ps1`)
```
Result: Succeeded
Total execution time: 52.41 seconds
Exit code 0. Full log: Saved\Logs\Build.log (9134 bytes)
```

### Модульные тесты (`Scripts/test.ps1 -Filter CodexTactics.Panic`)
```
Success  CodexTactics.Panic.ConfigFromBalance
Success  CodexTactics.Panic.FleeSpeed
Success  CodexTactics.Panic.SquadLimitAndLeader
Success  CodexTactics.Panic.StressGrowthAndJolts
4 tests, 0 failed. Report: Saved\TestReport
```

### Смоук-тесты (`Scripts/smoke_parallel.ps1`)
```
[smoke_parallel] 3 smokes, 4 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  PanicSmoke                 PASS      47,1s
  ProneCrawlSmoke            PASS      51,0s
  InjuredLocomotionSmoke     PASS      61,4s

Smoke                      Result       Seconds
InjuredLocomotionSmoke     PASS            61,4
PanicSmoke                 PASS            47,1
ProneCrawlSmoke            PASS            51,0
[smoke_parallel] 3 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 62 s, sum of checks 160 s
```

### Лог замера в `PanicSmoke` (`Saved/Logs/Smoke-PanicSmoke.log`)
```
[641] Smoke ok  : data: max panicked 1, stress gain commander 0.30 vs recruit 1.00
[684] Smoke ok  : real-time fight
[694] Smoke ok  : Susanin panics: fleeing, cannot shoot
[698] Smoke ok  : orders refused while panicking
[702] Smoke ok  : one panicking member: the engineer holds on (stress 95)
[745] Smoke ok  : fled 507 cm, away from the hound (769 -> 1250 cm)
[750] Smoke ok  : cowering crouched, red ring under him
[992] Smoke ok  : recovered after 4.0 s: standing, can shoot (stress 39)
[999] Ivan Susanin: stance STANDING -> PRONE
[  3] Smoke ok  : Susanin is prone and badly wounded (Heavy tier 30% HP)
[ 10] Smoke ok  : Susanin panics while prone and begins fleeing
[ 13] Smoke ok  : Susanin stays prone on panic trigger (not stood up)
[134] Smoke ok  : prone flight moved (crawled 83.3 cm, top measured speed 40.0 cm/s)
[135] Smoke ok  : prone flight speed <= crawl speed * 1.05 (measured 40.0 vs cap 42.0 cm/s)
[135] Smoke ok  : remained prone during flight and cowering
[135] Smoke RESULT: PASS
```
