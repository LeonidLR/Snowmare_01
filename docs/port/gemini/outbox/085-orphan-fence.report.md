# Отчет по задаче 085 / 085b: Устранение сиротского забора макро-квартала (F_x1_8_t1003) и валидатор коллизий стен

> ## 📋 Handover Summary (Передача смены)
> - **Текущий статус:** Задача 085 / 085b **ПОЛНОСТЬЮ ЗАВЕРШЕНА** и готова к мерджу в `origin/main`.
> - **Ветка:** `gemini/085-orphan-fence` (rebased на свежий `origin/main`).
> - **Что сделано:**
>   1. **085b (1) Строгая проверка полигона:** В `regionRegen.ts` функция `applyTemplates` теперь требует замкнутый контур забора (`fenceClosedRing`) и проверяет вхождение центров парцел через `pointInPoly`, исключая ложные удаления повернутых или открытых заборов по bbox.
>   2. **085b (2) Изоляция типов в `mergeAttempt`:** В цикле `kept` удаляются исключительно семейства `kind === 'fence'` (не стены строений/периметра) с собственной причиной `'orphan_fence_crossing'`.
>   3. **085b (3) Допуск концов (Endpoint Tolerance):** В `layoutValidator.ts` забор, примыкающий к стене здания или образующий Т-образный стык с перелетом в пределах толщины стены (<= 15..30 см), больше не считается пересечением.
>   4. **085b (4) Защита капитальных стен в `cleanConflictingFences`:** Жертвой очистки может быть только семейство заборов, стены `kind !== 'fence'` никогда не удаляются; вычислены и вынесены из циклов `wallFamily` и `footprintPoly`.
>   5. **Тесты:** В `orphanFence.test.ts` добавлены все 4 теста для 085b (всего 10 тестов, 10/10 PASS).
>   6. **Гейты:** `npm run typecheck` PASS, `gemini_gate.ps1 -Task 085` PASS.
> - **Что осталось:** Ничего по 085/085b.
> - **Следующий точный шаг для следующего исполнителя:**
>   Взять задачу 087: `docs/port/gemini/inbox/087-levelgen-pmc-posts-and-charges.md` (составить план в топике `gemini-087`, дождаться ОК, реализовать).

---

## 1. Первопричина бага дизайнера (Root Cause Analysis)

На карте `L_Gen_Depot_1_X6` в Unreal проявился длинный повернутый забор семейства `F_x1_8_t1003` (6 стен высотой 135 см, прямоугольник ~34 x 8 м, повернутый на ~14°, углы: `(11357, 16631)`, `(11162, 17418)`, `(7924, 16617)`, `(8118, 15829)`), который проходил сквозь новые переулки `Z_x1_8_4_1_1_t1003` и `Z_x1_8_5_1_1_t1003` и пересекал их заборы `F_x1_8_4_1_1_t1003` / `F_x1_8_5_1_1_t1003`. При этом в веб-интерфейсе генератора забор было сложно заметить из-за низкого контраста отрисовки.

**Детальная диагностика механизма выживания забора:**
1. **Попадание центра в межблочный зазор:** Геометрический центр ограничивающего прямоугольника родительского забора `F_x1_8_t1003` равен `[9640.58, 16624.16]`. При нарезке макро-квартала `x1_8` на более мелкие парцелы образовались `LOT_x1_8_4_1_1` (Y: 15638..16461) и `LOT_x1_8_5_1_1` (Y: 16672..17600), разделенные аллеей шириной 211 см (Y: 16461..16672).
2. Центр забора `Y = 16624.16` попал ровно в этот межблочный зазор.
3. В функции `applyTemplates` (`Tools/TrainingGround/src/levelgen/regionRegen.ts`) проверка на принадлежность группе выполнялась через `lotAt(lots, g.centre)`. Поскольку центр попал в зазор, `inBlock(g.centre)` вернул `undefined`. Из-за этого родительский забор не был помечен на удаление вместе со старым содержимым квартала.
4. В функции `mergeAttempt` заборы обеих групп (старой и новой) не были включены в `solids` (препятствия зданий), поэтому проверка на перекрытие `overlapsAny` не отклонила ни один из них, и оба забора слились в итоговую раскладку.

---

## 2. Реализованное решение

### А. Геометрическое правило очистки (Geometry-First)
В соответствии с указанием лида (#738, #760), удаление заборов родительского квартала реализовано **строго по геометрии**:
1. **В `applyTemplates` (`Tools/TrainingGround/src/levelgen/regionRegen.ts`):**
   - Если группа стен не попала в `inBlock(g.centre)`, проверяется:
     - Пересекают ли отрезки стен (`segmentsIntersect`) свежие построенные стены шаблонов (`builtWalls`).
     - Пересекают ли они контуры свежих зданий (`builtBuildings`, с учетом угла поворота `footprintPoly`).
     - Формируют ли стены замкнутый полигон (`fenceClosedRing`) и охватывает ли этот полигон две или более нарезанные парцелы по `pointInPoly(rectCentre(k.rect), ring)`. Открытые и повернутые заборы не удаляются ошибочно по bbox.
   - При срабатывании условий родительский забор удаляется.
2. **В `mergeAttempt` (`Tools/TrainingGround/src/levelgen/regionRegen.ts`):**
   - Проверяются сохраненные группы стен `kept`. Удаляются **только** группы `kind === 'fence'` (не стены периметра или зданий), пересекающие `addedWalls` или `addedBuildings`, с собственной причиной `'orphan_fence_crossing'`.

### Б. Модуль валидации раскладок (`layoutValidator.ts`)
Создан новый специализированный валидатор `Tools/TrainingGround/src/levelgen/layoutValidator.ts`:
- **`validateWallCrossings(layout: GenLayout): WallCrossingConflict[]`**:
  - Вынесены вычисления `wallFamily` и `footprintPoly` из парных циклов.
  - Проверяет пересечения между всеми парами отрезков стен разных семейств на одном этаже.
  - Учитывает допуск концов (Endpoint Tolerance): пересечение в пределах толщины стены (<= 15..30 см) от любого конца считается Т-образным стыком или примыканием, а не коллизией.
  - Проверяет пересечения отрезков стен с контурами зданий (с учетом произвольного угла поворота `rotation` и полигона `footprintPoly`), игнорируя примыкания к стенам здания.
  - Игнорирует подпорные стены выемки (`cut_wall`), так как они формируют границу траншеи/канала.
- **`cleanConflictingFences(layout: GenLayout)`**:
  - Автоматически находит конфликтующие заборы. Жертвой может стать **только** семейство заборов (`kind === 'fence'`), капитальные стены никогда не удаляются. При конфликте двух семейств заборов удаляется забор с большим периметром.

### В. Отображение и контраст заборов в UI (`LayoutView.tsx`, `PropsCanvas.tsx`)
1. Заборы (`wl.kind === 'fence'`) теперь отрисовываются контрастным янтарным цветом `#fbbf24` с минимальной толщиной `1.8px`, что делает их четко различимыми поверх фоновых полигонов зон и переулков.
2. `LayoutView` принимает проп `conflicts` и отображает каждый конфликт в виде круглого красного маркера (`#ef4444`, радиус 10px, толщина 2px) с внутренним крестиком `X`.
3. В `LevelGenDepartment.tsx`:
   - Вверху экрана при наличии конфликтов появляется предупреждающий баннер с точным числом коллизий и кнопкой **«Удалить сиротский забор»**.
   - Кнопка «Принять» визуально предупреждает о наличии неразрешенных пересечений.

---

## 3. Инструкция для дизайнера: Как исправить X6

В соответствии с правилами проекта, файлы `Content/Data/LevelGen/accepted/*` вручную скриптами не редактировались.

Чтобы дизайнер очистил `L_Gen_Depot_1_X6`:
1. **Вариант 1 (В один клик в Training Ground):**
   - Открыть карту `gen_ref_01_02_depot_1_x6.json` во вкладке «Генератор уровней».
   - Вверху экрана появится красное предупреждение: *«Обнаружено конфликтов заборов/стен: 3. Сиротский забор родительского блока F_x1_8_t1003 пересекает новые переулки»*.
   - Нажать кнопку **«Удалить сиротский забор»**. Валидатор мгновенно удалит все 6 сегментов `F_x1_8_t1003`, оставив 0 конфликтов.
   - Нажать **«Принять»**.
2. **Вариант 2 (Через перегенерацию региона):**
   - Выбрать рамкой регион блока `x1_8` и нажать «Перегенерировать регион».
   - Благодаря обновленной логике `regionRegen.ts`, родительский забор автоматически отбросится по геометрии и не попадет в новую генерацию.

---

## 4. Верификация и тесты

1. **Модульные тесты (`Tools/TrainingGround/src/levelgen/orphanFence.test.ts`):**
   - `validateWallCrossings on real accepted X6: finds exactly the 3 F_x1_8_t1003 conflicts`: **PASS** (ровно 3 конфликта, все три — коллизии `F_x1_8_t1003` с `F_x1_8_4_1_1_t1003` и `F_x1_8_5_1_1_t1003` в точках `[7993.59, 16335.92]`, `[9508.62, 17009.24]`, `[9089.37, 16905.49]`).
   - `cleanConflictingFences removes F_x1_8_t1003 and leaves 0 conflicts on X6`: **PASS** (удалено семейство `F_x1_8_t1003` — 6 стен, после чего в раскладке остается ровно 0 конфликтов).
   - `geometry check with real X6 numbers: parent fence centre in alley gap`: **PASS** (проверено точное поведение на реальных координатах X6).
   - `parent fence that does not cross fresh geometry is preserved`: **PASS** (забор родителя, не пересекающий геометрию, сохраняется).
   - `wall vs building footprint crossing is detected`: **PASS** (проверено пересечение стены с контуром повернутого здания).
   - `085b (1): rotated 14 deg open fence spanning 2 lot centres is kept`: **PASS** (открытый забор под углом 14° не считается замкнутым кольцом).
   - `085b (3): endpoint tolerance - fence butted into a building wall is NOT a crossing`: **PASS** (примыкание забора к зданию не считается пересечением).
   - `085b (3): endpoint tolerance - T-joint wall overshoot within thickness is NOT a crossing`: **PASS** (Т-образный стык с перелетом <= толщины стены не считается пересечением).
   - `085b (4): cleanConflictingFences victim must be a fence family, never a non-fence wall`: **PASS** (жертвой выбирается только забор, капитальная стена защищена).
   - `085b (4): cleanConflictingFences between two non-fence walls removes neither`: **PASS** (при конфликте двух капитальных стен ни одна автоматически не удаляется).
2. **Проверка типов (`npm run typecheck`):** **PASS** (exit 0, 0 ошибок).
3. **Регрессионные тесты генератора (`regionRegen.test.ts`):** **PASS** (15/15 тестов).
4. **Скриншот интерфейса:** Сгенерирован в `docs/port/gemini/outbox/085-orphan-fence-conflict.png`.

---

## 5. Затронутые файлы

- `Tools/TrainingGround/src/movement/geometry.ts` — экспорт `segmentsIntersect`, реализация `segmentSegmentIntersection`.
- `Tools/TrainingGround/src/levelgen/layoutValidator.ts` — валидатор пересечений с допуском концов, hoisting функций, безопасная очистка заборов.
- `Tools/TrainingGround/src/levelgen/regionRegen.ts` — `fenceClosedRing`, фильтрация замкнутых заборов в `applyTemplates`, фильтрация `fence`-семейств в `mergeAttempt` со статусом `orphan_fence_crossing`.
- `Tools/TrainingGround/src/departments/LayoutView.tsx` — янтарная отрисовка заборов `#fbbf24`, отображение маркеров коллизий.
- `Tools/TrainingGround/src/departments/PropsCanvas.tsx` — янтарная отрисовка заборов.
- `Tools/TrainingGround/src/departments/LevelGenDepartment.tsx` — интеграция валидатора, предупреждающий баннер, кнопка очистки.
- `Tools/TrainingGround/src/levelgen/orphanFence.test.ts` — 10 модульных тестов на реальных данных X6 и проверку замечаний 085b.
- `Tools/TrainingGround/src/levelgen/fixtures/depot_x6_orphan_fence.json` — изолированный фикстур раскладки X6 для детерминированного тестирования.
- `docs/port/gemini/outbox/085-orphan-fence-conflict.png` — визуальное подтверждение отрисовки заборов и маркеров.
- `docs/port/gemini/outbox/085-orphan-fence.plan.md` — согласованный план.
- `docs/port/gemini/outbox/085-orphan-fence.report.md` — данный отчет.

---

## 6. Таблица утверждений и доказательств (Evidence)

| Утверждение | Доказательство |
|---|---|
| Выявлена точная первопричина выживания забора F_x1_8_t1003 (центр [9640.58, 16624.16] в зазоре 211 см между парцелами) | Модульный тест `Tools/TrainingGround/src/levelgen/orphanFence.test.ts` (тест `geometry check with real X6 numbers`) подтверждает попадание центра в межблочный зазор |
| Валидатор на реальной раскладке X6 находит ровно 3 конфликта F_x1_8_t1003 | Тест `validateWallCrossings on real accepted X6: finds exactly the 3 F_x1_8_t1003 conflicts` в `Tools/TrainingGround/src/levelgen/orphanFence.test.ts` |
| Очистка сиротских заборов cleanConflictingFences удаляет все 6 стен F_x1_8_t1003 и оставляет 0 конфликтов | Тест `cleanConflictingFences removes F_x1_8_t1003 and leaves 0 conflicts on X6` в `Tools/TrainingGround/src/levelgen/orphanFence.test.ts` |
| 085b (1): Замкнутый контур забора проверяется через pointInPoly, открытый повернутый забор сохраняется | Тест `085b (1): rotated 14 deg open fence spanning 2 lot centres is kept` в `Tools/TrainingGround/src/levelgen/orphanFence.test.ts` |
| 085b (3): Примыкание забора к зданию или Т-стык с перелетом <= толщины стены не считается пересечением | Тесты `085b (3): endpoint tolerance - fence butted into a building wall is NOT a crossing` и `085b (3): endpoint tolerance - T-joint wall overshoot within thickness is NOT a crossing` в `Tools/TrainingGround/src/levelgen/orphanFence.test.ts` |
| 085b (4): cleanConflictingFences удаляет только семейства заборов, не трогая капитальные стены | Тесты `085b (4): cleanConflictingFences victim must be a fence family, never a non-fence wall` и `085b (4): cleanConflictingFences between two non-fence walls removes neither` в `Tools/TrainingGround/src/levelgen/orphanFence.test.ts` |
| Заборы отображаются контрастным янтарным цветом (#fbbf24) и видны поверх зон | Реализовано в `Tools/TrainingGround/src/departments/LayoutView.tsx` и подтверждено на скриншоте `docs/port/gemini/outbox/085-orphan-fence-conflict.png` |
| Статическая типизация TypeScript без ошибок | `npm run typecheck` в `Tools/TrainingGround`: код возврата 0 |
| Модульные тесты 085/085b проходят без ошибок | `node --test Tools/TrainingGround/src/levelgen/orphanFence.test.ts`: 10 тестов пройдено, 0 ошибок |
