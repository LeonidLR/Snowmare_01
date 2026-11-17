# G006 план — перетаскивание аптечки = помощь; саквояж в инвентаре

Фикс в этом коммите не начат. Ветка `grok/G006-first-aid` от main. Роутер: вызов `model_router.py --classify` не прошёл (сеть jev без одобрения). Уровень по критериям README: **4** (C++ логика дропа + диагноз спавна и UI). Модель: **grok-4.7**. Результат: диагноз записан, код не менялся.

## Что сломано (доказано по коду, без прогона)

### 1. Дроп на легко раненого — передача, не помощь

Правило дизайнера 2026-11-17: дроп аптечки / саквояжа на товарища с HP < max (любой тир) начинает помощь; Shift = передача; полный HP = передача.

Сейчас иначе:

- `Content/Data/Survival/first_aid.json:13-16` — `treat_on_drop_tiers` только `["heavy"]`. Комментарий в строке 16: любой другой тир = передача, Shift = всегда передача.
- Зеркало в коде: `FirstAidRules.h:154` `TreatOnDropTiers = { false, false, false, true }` (none / light / medium / heavy).
- `FirstAidRules.cpp:264-267` `TreatsOnDrop`: true только если тир в списке и Shift не зажат. HP не смотрит.
- `FirstAidSubsystem.cpp:298-301` `ShouldTreatOnDrop` передаёт только тир.
- Дроп: `CodexTacticsHUD.cpp:1845-1871` `HandleTransferDropOnActor`. Помощь зовётся только из `ShouldTreatOnDrop`. Иначе `MakeGive` (передача). Пошаговый бой по-прежнему передача (`1851`).
- Тесты закрепляют старое: `FirstAidRulesTest.cpp:130-133` — medium / light = передача, Shift на heavy = передача.

Тир `none` — это и «царапина» (HP < max, но не ранен), и полный HP (`FromWoundTier` → None). Если просто включить все тиры, дроп на полного тоже зайдёт в помощь, `RequestAid` вернёт `FullHealth`, а HUD (`1860-1862`) отдаст Failed и **не** передаст предмет. Поэтому проверка HP < max обязательна отдельно от списка тиров.

### 2. Саквояж «не видно»

Число старта уже в данных: `first_aid.json:23-25` `start_medical_bags.MedicSapper = 1`. Тест `FirstAidRulesTest.cpp:380` это читает. Выдача на спавне отряда:

- `CodexTacticsGameMode.cpp:512-517` — `MedicalBagsCount = max(текущее, StartMedicalBags(имя роли))`. `GetNameStringByValue` для `UENUM` срезает `::` (`Enum.cpp:769-785`) и даёт `MedicSapper`. Ключ JSON совпадает. На пути «ростер + PlayerStart» (Depot и сгенерированные карты, `docs/systems/demo01-lake.md` — сквад не стоит в карте) саквояж у медика должен быть 1.
- Дыра: `CodexTacticsGameMode.cpp:437-455`. Если в мире уже есть `AOperativeCharacter` (поставленный сквад), выдача `start_medical_bags` не вызывается, только carry и kit. Kit в `L_Gen_Depot_1.json` поля `medical_bags` не содержит (поиск по JSON: ключ есть только в `first_aid.json`). `-1` = оставить текущее, то есть 0 у поставленного медика.
- Ящик инвентаря показывает **лидера**, не весь сквад (`InventoryDrawerWidget.cpp:36-39`). Лидер по умолчанию — командир, у него 0. Строка есть всегда (`IsSlotVisible` её не прячет, `:322-333`), но текст `Medical bag: x0/1` (`:272-276`) и кнопка выключена (`:312`). Эмодзи 🩹 вырезает `StripUnsupportedGlyphs` (суррогат вне BMP) — как у аптечки; отдельной иконки `.uasset` нет и не нужно.
- Строка стоит последней в сетке (слот после костра). Рядом с аптечкой её нет.
- Смоук сам ставит саквояж командиру: `FirstAidSmokeCommand.cpp:319` `A->MedicalBagsCount = 1`, A = `Members[0]`. Старт медика не проверяется. Помощь проверяется только на heavy (`:417-458`).

## Как чиню

1. JSON `treat_on_drop_tiers`: `none`, `light`, `medium`, `heavy`. Умолчания кода — те же четыре true (тест DataFile сравнивает зеркало).
2. `TreatsOnDrop` получает HP и max: помощь только если включено, не Shift, тир в списке, `0 < HP < max`. Полный HP и мёртвый — false (дальше старая передача). Пошаговый бой не трогаю.
3. Тесты: light / medium / none с HP < max = помощь; Shift и полный HP = передача; конфиг только с `heavy` по-прежнему не лечит light (список остаётся данными).
4. Саквояж: выдача `start_medical_bags` и на ветке поставленного сквада (до kit, не после carry — иначе потраченный саквояж вернётся на следующем уровне). Имя роли — ключ JSON (`MedicSapper`), не только рефлексия.
5. Строка ящика: у лидера с 0 и саквояжем в скваде текст `Medical bag: x0/1 (N in squad)` (как печка). В раскладке строка сразу после аптечки. Английский текст (запрет кириллицы в UI). Иконку `.uasset` не добавляю.
6. Смоук `FirstAidSmoke`: (a) дроп аптечки на light — стартует помощь, не передача, затем отмена; (b) то же с `bForceHandOverModifierForTesting` — передача, счётчики потом возвращаю, чтобы heavy-стадии видели прежние 2 аптечки у B; (c) до ручной подстановки саквояжа — медик-сапёр, лидер = он, строка содержит `Medical bag` и его `MedicalBagsCount` равен `start_medical_bags`.

## Файлы (до правок)

- `Content/Data/Survival/first_aid.json`
- `Source/CodexTactics/Public/Survival/FirstAidRules.h`
- `Source/CodexTactics/Private/Survival/FirstAidRules.cpp`
- `Source/CodexTactics/Public/Survival/FirstAidSubsystem.h`
- `Source/CodexTactics/Private/Survival/FirstAidSubsystem.cpp`
- `Source/CodexTactics/Private/UI/CodexTacticsHUD.cpp` (комментарий у дропа; логика HP в правилах)
- `Source/CodexTactics/Private/UI/InventoryDrawerWidget.cpp`
- `Source/CodexTactics/Private/Core/CodexTacticsGameMode.cpp` (выдача на поставленном скваде — путь лутаута, который задание просит починить)
- `Source/CodexTacticsTests/Private/Survival/FirstAidRulesTest.cpp`
- `Source/CodexTacticsDev/Private/Debug/FirstAidSmokeCommand.cpp`
- отчёт: `docs/port/grok/outbox/G006-first-aid.report.md`

## Как докажу

Сборка `Scripts/build.ps1 -Remote worker` (exit 2 → локальный `Scripts/build.ps1`). Тесты `CodexTactics.Survival.FirstAid`. Смоук `FirstAidSmoke` дважды, затем `InventorySmoke`, `TransferSmoke`. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok`. В отчёт — хвост команд, не голое PASS.

## Не делаю

Карты, `.uasset`, ABP, `Config/**`, `L_Gen_Depot_1.json`, кириллица в UI, ослабление порогов смоуков.
