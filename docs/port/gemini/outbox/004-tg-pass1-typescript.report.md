# Отчёт по задаче 004: Training Ground pass 1 (TypeScript)

- **Дата**: 10 ноября 2026
- **Агент**: Gemini (`$env:CODEX_AGENT = "gemini"`)
- **Ворктри**: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-gemtgts`
- **Ветка**: `gemini/tg-pass1-ts` (ответвлена от `origin/main`)
- **Коммиты**:
  - `39bd71b99cd0d67873a7ddbb4dd8bb52cb21a0a8` — Реализация панелей погоды, вооружения и снаряжения уровня, фиксы багов C & D.
  - `b24fc8635f85a427f69ec861dd0255deadee949d` — Устранение 3 замечаний ревью (сохранение неизвестных ключей kit, удаление `kit_override_carry`, защита от командных инъекций в боте).
- **Затронутые файлы**:
  - `Tools/TrainingGround/src/types.ts`
  - `Tools/TrainingGround/src/utils/levelSettings.ts`
  - `Tools/TrainingGround/src/utils/levelSettings.test.ts`
  - `Tools/TrainingGround/src/utils/stealthCombat.test.ts`
  - `Tools/TrainingGround/src/utils/botRunner.ts` *(новый файл)*
  - `Tools/TrainingGround/src/utils/botRunner.test.ts` *(новый файл)*
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

## 2. Устранение замечаний ревью (004-tg-pass1-ts-review.md)

1. **Сохранение неизвестных per-operative ключей в `setOperativeKit`**:
   - Функция начинает очистку с клонирования существующего объекта бойца: `const cleaned: Record<string, any> = { ...existingOp, ...kit };`.
   - Модифицируются и удаляются только 4 стандартных поля (`grenades`, `medkits`, `heat_packs`, `ammo`), любые кастомные/дизайнерские поля (например, перки, броня) бережно сохраняются.
   - Добавлен тест в `levelSettings.test.ts`.

2. **Удаление фиктивного ключа `kit_override_carry`**:
   - Из кода полностью удалены `getKitOverrideCarry`, `setKitOverrideCarry`, чекбокс из `LevelOperativesPanel.tsx`, поле из интерфейса `LevelConfig` и соответствующие тесты.
   - В игре действует только `environment.override_carry`.

3. **Защита от командных инъекций в `/api/run-bot` (`vite.config.ts`)**:
   - Реализован отдельный валидатор `sanitizeBotRunArgs` в `src/utils/botRunner.ts`:
     - Строгий запрет спецсимволов командной строки: `/["&|<>^%!;\r\n\t`'$]/`.
     - Белый список для `runs`: целое число от 1 до 1000 (`/^\d+$/`).
     - Белый список для `profile` и `loadout`: заглавные латинские буквы, цифры и подчёркивание (`/^[A-Z0-9_]+$/`).
     - Белый список для `speed`: положительное число от 0.1 до 100.0 (`/^\d+(\.\d+)?$/`).
     - Белый список для `mapPath`: путь внутри проекта Unreal (`/^\/Game\/[A-Za-z0-9_\-\/]+$/` без `..`).
     - Белый список для `levelJsonPath`: безопасный путь к файлу `.json` без обхода каталогов (`..`).
   - Написан изолированный набор модульных тестов `src/utils/botRunner.test.ts` (проверка нормальных запусков и блокировки всех векторов инъекций).

---

## 3. Результаты тестов и верификации

### 3.1. Модульные тесты (`npm test`)
Запуск полного тестового набора (`node --test "src/**/*.test.ts"`):
```
ℹ tests 265
ℹ suites 0
ℹ pass 265
ℹ fail 0
ℹ cancelled 0
ℹ skipped 0
ℹ todo 0
ℹ duration_ms 121080.4073
```
Все **265 тестов пройдены успешно (0 ошибок)**.

### 3.2. Проверка типов (`npm run typecheck`)
- TypeScript компилятор завершился с кодом 0 (0 ошибок):
```
> training-ground@1.0.0 typecheck
> tsc && tsc -p tsconfig.node.json
```

### 3.3. Сборка приложения (`npm run build`)
- Production-сборка завершилась успешно:
```
vite v5.4.21 building for production...
✓ 2692 modules transformed.
dist/index.html                     0.60 kB │ gzip:   0.38 kB
dist/assets/index-Dvu5G13q.css     46.62 kB │ gzip:   8.58 kB
dist/assets/index-Dy2ZjZPL.js   1,050.99 kB │ gzip: 305.61 kB
✓ built in 19.89s
```

### 3.4. Защита файлов проекта
- Файл `Content/Data/LevelJson/L_Gen_Depot_1.json` не изменялся.
- В тестах использовались изолированные копии.

---

## 4. Скриншоты интерфейса

1. **Общий вид панели уровня с секцией погоды и оперативников**:
   - `docs/port/gemini/outbox/screenshots/tg_overview.png`
   ![TG Overview](screenshots/tg_overview.png)

2. **Полный экран Training Ground с раскрытыми секциями Environment и Operatives**:
   - `docs/port/gemini/outbox/screenshots/tg_full.png`
   ![TG Full](screenshots/tg_full.png)

---

## 5. Резюме и статус

- Все исходные требования и 3 замечания код-ревью выполнены на 100%.
- Коммиты `39bd71b9` и `b24fc863` находятся в ветке `gemini/tg-pass1-ts`.
- Ветка готова к слиянию.
