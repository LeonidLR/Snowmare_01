# 134 — два красных smoke на main

Статус: **PASS — готово к ревью Lead.**
Ветка: `codex/134-red-smokes`, worktree: `C:/Work/Lanes/CodexTactics-134`.
База: `640c1bc3`, затем fast-forward до `b28fb75b` (между ними только docs/tools, игровой код тот же).

## Изменения

- `Source/CodexTactics/Private/Characters/SquadSubsystemFormationConfined.cpp:483`: автономная команда leader-makes-room не подменяет цель, пока компонент лестницы занят. `IsBusy()` включает очередь, отложенный вход и выход; ручные приказы во время подъёма сохраняют прежнее поведение.
- `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerClickPreview.cpp:152`: лестничный hit с действующей stair-up целью проходит к обычной команде движения до cover preview.
- `Source/CodexTacticsDev/Private/Debug/WindowStairHoverSmokeCommand.cpp:395`: регрессионная проверка, что лестничный клик не включает cover preview; на :451 камера дверного луча поднята над телом бойца. Проверки попадания в створку, blue/red, подъёма и окна сохранены.
- `Source/CodexTacticsDev/Private/Debug/FormationStairSmokeCommand.cpp:44`: только S04 0→1 получает 83 s, обоснованные тремя замерами. Остальные legs — 40 s. Позиции, приказы, состав, этаж/дистанция, ladder checks и locked-way case прежние.
- `Scripts/test_map.json:429`: WindowStairHoverSmoke добавлен в impact map для изменений ClickPreview.

Карты, `.uasset`, Config, VFX и работа дизайнера не менялись. Алгоритм лестниц не менялся. Временная трассировка и диагностическое наблюдение удалены.

## Диагноз и доказательства

Собственное воспроизведение на базовых DLL: `Saved/Logs/Worker/dispatch-20261126-090505-18132/`.

- FormationStairSmoke FAIL 5/6 (81.4 s): :1972 — цель S04 на полу300, Commander/Engineer стоят на602 cm, Medic-Sapper на439 cm.
- WindowStairHoverSmoke FAIL (38.8 s): :1875 — stair hit включает cover preview; :1879 — за30 s не поднялся; :1880 — дверной луч попадает в BP_Operative_C_0, далее падают цвета.
- GAME BUG S04 подтверждён в `dispatch-20261126-091343-90624/Smoke-FormationStairSmoke.log:2363`: после первой лестницы сохраняется `(1692,4680,310)`. На :2475–2482 confined make-room во время второй ExitTop заменяет её на `(1432,2919,610)`.
- После IsBusy guard лидер продолжает маршрут; старый40 s deadline всё ещё срабатывает на спуске. Это отдельный SMOKE TIMING BUG из-за новой карты, а не дальнейшая потеря приказа.

SHA256 текущей Kit-карты, одинаковый в main и worker: `BB9787F9C8A68B11374705134321D3A59858C778AA3F65D7D45BE9946A9C23E8`.
Зелёный отчёт113 использовал `C8429E66DFCE449A10C23389C2B4F5413D6AB92C5B1E54EB09D3E3939D5CA285` и другую геометрию (2 перехода вместо нынешних5); этот PASS нельзя использовать как хорошую границу code bisect.

Lead явно разрешил калибровку по трём замерам (AgentChat #1339): запас ~1.5× худшего, только при разбросе≤20%, все остальные проверки прежние. Наблюдение сохраняло обязательный FAIL на исходных40 s, но не телепортировало отряд до80 s. Полное прибытие S04 0→1: **55.0 / 55.0 / 55.0 s, разброс0%**:

- `dispatch-20261126-093023-93792`
- `dispatch-20261126-093302-85624`
- `dispatch-20261126-093501-78780`

Эти диагностические прогоны намеренно оставались FAIL за40 s и не засчитываются как зелёные. Новый предел83 s = ceil(1.5×55), только для маршрута S04 0→1 через две наружные лестницы и внутренний спуск. SHA карты, маршрут и измерения указаны в коде.

## Проверки

- Окончательная сборка: штатный dispatcher → worker, `Result: Succeeded`, UBT14.71 s, wrapper exit0 (2.8 min с sync/передачей). Лог: `Saved/Logs/Worker/build-20261126-093755-74232/Build.log`.
- `Scripts/test.ps1 -Smart -SkipSmokes`: **438/438 PASS**, 0 ошибок,162 s; mapped smoke выполняются отдельно через dispatcher. `Saved/TestReport`, `Saved/Logs/AutomationTests.log`.
- `Scripts/test.ps1 -CheckMap`: PASS.
- Target smoke, окончательный код, jobs1, plain PASS без PASS*/known-fail:
  - `dispatch-20261126-094045-55504`: FormationStair PASS50.7 s; WindowStairHover PASS37.6 s.
  - `dispatch-20261126-094420-90400`: FormationStair PASS50.6 s; WindowStairHover PASS36.7 s.
  - `dispatch-20261126-094659-93320`: FormationStair PASS50.9 s; WindowStairHover PASS36.7 s.
  - Итого оба smoke **3/3 plain PASS**, 0 PASS*, 0 known-fail.
- Остальные9 mapped smoke: **9/9 plain PASS**, 0 retry, 0 known-fail; `dispatch-20261126-094936-95512/SmokeParallel-worker.json` (jobs2, SaveCoverage отдельно, wall219 s): CoverIndoor45.7 s, LowCoverMoveLake40.0 s, FormationWallHug48.0 s, FormationFenceCross40.4 s, TutorialAmbushLake47.7 s, TurnArenaLake36.8 s, SaveCoverageLake45.7 s, LakePauseOrders41.4 s, FormationWindow37.5 s.
- Финальный локальный boundary audit по PROTECTED_FOR[codex], проверка ветки и `git diff --check`: **PASS**, 6 файлов, защищённые пути не затронуты. TypeSafe **пропущен по прямому решению дизайнера**: только локальная проверка; автоматическое approval review отклонило отправку metadata в api.typesafe.ai. Внешний TypeSafe запрос не выполнялся.

Для Smart временный `core.excludesFile` скрывал лишь неизменённые untracked Content packs и скопированный при создании worktree Scalability. Tracked diff и весь Source видимы; проверки не отключались.

Базовая UBT сборка640c1bc3 тоже PASS410.32 s; wrapper exit2 после остановки медленной bulk-передачи старого Shipping.exe. Все4 editor-DLL + UnrealEditor.modules получены из той же сборки архивом, SHA2565/5 PASS (`Saved/134-baseline/hashes.json`). Вспомогательные binaries только прочитаны из main. Окончательная сборка выше завершилась полностью штатно.

## Не выполнено

Рендерные снимки/ручная визуальная проверка, packaged build и полный ночной suite не запускались. Все требуемые проверки задачи завершены. Merge/push не выполнялись; передача ветки остаётся Lead. G022 не начата.
