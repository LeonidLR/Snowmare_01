# 115 · Внешний выход S07 на крышу

База `9af96d4e`, правка `38790002`. На временной карте задачи 110 `Smoke-LevelGenBuildingsWalkSmoke.log:3060–3061` показал `S07_roof_outside [path] no complete path roof from door_1 at -2300 -5400 900`, Walk 5/6. `110-LevelGenIslandsProbe.log:1594` дал два острова S07 при одном у остальных зданий. Строка 1463 Walk подтвердила, что настенная лестница S07 уже была создана с 24 ступенями и включённым link.

Чистый план builder для фиксированной секции из 110 показал перенос рамы на 15 см наружу к `(-2300, -4075, 26.4)`. Верхняя точка layout `(-2300, -4200)` находится на 125 см внутрь от рамы и на 100 см от кромки крыши `y=-4100`. Прежний builder применял эту точку лишь для люка или лестницы без задней стены. Верхний link использовал 49,1 см, выход анимации — 40 см. Link оказывался в 24,1 см от края при радиусе агента Default 35 см.

- `Scripts/Editor/build_levelgen_map.py:1382` переносит заданный выход на крышу в `top_standoff` и `top_exit_standoff` для лестниц с назначением `roof:`. Высота, положение рамы, прорезь парапета и внутренние лестницы S07 сохранены.
- `Scripts/Editor/tests/test_build_levelgen_buildings.py:63` проверяет настенную лестницу S07 и оба смещения. До правки получил `49.1 != 125.0`, после правки — OK.
- `python -m unittest discover -s Scripts/Editor/tests` на тех же файлах и fixtures, 102 теста. База с новым тестом — 4 failures, 27 errors, 1 skip. С правкой — 3 failures, 27 errors, 1 skip. По именам тестов единственное изменение — PASS нового сценария. У старых ошибок отсутствуют архивные fixtures, среди оставшихся падений проверка тега `W_obstacle` и два теста вариантов маршрутов.
- Вход `node scripts/gen-story-showcase.ts <temp> --fixed-bays` дал тот же SHA-256, что и `gen_story_showcase_110R.json`, `E7187A801486F0A52E705D1D377F170D6C54CB7DF8F10D80BFA719662E6B3C6E`. Шесть сценариев, ноль ошибок валидатора.

## Worker slot2

Логи `Saved/Logs/Worker/20261125-020017/115-verify/`. Временная карта `/Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit_115Roof`. UBT `Result: Succeeded` за 544,42 с. Bake сохранил 343 тайла Default и 252 Vehicle. `KitDressLayout.txt:41` подтвердил 24 ступени, заднюю стену и оба смещения 125 см.

- Walk PASS 156,7 с, **6/6 зданий**, 20/20 основных подходов, 100/100 входов в квартиры и 6/6 проверок пути. `Smoke-LevelGenBuildingsWalkSmoke.log:2358` фиксирует вход на link лестницы S07 снизу, строка 3049 — итог.
- Ramp PASS 95,2 с, **36/36**. Cutaway PASS 47,4 с, `Smoke-CutawayClickSmoke.log:1859`.
- `115-LevelGenIslandsProbe.log:1583` — S07 один остров, 587 точек. Строка 1586 — по одному острову во всех шести зданиях.
- Временная карта и её LevelJson перенесены из игровых каталогов worker в `C:/Work/CodexTactics-slot2/Saved/115-verify/Package`. Worker сообщил ноль отслеживаемых изменений и освободил блокировку.

## Публикация карты Lead

После разрешения дизайнера на перезапись Lead пересобирает **`Content/Maps/KitPilot/L_Gen_StoryShowcase_Kit.umap`** по package path **`/Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit`**. Из `Tools/TrainingGround` создать вход `node scripts/gen-story-showcase.ts ../../Content/Data/LevelGen/accepted/gen_story_showcase.json --fixed-bays`. Из корня проекта собрать и запечь `Scripts/kit_dress.ps1 -Layout Content/Data/LevelGen/accepted/gen_story_showcase.json -Target /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -Steps build,bake`. При переносе выходной геометрии сохранить боевые и погодные параметры текущего LevelJson Showcase.

Главная карта, LevelJson, игровые правила и пороги смоуков без правок. Публикация основного пакета остаётся у Lead.
