# 061 — архив карт

Ветка `codex/061-map-archive` от `c2d54ede`. Архив Lead `origin/archive/maps-2026-11-23` = `d495ac83`. Все 39 снятых путей сверены с архивом побайтно по Git. Коммиты `0336233b`, `b55ceb82`, `896037bc`.

- Убраны 16 карт, включая две погодные из `Content/Environment/Weather/Test`, и 23 отдельные записи данных. Полный перечень даёт `git show --name-status 896037bc`. В ветке остались X6, FrozenLake, VehicleTest и MainMenu. Kit-витрина остаётся untracked, её LevelJson сохранён. Layout `gen_story_showcase` нужен Kit. Данные L_Lab и базовые Depot props остаются востребованы генераторами и тестовыми fixtures.
- Кампания 0.1, старт редактора и упаковка указывают на X6 по решению дизайнера. X6 в Git не менял. Её текущие настройки бессмертия и бесконечных патронов действуют при загрузке карты. TrainingGround и `bot_run.ps1` выбирают X6.
- `Scripts/test_map.json` — 43 активных и 275 снятых смоуков. `archivedMapSmokes` перечисляет каждый снятый чек с прежней картой. CutawayClick, RoofCutawayStorey и Doors перенесены на Kit. Vehicle, VehicleCombat и VehicleDamage перенесены на VehicleTest. LevelGenBuildingsWalk снят вместе с прежней картой BgenPlans. `smoke_parallel.ps1 -List` даёт 3 X6, 7 Kit, 28 FrozenLake, 2 MainMenu, 3 VehicleTest.
- `MissionGoalsRulesTest.cpp` ожидает 4 цели X6. `PropsPowerTest.cpp` ожидает 0 канистр при действующих проверках power и турелей. В исходном X6 слой power сохранился (9 объектов), loot пуст. Живые проверки Campaign, Firefight, Mortar, BotRoute и Props теперь используют X6. Проверки TacticalMap X4, трёх Train layouts, климата и CombatLab используют закреплённые fixtures. Численные проверки X4 сохранены, X2/X4 убраны из живых проверок сгенерированных карт.
- `test.ps1 -CheckMap` PASS, `check_smoke_names.py --reverse` 0/0 отсутствующих, 6 Python и 27 Node тестов PASS. TypeSafe для архивного коммита явно отметил разрешённые дизайнером пути Config, карт и старого Depot LevelJson. Правила аудита не менял.
- Worker сначала вернул LFS 404 на точную версию X6 из локального main. Скопирован и сверен SHA-256 одного объекта в Git LFS-кэше worker. После повтора слот 1 подтвердил HEAD `896037bc` и начал сборку. В очереди шесть ретаргетированных смоуков и весь UE automation.

## Остаётся

Получить результаты worker, сверить два теста X6 и дописать итог. Сборка и смоуки на ПК дизайнера не запускались. Локальный main и Kit map не редактировал. Push и merge не выполнял.
