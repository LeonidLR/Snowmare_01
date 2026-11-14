# Отчёт 027: Боевые здания и интерьерный левел-генератор

**Статус:** Пункты 1–5 завершены и проверены (`npm run typecheck`, `npm run build`, `npm test` — `340/340` зелёных на коммите `c9885564`).  
**Базовая документация:** [`docs/design/levelgen/BUILDINGS.md`](../../../design/levelgen/BUILDINGS.md)  
**Затронутые файлы (только TS / UI / Data / Docs, C++ не изменялся):**
- [`Tools/TrainingGround/src/levelgen/schema.ts`](../../../../Tools/TrainingGround/src/levelgen/schema.ts) + [`combatBuildingsSchema.test.ts`](../../../../Tools/TrainingGround/src/levelgen/combatBuildingsSchema.test.ts) (`324ba0ef`)
- [`Tools/TrainingGround/src/levelgen/generate.ts`](../../../../Tools/TrainingGround/src/levelgen/generate.ts) + [`combatBuildingsGenerate.test.ts`](../../../../Tools/TrainingGround/src/levelgen/combatBuildingsGenerate.test.ts) (`1bcccdb0`, фикс T-стыков дверей в `c9885564`, `B_shed` окно перенесено с `south` на `north` в `b16c765a`)
- [`Tools/TrainingGround/src/levelgen/tacticalEvaluate.ts`](../../../../Tools/TrainingGround/src/levelgen/tacticalEvaluate.ts), [`evaluate.ts`](../../../../Tools/TrainingGround/src/levelgen/evaluate.ts), [`critic.ts`](../../../../Tools/TrainingGround/src/levelgen/critic.ts) + [`combatBuildingsEvaluate.test.ts`](../../../../Tools/TrainingGround/src/levelgen/combatBuildingsEvaluate.test.ts) (`b16c765a`)
- [`Tools/TrainingGround/src/departments/LayoutView.tsx`](../../../../Tools/TrainingGround/src/departments/LayoutView.tsx), [`LevelGenDepartment.tsx`](../../../../Tools/TrainingGround/src/departments/LevelGenDepartment.tsx) (`4cb1a469`)
- [`Tools/TrainingGround/src/levelgen/trainingArenas.ts`](../../../../Tools/TrainingGround/src/levelgen/trainingArenas.ts), [`trainingArenas.test.ts`](../../../../Tools/TrainingGround/src/levelgen/trainingArenas.test.ts), [`Content/Data/LevelGen/accepted/L_Gen_Train_1.json`](../../../../Content/Data/LevelGen/accepted/L_Gen_Train_1.json), [`L_Gen_Train_2.json`](../../../../Content/Data/LevelGen/accepted/L_Gen_Train_2.json), [`L_Gen_Train_3.json`](../../../../Content/Data/LevelGen/accepted/L_Gen_Train_3.json) (`c9885564`)

---

## 1. До / После (`ref_01_02_depot`, seed 1, структурный прогон `noSim: true`)

Базовое «До» — сохранённый в репозитории слепок [`Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json`](../../../../Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json); «После» — актуальная генерация `generateLayout({ profile: ref_01_02_depot, seed: 1 })`.

| Метрика | До (`gen_ref_01_02_depot_1`) | После (`depot seed 1`) | Комментарий |
|---|---:|---:|---|
| Окна (`window`) | 0 | 61 | Фасады `B_shed`, `B_hall_n`, `B_office_s`, `B_warehouse`, `B_pump`, `B_ruin`, `B_ruin_annex`, `B_steelworks` (`120×90..210` см) |
| Бойницы (`loophole`) | 0 | 0 | В депо по умолчанию отключены; введены для `checkpoint`, `bunker`, `tower` (см. `L_Gen_Train_2`) |
| Перегородки (`partitions`) | 0 | 6 | 2-уровневый BSP в `B_shed`, `B_office_s`, `B_ruin` (этажи 0–1) |
| Комнаты достижимые / всего | 9 / 9 (виртуальные) | 15 / 15 | Реальные `GenRoom` с привязкой `door_ids` и графом переходов |
| Люки на крышу (`hatches`) | 0 | 2 | `B_hall_n`, `B_warehouse` (плюс внутренние лестницы `LAD_*_hatch`) |
| Парапеты крыш (`parapets`) | 0 | 20 | 4 парапета высотой 100 см по периметру каждой из 5 проходимых крыш |
| Линии огня на маршрут | 0 | 61 | Дальность `0.8–69.3 м` (средн. `24.2 м`), 0 заблокированных окон |
| Покрытие маршрута укрытиями | 83.4% | 85.9% | С учётом парапетов, стен и боксов в радиусе 4 м |
| Валидные кромки перелаза (`mantle_edges`) | 0 | 81 | `window`: 61, `parapet`: 20 (все в диапазоне `50..150` см) |
| Лестницы (`ladders`) | 4 | 6 | Включая люковые лестницы в `B_hall_n` и `B_warehouse` |
| Тактическая оценка (`tactical.score`) | 0.941 | 1.000 | Проверка `mockCritic`: `fits = true`, `similarity = 1.000` |

> Побочное улучшение: новый тактический критик сразу выявил, что старые южные окна `B_shed` смотрели в забор `F_compound_S_0` в пределах 150 см; перенесены на `['east', 'north']` (`blockedWindows = 0`). Кроме того, при генерации тренировочных арен выявлен и исправлен редкий конфликт BSP-перегородки 2-го уровня с центральным проёмом двери перегородки 1-го уровня (введён отступ `>= 1.8 м` и смещение дверей `t = 0.28`).

---

## 2. Расширение формата и подсистем Training Ground

### 2.1. Схема (`schema.ts`, `BUILDINGS.md`)
- **`WallOpening` (`door | window | loophole`)**: задаёт координаты `pos`, `floor`, размеры (`width`, `sill_height`, `top_height`) и угол обзора `yaw`. Обратная совместимость обеспечена функцией `unifyOpenings(b)`, которая прозрачно объединяет устаревший массив `doors` и новый `openings`.
- **`GenPartition` / `GenRoom`**: перегородки с собственными проёмами (дверями/смотровыми щелями) и явные комнаты (`bounds`, `door_ids`); `layerWalls()` при запросе этажа автоматически вырезает проёмы перегородок и предотвращает дублирование.
- **`BuildingFloor` / `FloorGap`**: описание межэтажных перекрытий и вырезов (`stairwell | landing | atrium | balcony | catwalk`).
- **`RoofHatch` / `RoofParapet`**: люки крыши с привязкой к `ladder_id` и парапеты (100 см).
- **`MantleEdge`**: кромки `50..150` см с нормалью и типом (`window | fence | parapet | obstacle | crate | partition | ledge`).

### 2.2. Тактическая оценка и критики (`tacticalEvaluate.ts`, `evaluate.ts`, `critic.ts`)
- **Граф достижимости комнат и крыши (BFS от `'street'`)**: проверяет связность через внешние двери, двери перегородок, лестничные пролёты, люки, проломы и приставные лестницы. Изолированные комнаты и недостижимые крыши понижают скор и переводят `mockCritic.fits` в `false`.
- **Чистота проёмов (`blockedWindows`)**: трассировка от 35 см до 150 см вдоль внешней нормали каждого окна/бойницы против стен и соседних зданий.
- **Линии огня (`firingLines`)**: трассировка лучей в секторе ±75° и дальностью до 150 м от каждого проёма на сэмплы обязательного маршрута; возвращаются сегменты с мин./средн./макс. дальностью.
- **Интеграция в `evaluateLayout`, `criticPrompt`, `jevState`, `mockCritic`**: добавлен блок `tactical` (включая сводку в 30-строчный `summary`).

### 2.3. Превью в Training Ground (`LayoutView.tsx`, `LevelGenDepartment.tsx`)
- В `LevelGenDepartment.tsx` добавлены быстрые переключатели этажей **`Все` / `Земля` / `Этаж 1` / `Этаж 2` / `Крыша`** (синхронизированы со списком слоёв), кнопка **`Линии огня`** и строка тактических метрик в блоке «Оценка».
- В `LayoutView.tsx` добавлена поэтажная фильтрация и отрисовка комнат, вырезов перекрытий (`FloorGap`), дверей перегородок, голубых окон, янтарных бойниц, оранжевых люков, зелёных парапетов, фиолетовых пунктирных кромок перелаза (`MantleEdge`) и пунктирных красных линий огня.

---

## 3. Три тренировочные арены (`Content/Data/LevelGen/accepted/`)

Все три арены генерируются детерминированно из [`trainingArenas.ts`](../../../../Tools/TrainingGround/src/levelgen/trainingArenas.ts) и проходят полную симуляцию отряда, `validateGenLayout`, тактические проверки и `mockCritic` (`fits = true`, `quality = 4/5`).

| Арена | Размер | Здания / Лестницы / Укрытия | Длина маршрута (`R_main` + варианты) | Проёмы / Перегородки / Люки / Парапеты | Линии огня | Перелазы (`mantle`) | Покрытие у маршрута |
|---|---:|---:|---:|---:|---:|---:|---:|
| **`L_Gen_Train_1`** *(двухэтажный жилой блок)* | `60 × 45 м` | 2 / 1 / 6 | `100 м` (`V1`: 65 м, `V2`: 54 м) | окна: 26, перег.: 8, парап.: 4 | 16 (`1.2–18.5 м`, средн. `8.9 м`) | 35 (`window`: 26, `parapet`: 4, `crate`: 2, `obstacle`: 2, `fence`: 1) | 76.3% |
| **`L_Gen_Train_2`** *(блокпост и вышка)* | `65 × 48 м` | 3 / 2 / 6 | `204 м` (`V1`: 99 м, `V2`: 59 м) | окна: 3, бойницы: 31, перег.: 9, люк: 1, парап.: 8 | 68 (`1.1–35.5 м`, средн. `15.1 м`) | 19 (`parapet`: 8, `obstacle`: 7, `window`: 3, `crate`: 1) | 82.5% |
| **`L_Gen_Train_3`** *(складской двор и пролёт)* | `75 × 55 м` | 3 / 2 / 5 | `255 м` (`V1`: 69 м, `V2`: 78 м) | окна: 41, перег.: 15, люк: 1, парап.: 8 | 54 (`1.3–31.3 м`, средн. `10.7 м`) | 54 (`window`: 41, `parapet`: 8, `obstacle`: 3, `crate`: 2) | 76.4% |

### Краткая планировка арен

1. **`L_Gen_Train_1` (`seed 101`)**: улица Восток–Запад по центру (`y = 22.5 м`); севернее — огороженный двухэтажный `B_house` (комнаты на обоих этажах, лестничная клетка на крышу с 4 парапетами, приставная лестница на востоке, окна на улицу и двор); южнее — одноэтажный гараж `B_garage` с перегородкой (`V1_south_garage`). Обязательный маршрут заводит отряд с улицы в дом, поднимает по внутреннему маршу через этаж 1 на крышу и спускает по приставной лестнице на восточный выход.
2. **`L_Gen_Train_2` (`seed 202`)**: центральная дорога под перекрёстным огнём узких бойниц двухэтажного `B_checkpoint` с севера (`loophole`) и одноэтажного ДОТа `B_bunker` с юго-востока; на юго-западе — полая двухъярусная наблюдательная вышка `B_tower` с внутренним люком `B_tower_roof_hatch` и лестницей `LAD_B_tower_hatch` на крышу; на дороге — змейка из мешков с песком (`sandbag`) и низких баррикад для перелаза.
3. **`L_Gen_Train_3` (`seed 303`)**: грузовой двор с двухэтажным `B_warehouse` (лестничный марш + люк на крышу + внешняя лестница), пристройкой `B_catwalk` (внешняя лестница на крышу, крышной пролом на 2-й этаж с двумя вырезами перекрытий `landing` и `catwalk` в `floors_detail`, напольный пролом спуска на 1-й этаж), южным складом `B_yard_shed` и погрузочными парапетами для перелаза.

---

## 4. Проверки после правок

- `npm run typecheck`: успешно (код возврата `0`).
- `npm run build` (после пункта 4): успешно (`✓ built in 8.88s`).
- `npm test`: **`340 / 340` пройдено**, `0` провалено, `0` пропущено (включая `combatBuildingsSchema.test.ts`, `combatBuildingsGenerate.test.ts`, `combatBuildingsEvaluate.test.ts`, `trainingArenas.test.ts`).
