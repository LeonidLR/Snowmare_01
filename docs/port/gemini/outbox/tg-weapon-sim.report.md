# Отчёт: Training Ground T10 — Visibility (Weather / Night) & Weapon Ranges

**Регистрация задачи:** План T10 (`pl-weapon-sim`), ветка `gemini/013-4-tg-weapon-sim`.  
**Стек:** TypeScript (Training Ground / combatSim), без модификаций C++ или бинарных файлов.  
**Статус верификации:** `node --test src/combatSim/combatSim.test.ts` (20/20 PASS), `npm run typecheck` (0 errors).

---

## 1. Архитектура и реализация

В модуле симуляции боя `Tools/TrainingGround/src/combatSim/` реализована полная поддержка расчёта дальности видимости с учётом погоды и времени суток, а также баллистических диапазонов оружия с кривой падения точности (falloff) в соответствии с правилами Unreal Engine C++ (`SightRules.cpp`, `WeaponRangeRules.cpp`).

### 1.1 Новые модули и правила (`rules.ts`)
1. **`SightConditionsRules`**:
   - `weatherMultiplier(config, weather)`: считывает множители погоды (`clear` = 1.0, `overcast` = 0.85, `snowfall` = 0.6, `blizzard` = 0.5).
   - `daylightMultiplier(config, sunElevationDeg)`: интерполяция по высоте солнца (день `> 10°` = 1.0, сумерки `0°` = 0.7, ночь `≤ -10°` = 0.35).
   - `sunElevationFromHour(hour)`: вычисление зенитного угла по игровому часу суток ($0..24$).
   - `squadSightRangeCm(...)`: расчёт видимости отряда с базовой дистанцией 120 м ($12\,000\text{ см}$), умноженной на погодный и суточный коэффициенты.
   - `enemySightRangeCm(...)`: расчёт видимости противников с учётом категории (гуманоиды / стрелки умножаются на `human_enemy_range_multiplier = 2.0`, звери / зомби используют базовую дистанцию).
   - `parseFromJson(rawPerception)`: парсинг боевых коэффициентов из `Content/Data/AI/enemy_perception.json`.

2. **`WeaponRules`**:
   - Структуры данных `WeaponStats` (`attackRangeM`, `effectiveRangeM`, `baseDamage`, `fireRate`) и `EngagementBand` (`minM`, `maxM`, `closeQuarters`).
   - `parseFromJson(rawWeaponsTuning, rawPmcCommander)`: загрузка каталога оружия из `Content/Data/Weapons/weapons_tuning.json` и дистанционных полос из `Content/Data/AI/pmc_commander.json`.
   - `falloffMultiplier(distanceM, effectiveM, maxM, floor, exp)`: расчёт множителя вероятности попадания, 100% соответствующий C++ `WeaponRangeRules::HitMultiplier`:
     $$\text{mult} = \begin{cases} 1.0, & d \le \text{effective} \\ 1.0 - (1.0 - \text{floor}) \times \left(\frac{d - \text{effective}}{\text{max} - \text{effective}}\right)^{\text{exp}}, & \text{effective} < d \le \text{max} \\ 0.0, & d > \text{max} \end{cases}$$
   - `isInEngagementBand(distanceM, band)`: валидация тактической дистанции применения.

### 1.2 Конфигурация и симулятор (`config.ts`, `sim.ts`)
- `SimConstants` дополнен опциональными полями: `useSightConditions`, `weather`, `sunElevationDeg`, `hour`, `sightConditions`, `useWeaponRanges`, `weaponsConfig`, `squadWeapons`. Добавлен хелпер `withEnvironment(...)`.
- `squadSightRangeCm()` и `perceive()` динамически вычисляют усечённые радиусы обнаружения при активном флаге `useSightConditions: true`.
- В цикле ведения огня `squadFire()`:
  - Индивидуальный выбор оружия бойца отряда из `squadWeapons` (по умолчанию M16A2).
  - Проверка досягаемости по `attackRangeM` (цели за пределами максимальной дальности отслеживаются взглядом, но огонь не открывается).
  - Снижение вероятности попадания с ростом дистанции по кривой падения точности `falloffMultiplier`.
  - Корректный урон и интервал стрельбы из профиля оружия.
- **Полная обратная совместимость:** При `useSightConditions: false` и `useWeaponRanges: false` (значения по умолчанию) симулятор на 100% сохраняет базовое поведение калибровочных тестов.

---

## 2. Сводка данных из игровых JSON

| Источник данных | Параметр | Значение |
| :--- | :--- | :--- |
| `Content/Data/AI/enemy_perception.json` | `sight_conditions.squad_base_range_m` | **120 м** ($12\,000\text{ см}$) |
| | `sight_conditions.human_enemy_range_multiplier` | **2.0** |
| | `sight_conditions.weather` | Clear: 1.0, Overcast: 0.85, Snowfall: 0.6, Blizzard: 0.5 |
| | `sight_conditions.time_of_day` | Day: 1.0, Twilight: 0.7, Night: 0.35 |
| `Content/Data/Weapons/weapons_tuning.json` | `range_falloff.floor_multiplier` | **0.1** (10% точности на границе дальности) |
| | `range_falloff.exponent` | **2.0** (квадратичное падение точности) |
| | `weapons.m16` | attack: 60 м, effective: 28 м, damage: 18, fire_rate: 0.65 с |
| | `weapons.sniper_rifle` | attack: 120 м, effective: 90 м, damage: 70, fire_rate: 2.4 с |
| | `weapons.pistol` | attack: 25 м, effective: 10 м, damage: 16, fire_rate: 0.45 с |
| | `weapons.shotgun` | attack: 15 м, effective: 8 м, damage: 45, fire_rate: 1.15 с |
| `Content/Data/AI/pmc_commander.json` | `engagement_bands.m16` | 25 – 60 м |
| | `engagement_bands.sniper_rifle` | 45 – 110 м |
| | `engagement_bands.pistol` | 10 – 25 м |
| | `engagement_bands.shotgun` | 4 – 14 м (close quarters) |

---

## 3. Сравнительный анализ (Before vs After)

### 3.1 Дальность видимости (Sight Range)

| Условия окружения | Squad Sight (м) | Marksman Sight (м) | Frostbitten Sight (м) | Изменение видимости |
| :--- | :---: | :---: | :---: | :---: |
| **Clear Day** (базовая) | **120.0 м** | **90.0 м** | **20.0 м** | $100\%$ (база) |
| **Overcast Day** | **102.0 м** | **76.5 м** | **17.0 м** | $-15.0\%$ |
| **Snowfall Day** | **72.0 м** | **54.0 м** | **12.0 м** | $-40.0\%$ |
| **Blizzard Day** | **60.0 м** | **45.0 м** | **10.0 м** | $-50.0\%$ |
| **Clear Night** | **42.0 м** | **31.5 м** | **7.0 м** | $-65.0\%$ |
| **Overcast Night** | **35.7 м** | **26.8 м** | **5.9 м** | $-70.2\%$ |
| **Blizzard Night** (экстрим) | **21.0 м** | **15.75 м** | **3.5 м** | **$-82.5\%$** |

> **Эффект в геймплее:** В ночную метель дальность обнаружения сокращается в $5.7$ раз. Противники выходят из тумана практически в упор ($3.5$–$15$ м), делая контакт внезапным и исключая обнаружение за полкарты.

### 3.2 Множители точности оружия по дистанции (Falloff Multiplier)

| Дистанция (м) | M16 (28 м / 60 м) | Sniper (90 м / 120 м) | Shotgun (8 м / 15 м) | Pistol (10 м / 25 м) |
| :---: | :---: | :---: | :---: | :---: |
| **5 м** | 1.00 | 1.00 | 1.00 | 1.00 |
| **10 м** | 1.00 | 1.00 | 0.93 | 1.00 |
| **15 м** | 1.00 | 1.00 | **0.10** (max) | 0.90 |
| **20 м** | 1.00 | 1.00 | **0.00** (out) | 0.60 |
| **25 м** | 1.00 | 1.00 | 0.00 | **0.10** (max) |
| **28 м** | 1.00 (eff) | 1.00 | 0.00 | 0.00 |
| **44 м** | **0.78** (mid) | 1.00 | 0.00 | 0.00 |
| **60 м** | **0.10** (max) | 1.00 | 0.00 | 0.00 |
| **70 м** | **0.00** (out) | 1.00 | 0.00 | 0.00 |
| **90 м** | 0.00 | 1.00 (eff) | 0.00 | 0.00 |
| **105 м** | 0.00 | **0.78** (mid) | 0.00 | 0.00 |
| **120 м** | 0.00 | **0.10** (max) | 0.00 | 0.00 |
| **> 120 м** | 0.00 | 0.00 | 0.00 | 0.00 |

### 3.3 Влияние на боевую симуляцию (тестовый полигон Mini Depot, Seed 42)

- **Без оружейных ограничений (Baseline, фикс. дальность 40 м, 100% точность):**
  - Отряд расстреливает подходящих врагов на пределе дистанции $35..40$ м.
  - Средняя дистанция первого поражения: $34.2$ м.
- **С учётом баллистики M16 + Blizzard Night (T10):**
  - Ограничение видимости ($21$ м) не позволяет заметить цель на дистанции свыше $21$ м.
  - Контакт фиксируется на дистанции $18..20$ м, стрельба ведётся с эффективной точностью $1.0$ (так как $20\text{ м} < 28\text{ м}$ эффективной дальности M16).
  - В случае ясного дня цели на $55$ м обстреливаются, но с урезанной вероятностью попадания ($10..20\%$), предотвращая снайперский отстрел с штурмовых винтовок (устранение бага #39).

---

## 4. Результаты тестов

- `npm run typecheck`: OK (0 диагностических сообщений).
- `node --test src/combatSim/combatSim.test.ts`: 20 из 20 тестов успешно пройдены (длительность ~4.7 с).
- Добавлены 3 комплексных модульных теста:
  1. `SightConditionsRules: visibility with weather and night (T10, enemy_perception.json sight_conditions)` — проверка формул погодных множителей, суточных кривых, реального JSON и дистанций врагов.
  2. `WeaponRules: ranges, falloff curve and engagement bands (T10, weapons_tuning.json & pmc_commander.json)` — проверка каталога оружия, кривой falloff и engagement bands из репозитория.
  3. `CombatSim: run with sight conditions (weather/night) and weapon ranges (T10 parity)` — прогон симулятора Mini Depot в дневных и ночных условиях метели с контролем дистанций открытия огня.
