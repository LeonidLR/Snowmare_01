# 139 — WIP checkpoint: roof ladder no path

Статус: **НЕ ЗАВЕРШЕНО**, диагностика подготовлена; игровой фикс ещё не внесён.
Ветка `codex/139-roof-ladder-no-path`, worktree `C:/Work/Lanes/CodexTactics-139`, база `5e510025` (origin/main при старте).

## Найдено до правки

- Лог дизайнера `docs/port/codex/inbox/139-roof-ladder-excerpt.log`: roof clicks `(11446..11700,-7557..-7667,300)` принимаются, боец остаётся у стены; нет события reached ladder link. Partial path пока гипотеза по логу, не подтверждён runtime probe.
- В accepted layout `Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x6.json` точка `(11500,-7640)` принадлежит `B_x1_3_1_t1004_gallery`: footprint `[[11151.85,-7977.43],[11852.85,-7194.43]]`, height300, floors1, roof_walkable=true, внутренних stairs/hatches нет.
- Его ladder id `LAD_B_x1_3_1_t1004_gallery`: pos `(11609.9,-7977.77)`, yaw -83.5, rungs8, plinth8.8; authored foot `(11621.22,-8077.13)`, top `(11597.45,-7868.48)`.
- Builder уже поддерживает authored landing inset для roof ladders (`Scripts/Editor/build_levelgen_map.py:1381–1390`, `:1727–1740`): выставляет top_standoff/top_exit_standoff по layout top. Нельзя предполагать, что сохранённый ALadderActor текущей карты имеет эти значения: это должен подтвердить runtime log.
- `ALadderActor` default TopStandoff49.1 / TopExitStandoff40.0; возможен старый inset у парапета, но это **не подтверждённый диагноз**.

## Что подготовлено

- Новый `Source/CodexTacticsDev/Private/Debug/RoofLadderSmokeCommand.cpp`, команда **CodexTactics.RoofLadderSmoke**.
- На X6: ждёт preload/nav, выключает фоновых PMC/pressure, находит фактическую лестницу у цели, пишет actor name/tags/transform/standoffs/enabled, bottom/top/exit/roof-goal и их nav projections, complete/partial paths с окнами/без окон, затем настоящий formation roof click.
- Требует, чтобы лидер поднялся по ladder и feet>=290 за60 s; иначе FAIL с фактическим положением. Геометрия, карта и ladder не подправляются.
- `Scripts/test_map.json`: регистрация, X6 options, отдельный impact rule.
- `Scripts/test.ps1 -CheckMap`: PASS. Компиляция и сам smoke ещё **не выполнены/не подтверждены**.
- Gameplay C++ и данные не менялись. Feedback **No way up пока НЕ реализован**.

## Следующий шаг после сброса

1. Проверить существующий dispatcher build (запущен из lane139, исходно PID93976; на10:00:56 стоял в общей очереди за worker). При отсутствии результата: `python Scripts/Tools/machine_dispatch.py build --lane C:/Work/Lanes/CodexTactics-139`.
2. `python Scripts/Tools/machine_dispatch.py smoke --lane C:/Work/Lanes/CodexTactics-139 --smokes CodexTactics.RoofLadderSmoke --jobs 1`.
3. По `[RoofLadder]` установить реальные actor/link ends/navmesh projections и partial path. При data issue — точный ladder/roof и предлагаемый inset/data fix в отчёте, без записи карты/X6 JSON. При runtime issue — минимальный фикс.
4. Добавить понятное сообщение No way up для недоступной верхней цели; сейчас OrderGroupMove игнорирует EOperativeOrderResult, а RequestMove принимает successful partial path. Сохранить подход к locked door и разрешённые partial moves на том же уровне; unit test для чистой логики, если она выделяется.
5. Build + Smart + smoke + локальный audit; TypeSafe пропускать по прямому решению дизайнера (из задачи134). FINISH только после реального завершения.

## Границы и лимит

Карты, `.uasset`, Config, VFX и несохранённый `L_Gen_Depot_1_X6.json` в main не менялись. Бинарные зависимости только прочитаны из main в собственный worktree, чтобы remote build не скачивал старые EXE; до успешной сборки139 они не считаются проверенной сборкой139.
Merge/push не выполнялись. Очередь Lead: **139 -> 136 -> 137 -> 138 -> 140 -> G022**; G022 только после merge G023.
Сброс текущего окна Codex: **2026-11-26 13:16 Asia/Jerusalem**. При10% остатка — LIMIT и ожидание reset.
