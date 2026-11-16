# Отчёт 076: Автоматическая генерация пропсов для сгенерированных карт

- **Задача:** 076 (`generated-map-props`)
- **Спецификация:** `docs/port/gemini/inbox/076-generated-map-props.md`, `.gemini/task.md`
- **Ветка:** `gemini/076-generated-props`
- **Автор:** Gemini CLI

---

## 1. Диагностика и архитектурное решение

1. **Причина отсутствия пропсов:**
   - В рантайме подсистема `Source/CodexTactics/Private/LevelGen/CodexLevelPropsSubsystem.cpp:100` ищет файл пропсов по пути `Content/Data/LevelGen/props/<MapName>.props.json`.
   - На диске существовал только один файл `Content/Data/LevelGen/props/L_Gen_Depot_1.props.json`.
   - Для сгенерированных и расширенных карт (`L_Gen_Depot_X4`, `L_Gen_Depot_1_X6`, `L_Gen_Test_X2`) файлы пропсов отсутствовали, поэтому мины, бочки, лут, ящики и турели не спавнились.

2. **Модель плотностей и масштабирование (`props_autogen.json`):**
   - Создана конфигурация `Content/Data/LevelGen/props_autogen.json:1`.
   - Базовая карта: `L_Gen_Depot_1` (площадь 21 948 м² / ~2.195 единиц по 10 000 м²).
   - Плотности и бюджеты на 10 000 м²:
     - **Ящики (crates):** режим `density` 0.25 на 100 м² пригодной площади; максимум масштабируется от базового капа 80 пропорционально площади карты.
     - **Бочки (barrels):** 3.64 на 10 000 м² (все бочки взрывные `barrel_explosive`, кластеры у стен).
     - **Турели (turrets):** 0.91 на 10 000 м² (кап 12).
     - **Мины (mines):** 1.82 на 10 000 м² (свободные мины `mine_proximity` в выемке) + 25% дверей + 10% заминированных ящиков.
     - **Лут (loot):** бюджет 273 на 10 000 м² (дефолтные таблицы `ammo_common`, `med_common`, `rare_roof`).

3. **Сохранение исходных пропсов при расширении (Expansion Preservation):**
   - Для расширенных карт пропсы исходной базовой карты внутри исходных границ переносятся с признаком `locked: true`, что позволяет алгоритму `placement.ts:makeInput` зафиксировать их как `input.kept`.
   - Новые пропсы размещаются детерминированно только на оставшуюся квоту в расширенных кольцах и блоках, после чего исходный флаг `locked` восстанавливается.

4. **Защита авторских файлов от перезаписи:**
   - Автоматически создаваемые файлы получают свойство `"source": "auto"`.
   - Функция `canOverwritePropsFile` в `Tools/TrainingGround/src/levelgen/props/autogen.ts:169` строго запрещает перезапись любых файлов без `"source": "auto"` (включая ручные правки и `L_Gen_Depot_1.props.json`).

5. **Генерация файлов для существующих карт:**
   - Написан CLI-скрипт `Tools/TrainingGround/scripts/autogen_props.ts:1`.
   - Сгенерированы и проверены быстрым навигационным гейтом 3 файла:
     - `Content/Data/LevelGen/props/L_Gen_Depot_X4.props.json:1` (242 объекта, fast gate: pass)
     - `Content/Data/LevelGen/props/L_Gen_Depot_1_X6.props.json:1` (275 объектов, fast gate: pass)
     - `Content/Data/LevelGen/props/L_Gen_Test_X2.props.json:1` (176 объектов, fast gate: pass)

6. **Хук сохранения на стороне сервера:**
   - В `Tools/TrainingGround/server/levelgenApi.ts:170` в эндпоинт `/api/levelgen/accept` встроен хук автоматической генерации пропсов для принимаемых карт с сохранением в `Content/Data/LevelGen/props/<MapName>.props.json`.

---

## 2. Таблица Claim | Evidence

| Утверждение (Claim) | Доказательство (Evidence) |
|---|---|
| Причина отсутствия пропсов на сгенерированных картах зафиксирована с указанием строки C++ | `Source/CodexTactics/Private/LevelGen/CodexLevelPropsSubsystem.cpp:100` |
| Создана конфигурация параметров плотности и ограничений автогенерации | `Content/Data/LevelGen/props_autogen.json:1` |
| Поддержка исключений спавна и целей реализована в `PropsLayoutView` | `Tools/TrainingGround/src/levelgen/props/layoutView.ts:460`, `Tools/TrainingGround/src/levelgen/props/layoutView.ts:526`, `Tools/TrainingGround/src/levelgen/props/layoutView.ts:1248` |
| Реализован детерминированный модуль автогенерации пропсов | `Tools/TrainingGround/src/levelgen/props/autogen.ts:180` |
| Реализовано правило защиты от перезаписи авторских файлов пропсов | `Tools/TrainingGround/src/levelgen/props/autogen.ts:169` |
| Написаны модульные тесты автогенерации (детерминизм, N сидов, сохранение исходных пропсов, защита от перезаписи, резолв имен) | `Tools/TrainingGround/src/levelgen/props/autogen.test.ts:24` |
| Полный набор тестов Training Ground проходит успешно (441 тест) | `npm test` в `Tools/TrainingGround` (pass 441, fail 0) |
| Проверка типов TypeScript проходит чисто без ошибок | `npx tsc --noEmit` в `Tools/TrainingGround` (exit code 0) |
| Реализован CLI-скрипт генерации пропсов | `Tools/TrainingGround/scripts/autogen_props.ts:1` |
| Сгенерирован и валидирован файл пропсов для `L_Gen_Depot_X4` | `Content/Data/LevelGen/props/L_Gen_Depot_X4.props.json:1` (242 объекта, fast gate: pass) |
| Сгенерирован и валидирован файл пропсов для `L_Gen_Depot_1_X6` | `Content/Data/LevelGen/props/L_Gen_Depot_1_X6.props.json:1` (275 объектов, fast gate: pass) |
| Сгенерирован и валидирован файл пропсов для `L_Gen_Test_X2` | `Content/Data/LevelGen/props/L_Gen_Test_X2.props.json:1` (176 объектов, fast gate: pass) |
| Встроен хук сохранения пропсов при принятии лейаута | `Tools/TrainingGround/server/levelgenApi.ts:170` |

---

## 3. Логи проверок

### Тесты Training Ground (`npm test`)
```
npm notice run training-ground@1.0.0 test
npm notice run node --test "src/**/*.test.ts"
✔ autogen props placement (task 076) > refuse-to-overwrite rule: authored files without source auto are protected
✔ autogen props placement (task 076) > map-name rule: resolves exact LevelJson target names, falls back to mapNameFor
✔ autogen props placement (task 076) > determinism: same layout and seed produces byte-identical canonical JSON
✔ autogen props placement (task 076) > validator and gates pass on N seeds (1..3)
✔ autogen props placement (task 076) > expanded map keeps original props from sourceProps inside base area
ℹ tests 441
ℹ suites 1
ℹ pass 441
ℹ fail 0
ℹ cancelled 0
ℹ skipped 0
ℹ todo 0
```

### Проверка типов (`npx tsc --noEmit`)
```
npm notice run training-ground@1.0.0 typecheck
npm notice run tsc && tsc -p tsconfig.node.json
(exit code 0, no errors)
```

### Сборка production-бандла (`npm run build`)
```
vite v5.4.21 building for production...
✓ 2732 modules transformed.
dist/index.html                     0.60 kB │ gzip:   0.38 kB
dist/assets/index-G3pLyw9-.css     55.53 kB │ gzip:   9.81 kB
dist/assets/index-DrPLtGv6.js   1,451.08 kB │ gzip: 438.00 kB
✓ built in 9.45s
```

### Генерация пропсов CLI-скриптом
```
[autogen_props] Project root: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-gemcli
[autogen_props] Using base source props: .../Content/Data/LevelGen/props/L_Gen_Depot_1.props.json
[autogen_props] Generating props for gen_ref_01_02_depot_1_x4 -> L_Gen_Depot_X4...
[autogen_props] Saved .../Content/Data/LevelGen/props/L_Gen_Depot_X4.props.json (242 props across 5 layers, fast gate: pass)
[autogen_props] Using base source props: .../Content/Data/LevelGen/props/L_Gen_Depot_1.props.json
[autogen_props] Generating props for gen_ref_01_02_depot_1_x6 -> L_Gen_Depot_1_X6...
[autogen_props] Saved .../Content/Data/LevelGen/props/L_Gen_Depot_1_X6.props.json (275 props across 5 layers, fast gate: pass)
[autogen_props] Using base source props: .../Content/Data/LevelGen/props/L_Gen_Depot_1.props.json
[autogen_props] Generating props for gen_ref_01_02_depot_1_x2 -> L_Gen_Test_X2...
[autogen_props] Saved .../Content/Data/LevelGen/props/L_Gen_Test_X2.props.json (176 props across 5 layers, fast gate: pass)
```
