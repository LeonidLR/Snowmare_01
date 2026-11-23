# 062 — переходы Aim/2 в укрытии

Правка в ветке `codex/062-indoor-cover-fixes` от `f1424fa7`, в отдельном worktree. Код `f15204aa`. Ревизия worker `76fac5b1`. Следующий коммит `18675b28` добавляет привязку тестов к правилу impact map.

## Диагноз и правка

- В базе `OperativeAnimInstanceCoverSet.cpp:404–416` снимал флаг переключения до того, как `OperativeAnimInstance.cpp:2251–2286` входил в огневую позу. Повторный выстрел мог прервать переход. Теперь фазы идут в порядке switch-in → fire → switch-out. Выстрел во время перехода дожидается огневой позы. Уход, новый приказ движения, смена укрытия и смерть сбрасывают фазу. См. `OperativeAnimInstance.cpp:538`, `:909`, `:2024`, `:2283` и `OperativeAnimInstanceCoverSet.cpp:419`, `:485`, `:506`.
- В базе `OperativeAnimInstanceCoverSet.cpp:157`, `:264` записывал даже пустой набор, после чего очищал родные слоты без ключа. Теперь набор попадает в список доступных, только если загружен хотя бы один базовый клип для тела. При пустом наборе остаются исходные клипы, в том числе у PMC. См. `OperativeAnimInstanceCoverSet.cpp:156`, `:192`, `:248`.
- При выходе из Aim/2 каждый из четырёх слотов заново получает боковой клип набора либо исходный нативный клип. В базе `OperativeAnimInstanceCoverSet.cpp:378–398` при пропуске оставался старый клип Aim/2. См. `OperativeAnimInstanceCoverSet.cpp:378` и `CoverAnimSetRules.cpp:207`.
- Регрессии `OverTopStages` и `MissingClips` в `CoverAnimSetRulesTest.cpp:121` и `:145`. Для `CoverIndoorSmoke` зарегистрированы карта Kit, LevelJson и правило проверки в `Scripts/test_map.json:66`, `:218`.

## Проверка

- `git diff --check` и TypeSafe boundary audit — PASS. `Scripts/test.ps1 -CheckMap` — `Impact map OK: every smoke command is registered and mapped.`
- Worker, слот 1, сборка — `Result: Succeeded`, exit 0, 502,17 с UBT. Лог `Saved/Logs/Worker/codex-062/Build.log`.
- Worker, `test.ps1 -Filter CodexTactics.Animation.CoverAnimSet` — `9 tests, 0 failed`. Оба новых теста `Success`.
- Worker, `CoverSmoke` — `Smoke RESULT: PASS`, 41,7 с, `Smoke-CoverSmoke.log:2069`.
- Worker, `CoverIndoorSmoke` — FAIL в паре и при повторе (45,4 и 38,7 с). `Smoke-CoverIndoorSmoke-alone.log:1392` подтверждает загрузку карты Kit, `:1433` — отсутствие Kit LevelJson, `:1714` — `has no loaded cover slots; using native clips`; `:1828` и `:1893` фиксируют фактические M4-клипы вместо Riffle/Aim/2. Каталогов с клипами для UE4 и PMC нет в обоих worker checkout. Прогон подтвердил нативный fallback для UE4. Переходы Aim/2 с реальными клипами и fallback PMC в игре остаются без runtime-проверки.

## Чего не сделал

Не менял карты, `.uasset` и копию Lead. Не запускал сборку или смоуки на ПК дизайнера. Для полноценного повтора нужны сгенерированные CoverIndoor-клипы на слоте 1 и Kit LevelJson из основной ветки.
