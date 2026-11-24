# 071 — проба Riffle снаружи и задание по оконному огню

В `Content/Data/Animation/cover_anim_sets.json:4–7` наружное укрытие выбирало M4, внутреннее Riffle. Добавил `Codex.Cover.RiffleOutdoor` в `Source/CodexTactics/Private/Characters/OperativeAnimInstanceCoverSet.cpp:24,223`. Значение 0 сохраняет выбор из JSON. Значение 1 пробует Riffle на обычных наружных стенах после повторного входа в укрытие. Выбор проходит проверку наличия набора и клипов для тела.

`Source/CodexTactics/Private/Characters/CoverAnimSetRules.cpp:120–135` ограничивает переключатель наружным контекстом. В `Source/CodexTacticsTests/Private/Characters/CoverAnimSetRulesTest.cpp:71–83` добавлены четыре проверки наружного режима, stealth, внутреннего override и главного отключения. `docs/port/grok/inbox/G016-window-cover-fire.md` содержит правила распознавания, позы, стекла, состава формации и план smoke.

Проверка diff и TypeSafe — PASS. UE automation и smoke с переустановкой укрытия и доступными Riffle-ассетами ожидают Lead/worker. Выстрел через окно описан в задании Grok, реализация 071 ограничена переключателем.
