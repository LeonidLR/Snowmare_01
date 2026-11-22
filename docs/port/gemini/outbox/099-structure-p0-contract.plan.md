# План реализации задачи 099: Destruction P0 Data Contract & Graph Validator (ADR-0004)

- **Задача**: 099 - Destruction P0: structure data contract + graph validator as a standalone TS module (`docs/port/gemini/inbox/099-structure-p0-contract.md`)
- **Спецификация**: `docs/architecture/adr-0004-destructible-buildings.md` (секции "Structural Model", "Data contract sketch", "Validator rules", фаза P0)
- **Контекст дизайна**: `docs/design/gdd/destruction-studio-review-2026-11-21.md` (секция 7 - открытые вопросы дизайнера, значения HP являются placeholder)
- **Ветка**: `gemini/099-structure-p0-contract`
- **База**: `main` (`300737aeba52e48630cf7a052eaf5b5e9b6291bc`)
- **Рабочее дерево**: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-gemday`
- **Исполнитель**: Gemini (3.8 Flash)

---

## 1. Разделение ответственности и изоляция (Split with 19c)

- Все новые модули создаются строго в отдельной изолированной директории `Tools/TrainingGround/src/levelgen/structure/`.
- Ни один существующий файл в `Tools/TrainingGround/src/levelgen/**` **не модифицируется** (горячий трек автора генератора 19c).
- Генератор не вызывает новый модуль в фазе P0 (`git diff --stat` подтвердит отсутствие изменений в существующих файлах levelgen).
- Разрушаемость остается **СТРОГО DATA ONLY**: никакого C++, изменений рантайма или карт.

---

## 2. Планируемые файлы и структура

### 2.1 `Tools/TrainingGround/src/levelgen/structure/types.ts`
Типы данных ADR-0004 и type guards:
- `StructureClass`: `'load_bearing' | 'partition' | 'annex' | 'facade'`
- `StructureMaterial`: `'concrete_panel' | 'brick' | 'plaster' | 'wood' | 'sheet_metal'`
- `StructureInfo`:
  - `structure: StructureClass`
  - `material: StructureMaterial`
  - `hp_class: string` (ключ в `structure_classes.json`)
  - `supports?: string[]` (список id элементов, которые данный элемент несет)
  - `protected?: boolean` (защищен от разрушения валидатором / геймдизайном)
- `SlabPanel`:
  - `id: string`
  - `floor: number`
  - `rect: [[number, number], [number, number]]` (координаты прямоугольника плиты)
  - `supported_by: string[]`
  - `needs: number` (минимальное число стоящих опор)
  - `on_fail: 'floor_hole' | 'none'`
  - `protected?: boolean`
- `StructuredWall`, `StructuredOpening`, `StructuredStair`, `StructuredBuilding` (автономные интерфейсы для валидатора, совместимые с расширением `GenBuilding`).
- Структуры конфигурации `StructureClassesData` (`classes`, `hp_classes`).
- Type guards: `isStructureClass`, `isStructureMaterial`, `isStructureInfo`, `isSlabPanel`.

### 2.2 `Content/Data/LevelGen/structure_classes.json`
Таблица классов прочности и сопоставлений:
- Все 20 пар `<structure>.<material>` (4 класса $\times$ 5 материалов), сопоставленные с классами HP.
- Блок `hp_classes` с полями:
  - `_placeholder: true`
  - `hp_per_cell: number`
  - `min_breach_class: string` (`heavy_explosive`, `medium_explosive`, `light_explosive`, `small_arms`, `none`)
  - `noise_m: number`
  - `rubble: string`
- Строгое соблюдение порядка прочности ADR-0004:
  `load_bearing.concrete_panel > facade.brick > annex.* > partition.plaster > *.wood`

### 2.3 `Tools/TrainingGround/src/levelgen/structure/validate.ts`
Чистые функции валидации без исключений (`throw`), возвращающие массив ошибок `{ rule: string; element_id: string; message: string }[]`:
- **V3.1 Acyclic**: проверка ацикличности графа передачи нагрузки (DFS с обнаружением обратных ребер).
- **V3.2 Supports Count**: для каждой панели плиты (`slab_panels`) и лестничного пролета (`stairs`) проверяется `supported_by.length >= needs`.
- **V3.3 Partition Supports Nothing**: элементы класса `partition` не могут иметь опорных связей (`supports` пуст или не задан).
- **V3.4 Annex Supports Annex Only**: элементы класса `annex` могут нести только другие элементы класса `annex` (не могут нести несущие стены, фасад, перегородки или плиты основного объема).
- **V3.5 HP Class Resolves**: каждый `hp_class`, указанный в `struct`, существует в `classesJson.hp_classes` (или резолвится через `classesJson.classes`).
- **V5 Structure Hash**: чистая детерминированная функция `structureHash(classesJson: object): string`:
  - Каноническая сортировка ключей объекта перед сериализацией в JSON.
  - Криптографический хэш SHA-256.
  - Гарантия байт-идентичности при 5 повторных вызовах с разным порядком ключей и изменения при модификации любого поля.

### 2.4 `Tools/TrainingGround/src/levelgen/structure/classify.ts`
Чистая таблица-справочник классификации по конструктивным системам (ADR-0004 proposal):
- Поддерживаемые системы: `block`, `hall`, `warehouse`, `office`, `shed`, `ruin`.
- Роли элементов: `core_wall`, `column_wall`, `perimeter`, `partition`, `annex`, `cladding`, `opening_door`, `opening_window`.
- Чистая функция `classifyElement(system: ConstructionSystem, role: ElementRole): { structure: StructureClass; material: StructureMaterial }`.

### 2.5 `Tools/TrainingGround/src/levelgen/structure/structure.test.ts`
Набор модульных тестов (`node --test`):
- Для каждого из 5 подправил V3: минимум 1 PASS и 1 FAIL тест (всего 10 тест-кейсов).
- V5 хэш: тест на стабильность (5 запусков) и чувствительность к правкам.
- Classify: тест на полноту покрытия всех 6 систем.

---

## 3. Критерии приемки и гейт качества

1. `npm test` в `Tools/TrainingGround`: все тесты (включая существующие 597 и новые) проходят.
2. `npm run typecheck` (`tsc && tsc -p tsconfig.node.json`): 0 ошибок.
3. `git diff --stat`: отсутствие изменений в существующих файлах `Tools/TrainingGround/src/levelgen/**`.
4. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`: PASS.
5. `git diff --check`: 0 ошибок пробелов.
6. Отчет `docs/port/gemini/outbox/099-structure-p0-contract.report.md` с таблицей `Claim | Evidence`.
7. Финальное сообщение в чат `gemini-099` по установленному шаблону.
