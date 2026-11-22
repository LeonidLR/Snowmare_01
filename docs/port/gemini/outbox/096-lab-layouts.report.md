# Отчет по задаче 096: Создание трех тестовых макетов лаборатории (Smart Enemies T-02)

- **Задача**: 096 - Three small lab layouts that reproduce the designer's screenshots (smart enemies T-02, layouts part).
- **Ветка**: `gemini/096-lab-layouts`
- **Рабочее дерево**: `CodexTactics-gemday`
- **Автор**: Gemini (3.8 Flash)
- **Дата**: 2026-11-22

---

## 1. Сводка выполненных работ

В соответствии со спецификацией **T-02** из `docs/design/gdd/smart-enemies-roadmap.md:171-193`, заданием `docs/port/gemini/inbox/096-lab-layouts.md` и ревью лида `docs/port/gemini/reviews/REVIEW_096_2026-11-22.md`, генератор макетов выделен в скрипт `Tools/TrainingGround/scripts/build-lab-layouts.ts`, а проверки вынесены в чистый тест `Tools/TrainingGround/src/levelgen/labLayouts.test.ts`.

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
- **Перекресток**: в центре `[0, 0]`, открытая улица `Z_ST_crossroads` с пересечением направлений.
- **Огороженный периметр на ближней стороне**:
  - Расположен в `X ∈ [-2200, -800]`, `Y ∈ [400, 1600]`. Высота стен 300 см.
  - Ровно **1 проход** шириной 200 см на восточной стене (`X = -800`, `Y ∈ [900, 1100]`), обращенный к перекрестку.
- **Укрытия на дальней стороне**:
  - 5 укрытий ($\ge 4$ по T-02) на дальней стороне (`X > 500`):
    - `cov_cross_1`: ящик `[1000, -250]`, номинальная высота **85 см** (`low_mantle`).
    - `cov_cross_2`: ящик `[1000, 250]`, номинальная высота **85 см** (`low_mantle`).
    - `cov_cross_3`: мешки с песком `[1500, -400]`, номинальная высота **95 см** (`low_vault`).
    - `cov_cross_4`: мешки с песком `[1500, 400]`, номинальная высота **95 см** (`low_vault`).
    - `cov_cross_5`: джерси `[2000, 0]`, номинальная высота **95 см** (`low_vault`).

```
       North (+X)
          |
     [+3500, 0] (GOAL)
          |
   [C4]   |   [C3]       (Far side covers: C1..C5 >= 4, H=85/95 cm)
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
- **Старт**: `[-2500, 0]`.
- **Цель (Fix A1)**: `[+1350, 0]` в горловине переулка в открытом пространстве (на 150 см перед сплошной стеной `B_two_storey` на `X = 1500`). Подтверждено юнит-тестом: цель не пересекает ни одну стену и находится вне периметров зданий.
- **Переулок (Lane)**:
  - Ширина ровно **4.0 м** (400 см, в диапазоне **3–5 м** по T-02) между фасадом `B_low` (`Y = 200`) и южной ограждающей стеной `W_alley_south` (`Y = -200`).
- **Одноэтажное здание (`B_low`) (Fix A3)**:
  - След: `X ∈ [-1500, 1200]`, `Y ∈ [200, 1600]`, высота 300 см, 1 этаж.
  - Дверь и окна расположены на стене переулка (`Y = 200`, `doorSides: ['west']`, `windowSides: ['west']` в координатах профиля, нормаль обращена в сторону переулка на юг).
- **Обход сзади (Back route)**:
  - Вариантный маршрут `V1_back_route` проходит к северу от `B_low` по открытому пространству `Y ∈ [1600, 2300]` и выходит к цели `[1350, 0]`.
- **Двухэтажное здание (`B_two_storey`) (Fix A2)**:
  - Расположено у горловины переулка: `X ∈ [1500, 2700]`, `Y ∈ [-1000, 1000]`.
  - Высота 600 см (2 этажа), эксплуатируемая крыша (`roof_walkable: true`).
  - Парапет по периметру крыши высотой 100 см (`parapets`).
  - Лестница (`LAD_B_two_storey`): установлена на фасаде горловины переулка (`X = 1490`, `Y = -250`), обращена в сторону переулка на юг (`yaw = 180` в UE); **16 ступеней** ($\ge 7$ по T-02) с цоколем 17.6 см.

```
       North (+X)
          |
       +---+---+
       | B_two |  (2-storey, H=600, roof walkable, parapets)
       | storey|  (Ladder on mouth face X=1500: 16 rungs >= 7, yaw 180)
       +---+---+
           [LAD]
      [+1350, 0] (GOAL @ lane's mouth, outside solid wall)
          |
     ============== (B_low north wall)
     |   B_low    | (1-storey, H=300)   <--- Back route round the back
     | (building) |                          around Y in [1600, 2300]
     ==[W]==[D]==== (B_low lane-side wall @ Y = 200: doors & windows)
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
    - `cov_bridge_near_1`: ящик `[-450, -100]`, высота **85 см** (`low_mantle`).
    - `cov_bridge_near_2`: ящик `[-450, 100]`, высота **85 см** (`low_mantle`).
- **Препятствие для преодоления (Climbable mantle obstacle)**:
  - Ящик высоты `low_mantle` (**85 см**) рядом с дальним концом моста:
    - `cov_bridge_far_mantle`: `[450, 250]`, высота 85 см, зарегистрированное ребро `me_cov_bridge_far_mantle`.
- **Здание с окнами на настил (`B_overlook`) (Fix B1)**:
  - След: `X ∈ [650, 1800]`, `Y ∈ [-1800, -300]`.
  - Окна установлены на стене `Y = -300` (`windowSides: ['east']` в координатах профиля), обращены на восток (`yaw = 90` в UE) прямо на настил моста (`Y ∈ [-200, 200]`).
  - Геометрическая проверка LOS: луч из окон вдоль нормали (+Y) напрямую пересекает створ настила моста на расстоянии 1–3 м.

```
       North (+X)
          |
      [+2500, 0] (GOAL)
          |
          |   +----------+
          |   |B_overlook| (Wall @ Y = -300)
          |   +-[W]-[W]--+ (Windows facing East +Y onto the deck, yaw 90)
          |
       [Mantle] (Climbable obstacle @ [450, 250], H=85cm)
  ~~~~~~~~|~~~~~~~~ (Cut edge X = 300)
  ~ CUT   |[DECK] ~ (Bridge deck 4.0 m wide, drop 300 cm)
  ~~~~~~~~|~~~~~~~~ (Cut edge X = -300)
       [C1] [C2]    (Two covers at near end: crates @ X = -450, H=85cm)
          |
      [-2500, 0] (START)
          |
       South (-X)
```

---

## 3. Отделение генератора от тестов и очистка метаданных

1. **Генерация и экспорт (Fix 3)**:
   - Полная логика сборки и экспорта перенесена в скрипт `Tools/TrainingGround/scripts/build-lab-layouts.ts`.
   - Тест `Tools/TrainingGround/src/levelgen/labLayouts.test.ts` стал строго read-only: при запуске `npm test` файлы на диске не перезаписываются, что гарантирует честность проверок.
   - Добавлены ассерты `assertGoalNotInsideObstacles`: проверяется, что цели миссий не попадают внутрь периметров зданий и не пересекают стены.
2. **Честные метаданные (Fix 4)**:
   - Из файлов `gen_lab_*.meta.json` удалены вымышленные вердикты Jev и фиктивный счет 1.
   - Мета-файлы содержат актуальный таймштамп, текстовую сводку и примечание: `"Standalone combat lab layout for smart enemies T-02 (layouts part); score and AI verdicts not computed"`.

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

| Claim | Evidence | Details |
|---|---|---|
| Спецификация T-02 соблюдена с учетом замечаний ревью | node --test src/levelgen/labLayouts.test.ts (PASS 4/4) | Проверены размеры <= 80x80m, дистанции, 1 проход ограды, дверь/окна Alley на переулке, лестница на горловине, окна Bridge на настил, цели вне стен |
| Тесты слоев и списка уровней проходят | node --test src/levelgen/layers.test.ts (8/8) && node --test src/utils/levelList.test.ts (6/6) | Макеты L_Lab_* валидны по слоям (roads, facade) и группам уровней |
| Typecheck TypeScript без ошибок | npm run typecheck (tsc exit code 0) | 0 ошибок компиляции в Tools/TrainingGround |
| Общий тестовый набор Training Ground проходит | npm test (PASS 597/597) | 0 падений во всех тестовых файлах |
| Отсутствие пробельных ошибок git | git diff --check (0 замечаний) | 0 пробельных ошибок |
| Безопасность и границы Jev | python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini | Boundary Breach: PASS, Clean workspace |

---

## 6. Что НЕ делалось
- Не запускался Unreal Editor и скрипты `build_levelgen_map.py` (сборка карт за Lead по регламенту T-02).
- Не затрагивались существующие карты `X2`, `X4`, `X6`, `Depot`, `BgenPlans`, `Content/Maps`.
- Не проводились изменения C++ кода.
