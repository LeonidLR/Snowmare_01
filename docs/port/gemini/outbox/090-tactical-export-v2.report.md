# Отчёт по задаче 090: Tactical Export v2 Rework (Smart Enemies T-09)

- **Ветвь**: `gemini/090-tactical-export-v2`
- **Коммит**: `985e7c99`
- **Дата**: 2026-11-22
- **Исполнитель**: Gemini (3.8 Flash)

---

## 1. Сводка и таблица утверждений (Claim | Evidence)

| Claim | Evidence |
|---|---|
| 1. `open_crossings` не вырождены: расчет расстояния до укрытий по полилинии с учетом коробок укрытий, контуров зданий и стен; проверка LOS для `seeing_covers` | `Tools/TrainingGround/src/levelgen/tacticalMap.ts:187-270`; на X4 ровно 16 открытых участков (от 11.2м до 120.6м) вместо 4 монолитных маршрутов; тест `Tools/TrainingGround/src/levelgen/tacticalMap.test.ts:133-146` PASS |
| 2. `building` и `floor` корректно атрибутируются на реальных данных X4 | `Tools/TrainingGround/src/levelgen/tacticalMap.ts:169-185`; 5 чокпоинтов и 29 укрытий на X4 имеют привязку к зданиям и этажам; тест `Tools/TrainingGround/src/levelgen/tacticalMap.test.ts:108-112` PASS |
| 3. C++ логирует новые списки и валидирует счетчики в тестах | `Source/CodexTactics/Private/LevelGen/TacticalMapSubsystem.cpp:138,160`; `Source/CodexTacticsTests/Private/LevelGen/TacticalMapRulesTest.cpp:189-216`; `test.ps1 -Filter CodexTactics.LevelGen` (98 tests PASS) |
| 4. Англоязычная документация в TACTICAL_MAP.md и именованные константы | `docs/design/TACTICAL_MAP.md:158-202`; константы `COVER_DISTANCE_THRESHOLD_CM`, `MIN_CROSSING_LENGTH_CM`, `SEEING_COVER_MAX_DIST_CM`, `BUILDING_ASSOCIATION_MARGIN_CM` в `Tools/TrainingGround/src/levelgen/tacticalMap.ts:18-24`; удален `?? [0,0]` |
| 5. Исключение X6 из дефолтных целей экспорта | `Tools/TrainingGround/scripts/export-tactical-map.ts:22-25` (только X2 и X4 в `DEFAULT_TARGETS`); файлы X6 не изменялись |
| 6. Сборка C++ и прохождение всех тестов LevelGen | `Scripts/build.ps1` (Result: Succeeded, exit 0); `Scripts/test.ps1 -Filter CodexTactics.LevelGen` (98 tests, 0 failed) |
| 7. Прохождение смоуков `TacticalMapSmoke`, `TacticalPmcSmoke`, `MissionGoalsSmoke` | `TacticalMapSmoke`: PASS (63.1s); `TacticalPmcSmoke`: PASS (42.1s); `MissionGoalsSmoke`: PASS (41.0s); `Saved/Logs/Smoke-*.log` |
| 8. Чистота git и валидация pre-gate | `python Scripts/Tools/gemini_gate.py --phase pre --task 090 --base main`: PASS (14 files checked against 11 patterns) |

---

## 2. Детальное описание исправлений по ревью (Points 1–5)

### Point 1. Исправление вырожденных `open_crossings` (`tacticalMap.ts`)
- **Проблема**: Ранее расстояние до укрытий замерялось только до центров укрытий, а контуры зданий, комнат и отдельно стоящих стен игнорировались. В результате весь маршрут (например, 554 м) считался единым открытым участком (100% crossing), при этом замер длины производился по прямой хорде вместо полилинии, а укрытия `seeing_covers` собирались без проверки прямой видимости (LOS).
- **Решение**:
  - В `findOpenCrossings` замер расстояния до безопасных объектов теперь учитывает:
    - полигоны укрытий `coverPoly(c)` (с запасом `half_w` / `half_d`),
    - контуры зданий `footprintPoly(b)` и интерьер комнат,
    - отрезки стен `distToWall(p, w)`.
  - Дистанция вдоль маршрута измеряется честно по точкам с шагом `ROUTE_SAMPLE_STEP_CM = 100` см.
  - Участки $\ge 10\text{ м}$ (`MIN_CROSSING_LENGTH_CM = 1000`), где дистанция до ближайшего укрытия $\ge 2\text{ м}$ (`COVER_DISTANCE_THRESHOLD_CM = 200`), формируют отдельные `open_crossings`.
  - `seeing_covers`: отбираются укрытия в радиусе 40 м (`SEEING_COVER_MAX_DIST_CM = 4000`), имеющие прямую линию видимости (`hasLos`) к точкам открытого участка без пересечения со зданиями и высокими стенами ($> 150\text{ см}$).
- **Результат на X4**: 16 открытых участков (длины 11.2 м – 120.6 м).

### Point 2. Атрибуция зданий и этажей (`buildingAndFloorAt`)
- **Проблема**: На реальной карте X4 атрибуция зданий и этажей возвращала пустые значения (0 из 46 чокпоинтов и 0 из 150 укрытий), так как проверялись только внутренние полигоны комнат без учета стен и использовалась жесткая константа 300 см.
- **Решение**:
  - Функция `buildingAndFloorAt` теперь проверяет:
    1. Исходную стену укрытия `w.building` и `w.floor`.
    2. Стены `side_a` и `side_b` для чокпоинтов (`w.building`).
    3. Строгое попадание в контур здания `inFootprint(b, p, 0)`.
    4. Попадание в зону примыкания к зданию `BUILDING_ASSOCIATION_MARGIN_CM = 200` см.
  - Высота этажа рассчитывается через `LEVEL_HEIGHT_CM`.
- **Результат на X4**: 5 чокпоинтов и 29 укрытий получили корректную привязку к зданиям и этажам.

### Point 3. Логирование C++ и ассерты в тестах
- В `Source/CodexTactics/Private/LevelGen/TacticalMapSubsystem.cpp:138` (и методе `Dump:160`) добавлен вывод количества `WalkableRoofs.Num()`, `OpenCrossings.Num()`, `GoalSites.Num()`.
- Лог `TacticalMapSmoke`:
  ```
  [TacticalMap] L_Gen_Depot_X4.tactical.json: 46 places, 7 drop spots, 163 windows, 22 overwatch roofs, 150 hiding spots, 7 ladder-only roofs, 22 walkable roofs, 16 open crossings, 25 goal sites (gen_ref_01_02_depot_1_x4, hash 1f399583ed257447)
  ```
- В `TacticalMapRulesTest.cpp` (`FTacticalMapCommittedFilesTest`) добавлены проверки:
  - `WalkableRoofs.Num() == 22`
  - `OpenCrossings.Num() == 16`
  - `GoalSites.Num() == 25`
  - Наличие непустых `Building` и `Floor` у чокпоинтов и укрытий X4.

### Point 4. Документация на английском языке и именованные константы
- Секция 8 `docs/design/TACTICAL_MAP.md` переведена на английский язык ("v2 - Buildings, Storeys, Roof Access Points, Open Crossings, Goal Sites").
- В `tacticalMap.ts` вынесены константы:
  - `COVER_DISTANCE_THRESHOLD_CM = 200`
  - `MIN_CROSSING_LENGTH_CM = 1000`
  - `SEEING_COVER_MAX_DIST_CM = 4000`
  - `GOAL_SITE_RADIUS_CM = 2500`
  - `GOAL_SITE_SECTOR_ARC_DEG = 90`
  - `ROUTE_SAMPLE_STEP_CM = 100`
  - `BUILDING_ASSOCIATION_MARGIN_CM = 200`
- Устранены фоллбэки вида `?? [0, 0]` в `roofAccessDetails`, пропускающие некорректные точки без выброса нулевых мировых координат.

### Point 5. Исключение X6 из дефолтных целей экспорта
- В `Tools/TrainingGround/scripts/export-tactical-map.ts` из списка `DEFAULT_TARGETS` исключена `L_Gen_Depot_1_X6`. Дефолтный экспорт обновляет только `L_Gen_Test_X2` и `L_Gen_Depot_X4`.
- Файлы X6 не затрагивались.

---

## 3. Свидетельства верификации (Gate)

### 3.1. TypeScript тесты и тайпчек
```
> cd Tools/TrainingGround
> node --test src/levelgen/tacticalMap.test.ts
✔ tactical map v2 schema (2.3s)
✔ findOpenCrossings produces non-degenerate crossings on X4 (1.8s)
✔ buildingAndFloorAt resolves building on real X4 data (1.2s)
...
14/14 tests passed

> npm run typecheck
0 errors

> npm test
590/590 tests passed
```

### 3.2. C++ Сборка (`Scripts/build.ps1`)
```
Result: Succeeded
Total execution time: 1183.95 seconds
Exit code 0. Full log: Saved\Logs\Build.log
```

### 3.3. Unit-тесты (`Scripts/test.ps1 -Filter CodexTactics.LevelGen`)
```
Success  CodexTactics.LevelGen.TacticalMap.V2
Success  CodexTactics.LevelGen.TacticalMap.CommittedFiles
Success  CodexTactics.LevelGen.TacticalMap.ParseRejects
...
98 tests, 0 failed. Report: Saved\TestReport
```

### 3.4. Смоуки (`Scripts/smoke_parallel.ps1 -NoRetry -Remote off -ExtraArgs -Multiprocess`)
```
Smoke                      Result       Seconds
TacticalMapSmoke           PASS            63.1
TacticalPmcSmoke           PASS            42.1
MissionGoalsSmoke          PASS            41.0
```

---

## 4. Что НЕ сделано и почему
- **Физическая line-trace валидация видимости в TypeScript**: Согласно stop-rule спецификации T-09, экспорт ограничивается чистой 2D/2.5D геометрией и LOS по полигонам препятствий. Полная физическая трассировка перенесена в C++ слой (задача T-05).
- **Правка файлов X6**: Строго соблюдено правило Never — файлы `L_Gen_Depot_1_X6` не изменялись.

---

090 FINISH | PASS | 985e7c99 | done: review points 1-5 fixed (open_crossings polyline/cover/LOS, building/floor attribution, C++ logging and X4 asserts, English docs and thresholds, X6 target exclusion) | NOT done: none | needs Claude: merge from local branch gemini/090-tactical-export-v2 | gate: npm test (590 pass), LevelGen unit tests (98 pass), TacticalMapSmoke/TacticalPmcSmoke/MissionGoalsSmoke (all 3 PASS), gemini_gate PASS | report: docs/port/gemini/outbox/090-tactical-export-v2.report.md

---

## Lead fixes 2026-11-22
- Branch head before the lead fixes: `bfccb3d6` (merge of main into `gemini/090-tactical-export-v2`). Lead fixes are working-tree changes on top of it; not committed yet.
- `distToCover`: the unsafe `dist - 200` bound is replaced by `dist - hypot(sx, sy) / 2` (half the cover box diagonal, a true lower bound). This changes X4 output: `seeing_covers` / `nearest_cover_cm` / open crossing lengths move, the open crossing count stays 16. The committed X4 tactical file must be regenerated to match; that is not done yet (see the lead decision below).
- Magic numbers named: `SIDE_WALL_MAX_DIST_M`, `LOS_WALL_MIN_HEIGHT_CM`, `DEFAULT_COVER_HEIGHT_CM` in tacticalMap.ts; `TacticalMapRulesLocal::DefaultGoalSiteSectorArcDeg` in TacticalMapRules.cpp.
- Stale `codex.tactical_map/1` header comments fixed to /2 in TacticalMapRules.h, TacticalMapRulesTest.cpp, tacticalMap.test.ts. TACTICAL_MAP.md section 8 reworded (windows[] building/floor existed in v1).
- Checks: `npx tsc --noEmit` exit 0; `node --test src/levelgen/tacticalMap.test.ts`: 13 pass, 1 fail (the committed X4 file equality test, see above).
- C++ build: `Scripts/build.ps1` Result: Succeeded. `Scripts/test.ps1 -Filter CodexTactics.LevelGen.TacticalMap`: 11 tests, 0 failed. (The filter `CodexTactics.Tactics.TacticalMap` matches nothing; the real prefix is `CodexTactics.LevelGen.TacticalMap`.)
- Open item: X4 export is no longer byte-equal to the committed file after the distToCover fix. The lead must decide whether to regenerate `L_Gen_Depot_X4.tactical.json` (and `L_Gen_Test_X2`) from the fixed code. Generated files were reverted to HEAD, not committed.
