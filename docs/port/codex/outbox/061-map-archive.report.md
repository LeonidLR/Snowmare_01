# 061 — архив карт

Ветка `codex/061-map-archive`, база `4e5095dd`. Архив Lead `origin/archive/maps-2026-11-23` указывает на `d495ac83`. Git подтвердил совпадение каждого снятого файла с архивом. Коммит `1f96b893` содержит снятые карты и данные, а также две отдельно разрешённые ссылки Config.

## Карты и данные

Сняты 16 карт:

- В `Content/Maps/Generated/` — `L_Gen_BgenPlans`, `L_Gen_BgenTest_X6`, `L_Gen_Depot_1`, `L_Gen_Depot_X4`, `L_Gen_StoryShowcase`, `L_Gen_Test_X2`, `L_Gen_Train_1`, `L_Gen_Train_2`, `L_Gen_Train_3`.
- В `Content/Maps/` — `L_Demo01_B_FrozenFactory`, `L_FormationLab`, `L_FormationLabCombat`, `L_MovementTest`, `L_PatrolTest`.
- В `Content/Environment/Weather/Test/` — `L_SnowTerrainTest`, `L_WeatherRainTest`.

Убраны 23 отдельные записи данных с прежних путей: 13 LevelJson, четыре accepted-layout, по две записи objectives, props и tactical для X2/X4. Полный перечень — `git show --name-status 1f96b893`. Сохранены общие layouts, базовый Depot props и данные L_Lab, которые используют инструменты генерации и тесты. Данные сохранённой Kit-витрины остаются вместе с `gen_story_showcase`.

Отслеживаемые карты теперь X6, FrozenLake, VehicleTest и MainMenu. Kit-витрина остаётся untracked. По решению дизайнера миссия 0.1 открывает X6 (`campaign.json:6`), X6 также выбрана для старта редактора (`DefaultEngine.ini:5`) и упаковки (`DefaultGame.ini:29`). Её LevelJson сохранён побайтно, поэтому на X6 действуют текущие бессмертие и бесконечные патроны. TrainingGround и `bot_run.ps1` тоже выбирают X6.

## Проверки

- `Scripts/test_map.json:76` содержит все 277 снятых смоуков по прежним картам. `smoke_parallel.ps1 -List` показывает 41 активный: 3 X6, 6 Kit, 28 FrozenLake, 2 MainMenu, 2 VehicleTest. Для отсутствующей Kit-карты runner вернул exit 1 с `map missing` до запуска UE (`smoke_parallel.ps1:104`).
- На Kit перенесены `CutawayClickSmoke` и `RoofCutawayStoreySmoke`, на VehicleTest — `VehicleCombatSmoke` и `VehicleDamageSmoke`. Все четыре PASS на `896037bc`. `DoorsSmoke` снят после 5 ошибок на Kit, в том числе обхода закрытой двери и выстрела по замку (`Smoke-DoorsSmoke-alone.log:2364`, `:2437`). `VehicleSmoke` снят после ошибок проверки opt-in navmesh и контакта со стеной (`Smoke-VehicleSmoke-alone.log:1915`, `:2000`). `LevelGenBuildingsWalkSmoke` снят вместе с BgenPlans. `FrontendSmoke` проверяет реальный старт FrozenLake, где брифинг отключён. У `StartFlickerSmoke` сохранены проверки FrozenLake и X6, без арктических проверок Factory/MovementTest и temperate-погона RainTest.
- `MissionGoalsRulesTest.cpp:327` проверяет 4 цели X6. `PropsPowerTest.cpp:207` проверяет 0 канистр и сохраняет проверки генераторов и турелей. Слой `power` в зафиксированном X6 сохранился, 9 объектов, слой `loot` пуст. Живые тесты Campaign, Firefight, Mortar, BotRoute и Props используют X6. Численные проверки TacticalMap X4, три Train-layout, климат и CombatLab сохранены с закреплёнными fixtures. Из живых проверок удалены чтения X2/X4 и утверждение о наличии карты Frozen Factory.
- `test.ps1 -CheckMap` PASS, `check_smoke_names.py --reverse` 0/0 отсутствующих. 12 целевых Python-тестов и 27 Node-тестов прошли. `git diff --check` PASS.
- Worker сначала вернул LFS 404 для X6 из локального main. SHA-256 и размер 10 837 095 байт проверены с обеих сторон, тот же объект передан в общий Git LFS-кэш worker, после повтора синхронизация прошла. На `896037bc` сборка PASS (497,71 с UBT), `1647 tests, 0 failed`.
- На `91e28bad` сборка PASS (1271,81 с UBT). Семь смоуков PASS: MainMenu, Frontend, четыре варианта GateExitLake и StartFlicker. Логи в `Saved/Logs/Worker/codex-061-final/`.
- Перебазированная ревизия `18f314b3` собрана на worker: `Result: Succeeded`, exit 0, 1332,86 с UBT. Полный набор: `1651 tests, 0 failed`, оба теста X6 — `Success`. Контрольный `GateExitLakeSmoke` PASS (86,0 с), лог `Smoke-GateExitLakeSmoke.log:2066` подтверждает загрузку X6. `CoverIndoorSmoke` PASS (49,2 с) с установленными Riffle-клипами, в логе `Smoke-CoverIndoorSmoke.log:1982`, `:1987`, `:1992` видны switch-in, Aim/2 idle и switch-out. Логи в `Saved/Logs/Worker/codex-061-merged-base/`.
- TypeSafe для архивного коммита отметил защищённые пути, прямо разрешённые дизайнером в 061. Аудит оставлен без изменений. Коммиты инструментов и итогового отчёта получили TypeSafe PASS.

## Чего не сделал

Упаковку и весь список из 41 смоука не запускал. Не менял X6, Kit-карту и исходный архив. Сборки и UE-тесты выполнялись на слоте 1. Main, его рабочие файлы, push и merge оставлены Lead.
