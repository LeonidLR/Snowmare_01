# 106 · Секторы тренировки X6

## Источник и интеграция

X6 выбирал старт из PlayerStart (`CodexTacticsGameMode.cpp:483` до изменений), а `PmcEncounterSubsystem.cpp:79` читал общий массив PMC уровня. Геометрия сектора нужна до старта отряда и расписания, поэтому она хранится на уровне UWorld и применяется перед игровым preload.

- `TrainingSectorRules.cpp:66` проверяет замороженную схему из 105. Идентификаторы 1–6, обе стороны прямоугольника от 60 м, высоты и позиции 2–3 стартов и постов. `TrainingSectorRules.cpp:180` выбирает старт и пост через один seeded stream.
- `TrainingSectorSubsystem.cpp:216` читает `Saved/Training/active_sector.json` в формате `{"map":"L_Gen_Depot_1_X6","sector":N}`. `-Sector=N` перекрывает сохранённый выбор, `Codex.Sector N` меняет текущий мир. Ноль включает штатный X6, пропущенные или ошибочные данные оставляют карту целой. Во время активного перелезания или падения переключение ждёт повторной команды после окончания перехода.
- `TrainingSectorSubsystem.cpp:373` ставит четыре стены с реакцией на Pawn, четыре внешних модификатора UNavArea_Null и линию на земле. Снаряды и луч видимости игнорируют стены. `CodexTacticsGameMode.cpp:464` использует выбранный старт, а переключение возвращает вышедших игроков внутрь и пересоздаёт расписание PMC. `PmcEncounterSubsystem.cpp:79` планирует одну группу из сектора. `CombatPressureSubsystem.cpp` при активном секторе останавливает pressure.
- `OperativeCharacterMovement.cpp:312` ограничивает цель перед построением пути, `PmcSquadSubsystem.cpp:2789` ограничивает навигационные запросы PMC и отбрасывает внешние тактические посты. Проверки границы применены к выбору укрытия, перелезанию, окнам, лестницам и падениям, включая пути, которые меняют позицию вне обычного перемещения.
- `TacticalCameraPawn.cpp:471` ограничивает камеру сектором с установленным отступом. `CodexTacticsHUDMinimap.cpp:178` выводит контур. В JSONL-сессии добавлены поля `sector_id` и `sector_box_cm`. Переключение разделяет записи по секторам.

## Проверка

- `test.ps1 -CheckMap` — PASS, `Impact map OK: every smoke command is registered and mapped.`
- `python Scripts/Tools/check_smoke_names.py --reverse` — 0 отсутствующих команд и 0 пропущенных привязок.
- `test.ps1 -Smart -SkipSmokes -DryRun` — PASS, в плане есть CodexTactics.Training.Sector и SectorBoundarySmoke. В unit-тестах подготовлены проверка схемы, детерминизм, разнообразие стартов и clamp. Смоук приказывает выйти за границу и проверяет удержание игрока и PMC внутри.
- TypeSafe PASS для `5480fdae` и `aaa81d4d`, `git diff --check` PASS.

Вердикт FAIL для приёмки UE. Ветка содержит код и тесты, а C++ сборка, -Smart с UE и смоук ожидают дневное окно slot1 после 16:30 и набор данных Gemini 105. Контур, физика стены и отключение внешнего navmesh пока проверены по API UE 5.8 и путям вызова. Дизайнерский LevelJson, файл секторов Gemini, карты и настройки холода остались в ведении их владельцев.

Следующий worker-прогон после объединения 105/106: `Scripts/Tools/worker_sync.ps1 -RemoteRoot C:/Work/CodexTactics-slot1 -ShipHead -ShareMachine -Build -SkipBinaries -Commands CodexTactics.SectorBoundarySmoke -Jobs 1`, затем `test.ps1 -Smart -Base <база> -SkipSmokes` на Worker на том же SHA.
