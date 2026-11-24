# 074 — локальная запись игровых сессий

Статус **FAIL на этапе запуска**. Код `79c04c2b`, сборка PASS. Windows Application Control не разрешил загрузить DLL для smoke и тестов.

## Диагноз

Раньше `Source/CodexTactics/Private/Telemetry/RunTelemetrySubsystem.cpp:114–122` обрабатывал исход боя, но не сохранял состояние перед приказами. Планирование углового выглядывания обслуживает и hover-превью, поэтому запись поставлена в пути подтверждения, рядом с вызовом приказа. `SquadSubsystem.cpp:1480–1492` выпускает отложенные приказы напрямую в оперативника, поэтому отдельный `FDemoSuppressScope` при выходе из паузы не понадобился.

## Сделано

- `Source/CodexTactics/Private/Telemetry/SessionRecordingRules.cpp:6,23,53,77,113,143` — условия записи и сохранения, имя файла, метки каталога PMC, азимут и стабильный JSONL.
- `Source/CodexTactics/Private/Telemetry/SessionRecordingSubsystem.cpp` — таймер мира, снятие состояния до исполнения, результат GameFlow, метки, атомарное сохранение UTF-8 без BOM и предупреждение о размере. `SquadSubsystem.h:223`, `PmcSquadSubsystem.cpp:258–280` и `EnemyRegistrySubsystem.cpp:139–170` добавляют чтение существующих реестров без копирования списков в записи.
- Снимки просматривают существующую видимость отряда, включая PMC, и сохраняют не более 12 ближайших угроз. `flags.immortal` и `flags.infinite_ammo` читаются из существующих переключателей. Активная цель — первая незавершённая цель из `MissionGoalsSubsystem`.
- `docs/systems/session-recording.md:1` описывает схему и таблицу соответствий. `Source/CodexTacticsTests/Private/Telemetry/SessionRecordingTest.cpp:21–243` содержит семь автотестов.

### Строки хуков

Пути контроллера ниже относительно `Source/CodexTactics/Private/Core`.

- `CodexTacticsPlayerControllerBreach.cpp:10,247,254,333,351`
- `CodexTacticsPlayerControllerCover.cpp:7,232,234,278,280,894,908,936,948,990`
- `CodexTacticsPlayerControllerItems.cpp:7,83,163`
- `CodexTacticsPlayerControllerOrders.cpp:7,117–121,150,1178,1189,1334,1367,1389–1390,1405,1450–1451,1489–1490,1587–1589,1601–1604`
- `CodexTacticsPlayerControllerRunPeek.cpp:12,422,431,454,504,614,623,636`
- `CodexTacticsPlayerControllerSelection.cpp:6,165–166,386–389`
- `CodexTacticsPlayerControllerStance.cpp:6,19–20,148,174`
- `CodexTacticsPlayerControllerTime.cpp:6,79–82`
- `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp:67,4061–4064`

Приказ Medkit наблюдается на двух реальных входах диспетчера, глубина защищает от дублей. Пауза записывается после подтверждённого переключения GameFlow, чтобы нажатия, которые поглотило меню, не попадали в данные. В первом снимке используются доступные поля: запрошенные геттеры и оба переключателя доступны. Обычная подготовка remote build ограничивает untracked-архив путями Source и Config (`Scripts/Tools/build_remote.ps1:36`).

## Проверка

- `git diff --check` и аудит индекса TypeSafe — PASS. `git check-ignore Saved/Demos/x.jsonl` → `Saved/Demos/x.jsonl`, `git status --short -- Saved` пуст. `worker_sync.ps1:233–246` посылает Git bundle коммитов, а режим этой проверки не передавал Patch или ExtraZip.
- Первый запуск worker в 08:27–08:34 оборвался на 15/59 с `Build.bat -1` без строки `Result:` и без ошибок компилятора. Повтор был запущен для актуальной версии.
- Третья сборка `Scripts/build.ps1` на slot1 — PASS, UBT `Result: Succeeded`, 584,20 с, `Build.log`. Перед этим второй запуск оборвался после линковки игрового модуля, а третий зафиксировал `Low on memory (37.5gb/39.4gb)` для одного действия, затем завершился успешно. Временно отключённые UBA detours подтверждены пометкой `[NoUba]`. Исходный проектный конфиг сборки на worker восстановлен по хешу.
- `MainMenuSmoke` — FAIL через 30,6 с. `Smoke-MainMenuSmoke.log:459` подтверждает `-game -unattended -NoTelemetry`, строка 1167 — `GetLastError=4551`. Игровой модуль не загрузился, поэтому строки `[DemoRecord] off` в этом прогоне нет. Windows Code Integrity, события 3033 и 3077, отмечает уровень Enterprise signing. Проектные DLL показывают `NotSigned`.
- `test.ps1 -Filter CodexTactics.Telemetry.SessionRecording` и полный `test.ps1` на проверенном HEAD завершились с кодом 1. Оба напечатали `No tests matched`, лог каждого на строке 1224 показывает ту же блокировку DLL с кодом 4551. Исполненных тестов нет.
- До smoke и после обоих запусков тестов `Saved/Demos` на slot1 содержал 0 файлов. Отпечаток списка имён, размеров и времени изменений совпал (`E3B0C442…7852B855`). Это подтверждает отсутствие новых файлов в этих прогонах. Для проверки штатного режима нужны проектные DLL, которым доверяет политика worker.
- Логи сборки и smoke — `C:/Work/Lanes/CodexTactics-cx074/Saved/Logs/Worker/codex-074-local-executor`. Логи automation — `C:/Work/CodexTactics-slot1/Saved/Logs/Codex074-focused-Automation.log` и `Codex074-full-Automation.log` на worker.

## За пределами проверки

Живую PIE-сессию проверяет Lead. Замер ≤0,1 мс на реальных 4 оперативниках и 12 угрозах оставлен открытым. Проверка по коду: нет Tick, есть два CPU trace scope, заранее выделенные плоские буферы и список ближайших угроз с inline storage. Выключенный вручную реестр использует штатный обход мира; за пределами резерва буферы могут расширяться. Learning Agents, конвертер и изменения карт в 074 не входят.
