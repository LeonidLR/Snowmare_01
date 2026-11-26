# 142 — выбор Main / Play в Training Ground

Статус: PASS, готово к ревью Lead. Ветка `codex/142-tg-build-switch`, worktree `C:/Work/Lanes/CodexTactics-142`. Основание: inbox 142 и срочные указания Lead #1381; остановка #1385 соблюдена WIP41ad7f5c, продолжение только после RESUME #1388.

## Изменения

- `Tools/TrainingGround/server/trainingApi.ts:314`: build принимает только main/play; Main — root TG, Play — фиксированный сосед CodexTactics-play. Произвольные пути не принимаются, отсутствующая Play возвращает 404 по-русски.
- `trainingApi.ts:318`: доступность, наличие DLL, свежесть по тому же сравнению max DLL mtime >= max Source mtime, что в editor_status.py. Показывается хеш последней успешной RemoteBuilds записи; при отсутствии записи — HEAD. Свежая собранная Play — default, иначе Main.
- `trainingApi.ts:493` и `:511`: open-editor и play используют uproject/cwd выбранной сборки; проверки Saved/agent.lock и запущенного редактора получают именно её root. Правило Lanes сохранено. Выбор сектора пишется на root TG, а не root сборки. Старый пустой POST open-editor по-прежнему запускает Main.
- `Tools/TrainingGround/src/departments/AiTrainingDepartment.tsx:199` и `:297`: русский переключатель «Сборка для игры и редактора» рядом с сектором и обеими кнопками, подпись состояния Play, выбранная сборка в кнопках; выбор сохраняется в localStorage.
- `Tools/TrainingGround/src/aiTraining/trainingTypes.ts:56`: типы статуса и идентификатора сборки.

## Проверки

- `node --test --test-concurrency=2 Tools/TrainingGround/src/aiTraining/trainingApi.test.ts`: PASS, 6/6. Проверены canonical cwd/uproject Main и Play, невозможность подмены пути, запись выбора на root TG, locks/running выбранной сборки, Lanes403, bad build400, missing Play404, freshness/default, HTTP payload open-editor. Отдельная маленькая fixture с реальным junction проверяет demo header/round_end/footer -> счётчик сессий/раундов/побед. Это unit fixture, не доказательство игрового прогона.
- `npm run typecheck` в Tools/TrainingGround: PASS.
- Полностью локальный аудит protected paths/scope и `git diff --check`: PASS. Внешний TypeSafe пропущен по прямому решению дизайнера; метаданные и код в API не отправлялись.
- Изменения только Tools и этот отчёт; C++/uproject не менялись, engine build и dispatcher smokes не требуются и не запускались для142.

## Настоящая проверка Play -> общие записи -> таблица TG

Изменённый UI своей lane работал на5180. Временный проверочный HTTP-сервер5181 подключал ровно изменённый setupTrainingApi к каноническому main root и проксировал UI5180; никаких подмен launch deps и отключения guards. Исходники main/play не менялись. Это проверка будущего поведения после merge, существующий сервер дизайнера5173 оставлен без изменений.

В браузере при первом открытии выбрана Play, подпись `Play: собрано, a12dea10`; ручной выбор Main сохранился после reload. Выбор Play синхронно меняет подписи обеих кнопок. Затем через кнопку «Играть сектор 3 (Play)» действительно запущен PID37108:

`UnrealEditor.exe <...>/CodexTactics-play/CodexTactics.uproject /Game/Maps/Generated/L_Gen_Depot_1_X6 -game -Sector=3 -windowed -ResX=1600 -ResY=900 -NoZenAutoLaunch`

Настоящая карта загрузилась, принят приказ движения через GUI. Игра нормально закрыта Alt+F4, процесс завершился. Лог Play: `[2026.11.26-12.47.21:842] [DemoRecord] saved 2026-11-26_L_Gen_Depot_1_X6_143736.jsonl`.

Файл существует в main/Saved/Demos, 407202 байта. Header: world Game, map L_Gen_Depot_1_X6, sector_id3, started_utc2026-11-26T12:37:36.891Z. После reload TG реально показывает сектор3: сессии **2 -> 3**, минуты **19.3 -> 29.0**, последняя игра26.11; файл также первый в recent runs. Общий счёт сессий12 ->13. Раунды остались7, победы/поражения3/4: в ручной проверочной сессии раунд не завершался, лишний раунд не приписан. Завершённые раунды проверены unit fixture.

Проверены реальные junctions (все направлены из CodexTactics-play в основной CodexTactics):

- Saved/Demos -> main/Saved/Demos.
- Saved/Training -> main/Saved/Training.
- Content/Maps/KitPilot -> main/Content/Maps/KitPilot.

После проверки исходный active sector1 восстановлен (только если оставался нашим sector3). Проверочная игра закрыта. Собственные серверы5180 PID98168 и5181 PID5468 остановлены; тестовая вкладка закрыта. Демо оставлено как фактическое доказательство, в git не добавлялось.

## Ограничения / не сделано

Реальный запуск редактора без -game проверен unit-тестами аргументов и HTTP endpoint; отдельный тяжёлый GUI-редактор не открывался. Свежесть намеренно повторяет существующую эвристику editor_status.py, не является полным fingerprint бинарника. Merge, push и обновление production TG5173 выполняет Lead. После передачи142 очередь возвращается к139.
