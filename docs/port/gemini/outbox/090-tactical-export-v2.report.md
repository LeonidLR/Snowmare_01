# Отчёт по задаче 090: Tactical Export v2 (Smart Enemies T-09)

- **Ветвь**: `gemini/090-tactical-export-v2`
- **Коммит**: `02192451`
- **Дата**: 2026-11-20
- **Исполнитель**: Gemini

---

## 1. Цель задачи
Реализация этапа **T-09** инициативы **Smart Enemies**: расширение тактической карты уровня до версии схемы `codex.tactical_map/2` с сохранением полной обратной совместимости с версией `/1`.
Экспорт обогащён метаданными о зданиях и этажах, точными геометрическими координатами выходов на крыши (stairs / ladder / hatch), полным списком доступных для перемещения крыш (`walkable_roofs`), протяжёнными открытыми участками маршрутов (`open_crossings`) и укрытиями вокруг целей миссий (`goal_sites`).

---

## 2. Реализованные изменения

### 2.1. Схема данных `codex.tactical_map/2` (TypeScript в `Tools/TrainingGround`)
- **`src/levelgen/tacticalMap.ts`**:
  - `TACTICAL_MAP_SCHEMA = 'codex.tactical_map/2'` (поддерживает константы `TACTICAL_MAP_SCHEMA_V1` и `TACTICAL_MAP_SCHEMA_V2`).
  - Добавлены интерфейсы: `TacticalRoofAccess`, `TacticalWalkableRoof`, `TacticalOpenCrossing`, `TacticalGoalSite`.
  - Добавлена атрибуция `building` и `floor` для `TacticalChokepoint`, `TacticalOverwatchSpot`, `TacticalHidingSpot`, `TacticalOverwatchRoof`.
  - Реализованы алгоритмы:
    - `buildingAndFloorAt(layout, pos, defaultZ)`: определение здания и этажа по полигонам комнат/зданий на соответствующей высоте.
    - `roofAccessDetails(layout, building)`: сбор выходов на крышу с точными мировыми координатами `top` (stairs, ladder, hatch).
    - `findOpenCrossings(layout, groundZ)`: детектирование сегментов основных маршрутов $\ge 10\text{ м}$, не имеющих укрытий ближе 2 м от пути (`nearest_cover_cm`), и сбор простреливающих укрытий в радиусе 40 м (`seeing_covers`).
    - `findGoalSites(layout, groundZ)`: сбор позиций укрытий в радиусе 25 м от каждой цели миссии с `dist_m`, `facing_yaw` (ориентирован от укрытия к цели), `sector_arc_deg: 90`, `cover_kind` и `cover_height_cm`.
    - Stop rule: чисто геометрический экспорт позиций в TS, без симуляции физического line trace (валидация видимости перенесена в C++ / T-05).
- **`scripts/export-tactical-map.ts`**:
  - Обновлён вывод статистики при экспорте (включая walkable roofs, open crossings, goal sites).
- **`src/levelgen/tacticalMap.test.ts`**:
  - Добавлены тесты парсинга схемы `/2`, проверки атрибуции зданий/этажей, выходов на крыши, открытых участков и позиций у целей. Все 14 тестов модуля проходят успешно.

### 2.2. Загрузчик и структуры C++ (`Source/CodexTactics`, `Source/CodexTacticsTests`)
- **`TacticalMapRules.h` & `TacticalMapRules.cpp`**:
  - Константы `SchemaV1` (`codex.tactical_map/1`) и `SchemaV2` (`codex.tactical_map/2`).
  - Новые структуры: `FTacticalRoofAccess`, `FTacticalWalkableRoof`, `FTacticalOpenCrossing`, `FTacticalGoalSite`.
  - Поля `Building` и `Floor` добавлены в `FTacticalChokepoint`, `FTacticalOverwatchSpot`, `FTacticalHidingSpot`.
  - Списки `AccessPoints` добавлены в `FTacticalOverwatchRoof` и `FTacticalLadderRoof`.
  - Списки `WalkableRoofs`, `OpenCrossings`, `GoalSites` добавлены в `FTacticalMapFile`.
  - Метод `TacticalMapRules::Parse` поддерживает как `/1`, так и `/2`. При парсинге `/1` новые поля остаются пустыми без ошибок. Более новые версии (`/3`+) корректно отклоняются.
  - Добавлен запрос `TacticalMapRules::GetGoalSitesForGoal(File, GoalId)`.
- **`TacticalMapSubsystem.h`**:
  - Добавлен метод `GetGoalSitesForGoal(const FString& GoalId) const`.
- **`TacticalMapRulesTest.cpp`**:
  - Добавлен unit-тест `FTacticalMapV2Test` ("V2"), проверяющий парсинг всех полей v2 и запрос `GetGoalSitesForGoal`.
  - Обновлён тест `ParseRejects` (проверка пустых списков v2 и отклонения схемы v3).
  - Тест `CommittedFiles` проверяет реальные файлы карт (включая экспортированные v2 карты).

### 2.3. Перегенерированные карты
- `Content/Data/LevelGen/tactical/L_Gen_Test_X2.tactical.json`:
  - Schema: `codex.tactical_map/2`
  - 26 chokepoints, 23 drop spots, 29 windows, 10 overwatch roofs, 150 hiding spots, 3 ladder-only roofs
  - 10 walkable roofs, 4 open crossings, 40 goal sites
- `Content/Data/LevelGen/tactical/L_Gen_Depot_X4.tactical.json`:
  - Schema: `codex.tactical_map/2`
  - 46 chokepoints, 7 drop spots, 163 windows, 22 overwatch roofs, 150 hiding spots, 7 ladder-only roofs
  - 22 walkable roofs, 4 open crossings, 25 goal sites
- **Важно**: Карты `X6` (`L_Gen_Depot_1_X6`) строго **не изменялись**.

### 2.4. Документация
- Обновлён документ `docs/design/TACTICAL_MAP.md` — добавлена секция 8 («v2 — Здания, этажность, выходы на крыши, открытые прострелы, позиции у целей (Smart Enemies T-09, 2026-11-20)»).

---

## 3. Результаты верификации (Gate)

1. **TypeScript Tools/TrainingGround**:
   - `npm run typecheck`: **0 errors (PASS)**
   - `node --test src/levelgen/tacticalMap.test.ts`: **14/14 tests PASS**
   - `npm test`: **590/590 tests PASS**
2. **C++ Сборка и Автоматизация**:
   - Сборка `CodexTacticsEditor` через `Scripts/build.ps1`: **Succeeded (exit code 0)**
   - `powershell -File Scripts/test.ps1 -Filter CodexTactics.LevelGen`: **98/98 tests PASS** (включая `TacticalMap.V2` и `TacticalMap.CommittedFiles`)
3. **Headless Смоуки**:
   - `TacticalMapSmoke`: **PASS** (40.6s)
   - `MissionGoalsSmoke`: **PASS** (40.9s)
   - `TacticalPmcSmoke`: **PASS** (42.6s)
4. **Аудит чистоты**:
   - `git diff --check`: **PASS (0 whitespace errors)**
   - Затронуты строго необходимые файлы задачи (10 файлов).

---

## 4. Связь со следующими задачами Smart Enemies
- **T-05 (Line-trace валидация позиций)**:
  - C++ структуры `FTacticalOpenCrossing` и `FTacticalGoalSite` содержат исходные точки и направления, готовые для выполнения `LineTraceSingleByChannel` в Unreal Engine.
- **T-12 (Защита целей)**:
  - Метод `UTacticalMapSubsystem::GetGoalSitesForGoal(GoalId)` предоставляет ботам PMC и командиру кандидаты укрытий вокруг точек захвата/эвакуации с готовыми углами обзора `FacingYaw`.

---

090 FINISH | PASS | 02192451 | done: schema /2 with building/floor attribution, roof access points, walkable_roofs, open_crossings, goal_sites, C++ loader and queries, regenerated X2/X4 maps, doc section 8 | NOT done: none | needs Claude: merge from local branch gemini/090-tactical-export-v2 | gate: npm test (590 pass), LevelGen unit tests (98 pass), TacticalMapSmoke/TacticalPmcSmoke/MissionGoalsSmoke (all 3 PASS) | report: docs/port/gemini/outbox/090-tactical-export-v2.report.md
