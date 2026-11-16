# Отчет по задаче 068: Альтернативная походка с дробовиком и устранение скольжения ног (Shotgun Alternate Walk & Feet Slide)

## Резюме
Задача 068 полностью выполнена.
1. **Разделение походки бойцов (Carrier Split):**
   - Боец Rank 0 (первый по squad index или одиночный боец с дробовиком, Commander) передвигается на ногах M16 (анимационный граф ABP).
   - Боец Rank 1 (второй боец в отряде с дробовиком, Engineer) передвигается на ногах анимационного пака дробовика (`Loop_Root`), исключая эффект «близнецов».
   - Этап 21 теста `ShotgunSmoke` успешно подтверждает: Commander — ноги ABP, Engineer — ноги пака.

2. **Устранение скольжения ног (Speed Match):**
   - В `weapon_anim_sets.json` вынесен параметр `"legs_abp_walk_speed": 120.0`.
   - В `OperativeAnimInstanceShotgun.cpp` при ходьбе на ногах ABP скорость блендспейса фиксируется на `StandBlendSpeed = 120.0` и динамически масштабируется `StandPlayRate = LocomotionSpeed / 120.0`, устраняя паразитное подмешивание бега.

3. **Окно stance по физической фазе контакта и скорости стопы (Claude #530):**
   - Устранены захардкоженные номера кадров (`LowerFrames`).
   - Реализовано единое правило для обоих типов походки: контакт с землей (`Height < MinFootZ[Lower] + 2.5f`), относительное движение стопы назад против направления движения тела (`FootRelForward < 0.f`) и скорость стопы в мире `< Speed * 0.85f`.
   - Добавлены жесткие проверки: окно контакта не менее 0.5 с и не менее 15 кадров, иначе FAIL.

4. **Результаты дрейфа стоп (ShotgunSmoke PASS):**
   - **Rank 0 Ходьба (ноги ABP):** дрейф **24%** (42.0 см/с) при лимите **35%**, окно 0.98 с (59 кадров) — **PASS**.
   - **Rank 0 Спринт (ноги ABP):** дрейф **11%** (77.8 см/с) при лимите **35%**, окно 0.62 с (37 кадров) — **PASS**.
   - **Rank 1 Ходьба (ноги пака):** дрейф **5%** (8.5 см/с) при лимите **20%**, окно 3.58 с (215 кадров) — **PASS**.

5. **Тестирование и валидация:**
   - `build.ps1` — Succeeded (exit 0).
   - `test.ps1 -Filter CodexTactics.Animation.WeaponLayer` — 11 passed, 0 failed.
   - `smoke_parallel.ps1 -Commands ShotgunSmoke,ShotgunCoverSmoke,PistolSmoke,WeaponSelectorSmoke` — 4 run, 0 failed (PASS).
   - `FormationJitterSmoke` — FAIL (известная проблема main, follower wobble, зафиксировано по инструкции без правок).

---

## Таблица утверждений и доказательств (Claim | Evidence)

| Утверждение (Claim) | Доказательство (Evidence) |
|---|---|
| Целевая скорость ходьбы на ногах ABP вынесена в конфигурацию JSON без магических чисел | `Content/Data/Weapons/weapon_anim_sets.json:12` (`"legs_abp_walk_speed": 120.0`) |
| В структуре `FShotgunAnimSet` добавлено поле целевой скорости ходьбы `LegsAbpWalkSpeed` | `Source/CodexTactics/Public/Characters/OperativeAnimInstance.h:1901` |
| Поле `LegsAbpWalkSpeed` считывается из блока JSON конфигурации оружия | `Source/CodexTactics/Private/Characters/OperativeAnimInstanceShotgun.cpp:346` |
| Добавлены методы проверки походки `DoesShotgunWalkUseAbpLegs` и немедленной инвалидации таймера `InvalidateShotgunWalkVariant` (`-1.f`) | `Source/CodexTactics/Public/Characters/OperativeAnimInstance.h:1456`, `Source/CodexTactics/Private/Characters/OperativeAnimInstanceShotgun.cpp:605,612` |
| Реализован speed matching: при скорости выше шага фиксируется чистый шаг (StandBlendSpeed=120) и масштабируется StandPlayRate | `Source/CodexTactics/Private/Characters/OperativeAnimInstanceShotgun.cpp:958` |
| При замедлении ниже 120 см/с параметры StandBlendSpeed и StandPlayRate не зажимаются, обеспечивая плавное затухание в idle | `Source/CodexTactics/Private/Characters/OperativeAnimInstanceShotgun.cpp:955` |
| В ShotgunSmoke добавлен безопасный сброс экипировки отряда `RestoreSquad` с удалением выданного дробовика из AvailableWeapons | `Source/CodexTacticsDev/Private/Debug/ShotgunSmokeCommand.cpp:187-200` |
| Окно stance переведено на физическую фазу контакта и скорость стопы без номеров кадров согласно #530 | `Source/CodexTacticsDev/Private/Debug/ShotgunSmokeCommand.cpp:425-440` |
| Введена строгая проверка длительности окна контакта (не менее 0.5 с и >= 15 кадров) на всех стадиях | `Source/CodexTacticsDev/Private/Debug/ShotgunSmokeCommand.cpp:712,827` |
| Добавлены стадии 20-22 для валидации второго носителя дробовика (Rank 1, ноги пака) | `Source/CodexTacticsDev/Private/Debug/ShotgunSmokeCommand.cpp:742-845` |
| Инкрементальная сборка проекта успешна (exit code 0) | `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1` -> `Result: Succeeded`, `Exit code 0` |
| Модульные тесты WeaponLayer проходят без ошибок (11 из 11) | `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter CodexTactics.Animation.WeaponLayer` -> `11 tests, 0 failed` |
| Смоук-тест ShotgunSmoke полностью проходит со всеми проверками обеих походок | `powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands ShotgunSmoke` -> `ShotgunSmoke PASS 83.5s` |
| Дрейф стопы Rank 0 при ходьбе (ноги ABP) составляет 24% при лимите 35% | `Saved/Logs/Smoke-ShotgunSmoke.log`: `Smoke ok : 2b: walking, no foot sliding: the planted foot drifts 42.0 cm/s on the ground (24 % of his 175 cm/s; limit 35 %, the M16 (ABP) legs)` |
| Дрейф стопы Rank 0 при спринте составляет 11% при лимите 35% | `Saved/Logs/Smoke-ShotgunSmoke.log`: `Smoke ok : 2b: sprinting, no foot sliding: the planted foot drifts 77.8 cm/s on the ground (11 % of his 683 cm/s; limit 35 %, the M16 (ABP) legs)` |
| Разделение походки носителей подтверждено тестом | `Saved/Logs/Smoke-ShotgunSmoke.log`: `Smoke ok : 2b: rank 0 carrier (Commander) walks on ABP legs`, `Smoke ok : 2b: rank 1 carrier (Engineer) walks on shotgun pack legs` |
| Дрейф стопы Rank 1 при ходьбе (ноги пака) составляет 5% при лимите 20% | `Saved/Logs/Smoke-ShotgunSmoke.log`: `Smoke ok : 2b: rank 1 walking, no foot sliding: planted foot drifts 8.5 cm/s (5 % of 174 cm/s; limit 20 %, the pack's legs)` |
| Смежные смоук-тесты проходят без регрессий (4/4 PASS) | `smoke_parallel.ps1 -Commands ShotgunSmoke,ShotgunCoverSmoke,PistolSmoke,WeaponSelectorSmoke` -> `4 run, 0 failed (PASS)` |
| Смоук-тест FormationJitterSmoke проверен согласно п. 41 спецификации | `Saved/Logs/Smoke-FormationJitterSmoke.log`: падает на основном коде (D-1 follower wobble), оставлен без изменений |

---

## Сравнение дрейфа опорной ноги (Drift %) до и после

| Режим / Носитель | До изменений (база) | После изменений | Замер окна (время / кадры) | Лимит теста | Результат |
|---|---|---|---|---|---|
| **Rank 0 Ходьба (ноги ABP)** | 78% (136.2 см/с) | **24%** (42.0 см/с) | 0.98 с / 59 кадров | 35% | **PASS** |
| **Rank 0 Спринт (ноги ABP)** | 57% (371.7 см/с, ноги заморожены) | **11%** (77.8 см/с, ноги анимированы) | 0.62 с / 37 кадров | 35% | **PASS** |
| **Rank 1 Ходьба (ноги пака)** | не тестировался | **5%** (8.5 см/с) | 3.58 с / 215 кадров | 20% | **PASS** |

---

## Что НЕ делалось и почему

1. **Не изменялись ассеты `.uasset` и файлы Animation Blueprint (ABP):**
   Согласно правилам проекта и тексту спецификации, редактирование бинарных ассетов `.uasset` агенту запрещено. Все исправления выполнены строго на уровне C++ и JSON.
2. **Не повышались лимиты допустимого дрейфа (35% и 20%):**
   Пороги сохранены в точности согласно базовым требованиям. Исправление достигнуто за счет синхронизации скорости анимации и физической калибровки окна контакта стопы.
3. **Не модифицировался `FormationJitterSmoke`:**
   В пункте 41 спецификации прямо указано: тест падает на main из-за известного бага `D-1 follower wobble`, запускать и рапортовать, но не чинить в рамках 068.

---

## Ветка и коммиты передачи Клоду
- **Ветка:** `gemini/068-shotgun-walk`
- **Базовый коммит:** `origin/wip/2026-11-15-pause` (`526aad44`)
- **Коммиты:**
  - `1253b29d` — `docs(anim): plan for task 068 shotgun alternate walk feet (068)`
  - `3dae4a5c` — `wip(anim): shotgun alternate walk speed match and carrier split validation (068)`
  - `bcf9c80e` — `fix(anim): stance contact window without frame counts and two-carrier drift verification (068)`
  - Финальный коммит отчета
- **Следующая задача в очереди:** 071 (`docs/port/gemini/inbox/071-panic-flee-speed.md`).
