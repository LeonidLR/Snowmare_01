# Отчет по задаче 099: Destruction P0: Structure Data Contract & Graph Validator (ADR-0004)

- **Задача**: 099 - Destruction P0: structure data contract + graph validator as a standalone TS module (ADR-0004).
- **Ветка**: `gemini/099-structure-p0-contract`
- **Рабочее дерево**: `CodexTactics-gemday`
- **Автор**: Gemini (3.8 Flash)
- **Дата**: 2026-11-22
- **Спецификация**: `docs/architecture/adr-0004-destructible-buildings.md`

---

## 1. Сводка выполненных работ

В соответствии со спецификацией **ADR-0004** и заданием `docs/port/gemini/inbox/099-structure-p0-contract.md`, в изолированной директории `Tools/TrainingGround/src/levelgen/structure/` реализован автономный контракт данных и чистый валидатор графа нагрузок (фаза P0: Data Only).

Существующие файлы генератора в `Tools/TrainingGround/src/levelgen/**` **не модифицировались** (0 измененных существующих файлов, что подтверждается проверкой `git status`), сохраняя горячий трек автора генератора (19c).

Созданы следующие модули:
1. `Tools/TrainingGround/src/levelgen/structure/types.ts` — типы данных спецификации ADR-0004 и runtime type guards.
2. `Content/Data/LevelGen/structure_classes.json` — таблица 20 пар `<structure>.<material>` и классов прочности `hp_classes` с placeholder-значениями.
3. `Tools/TrainingGround/src/levelgen/structure/validate.ts` — чистые функции валидации подправил V3 и детерминированного хэширования V5.
4. `Tools/TrainingGround/src/levelgen/structure/classify.ts` — чистый справочник классификации элементов по 6 конструктивным системам.
5. `Tools/TrainingGround/src/levelgen/structure/structure.test.ts` — юнит-тесты (13 тестов, включая PASS и FAIL для каждого подправила V3).

---

## 2. Описание созданных файлов

### 2.1 `Tools/TrainingGround/src/levelgen/structure/types.ts`
- Определяет ключевые объединения и интерфейсы ADR-0004:
  - `StructureClass`: `'load_bearing' | 'partition' | 'annex' | 'facade'`.
  - `StructureMaterial`: `'concrete_panel' | 'brick' | 'plaster' | 'wood' | 'sheet_metal'`.
  - `StructureInfo`: `{ structure, material, hp_class, supports?, protected? }`.
  - `SlabPanel`: `{ id, floor, rect, supported_by, needs, on_fail, protected? }`.
  - Интерфейсы совместимости: `StructuredWall`, `StructuredOpening`, `StructuredStair`, `StructuredBuilding`.
  - Конфигурационные типы: `HpClassConfig`, `StructureClassesConfig`.
- Реализует защитные type guards:
  - `isStructureClass(val)`
  - `isStructureMaterial(val)`
  - `isStructureInfo(val)`
  - `isSlabPanel(val)`

### 2.2 `Content/Data/LevelGen/structure_classes.json`
- Описывает 20 пар `<structure>.<material>` (4 класса $\times$ 5 материалов), сопоставленных с классами HP.
- Содержит описания классов в `hp_classes` с полями `hp_per_cell`, `min_breach_class`, `noise_m`, `rubble`.
- Все классы помечены флагом `"_placeholder": true` для последующей калибровки геймдизайнером.
- Строго соблюден порядок относительной прочности из ADR-0004:
  `load_bearing.concrete_panel (5000) > facade.brick (2000) > annex.* (500..1200) > partition.plaster (300) > *.wood (150..250)`.

### 2.3 `Tools/TrainingGround/src/levelgen/structure/validate.ts`
- **V3 Validator (`validateBuildingStructure`)**:
  - Чистая функция без побочных эффектов, не выбрасывает исключений (`throw`), возвращает массив ошибок `{ rule, element_id, message }`.
  - **V3.1 (`V3_acyclic`)**: строит направленный граф передачи нагрузок и выполняет поиск циклов (DFS с проверкой стека рекурсии). При обнаружении цикла возвращает диагностическое сообщение с путем зависимости `u1 -> u2 -> ... -> u1`.
  - **V3.2 (`V3_supports_count`)**: проверяет, что для каждой панели плиты (`slab_panels`) и лестницы число доступных опор удовлетворяет условию `supported_by.length >= needs`.
  - **V3.3 (`V3_partition_supports_nothing`)**: проверяет, что перегородки (`structure === 'partition'`) не имеют исходящих опорных связей (`supports` пуст) и не несут другие элементы.
  - **V3.4 (`V3_annex_supports_annex_only`)**: проверяет, что элементы пристроек (`structure === 'annex'`) могут нести только другие элементы пристроек и не могут быть опорой для основного каркаса или несущих стен.
  - **V3.5 (`V3_hp_class_resolves`)**: проверяет, что каждый указанный в элементе `hp_class` действительно зарегистрирован в конфигурации `structure_classes.json`.
- **V5 Helper (`structureHash`)**:
  - Реализует функцию канонической сортировки ключей `canonicalizeJson`.
  - Вычисляет хэш SHA-256 (`node:crypto`).
  - Гарантирует байт-идентичный результат при любом исходном порядке ключей в JSON-объекте и изменяется при модификации любого поля.

### 2.4 `Tools/TrainingGround/src/levelgen/structure/classify.ts`
- Реализует проектную таблицу классификации по типам конструктивных систем из ADR-0004:
  - Поддерживаемые системы: `block`, `hall`, `warehouse`, `office`, `shed`, `ruin`.
  - Поддерживаемые роли: `core_wall`, `column_wall`, `perimeter_wall`, `room_partition`, `annex_volume`, `cladding`, `door_leaf`, `window_pane`, `roof_slab`, `floor_slab`.
- Экспортирует чистую функцию `classifyElement(system, role): ClassifiedStructure`.

### 2.5 `Tools/TrainingGround/src/levelgen/structure/structure.test.ts`
- Модульный тестовый набор (13 тестов):
  - Type guards для классов и материалов.
  - V3.1: PASS (корректный DAG) и FAIL (цикл A -> B -> A).
  - V3.2: PASS (3 опоры при needs=2) и FAIL (1 опора при needs=2).
  - V3.3: PASS (перегородка без опор) и FAIL (перегородка с исходящей опорой).
  - V3.4: PASS (пристройка несет перекрытие пристройки) и FAIL (пристройка несет несущую стену).
  - V3.5: PASS (существующий класс HP) и FAIL (несуществующий класс HP).
  - V5: 5 последовательных вызовов `structureHash` с перемешанными ключами дают идентичный хэш; изменение `hp_per_cell` изменяет хэш.
  - Classify: полнота покрытия всех 6 систем по всем 10 ролям.
  - Контракт данных: валидация всех 20 пар и соотношений прочности.

---

## 3. Что необходимо подключить автору генератора (19c) в следующих фазах

Для завершения перехода к фазам P1–P2 разработчику генератора зданий (19c) потребуется:
1. **Назначение классов при генерации**:
   - При создании стен и проемов вызывать `classifyElement(buildingSystem, role)` и прописывать секцию `struct: StructureInfo` в `GenWall`, `WallOpening` и `GenPartition`.
2. **Формирование плит перекрытий (`slab_panels`)**:
   - Разбивать этажные перекрытия на панели по сетке комнат/ячеек, заполняя `supported_by` идентификаторами несущих стен (`load_bearing`).
3. **Правила проходимости V2 и V4**:
   - Реализовать симуляцию единичного отказа несущего элемента L1 на графе проходимости (`walkable graph`) и маркировать незаменимые элементы флагом `protected: true`.
4. **Копирование тегов строителем**:
   - Передавать вычисленные свойства `structure=` и `material=` в теги акторов при экспорте в Unreal.

---

## 4. Валидация и гейты качества

| Claim | Evidence | Details |
|---|---|---|
| Все тесты модуля структуры проходят | node --test src/levelgen/structure/structure.test.ts (PASS 13/13) | Проверены все 5 подправил V3 (PASS+FAIL), хэш V5 и классификатор |
| Общий тестовый набор Training Ground проходит | npm test (PASS 606/606) | 0 падений во всех 64 тестовых файлах |
| Typecheck TypeScript без ошибок | npm run typecheck (tsc exit code 0) | 0 ошибок в типах и строгом режиме |
| Существующие файлы генератора не затронуты | git status (0 modified files) | Затронута только изолированная папка levelgen/structure/ |
| Контракт данных structure_classes.json валиден | node --test src/levelgen/structure/structure.test.ts | Все 20 пар сопоставлены, соотношения прочности соблюдены |
| Форматирование git | git diff --check (0 замечаний) | 0 ошибок пробелов |
| Безопасность и аудит Jev | python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini | Boundary Breach: PASS |

---

## 5. Что НЕ делалось
- Не модифицировались существующие файлы генератора зданий `Tools/TrainingGround/src/levelgen/**` (задача 19c).
- Не изменялся C++ код и не создавались компоненты рантайма разрушений.
- Не реализовывались правила V2 и V4 (требуют граф проходимости, находящийся в разработке 19c).
- Не менялись числовые значения параметров баланса HP (оставлены в статусе placeholder для геймдизайнера).
