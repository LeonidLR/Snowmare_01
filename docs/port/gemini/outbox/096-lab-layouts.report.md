# Отчет по задаче 096: Создание трех тестовых макетов лаборатории (Smart Enemies T-02)

- **Задача**: 096 - Three small lab layouts that reproduce the designer's screenshots (smart enemies T-02, layouts part).
- **Ветка**: `gemini/096-lab-layouts`
- **Рабочее дерево**: `CodexTactics-gemday`
- **Автор**: Gemini (3.8 Flash)
- **Дата**: 2026-11-22

---

## 1. Сводка выполненных работ

В соответствии со спецификацией **T-02** из `docs/design/gdd/smart-enemies-roadmap.md:171-193` и заданием `docs/port/gemini/inbox/096-lab-layouts.md`, в генераторе уровней Training Ground реализован модуль `Tools/TrainingGround/src/levelgen/labLayouts.ts` и юнит-тесты `labLayouts.test.ts`.

Созданы и экспортированы:
1. **Три принятых макета (Accepted Layouts)**:
   - `Content/Data/LevelGen/accepted/gen_lab_crossroads.json` (+ `.meta.json`)
   - `Content/Data/LevelGen/accepted/gen_lab_alley.json` (+ `.meta.json`)
   - `Content/Data/LevelGen/accepted/gen_lab_bridge.json` (+ `.meta.json`)
2. **Экспорты тактических карт**:
   - `Content/Data/LevelGen/tactical/L_Lab_Crossroads.tactical.json`
   - `Content/Data/LevelGen/tactical/L_Lab_Alley.tactical.json`
   - `Content/Data/LevelGen/tactical/L_Lab_Bridge.tactical.json`
3. **Экспорты целей миссий**:
   - `Content/Data/LevelGen/objectives/L_Lab_Crossroads.objectives.json`
   - `Content/Data/LevelGen/objectives/L_Lab_Alley.objectives.json`
   - `Content/Data/LevelGen/objectives/L_Lab_Bridge.objectives.json`
4. **Экспорты пропсов**:
   - `Content/Data/LevelGen/props/L_Lab_Crossroads.props.json`
   - `Content/Data/LevelGen/props/L_Lab_Alley.props.json`
   - `Content/Data/LevelGen/props/L_Lab_Bridge.props.json`
5. **Конфигурации уровней**:
   - `Content/Data/LevelJson/L_Lab_Crossroads.json`
   - `Content/Data/LevelJson/L_Lab_Alley.json`
   - `Content/Data/LevelJson/L_Lab_Bridge.json`

---

## 2. Спецификация и ASCII-схемы макетов

### 2.1 L_Lab_Crossroads (`gen_lab_crossroads.json`)
- **Размер**: 75 × 75 м (границы: `min: [-3750, -3750]`, `max: [3750, 3750]` см, строго $\le 80 \times 80$ м).
- **Дистанция старт — цель**:
  - Старт: `[-3500, 0]`
  - Цель: `[+3500, 0]`
  - Прямое расстояние: ровно **70.0 м** (в диапазоне **60–80 м** по T-02).
- **Перекресток**: в центре `[0, 0]`, открытая зона `open_square` 20 × 20 м с пересечением улиц.
- **Огороженный периметр на ближней стороне**:
  - Расположен в `X ∈ [-2200, -800]`, `Y ∈ [400, 1600]`. Высота стен 300 см.
  - Ровно **1 проход** шириной 200 см на восточной стене (`X = -800`, `Y ∈ [900, 1100]`), обращенный к перекрестку.
- **Укрытия на дальней стороне**:
  - 5 укрытий ($\ge 4$ по T-02) на дальней стороне (`X > 500`):
    - `cov_cross_1`: ящик `[1000, -250]`, высота 100 см (low mantle).
    - `cov_cross_2`: ящик `[1000, 250]`, высота 100 см (low mantle).
    - `cov_cross_3`: мешки с песком `[1500, -400]`, высота 100 см.
    - `cov_cross_4`: мешки с песком `[1500, 400]`, высота 100 см.
    - `cov_cross_5`: джерси `[2000, 0]`, высота 100 см.

```
       North (+X)
          |
     [+3500, 0] (GOAL)
          |
   [C4]   |   [C3]       (Far side covers: C1..C5 >= 4)
      [C5]|
   [C2]   |   [C1]
          |
---+------+------+---  (Crossroads [0, 0])
   |      |      |
---+------+------+---
          |   +-------+
          |   |       |  (Walled enclosure: X in [-2200, -800])
          |   |       |  (East wall has exactly 1 gap facing [0, 0])
          |   +--- ---+
          |
     [-3500, 0] (START, 70m from Goal)
          |
       South (-X)
```

---

### 2.2 L_Lab_Alley (`gen_lab_alley.json`)
- **Размер**: 60 × 50 м (границы: `min: [-3000, -2500]`, `max: [3000, 2500]` см, $\le 80 \times 80$ м).
- **Старт**: `[-2500, 0]`, **Цель**: `[+1500, 0]` у горловины переулка.
- **Переулок (Lane)**:
  - Ширина ровно **4.0 м** (400 см, в диапазоне **3–5 м** по T-02) между южным фасадом `B_low` (`Y = 200`) и южной ограждающей стеной `W_alley_south` (`Y = -200`).
- **Одноэтажное здание (`B_low`)**:
  - След: `X ∈ [-1500, 1200]`, `Y ∈ [200, 1600]`, высота 300 см, 1 этаж.
  - Дверь и окна выходят на юг в переулок.
- **Обход сзади (Back route)**:
  - Вариантный маршрут `V1_back_route` проходит к северу от `B_low` по открытому пространству `Y ∈ [1600, 2300]` и выходит к горловине переулка.
- **Двухэтажное здание (`B_two_storey`)**:
  - Расположено у горловины переулка: `X ∈ [1500, 2700]`, `Y ∈ [-1000, 1000]`.
  - Высота 600 см (2 этажа), эксплуатируемая крыша (`roof_walkable: true`).
  - Парапет по периметру крыши высотой 100 см (`parapets`).
  - Лестница (`LAD_B_two_storey`) у горловины переулка на западном фасаде: **16 ступеней** ($\ge 7$ по T-02) с цоколем 17.6 см (точный расчет по формуле `ladderForLevels(2)`).
- **Цель**: расположена точно у выхода из переулка (`[1500, 0]`).

```
       North (+X)
          |
      +---+---+
      | B_two |  (2-storey, H=600, roof walkable, parapets)
      | storey|  (Ladder on west face: 16 rungs >= 7)
      +---+---+
          |
     [+1500, 0] (GOAL @ lane's mouth)
          |
    ============== (B_low north wall)
    |   B_low    | (1-storey, H=300)   <--- Back route round the back
    | (building) |                          around Y in [1600, 2300]
    ============== (B_low south wall @ Y = 200)
    .............. (Lane: width 4.0 m in 3-5 m range)
    -------------- (South wall @ Y = -200)
          |
     [-2500, 0] (START)
          |
       South (-X)
```

---

### 2.3 L_Lab_Bridge (`gen_lab_bridge.json`)
- **Размер**: 60 × 50 м (границы: `min: [-3000, -2500]`, `max: [3000, 2500]` см, $\le 80 \times 80$ м).
- **Старт**: `[-2500, 0]`, **Цель**: `[+2500, 0]`.
- **Срез / ров (Cut)**:
  - Проходит поперек карты: `X ∈ [-300, 300]` (ширина **6.0 м**), `Y ∈ [-2300, 2300]`.
  - Уровень `-1`, глубина перепада **300 см** (`floor_z = -300`, в пределах drop rule).
- **Мост / настил (Bridge deck crossing)**:
  - Переход через срез: `X ∈ [-300, 300]`, `Y ∈ [-200, 200]`, ширина **4.0 м** (400 см).
- **Укрытия на ближнем конце**:
  - Ровно **2 укрытия** на ближнем конце (`X < -300`):
    - `cov_bridge_near_1`: ящик `[-450, -100]`.
    - `cov_bridge_near_2`: ящик `[-450, 100]`.
- **Препятствие для преодоления (Climbable mantle obstacle)**:
  - Укрытие высоты стола MANTLING (100 см) рядом с дальним концом моста:
    - `cov_bridge_far_mantle`: `[450, 250]`, высота 100 см, зарегистрированное ребро `me_cov_bridge_far_mantle`.
- **Здание с окнами на настил (`B_overlook`)**:
  - След: `X ∈ [650, 1800]`, `Y ∈ [-1800, -300]`.
  - Отстоит от среза на расстояние $> 250$ см (`reach = 150 + 100 = 250`), удовлетворяя валидатор отсутствия коллизий со стенами среза.
  - Окна на южном фасаде (`X = 650`, `yaw = 180`) смотрят прямо на юг на настил моста.

```
       North (+X)
          |
     [+2500, 0] (GOAL)
          |
          |   +----------+
          |   |B_overlook| (Windows facing South onto the deck)
          |   +----------+
          |
       [Mantle] (Climbable obstacle @ [450, 250], H=100cm)
  ~~~~~~~~|~~~~~~~~ (Cut edge X = 300)
  ~ CUT   |[DECK] ~ (Bridge deck 4.0 m wide, drop 300 cm)
  ~~~~~~~~|~~~~~~~~ (Cut edge X = -300)
       [C1] [C2]    (Two covers at near end: crates @ X = -450)
          |
     [-2500, 0] (START)
          |
       South (-X)
```

---

## 3. Ограничения модели данных генератора

В соответствии со stop-rule задачи T-02 ("If the generator data model cannot express an element... list the missing module in the report"):

1. **Walled enclosure with one gap**:
   - Полностью выражено с помощью составных сегментов стен `addWall` с разрывом 200 см на восточной стене. Валидатор `validateGenLayout` принимает конструкцию без замечаний.
2. **Lane 3-5 m wide**:
   - Полностью выражено взаимным позиционированием южного фасада здания и отдельной стены переулка (ширина 400 см = 4.0 м).
3. **Walkable roof, parapet and ladder >= 7 rungs**:
   - Полностью выражено свойствами `roof_walkable: true`, генерацией `RoofParapet` по периметру и расчетом лестницы `ladderForLevels(2)` (16 ступеней).
4. **Cut with drop and bridge crossing**:
   - Полностью выражено блоком `layout.cut` (уровень -1, глубина 300 см, переправа `crossing_deck` вида `bridge`).
5. **Climbable obstacle with mantling height**:
   - Полностью выражено через `GenCover` высотой 100 см с зарегистрированным ребром `MantleEdge` (диапазон 50–150 см).
6. **Отсутствующие модули**:
   - Ни один модуль не потребовалось пропускать — все заявленные элементы спецификации T-02 полностью поддержаны существующей моделью `codex.levelgen_layout/1`.

---

## 4. Команды для сборки карт в Unreal (для Lead)

После слияния ветки Lead выполняет сборку уровней скриптом генератора (редактор дизайнера должен быть закрыт):

```powershell
# 1. Сборка L_Lab_Crossroads
python Scripts/Editor/build_levelgen_map.py -Layout gen_lab_crossroads -Map L_Lab_Crossroads

# 2. Сборка L_Lab_Alley
python Scripts/Editor/build_levelgen_map.py -Layout gen_lab_alley -Map L_Lab_Alley

# 3. Сборка L_Lab_Bridge
python Scripts/Editor/build_levelgen_map.py -Layout gen_lab_bridge -Map L_Lab_Bridge
```

---

## 5. Валидация и гейты качества

| Проверка | Команда | Результат | Примечания |
|---|---|---|---|
| Спецификация T-02 | `node --test src/levelgen/labLayouts.test.ts` | **PASS (4/4)** | Все проверки размеров, дистанций, укрытий, ступеней и ребер |
| Typecheck TS | `npm run typecheck` | **PASS (exit 0)** | 0 ошибок компиляции TypeScript |
| Общий тестовый набор | `npm test` | **PASS (593/593)** | 0 падений во всех 62 тестовых файлах |
| Проверка списков уровней | `node --test src/utils/levelList.test.ts` | **PASS (6/6)** | Новые уровни `L_Lab_*` корректно включены в группу Labs & tests |
| Форматирование git | `git diff --check` | **PASS (0 whitespace)** | 0 замечаний |
| Triage Audit (Jev) | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` | **PASS** | Boundary Breach: PASS, Risk Rating: 0.3 / 4 |

---

## 6. Что НЕ делалось
- Не запускался Unreal Editor и скрипты `build_levelgen_map.py` (сборка карт за Lead по регламенту T-02).
- Не затрагивались существующие карты `X2`, `X4`, `X6`, `Depot`, `BgenPlans`, `Content/Maps`.
- Не проводились изменения C++ кода.
