# Отчёт о выполнении задачи 099b: Structure Nits

**Дата:** 2026-11-22
**Модель/Исполнитель:** Gemini (Gemini 3.8 Flash)
**Ветка:** `gemini/099b-structure-nits`
**Рабочее дерево:** `CodexTactics-gemday`
**Базовая ветка:** `origin/main`

---

## 1. Краткое резюме выполненных правок

В соответствии с указанием лида (сообщение #924 в `gemini-queue`):
1. **Отчёты о висячих ссылках в V3 (Dangling references):**
   - В валидатор `validateBuildingStructure` (`Tools/TrainingGround/src/levelgen/structure/validate.ts:173-198`) добавлена проверка идентификаторов в массивах `supports` и `supported_by` против собранного множества элементов здания `elements`.
   - При обнаружении ссылки на несуществующий элемент регистрируется отказ с правилом `V3_dangling_reference`.
2. **Явное свойство `needs` для лестниц (Stairs explicit needs):**
   - В интерфейс `StructuredStair` (`Tools/TrainingGround/src/levelgen/structure/types.ts:55-61`) добавлено опциональное типизированное свойство `needs?: number;`.
   - В `validate.ts:112` удален опасный каст `(s as any).needs`, чтение свойства выполняется строго типизировано (`typeof s.needs === 'number' ? s.needs : ...`).
3. **Защита `isSlabPanel` от `NaN` и `Infinity`:**
   - В тайпгарде `isSlabPanel` (`Tools/TrainingGround/src/levelgen/structure/types.ts:117-147`) все 4 координаты `rect` (`rect[0][0]`, `rect[0][1]`, `rect[1][0]`, `rect[1][1]`) и значение `needs` валидируются через `Number.isFinite()`, что гарантирует отсечение `NaN`, `+Infinity` и `-Infinity`.
4. **Юнит-тесты:**
   - В `Tools/TrainingGround/src/levelgen/structure/structure.test.ts` добавлены тесты на проверку `NaN` / `Infinity` для плит, тест на проверку явного `needs` у лестниц и тесты для обеих веток `V3_dangling_reference` (`supports` и `supported_by`). Все 16 тестов успешно пройдены (100% PASS).
   - Проверка компилятора TypeScript (`npx tsc --noEmit`) завершена без единой ошибки.

---

## 2. Результаты запуска тестов

```text
> node --test src/levelgen/structure/structure.test.ts

✔ structure/types: type guards identify valid and invalid values (2.2757ms)
✔ structure/validate: V3.1 Acyclic rule (PASS case) (1.4159ms)
✔ structure/validate: V3.1 Acyclic rule (FAIL case: circular loop A -> B -> A) (0.489ms)
✔ structure/validate: V3.2 Supports Count (PASS case) (0.2884ms)
✔ structure/validate: V3.2 Supports Count (FAIL case) (0.3332ms)
✔ structure/validate: V3.3 Partition Supports Nothing (PASS case) (0.2613ms)
✔ structure/validate: V3.3 Partition Supports Nothing (FAIL case) (0.3475ms)
✔ structure/validate: V3.4 Annex Supports Annex Only (PASS case) (0.2946ms)
✔ structure/validate: V3.4 Annex Supports Annex Only (FAIL case) (0.3452ms)
✔ structure/validate: V3.5 HP Class Resolves (PASS and FAIL cases) (2.4906ms)
✔ structure/validate: V5 structureHash is byte-identical across 5 repeated calls with re-ordered keys (4.0696ms)
✔ structure/classify: covers all 6 construction systems across all 10 roles (1.6817ms)
✔ structure_classes.json: all 20 structure.material pairs mapped and HP ordering holds (2.8009ms)
✔ structure/validate: stairs explicit needs validation (0.3774ms)
✔ structure/validate: V3 Dangling Reference rule (supports non-existent element) (0.4492ms)
✔ structure/validate: V3 Dangling Reference rule (supported_by non-existent element) (0.5173ms)
ℹ tests 16
ℹ suites 0
ℹ pass 16
ℹ fail 0
ℹ cancelled 0
ℹ skipped 0
ℹ todo 0
ℹ duration_ms 312.7869
```

---

## 3. Таблица соответствия (Claim | Evidence)

| Утверждение (Claim) | Доказательство (Evidence) |
|:---|:---|
| Правило `V3_dangling_reference` регистрирует висячие ID в `supports` и `supported_by` | `Tools/TrainingGround/src/levelgen/structure/validate.ts:173-198` |
| Интерфейс `StructuredStair` содержит явное `needs?: number` | `Tools/TrainingGround/src/levelgen/structure/types.ts:58` |
| Чтение `stairs[i].needs` в `validate.ts` без неявного `as any` | `Tools/TrainingGround/src/levelgen/structure/validate.ts:112` |
| `isSlabPanel` отклоняет `NaN`, `+Infinity`, `-Infinity` для координат и `needs` | `Tools/TrainingGround/src/levelgen/structure/types.ts:128-143` |
| Юнит-тесты на `NaN` и `Infinity` добавлены и проходят | `Tools/TrainingGround/src/levelgen/structure/structure.test.ts:79-83` |
| Юнит-тесты на `needs` у лестниц и висячие ссылки добавлены и проходят | `Tools/TrainingGround/src/levelgen/structure/structure.test.ts:480-544` |
| TypeScript компилируется без ошибок (`npx tsc --noEmit` exit 0) | Запуск в `Tools/TrainingGround`, exit code 0 |
| Код зафиксирован в коммите ветки `gemini/099b-structure-nits` | Commit `bf6a6490` |

---

## 4. Что НЕ было сделано и почему

1. **Не изменялись существующие файлы генератора зданий за пределами `Tools/TrainingGround/src/levelgen/structure/`:**
   - Задача 099b строго локализована внутри модуля контракта разрушаемости P0.
   - Подключение контракта к реальному генератору зданий запланировано в рамках отдельных задач Lead/Bgen сессии.
