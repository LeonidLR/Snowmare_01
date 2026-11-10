# Отчёт по задаче 008: Formation Editor в Training Ground (TypeScript)

- **Дата**: 10 ноября 2026
- **Агент**: Gemini (`$env:CODEX_AGENT = "gemini"`)
- **Ворктри**: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-gemformed`
- **Ветка**: `gemini/formation-editor` (отведена от `origin/main`)
- **Коммиты**:
  - `89c9c173` — feat(movement): formation shapes schema codex.formation_shapes/1 and validator
  - `082f5c03` — feat(movement): initial formation_shapes.json and shapesCoverage live checks
  - `45b78cf6` — feat(movement): dev server endpoints for formation shapes
  - `4a28aa0e` — feat(movement): apply saved formation shapes in TS simulator
  - `e27d064e` — feat(movement): FormationEditor component and tab integration in Training Ground
  - `22fc6550` — test(movement): unit tests and golden vectors for formation shapes
- **Затронутые и созданные файлы**:
  - `Content/Data/FormationLab/formation_shapes.json` *(новый)*
  - `Content/Data/FormationLab/fixtures/formation_shapes_diamond4.json` *(новый)*
  - `Content/Data/FormationLab/fixtures/formation_shapes_echelon_mirror.json` *(новый)*
  - `Tools/TrainingGround/src/movement/shapesSchema.ts` *(новый)*
  - `Tools/TrainingGround/src/movement/shapesCoverage.ts` *(новый)*
  - `Tools/TrainingGround/src/movement/shapes.test.ts` *(новый)*
  - `Tools/TrainingGround/server/movementApi.ts`
  - `Tools/TrainingGround/src/movement/sim.ts`
  - `Tools/TrainingGround/src/departments/FormationEditor.tsx` *(новый)*
  - `Tools/TrainingGround/src/departments/MovementDepartment.tsx`

---

## 1. Выполненные пункты задачи

### 1.1. Схема данных и файл `formation_shapes.json` (`codex.formation_shapes/1`)
- Создан файл данных `Content/Data/FormationLab/formation_shapes.json` с начальной схемой `codex.formation_shapes/1`.
- Реализован модуль [`shapesSchema.ts`](file:///C:/Users/Zephyrus15Duo/Documents/Unreal%20Projects/CodexTactics-gemformed/Tools/TrainingGround/src/movement/shapesSchema.ts):
  - Строгая валидация типов: целые сантиметры (`fwd`, `right`), целые градусы (`look` в `(-180, 180]`, `arc` в `[10, 360]`), допустимые роли (`commander`, `right`, `left`, `rear`), количество бойцов 2..4.
  - Командир (`commander`) жестко зафиксирован на координатах `(fwd: 0, right: 0)`.
  - Предельное расстояние бойца от командира ограничено константой `CUSTOM_MAX_RADIUS` (4000 см).
  - Форматирование с сохранением стабильного порядка ключей (`schema`, `formations`, `custom`), неизвестных пользовательских полей и CRLF/LF.
  - Атомарная запись через временный файл и переименование (`writeFormationShapesFile`).

### 1.2. Модуль расчета секторов и валидации (`shapesCoverage.ts`)
- Реализован модуль [`shapesCoverage.ts`](file:///C:/Users/Zephyrus15Duo/Documents/Unreal%20Projects/CodexTactics-gemformed/Tools/TrainingGround/src/movement/shapesCoverage.ts):
  - **Круговое покрытие 360°**: точный расчет покрытых, не покрытых (слепых) и перекрывающихся (дублированных) угловых диапазонов.
  - **Живая проверка дистанций**: выявление пар бойцов с расстоянием `< slot.min_spacing` (120 см).
  - **Живая проверка радиуса**: выявление бойцов за пределами `CUSTOM_MAX_RADIUS` (4000 см).
  - **Зеркалирование (`mirrorFormationShape`)**:
    - Инверсия горизонтальной координаты: `right = -right`.
    - Инверсия угла взгляда: `look = normYaw(-look)`.
    - Обмен ролями: `left <-> right` (`commander` и `rear` сохраняются).
    - Переключение типов: `echelon_left <-> echelon_right`.
  - **Сброс и дефолты**: генерация начального состояния строя из правил `formation_config.json` (`computeDefaultFormationShape`) и удаление оверрайдов (`resetFormationShape`).

### 1.3. Эндпоинты Dev-сервера (`movementApi.ts`)
- В [`movementApi.ts`](file:///C:/Users/Zephyrus15Duo/Documents/Unreal%20Projects/CodexTactics-gemformed/Tools/TrainingGround/server/movementApi.ts) добавлены маршруты:
  - `GET /api/movement/formation-shapes`: возвращает содержимое `formation_shapes.json`.
  - `POST /api/movement/formation-shapes`: валидирует тело запроса по схеме `codex.formation_shapes/1` и производит атомарную запись в файл.
  - Исключение `formation_shapes` из списка лейаутов в `layoutNames()`.

### 1.4. Интеграция с TS-симулятором (`sim.ts`)
- В опции симуляции `SimOptions` добавлено поле `savedShapes?: FormationShapesFile`.
- При наличии сохраненной формы для активного строя и количества бойцов:
  - Координаты спавна и целевые слоты бойцов берутся из файла `formation_shapes.json`.
  - Направления взглядов берутся строго из файла (`normYaw(heading + m.look)`), блокируя автоматическое перезаписывание аллокатором обзора.

### 1.5. Визуальный редактор строев (`FormationEditor.tsx`) и вкладка «Перемещение»
- Создан полноценный интерактивный компонент [`FormationEditor.tsx`](file:///C:/Users/Zephyrus15Duo/Documents/Unreal%20Projects/CodexTactics-gemformed/Tools/TrainingGround/src/departments/FormationEditor.tsx):
  - Интерактивный SVG холст со смещением масштаба (зум 30%..120%) и сеткой с шагом 100 см и 50 см.
  - Привязка перетаскивания к сетке 10 см.
  - Перетаскивание бойцов по холсту (командир зафиксирован на [0, 0]).
  - Перетаскивание стрелки направления взгляда для каждого бойца.
  - Перетаскивание края сектора обзора (изменение ширины сектора `arc`).
  - Линии предупреждения между бойцами при нарушении минимальной дистанции (< 120 см).
  - Круговой мини-радар 360° с подсветкой слепых зон (красный), зон под контролем (бирюзовый) и зон дублирования (янтарный).
  - Кнопки **«Зеркало»**, **«Сброс к дефолту»** и **«Сохранить»** с русскими подписями.
- Во вкладке [`MovementDepartment.tsx`](file:///C:/Users/Zephyrus15Duo/Documents/Unreal%20Projects/CodexTactics-gemformed/Tools/TrainingGround/src/departments/MovementDepartment.tsx) добавлен переключатель под-вкладок:
  - **«Прогон в симуляторе»**
  - **«Редактор строев (360° / сетка)»**
- Сохраненные формы сразу же передаются в симулятор и отображаются при прогоне.

---

## 2. Результаты тестов

1. **Проверка типов (`npm run typecheck`)**:
   - `tsc && tsc -p tsconfig.node.json` завершается с кодом 0 (0 ошибок).
2. **Юнит-тесты строев (`shapes.test.ts`)**:
   - 10 из 10 тестов пройдены успешно:
     - `validateFormationShapes: accepts valid documents and golden fixtures`
     - `validateFormationShapes: rejects invalid schema, bad coordinates and missing leader`
     - `formatFormationShapesJson: roundtrip preserves unknown keys and stable key order`
     - `readFormationShapesFile & writeFormationShapesFile: atomic writing with temp and rename`
     - `computeCoverage: correctly calculates covered, uncovered and overlapping spans`
     - `validateLiveShape: flags spacing violations and radius violations`
     - `mirrorFormationShape: mirrors coordinates, look angles and left<->right roles`
     - `computeDefaultFormationShape & resetFormationShape`
     - `TS Simulator integration: simulate uses savedShapes overrides`
     - `Golden fixtures: verify formation_shapes_diamond4 and formation_shapes_echelon_mirror`
3. **Регрессионный прогон всего набора тестов движения**:
   - `node --test "src/movement/**/*.test.ts"`: **177 из 177 тестов успешно пройдены (0 падений)**.

---

## 3. Эталонные векторы (Golden Vectors)

Созданы файлы в `Content/Data/FormationLab/fixtures/`:
1. `formation_shapes_diamond4.json` — сохраненный 4-местный ромб с настроенными секторами.
2. `formation_shapes_echelon_mirror.json` — сохраненный 4-местный эшелон влево и его точное зеркальное отражение в эшелон вправо.

---

## 4. Памятка для Claude (C++ Port)

При портировании чтения строев в Unreal C++:
1. **Путь к файлу**: `Content/Data/FormationLab/formation_shapes.json`.
2. **Схема**: `codex.formation_shapes/1`.
3. **Логика чтения**:
   - Если файл отсутствует или в объекте `formations` нет ключа `[formation][memberCount]`: строй рассчитывается процедурно из `formation_config.json`, как сейчас.
   - Если запись присутствует:
     - Считывается массив `members`:
       - `role`: `"commander"` (всегда [0, 0]), `"right"`, `"left"`, `"rear"`.
       - Координаты в системе отсчета командира (Heading = +X, Right = +Y):
         - `fwd`: см вперед (UE координата X = `fwd`).
         - `right`: см вправо (UE координата Y = `right`).
       - `look`: угол взгляда в градусах относительно направления движения (`normYaw(HeadingYaw + look)`).
       - `arc`: ширина сектора обзора в градусах (по умолчанию 90°).
   - Строи `wall`, `hedgehog`, `blizzard` остаются процедурными и не редактируются через этот файл.
