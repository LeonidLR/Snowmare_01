# 113 · Лестничный смоук Showcase Kit

## Диагноз

В ночном логе `Saved/Logs/Worker/20261125-000028/Smoke-FormationStairSmoke.log` фоновый отряд из шести ЧВК появился на строке 1604 до старта смоука на строке 1890. Командир погиб на строке 2038 до первой проверки подъёма. Итог на строке 2087 — 0/3. Дневной PASS этого смоука тоже зависел от результатов боя. В `Saved/Logs/Worker/merge-gate-20261124-155612-020/Smoke-FormationStairSmoke.log:2115` и `:2157` погибли два ведомых, а на строке 2176 в S10 пошёл один командир.

- На slot2 с тем же файлом Showcase Kit и теми же параметрами `f6e6a60e` дал FAIL 4/4, включая 0/3, 1/3, 1/3 за 62,8, 69,2 и 68,4 с в контрольных прогонах задачи 112.
- `d1e23cbd` дал FAIL 3/3 за 79,7, 74,7 и 70,1 с, подъёмы 0/3, 1/3, 1/3. В `Saved/Logs/Worker/20261125-004714/Smoke-FormationStairSmoke-r1.log:2139`, `:2144`, `:2157` повторились оба провала подъёма и проверка запертой двери. В заданном диапазоне нет подтверждённой хорошей границы для bisect.
- На том же `d1e23cbd` диагностический запуск с ранним `Codex.Pmc.Encounters 0` дал PASS 3/3 за 58,2 с. На обоих подъёмах было три бойца, см. строки 1850, 1924, 2008 в `Saved/Logs/Worker/20261125-010235/Smoke-FormationStairSmoke.log`. Источник фонового боя по истории — merge 076 `9401912d`, который ввёл `pmc_squads` и реальный урон на Showcase Kit до `d1e23cbd`.

## Изменения

- `Source/CodexTacticsDev/Private/Debug/FormationStairSmokeCommand.cpp:455` использует существующий `SmokeUtils::StopLevelPmc`, который отменяет ожидающие спавны и удаляет уже созданных ЧВК. Строки 238 и 254 требуют отряд с ведомым и отклоняют результат после потери бойца. Условия подъёма, 40 с, дистанция 8 м и проверка запертой двери сохранены.
- По `Scripts/test_map.json:34`, `:59`, `:60`, `:61`, `:65`, `:66` проверены все шесть зарегистрированных смоуков Showcase Kit. CoverIndoor, FormationWindow, WindowStairHover, CutawayClick и RoofCutawayStorey уже вызывают очистку из 085. В ручной команде `StoryShowcaseFixShotsCommand.cpp:278` устранён тот же пробел.
- `Source/CodexTacticsDev/Private/Debug/SmokeUtilsTest.cpp:9` проверяет удаление заранее зарегистрированного бойца ЧВК, выключение фонового спавна и сохранение игрока. Фильтр добавлен в impact map `Scripts/test_map.json:177`.

## Проверки

- `44023887` на worker slot2. `Scripts/build.ps1` → `Result: Succeeded`, 527,40 с UBT.
- `Scripts/test.ps1 -Filter CodexTactics.Smoke.Fixture` → `Success CodexTactics.Smoke.Fixture.StopsPreloadedPmc`, 1 тест, 0 ошибок.
- Штатный `FormationStairSmoke -Repeat 3 -Jobs 1 -NoRetry` с `-FixedSeed` → PASS 3/3 прогонов за 91,6, 57,6 и 50,9 с. В каждом прогоне отряд из шести ЧВК успел появиться до команды, и после её запуска все лестничные проверки прошли с тремя бойцами игрока, без гибели. `Saved/Logs/Worker/20261125-010920/Smoke-FormationStairSmoke-r1.log:1602`, `:1888`, `:1897`, `:1978`, `:2063`. Итоги r2 и r3 на строках 2060.
- SHA-256 карты для сравнений slot2 `C8429E66DFCE449A10C23389C2B4F5413D6AB92C5B1E54EB09D3E3939D5CA285`, совпадает с текущей копией в папке проекта. `Scripts/test.ps1 -CheckMap` → `Impact map OK: every smoke command is registered and mapped.`

## Границы

Код игрового строя, ЧВК, лестниц, данные и карты без правок. Режимы ручных снимков отдельно не рендерились. Bisect с неподтверждённой исходной точкой не запускался. Использованы локальные коммиты и сборка на slot2, блокировка освобождена. Слияние оставлено Lead.
