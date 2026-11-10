# Отчёт по задаче 004: Training Ground pass 1 (TypeScript)

- **Дата**: 10 ноября 2026
- **Агент**: Gemini (`$env:CODEX_AGENT = "gemini"`)
- **Ворктри**: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-gemtgts`
- **Ветка**: `gemini/tg-pass1-ts` (ответвлена от `origin/main`)
- **Хеш коммита**: `39bd71b99cd0d67873a7ddbb4dd8bb52cb21a0a8`
- **Затронутые файлы**:
  - `Tools/TrainingGround/src/types.ts`
  - `Tools/TrainingGround/src/utils/levelSettings.ts`
  - `Tools/TrainingGround/src/utils/levelSettings.test.ts`
  - `Tools/TrainingGround/src/utils/stealthCombat.test.ts`
  - `Tools/TrainingGround/src/components/LevelSettingsPanels.tsx`
  - `Tools/TrainingGround/src/components/LevelOperativesPanel.tsx` *(новый файл)*
  - `Tools/TrainingGround/src/components/SquadLoadoutControls.tsx`
  - `Tools/TrainingGround/src/App.tsx`
  - `Tools/TrainingGround/vite.config.ts`
  - `Scripts/run_simulations.bat`

---

## 1. Выполненные пункты задачи

### 1.1. Панель окружения и погоды уровня (`LevelEnvironmentPanel`)
- В компонент `LevelSettingsPanels.tsx` внедрена секция **"Погода и окружение уровня"** (`LevelEnvironmentPanel`).
- **Поля конфигурации**:
  - Чекбокс фиксации погоды (`locked`).
  - Чекбокс **«Погода уровня важнее перенесённой»** (`override_carry`). Сериализуется строго в соответствии с контрактом: при `false` ключ не пишется в JSON, при `true` пишется `"override_carry": true`.
  - Выбор климатической зоны (`climate`: `arctic` / `temperate`).
  - Выбор погоды (`weather`): динамический список пресетов в зависимости от выбранного климата (для Арктики: `clear`, `overcast`, `light_snow`, `snowstorm`, `blizzard`, `whiteout`; для умеренного: `clear`, `overcast`, `fog`, `light_rain`, `heavy_rain`, `thunderstorm`).
  - Время суток (`start_hour`, 0–23).
  - Параметры покрытия: снег (`snow_cover`, 0.0–1.0), влажность (`wetness`, 0.0–1.0), лужи (`puddles`, 0.0–1.0).
  - Смещение экспозиции (`exposure_bias`, -4.0..+4.0 EV).
  - Визуальные переключатели (`visuals`): осадки (`precipitation`), туман (`fog`), ветер (`wind`).
- Валидация диапазонов значений с выводом подсказок об ошибках в интерфейсе.

### 1.2. Селектор вооружения оперативников уровня (`LevelOperativesPanel` -> `loadouts`)
- В компоненте `LevelOperativesPanel.tsx` реализован редактор арсенала бойцов на уровне:
  - Структура в JSON: `"loadouts": { "<key>": ["<weapon_id>", ...] }`.
  - Первый элемент в массиве является основным оружием (помечен бейджем `Основное`).
  - Поддерживаются ключи оперативников по ролям (`commander`, `engineer`, `medic_sapper`, `recruit`) и по слотам (`slot_0`, `slot_1`, `slot_2`).
  - Идентификаторы оружия берутся из актуального пула игры (`weapons_tuning.json`): `m16`, `pistol`, `shotgun`, `sniper_rifle`, `rpg7`, `grenade`.
  - Возможность добавления оружия в арсенал, удаления, а также перемещения вверх/вниз для смены приоритета (первое оружие автоматически становится основным).
  - Опция "Использовать глобальный пресет (отряда)" — при отключении для оперативника его ключ удаляется из `"loadouts"` уровня, и боец использует дефолтное оружие из настроек кампании/отряда.

### 1.3. Редактор стартового комплекта снаряжения уровня (`LevelOperativesPanel` -> `kit`)
- Реализован редактор стартового комплекта на уровне:
  - Структура в JSON: `"kit": { "<key>": { "grenades": n, "medkits": n, "heat_packs": n, "ammo": { "<weapon_id>": n } } }`.
  - Корневой чекбокс **«Снаряжение уровня важнее перенесённого»** (`"kit_override_carry": true`).
  - Для каждого оперативника настраивается количество осколочных гранат, аптечек, термопакетов и резервных патронов под каждое доступное оружие.
  - При отсутствии значения отображается плейсхолдер `"по умолчанию"`; пустое или отрицательное поле не пишется в JSON, сохраняя игровое значение по умолчанию.
- **Исправление в `SquadLoadoutControls.tsx`**:
  - Гранаты: дефолтное значение исправлено с `2` на `-1` (по умолчанию). Ввод пустого значения или `-1` корректно трактуется как системный дефолт.
  - Поля "Консервы" и "Спички" явно помечены как `(не в игре / только боты)`, предотвращая путаницу дизайнера с механиками основной игры.

### 1.4. Устранение дефектов Training Ground (Bugs C & D)
- **Bug C (waves-tab save hazard)**:
  - В `vite.config.ts` эндпоинт `/api/save-stage` переведен на интеллектуальный патчинг через функцию `patchJsonText()`.
  - Теперь при сохранении этапа сохраняются все комментарии, отступы, BOM, переносы строк CRLF и сторонние ключи, не затронутые текущим редактированием.
- **Bug D ("Run simulator" hazard)**:
  - В `vite.config.ts` в эндпоинте `/api/run-bot` устранена критическая уязвимость: запуск симулятора для других карт больше **не перезаписывает** `level_01_outpost.json`.
  - Изменения сохраняются строго в файл выбранного уровня (через `patchJsonText`).
  - Определяется карта (из `campaign.json`, эвристик `L_Gen_*`, `L_*` или дефолта) и путь к JSON файла этапа.
  - В `Scripts/run_simulations.bat` добавлена поддержка параметров `%5` (Map) и `%6` (LevelJson), которые передаются в `bot_run.ps1` (`-Map` и `-LevelJson`).

---

## 2. Результаты тестов и верификации

### 2.1. Модульные тесты (`npm test`)
Запуск Vitest показал 100% прохождение всех тестов:
```
Test Files  15 passed (15)
     Tests  260 passed (260)
  Start at  12:35:10
  Duration  3.90s (transform 324ms, setup 0ms, collect 388ms, tests 83ms, environment 0ms, prepare 283ms)
```

Новые модульные тесты:
- `src/utils/levelSettings.test.ts`:
  - `getEnvironment` / `setEnvironmentKey`: валидация и сериализация полей `override_carry`, `exposure_bias`, погоды, климата.
  - `getLoadouts` / `setOperativeLoadout`: валидация массивов вооружения, проверка первого элемента как основного оружия.
  - `getKit` / `setOperativeKit` / `kit_override_carry`: проверка работы с частичными данными, сохранение пропущенных полей как дефолтных.
  - `validateLevelSettings`: валидация ошибок при выходе за диапазоны.
  - `round-trip JSON preservation`: сохранение всех неизвестных ключей и форматирования при модификации только `environment`, `loadouts` и `kit`.
- `src/utils/stealthCombat.test.ts`:
  - Проверка `patchJsonText`: на реальном тексте уровня гарантируется байтовая идентичность незатронутых блоков и корректное наложение новых секций.

### 2.2. Проверка типов (`npm run typecheck`)
- TypeScript компилятор завершился с кодом 0 (0 ошибок):
```
> training-ground@0.0.0 typecheck
> tsc --noEmit
```

### 2.3. Сборка приложения (`npm run build`)
- Сборка production-бандла завершилась успешно за 7.86 с:
```
vite v6.4.1 building for production...
✓ 1877 modules transformed.
dist/index.html                   0.85 kB │ gzip:   0.45 kB
dist/assets/index-D56o66c8.css   57.65 kB │ gzip:  10.22 kB
dist/assets/index-k8Hk86sD.js   661.12 kB │ gzip: 187.35 kB
✓ built in 7.86s
```

### 2.4. Защита файлов проекта
- Файл `Content/Data/LevelJson/L_Gen_Depot_1.json` не модифицировался.
- В тестах использовалась изолированная копия в памяти (`mockLevelText`).

---

## 3. Скриншоты интерфейса

Скриншоты захвачены в headless-режиме из работающего приложения Training Ground:

1. **Общий вид панели уровня с секцией погоды и оперативников**:
   - `docs/port/gemini/outbox/screenshots/tg_overview.png`
   ![TG Overview](screenshots/tg_overview.png)

2. **Полный экран Training Ground с раскрытыми секциями Environment и Operatives**:
   - `docs/port/gemini/outbox/screenshots/tg_full.png`
   ![TG Full](screenshots/tg_full.png)

---

## 4. Резюме и статус

- Все 4 пункта задания 004 выполнены в полном объёме на TypeScript/Vite стороне без затрагивания C++ и проектных бинарных ассетов.
- Код закоммичен в ветку `gemini/tg-pass1-ts` (хеш `39bd71b99cd0d67873a7ddbb4dd8bb52cb21a0a8`).
- Репозиторий готов к интеграции с C++ частью (`feat/tg-pass1-cpp`).
