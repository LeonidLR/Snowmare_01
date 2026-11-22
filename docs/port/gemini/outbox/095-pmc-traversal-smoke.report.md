# Отчёт о выполнении задачи 095: PMC Traversal Smoke (Smart Enemies T-12)

**Дата:** 2026-11-22
**Модель/Исполнитель:** Gemini (Gemini 3.8 Flash)
**Ветка:** `gemini/095-pmc-traversal-smoke`
**Рабочее дерево:** `CodexTactics-gemday`
**Карта валидации:** `/Game/Maps/Generated/L_Gen_Depot_X4` (`-LevelJson=L_Gen_Depot_X4.json`)
**Команда смоука:** `CodexTactics.PmcTraversalSmoke`

---

## 1. Краткое резюме задачи

В соответствии с дорожной картой умных противников (Smart Enemies Roadmap, раздел T-12, `docs/design/gdd/smart-enemies-roadmap.md`) и техническим заданием `docs/port/gemini/inbox/095-pmc-traversal-smoke.md`:
1. Разработана и зарегистрирована C++ консольная команда `CodexTactics.PmcTraversalSmoke` в модуле `CodexTacticsDev` (`Source/CodexTacticsDev/Private/Debug/PmcTraversalSmokeCommand.cpp`).
2. Произведено тестирование физической проходимости всех типов связей карты `L_Gen_Depot_X4` одиночным вражеским оперативником (Team 1, PMC hostile, `SetTeamCommanded(1, false)`, управление отрядом игрока в режиме Solo Hold).
3. Все константы (радиусы прибытия, пороги застревания, таймауты этапов, смещения точек подхода и выборки) вынесены в структурированный JSON-конфиг `docs/port/gemini/pmc_traversal_config.json`.
4. Реализована детерминированная выборка (stride sampling со строгим порядком координат) для объемных наборов связей:
   - 120 связей перелезания (mantle) из 3107 существующих на карте;
   - 80 связей спрыгивания (drop) из 409 существующих на карте.
   Все лестницы (19 шт.) и вертикальные трапы высотой $\ge 7$ ступеней (23 шт.) протестированы в полном объеме (100%).
5. Реализована полная очистка состояния бойца между перемещениями (`AbortClimb` трапов, восстановление здоровья `Heal(1000)`, сброс коллизии и возвращение в стойку Standing), а также проекция целевых точек `NavGoal` на NavMesh, что исключило каскадные ложные застревания.
6. По результатам замеров текущий уровень проходимости составил: Stairs 68.4%, Ladders 52.2%, Mantles 1.7%, Drops 0.0%, что ниже целевого порога $\ge 95\%$. Смоук зарегистрирован как `knownFail: true` в `Scripts/test_map.json` с фиксацией базовых метрик и причин в поле `_why`.

---

## 2. Результаты тестирования проходимости (Baseline Metrics)

| Тип связи (Link Kind) | Пройдено / Всего в тесте | Процент успеха | Всего связей на карте X4 | Статус | Основная причина неудач |
|:---|:---:|:---:|:---:|:---:|:---|
| **Stairs (Лестничные марши)** | **13 / 19** | **68.4%** | 19 | **FAIL** | Застревание / таймаут на внутренних лестницах зданий (`stuck`) |
| **Ladders (Трапы $\ge 7$ ступеней)** | **12 / 23** | **52.2%** | 23 | **FAIL** | Застревание у люков крыш / галерей (`stuck (timed out)`) |
| **Mantles (Перелезания через укрытия)** | **2 / 120** | **1.7%** | 3107 | **FAIL** | `no path` (97.5%): отсутствие разрешения `bMantlePathAllowed` у Team 1 |
| **Drops (Спрыгивания с высоты)** | **0 / 80** | **0.0%** | 409 | **FAIL** | `no path` (100%): навигационный фильтр PMC не открывает зоны дропов |

---

## 3. Детальные списки отказов по идентификаторам и типам связей

### 3.1. Лестницы (Stairs): 13 из 19 успешно, 6 отказов
- **Успешно пройдены (13):**
  - Все наружные и платформенные марши депо (`STAIR_in_0` .. `STAIR_in_7`, марши ангаров, `B_x2_14_t8_store_stair_0`, `B_x2_16_t8_hall_stair_0`, `B_x2_21_t8_hall_stair_0`).
- **Отказы (6):**
  - `[B_hall_n_stair_0: stuck]`
  - `[STAIR_in_8: stuck (timed out)]` (известный сбой наклонного пандуса L_Gen_Depot_1/X4)
  - `[B_x1_8_t7_block_stair_0: stuck]`
  - `[B_x1_16_t7_block_stair_0: stuck]`
  - `[B_x2_4_t8_hall_stair_0: stuck (timed out)]`
  - `[B_x2_25_t8_block_stair_0: stuck]`

### 3.2. Вертикальные трапы (Ladders $\ge 7$ ступеней): 12 из 23 успешно, 11 отказов
- **Успешно пройдены (12):**
  - `LAD_cut_1`, `LAD_cut_2`, `LAD_cut_3` (трапы выемки)
  - `LAD_B_gallery`, `LAD_B_x1_1_t7_gallery`, `LAD_B_x1_3_t1003_gallery`, `LAD_B_x2_7_t1003_gallery`
  - `LAD_B_t1001_tower_ladder_0`, `LAD_B_x1_11_t1001_tower_ladder_0`, `LAD_B_x2_9_t1001_tower_ladder_0`, `LAD_B_x2_23_t1001_tower_ladder_0`, `LAD_B_x2_25_t8_block_hatch`
- **Отказы (11):**
  - `[LAD_B_hall_n_hatch: stuck (timed out)]`
  - `[LAD_B_warehouse_hatch: stuck (timed out)]`
  - `[LAD_B_x1_9_t7_gallery: stuck]`
  - `[LAD_B_x1_19_t7_hall_hatch: stuck (timed out)]`
  - `[LAD_B_x2_4_t8_hall_hatch: stuck (timed out)]`
  - `[LAD_B_x2_14_t8_store_hatch: stuck (timed out)]`
  - `[LAD_B_x2_16_t8_hall_hatch: stuck (timed out)]`
  - `[LAD_B_x2_21_t8_hall_hatch: stuck (timed out)]`
  - `[LAD_B_x2_28_t8_store_hatch: stuck (timed out)]`
  - `[LAD_B_x2_10_t1003_hall_hatch: stuck (timed out)]`
  - `[LAD_B_x2_26_t1003_hall_hatch: stuck (timed out)]`

### 3.3. Перелезания (Mantles): 2 из 120 успешно, 118 отказов
- **Успешно пройдены (2):** `mantle_51`, `mantle_77`.
- **Застревания (3):** `[mantle_0: stuck (timed out)]`, `[mantle_25: stuck (timed out)]`, `[mantle_103: stuck]`.
- **Отказы по отсутствию пути (115 `no path`):**
  - `mantle_129` .. `mantle_3081` (из-за блокировки мантлов в `UNavFilter_PmcFighterNoMantle`).

### 3.4. Спрыгивания (Drops): 0 из 80 успешно, 80 отказов
- **Отказы по отсутствию пути (80 `no path`):**
  - Все 80 связей выборки со страйдом (`drop_0`, `drop_5` .. `drop_403`) получили статус `FAIL (no path)`.

---

## 4. Архитектурный анализ первопричин (Root Cause Analysis)

### 4.1. Архитектурный дефект мантлов: флаг `bMantlePathAllowed` завязан на отряд игрока
При исследовании исходного кода движения бойцов выявлено:
В `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp` (строки 66-70):
```cpp
const USquadSubsystem* MantleSquad = SquadSubsystem.Get();
bMantlePathAllowed = !bAutonomousOrder && MantleSquad &&
    (MantleSquad->IsSoloMode() || (USquadSubsystem::IsSquadMantleEnabled() && MantleSquad->GetFormationLeader() == this));
```
И далее при выборе фильтра навигации (строка 497):
```cpp
return PmcEncounterNav::FilterFor(Stance == EOperativeStance::Prone, bMantlePathAllowed);
```
**Вывод:**
1. Для вражеского бойца Team 1 ссылка `SquadSubsystem` возвращает подсистему отряда игрока (`USquadSubsystem`), так как бойцы противника управляются через `UPmcSquadSubsystem`.
2. Вражеский боец никогда не является лидером строя (`GetFormationLeader() == this` всегда `false`).
3. Режим соло отряда игрока (`MantleSquad->IsSoloMode()`) во время обычной игры выключен.
4. В результате `bMantlePathAllowed` для любого бойца Team 1 ВСЕГДА равен `false`.
5. Метод `PmcEncounterNav::FilterFor(false, false)` возвращает фильтр `UNavFilter_PmcFighterNoMantle`, который закрывает зоны мантлов через `MantleNavigation::CloseMantleArea(Areas, false)`. Навигационная сетка блокирует переход через укрытия, и `OrderMoveTo` возвращает `EOperativeOrderResult::Unreachable` (`no path`).

### 4.2. Архитектурный дефект дропов: навигационный фильтр PMC исключает зоны DropLink
В отличие от специализированных фильтров перемещения игрока (`DropLinkNavigation::OpenDropAreas`), стандартный поисковый фильтр `PmcEncounterNav::FilterFor` учитывает только исключение своих зарядов (`OwnedCharges`) и блокировку мантлов, но **не открывает навигационные зоны падения** (`UNavArea_Drop`, `UNavArea_DropHurt`). Вследствие этого навигатор для бойцов PMC считает обрывы и люки непреодолимыми препятствиями, что приводит к 100% отказов `no path` при попытке спрыгнуть.

### 4.3. Влияние механики замерзания (Frostbite) на изоляцию смоук-тестов
На процедурно сгенерированных картах (`L_Gen_Depot_X4`) по умолчанию активна подсистема выживания на холоде. Без отключения тика `ColdSurvivalComponent` боец за первые 30 секунд накапливает критический уровень обморожения, переходит в состояние паники (`Operator Kessler panics (Critical stress: [Frostbite])`, `IsPanicking() == true`), после чего `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp:71` принудительно отклоняет любые приказы на перемещение (`EOperativeOrderResult::Refused`).
В смоук-тесте `CodexTactics.PmcTraversalSmoke` данная внешняя система изолирована:
```cpp
ColdComp->SetComponentTickEnabled(false);
ColdComp->ColdLevel = 0.f;
```
Это позволило измерять строго физическую и навигационную проходимость объектов уровня без интерференции статусных эффектов.

---

## 5. Выдержка из лога смоука (Saved/Logs/Smoke-PmcTraversalSmoke.log)

```text
[2026.11.22-15.53.45:963][483]LogCodexTactics: Display: Smoke summary: stairs: 13 of 19 crossed (68.4%) [total in map: 19] - FAIL
[2026.11.22-15.53.45:986][483]LogCodexTactics: Display: Smoke: stairs failed 6: [B_hall_n_stair_0: stuck], [STAIR_in_8: stuck (timed out)], [B_x1_8_t7_block_stair_0: stuck], [B_x1_16_t7_block_stair_0: stuck], [B_x2_4_t8_hall_stair_0: stuck (timed out)], [B_x2_25_t8_block_stair_0: stuck]
[2026.11.22-15.53.46:002][483]LogCodexTactics: Display: Smoke summary: ladders: 12 of 23 crossed (52.2%) [total in map: 23] - FAIL
[2026.11.22-15.53.46:017][483]LogCodexTactics: Display: Smoke: ladders failed 11: [LAD_B_hall_n_hatch: stuck (timed out)], [LAD_B_warehouse_hatch: stuck (timed out)], [LAD_B_x1_9_t7_gallery: stuck], [LAD_B_x1_19_t7_hall_hatch: stuck (timed out)], [LAD_B_x2_4_t8_hall_hatch: stuck (timed out)], [LAD_B_x2_14_t8_store_hatch: stuck (timed out)], [LAD_B_x2_16_t8_hall_hatch: stuck (timed out)], [LAD_B_x2_21_t8_hall_hatch: stuck (timed out)], [LAD_B_x2_28_t8_store_hatch: stuck (timed out)], [LAD_B_x2_10_t1003_hall_hatch: stuck (timed out)], [LAD_B_x2_26_t1003_hall_hatch: stuck (timed out)]
[2026.11.22-15.53.46:032][483]LogCodexTactics: Display: Smoke summary: mantles: 2 of 120 crossed (1.7%) [total in map: 3107] - FAIL
[2026.11.22-15.53.46:065][483]LogCodexTactics: Display: Smoke summary: drops: 0 of 80 crossed (0.0%) [total in map: 409] - FAIL
[2026.11.22-15.53.46:096][483]LogCodexTactics: Display: Smoke RESULT: FAIL
```

---

## 6. Таблица соответствия (Claim | Evidence)

| Утверждение (Claim) | Доказательство (Evidence) |
|:---|:---|
| Команда `CodexTactics.PmcTraversalSmoke` реализована в модуле Dev | `Source/CodexTacticsDev/Private/Debug/PmcTraversalSmokeCommand.cpp:823-827` |
| Все константы и пороги вынесены в JSON-конфиг без магических чисел | `docs/port/gemini/pmc_traversal_config.json`, загрузка в `FPmcTraversalConfig::Load()` строки 70-96 |
| Изоляция вражеского бойца: Team 1, коммандер OFF, режим Solo Hold игрока | `Source/CodexTacticsDev/Private/Debug/PmcTraversalSmokeCommand.cpp:211-231` |
| Изоляция обморожения и паники для чистоты замеров | `Source/CodexTacticsDev/Private/Debug/PmcTraversalSmokeCommand.cpp:577-582` |
| Детерминированная выборка со страйдом и строгим порядком 120 mantles и 80 drops | `Source/CodexTacticsDev/Private/Debug/PmcTraversalSmokeCommand.cpp:371-440` |
| Проекция цели NavGoal и сброс состояния (AbordClimb, Heal, Stance) между этапами | `Source/CodexTacticsDev/Private/Debug/PmcTraversalSmokeCommand.cpp:630-660` |
| Полный прогон смоука укладывается в 2 минуты (111.2 с) | `Saved/Logs/Smoke-PmcTraversalSmoke.log`, запуск 15:51:54, завершение 15:53:46 |
| Регистрация `PmcTraversalSmoke` как `knownFail: true` с обоснованием | `Scripts/test_map.json:151-157` |
| Первопричина отказов мантлов подтверждена в коде | `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp:68, 497` |

---

## 7. Что НЕ было сделано и почему

1. **Не вносились правки в навигационные фильтры и логику разрешений мантлов/дропов (`OperativeCharacterMovement.cpp`, `PmcEncounterNav.cpp`):**
   - Задача 095 является задачей валидации и мониторинга (Smoke Dev Check).
   - Согласно регламенту и указаниям лида (Claude), исправление разрешений перемещения врагов и навигационных сеток запланировано в рамках отдельных задач Фазы 2 (T-07).
2. **Не изменялись файлы геометрии и генератора уровней:**
   - Пути `Content/Maps/**` и `Tools/TrainingGround/src/levelgen/**` строго защищены правилами разработки. Проблемы застревания на люках и внутренних лестницах зафиксированы для ответственных модулей.
