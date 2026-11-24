# 112 · Плановый групповой приказ на FrozenLake

Gemini локализовал регрессию на `386add9b7` (`addfe48bc` PASS 62,5 с). В предоставленном логе `Saved/Logs/Worker/20261124-195825/Smoke-LakePauseOrdersSmoke-alone.log:1852` очередь разместила двух ведомых в помещении. На строке 1953 confined уже выключен. При возобновлении Commander и Engineer приняли свои приказы, а заявка Medic-Sapper к смещённой точке `(31327, -497)` вернулась с `unreachable` (1958–1960). Собственный приказ отсутствовал ещё до первого тика формирования (1964–1969). Позднее боец прошёл 689 см, следуя строю, и принял отдельный приказ в реальном времени.

- `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp:202` передаёт исходную точку группового клика вместе со смещённым местом каждого бойца. `SquadSubsystem.cpp:1451` ограничивает обе точки одним сохранённым радиусом паузы.
- `SquadSubsystem.cpp:1493` повторяет личный `OrderMoveTo` к исходному клику только после `Unreachable` на другом групповом месте. Принятые места, отказы, приказы без контроллера и индивидуальные планы сохраняют своё поведение. Код входной очереди не менялся.
- `SquadOrderRulesTest.cpp:42` проверяет фактические координаты регрессии, отсутствие повтора для принятого или отказанного приказа, уже проверенного центра и плана без запасной точки.


## Проверки

- `d317c274`, worker slot2, `Scripts/build.ps1 -FullUnity` → `Result: Succeeded`, 289,36 с UBT.
- `LakePauseOrdersSmoke` → PASS, 103,8 с. В `Saved/Logs/Worker/20261124-234956/Smoke-LakePauseOrdersSmoke.log:1949` Medic-Sapper получил `unreachable` к `(31330, -496)`. Повтор к `(31242, -673)` принят на строке 1951, собственный приказ есть на строке 1955, смоук подтвердил исполнение на строке 1960.
- `Scripts/test.ps1 -CheckMap` → `Impact map OK: every smoke command is registered and mapped.`
- `Scripts/test.ps1 -Filter CodexTactics.Formation.Confined+CodexTactics.Squad.Orders` → 31 тест, 0 ошибок. В том числе `Success CodexTactics.Squad.Orders.PlannedGroupRetriesUnreachableOffset`. Вывод в `Saved/Logs/Worker/112-formation/worker.log`.
- `FormationWindowSmoke` → PASS за 51,6 с. `FormationStairSmoke` → FAIL за 73,6 с, 1/3 подъёма. Оба сбоя на переходах с 0-го на 1-й этаж, S04 и S10. Строки 2089, 2156 и 2171 в `Saved/Logs/Worker/112-formation/Smoke-FormationStairSmoke.log`.
- Контрольный `FormationStairSmoke` на `f6e6a60e` с тем же SHA-256 карты, `-FixedSeed` и `-FPS=60` → FAIL во всех четырёх прогонах. Первый дал 2/3 за 56,9 с. При переходе S10 в строю осталось два бойца, а к окончанию один. Ещё три дали 0/3, 1/3 и 1/3 за 62,8, 69,2 и 68,4 с. В `Saved/Logs/Worker/20261125-003109/Smoke-FormationStairSmoke-r2.log:2088` и `:2142` все три бойца на S04 и S10 остановились на тех же координатах, что и после правки. Лестничный сбой воспроизводится до изменения.
- На slot2 уже присутствовал Showcase Kit с SHA-256 `C8429E66DFCE449A10C23389C2B4F5413D6AB92C5B1E54EB09D3E3939D5CA285`. Его использовали для всех прогонов строя. SHA-256 на slot1 отличается, карту slot1 в этих проверках не запускали.

## Границы

Код лестниц, окна, карты, конфигурации и входная очередь без правок. Сборку и смоуки выполняли на slot2, блокировки своих прогонов освобождены. В первом запуске штатная обёртка выполнила `git lfs push origin` без вывода о загрузке объектов. Ссылки Git не отправлялись. Повторные прогоны использовали локальную копию обёртки без вызова LFS push. Слияние ветки оставлено Lead.
