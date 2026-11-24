# 076 — бои с PMC на улицах Showcase Kit

Код `eac737e3`, первоначальная настройка карты `bd695d2b`. Автономная проверка PASS. Интеграционную проверку UE предстоит выполнить Lead на штатном пути сборки.

## Диагноз

Прежний LevelJson включал бессмертие, волну из двух монстров и pressure по умолчанию. Общий расход патронов в `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp:1054` применял флаг `cheats.infinite_ammo` к обеим сторонам. Lead согласовал отдельный ключ для команды игрока в агентском чате #1020. Каждая запись PMC создаёт отдельную команду, а `PmcSquadSubsystem.h:20` считает разные команды враждебными, поэтому выбран один отряд из шести.

## Сделано

- `Content/Data/LevelJson/L_Gen_StoryShowcase_Kit.json:9,12–24,34–60` фиксирует Clear и отключает процедурные погодные эффекты, устанавливает обычный урон и бесконечные патроны команды игрока. Волны пусты, pressure и орда отключены.
- `L_Gen_StoryShowcase_Kit.json:35–52` ставит 6 PMC на наземном посту `(0,-3800,0)` в 33 м от старта и в 15,8 м от ближайшего здания. Принятый layout содержит шесть зданий и ноль точек спавна, засады и патрульных маршрутов монстров.
- `Source/CodexTactics/Private/Data/LevelJsonRules.cpp:641` читает `cheats.infinite_ammo_squad`. `Source/CodexTactics/Private/Combat/CombatCheats.cpp:45–57` применяет его к команде 0, сохраняя глобальный `infinite_ammo` и консольные приоритеты. Три пути расхода передают `IsPlayerTeam()`: `OperativeCharacterCombat.cpp:1054`, `OperativeCharacterQuickFire.cpp:357`, `OperativeCharacterWeapons.cpp:333`.
- Запись сессии читает эффективный флаг игрока через прежний вызов `IsInfiniteAmmo` в `Source/CodexTactics/Private/Telemetry/SessionRecordingSubsystem.cpp:247` и выставляет `flags.infinite_ammo` для lab.
- `Source/CodexTacticsTests/Private/Combat/CombatPressureRulesTest.cpp:211–217` проверяет оба режима, консоль и Shipping. `Source/CodexTacticsTests/Private/Core/LevelJsonRulesTest.cpp:170–173` проверяет отдельный ключ.

## Проверка

- `LevelJson schema + Showcase flags: PASS`.
- MSVC `/Zs` проверил извлечённые тела `Resolve` и `ResolveInfiniteAmmo` на 48 сочетаниях с `static_assert`. Вывод `Actual C++ ammo resolver: 48/48 static assertions PASS`.
- TypeSafe и `git diff --check` — PASS. Неотслеживаемая `.umap` отсутствует в рабочей копии 076.
- Сборку UE и automation 076 делегировал Lead для доверенных DLL. `CutawayClickSmoke` был поставлен в очередь worker slot1 и снят до запуска после обнаружения конфликта патронов. Slot1 занят 081. Проверка фактического спавна на navmesh и боевой записи остаётся открытой.
