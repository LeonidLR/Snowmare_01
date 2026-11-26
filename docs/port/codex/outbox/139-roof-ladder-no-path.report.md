# 139 — диагностика лестницы на крышу / No way up

## Fresh headless run 2026-11-27 - MERGE-READY (map fix pending)

This section supersedes the earlier environment-blocker and LIMIT checkpoints. Existing lane/branch reused at `8b8d7d5c`; code baseline `9ce2d0f5`.

- AGENTS.md absent in lane and checked parent locations; CLAUDE.md/TANDEM/HANDOFF/active state and both task documents/review checkpoint read.
- `Scripts/test.ps1 -Smart -SkipSmokes -Base origin/main`: PASS 927/927, 117 s. To avoid spending minutes resolving tens of thousands of irrelevant untracked asset-pack files, the invocation used a temporary git core.excludesFile under lane Saved excluding untracked Content/Config only; tracked changes and all Source files remained included.
- Worker dispatch batch `dispatch-20261127-010644-99548`: RoofLadderSmoke FAIL as expected; RoofLadderRepairProbe PASS; FormationStairSmoke PASS (6/6 stair legs); WindowDetourSmoke PASS; WindowCoverFireSmoke PASS; CutawayClickSmoke PASS; CoverIndoorSmoke PASS; FormationWallHugSmoke PASS. LowCoverMoveLakeSmoke FAIL, reproduced on isolated retry.
- Remaining regression: crouched low cover had current capsule half-height about58.5cm, feet about2.2cm and an existing standing-centre goal89.5cm. The current capsule produced only80cm slack, so a complete ground path ending at z0 was incorrectly refused. Caller now includes the authored standing capsule height (class defaults, including BP overrides) in the allowance. Strict partial/unreachable upper-storey rejection remains unchanged. Smoke assertions were not weakened.
- The nine full names above are exactly the mapped smoke union for the five task139 Source files. Worker rebuild of the crouch correction PASS406.18s (`Saved/Logs/Worker/build-20261127-011215-25336/Build.log`). Final Smart recheck PASS927/927,0 failures,83s. Final exact nine-smoke recheck complete: eight PASS first try; only strict saved-map RoofLadderSmoke FAIL (expected, including isolated retry).
- Dispatcher initially chose the allowed main-PC fallback, which failed Kit-map preflight and ran no smokes. Subsequent dispatch used the existing dispatcher with main selection disabled in its calling process, preserving worker queue and lock behavior; no map copied or edited.
- The pre-existing worker_sync tool automatically invoked `git lfs push` for unpushed HEAD (objects only; branch refs stayed unpushed). This was discovered from its output. Removed that implicit upload from both authorized lane scripts; future syncs ship code by bundle. The first121 build was already waiting inside the old script and also used its old object-upload command. No manual branch push was performed.
- No maps, assets, Content JSON, Config or VFX edited; no designer Editor started or closed. No main/play checkout, commit or merge.


### Final validation of code f4d0c498

| Check | Result |
| --- | --- |
| Worker build | PASS406.18s |
| Smart -SkipSmokes -Base origin/main | PASS927/927,0 failures,83s |
| RoofLadderSmoke | FAIL expected: saved LadderActor_22 map inset still needs125/125 |
| RoofLadderRepairProbe | PASS; reaches feet302.2 in7.2s via LadderActor_22 |
| LowCoverMoveLakeSmoke | PASS |
| FormationStairSmoke | PASS |
| WindowDetourSmoke | PASS |
| WindowCoverFireSmoke | PASS |
| CutawayClickSmoke | PASS |
| CoverIndoorSmoke | PASS |
| FormationWallHugSmoke | PASS |

Final worker artifacts: `Saved/Logs/Worker/139-final/SmokeParallel-worker.json` and `Smoke-<Name>.log` (failed roof retry: `Smoke-RoofLadderSmoke-alone.log`). Batch wall254.4s, jobs2, no PASS* reruns. Final rerun invoked the existing machine_dispatch worker command builder directly, using normal worker_sync PAUSE/SYNC and locks to wait for the night loop, with no local fallback.

The low-cover regression is fixed in gameplay code; no smoke assertion changed. Local diff/boundary audit passed. Remaining designer action: apply LadderActor_22 TopStandoff=125 / TopExitStandoff=125, rebuild/save navmesh and rerun the original strict RoofLadderSmoke. No new engine build is needed for that map-only action. This report update changes no C++ beyond tested f4d0c498.

## Night resume 2026-11-27 00:55 Asia/Jerusalem — BLOCKED

This checkpoint supersedes the older LIMIT status below. Lane remains `codex/139-roof-ladder-no-path`; gameplay code remains `9ce2d0f525b2da1384919ea411cb2d2fdb64216a`. No gameplay changes in this resume.

- Existing final worker build confirmed PASS: `Saved/Logs/RemoteBuilds.jsonl` entry at `2026-11-26T17:31:49`, head `9ce2d0f5`, exit 0, total 939 s. `Saved/Logs/Build.log` ends `Result: Succeeded`, engine execution 453.07 s. No rebuild needed.
- Requested `Scripts/test.ps1 -Smart -SkipSmokes -Base origin/main` launched, but produced no output or fresh automation log before the environment blocker. Stopped our invocation; no new unit PASS claimed. Existing automation log is still dated November 26, 17:29.
- Requested dispatcher with FULL names: `python Scripts/Tools/machine_dispatch.py smoke --lane . --smokes RoofLadderSmoke,RoofLadderRepairProbe,LowCoverMoveLakeSmoke,FormationStairSmoke --jobs 2`.
- Worker unavailable: SSH reports `Permission denied` reading `/c/Users/Zephyrus15Duo/.ssh/known_hosts`, then `Host key verification failed`. Dispatcher selected the allowed/free main PC fallback.
- Fallback exited 1 during smoke preflight: `FormationStairSmoke` requires `/Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit`, but `Content/Maps/KitPilot/L_Gen_StoryShowcase_Kit.umap` is missing in this lane. No smoke runtime results from this resume; regression closure remains unverified.
- No maps, JSON, assets, Config or VFX edited. No merge, push, or main changes. AGENTS.md was absent in the lane and checked parent locations; CLAUDE.md, TANDEM.md, active session state, HANDOFF and task/checkpoint read.
- `git diff --check` passed. Report commit attempted but filesystem permissions deny creation of `.git/worktrees/CodexTactics-139/index.lock`; report remains saved and uncommitted, HEAD `0de781e6`. No approval requested.

Stopped under the explicit no-approvals/blocker instruction. Lead needs to restore worker SSH access or provide the required lane smoke maps before retrying step 2. The strict RoofLadderSmoke remains unchanged; the separately documented LadderActor_22 map fix (125/125 cm) still belongs to Lead. Task 121 was not started.

Статус: LIMIT — НЕ ЗАВЕРШЕНО. Primary used90%, осталось10%; остановка по правилу дизайнера до26.11.2026 18:23:42 Asia/Jerusalem. Предыдущее продолжение после RESUME Lead#1388. Worktree C:/Work/Lanes/CodexTactics-139, ветка codex/139-roof-ladder-no-path. В собственную ветку влит origin/main b65ec5f7 (merge abb03556), main не менялся.

## Подтверждённая причина

Baseline worker build PASS381.82s. Smoke CodexTactics.RoofLadderSmoke FAIL48.6s, сохраняет требование feet>=290 и реального подъёма по лестнице за60s. Лог: Saved/Logs/Worker/dispatch-20261126-163537-103732/Smoke-RoofLadderSmoke.log.

Фактическая лестница: LadderActor_22, tags ladder=LAD_B_x1_3_1_t1004_gallery, to=roof:B_x1_3_1_t1004_gallery. Позиция11611.60,-7992.70,8.80, yaw-83.5,8 ступеней, climbable/link enabled. Сохранённые TopStandoff49.1 и TopExitStandoff40.0 слишком малы:

- bottom11626.28,-8059.67,8.8 -> nav11626.28,-8059.67,10 (offset1.2cm).
- top11613.00,-7943.12,300 -> nav11613.00,-7904,310 (offset40.4cm).
- exit11614.03,-7952.16,300 -> nav11614.03,-7904,310 (offset49.2cm).
- roof goal11510,-7621,300 -> nav11510,-7621,310 (offset10cm).
- Оба пути bottom->roof, default и no-window, valid1/partial1, конец на земле11565.56,-8037.72,10. После60s feet11.0/climbs0. Причина не в formation filter: исходная навигационная связь на верхней площадке не подключена.

Предлагаемая data/map правка для Lead: применить authored top11597.45,-7868.48 к LadderActor_22 — порядка125cm от фактической плоскости ступеней, то есть TopStandoff125/TopExitStandoff125 вместо49.1/40.0; перестроить navmesh и проверить исходный smoke. Builder Scripts/Editor/build_levelgen_map.py уже выставляет authored inset для facade roof ladders (uses_layout_top): сохранённый actor текущей карты всё ещё имеет старые defaults. Карта, JSON дизайнера и assets не правились. Диагноз передан Lead#1403.

## Подготовлено после baseline

- Чистое правило OperativeMovementRules::HasNoWayUp: верхняя цель требует полного маршрута, заканчивающегося на её уровне (slack80cm); обычные partial approaches на том же уровне и спуски не меняются.
- RequestMove для собственной команды игрока отказывает с сообщением No way up, вместо молчаливой ходьбы к стене. Formation follow и PMC не меняются; locked-door redirect выполняется раньше в click handler.
- Unit Movement.NoWayUp: неполный путь, проекция на нижний пол, отсутствие пути, disconnected upper island, нормальная лестница/ступени, тот же уровень, граница80cm, спуск.
- Smoke также проверяет реальный отказ/сообщение/отсутствие собственной ходьбы на невозможной верхней цели. Исходный RoofLadderSmoke не исправляет геометрию и сохраняет строгую проверку подъёма.
- Отдельный RoofLadderRepairProbe меняет только runtime link/climb targets на125cm, ничего не сохраняет; нужен для доказательства предлагаемой data fix, не заменяет исходный smoke.
- Оба smoke явно используют -Sector=0, чтобы чужой сохранённый тренировочный сектор не обрезал путь.

## Текущие проверки

CheckMap PASS. Вторая worker-сборка (No way up + repair probe) PASS406.48s. Smart -SkipSmokes -Base origin/main: PASS320/320,85s (319 success,1 success-with-warnings,0 failed), включая Movement.NoWayUp. Совместимые smoke ещё идут; их полный итог пока не заявлен. Внешний TypeSafe пропущен по прямому решению дизайнера; только локальный аудит границ. Merge/push не выполнялись. Следующая задача147, затем143->146->136->137->138->140->G022.

## Проверено после правки

Код сохранён в44863738. Вторая серия worker проверок:

- RoofLadderSmoke на сохранённой карте по-прежнему FAIL: невозможная верхняя цель1300 корректно возвращает Unreachable, сообщение и отсутствие собственной ходьбы; реальная крыша300 тоже получает No way up (partial1), боец остаётся feet2.2 без ложного подхода к стене. Строгое требование z>=290/подъёма сохранено, knownFail не выставлялся.
- RoofLadderRepairProbe PASS: только в памяти TopStandoff/TopExitStandoff125; top/exit11604.40,-7867.71,300 проецируются без XY-смещения в navz310. После обновления nav-link реальная formation-команда получает partial0. Лог14:49:23.222Z: reached LadderActor_22 link going up;14:49:24.097Z: feet302.2 за7.2s; итогPASS14:49:24.108Z.
- Ни smoke, ни игровой код не сохраняют карту. Применение data fix к сохранённому actor остаётся у Lead. Повторная engine-сборка после редактирования карты не нужна, но нужен исходный RoofLadderSmoke после обновления navmesh.

## Compatibility finding (исправляется, не закрыто)

Первая серия9 smoke: RoofLadderRepairProbe, WindowDetour, WindowCoverFire, CutawayClick, CoverIndoor, FormationWallHug PASS; RoofLadderSmoke FAIL на сохранённой карте; LowCoverMoveLake и FormationStair FAIL из-за новой проверки высоты. Логи в Saved/Logs/Worker/dispatch-20261126-164737-100060/.

Подтверждение регрессии: LowCoverMoveLake передаёт GoalZ89.5 при feet2.2, полный путь заканчивается z0; locked-door approach FormationStair передаёт GoalZ92.15 при feet2.2, полный путь заканчивается z10. Это существующие capsule-centre вызовы, а не верхний этаж. Проверка теперь учитывает высоту капсулы: верхняя цель должна быть выше feet + max(80cm, capsuleHalfHeight+20cm); оценка end-level остаётся80cm. Добавлены точный legacy low-cover regression и отдельный prone-capsule case. Сами smoke не менялись. Третья worker-сборка стартовала16:55; повторные тесты этой ревизии ещё НЕ выполнены. Промежуточный44863738 не является финальным (Lead предупреждён#1409).

## Resume checkpoint — 2026-11-26 evening

- Final code revision: 9ce2d0f5 (capsule-centre compatibility correction).
- Third worker build: PASS, 416.42 s; Saved/Logs/Worker/build-20261126-165459-91580/Build.log ends Result: Succeeded.
- git diff --check: PASS. No designer JSON, map, asset, Config or VFX edits.
- Rechecks launched: smart unit tests (session 44650), focused Movement.NoWayUp (session 31742), dispatcher four smokes (session 44620, dispatcher pid 96588). Results pending at this checkpoint; do not claim regression closure.
- Dispatcher request: RoofLadderSmoke, RoofLadderRepairProbe, LowCoverMoveLakeSmoke, FormationStairSmoke; jobs=2. Initial sandbox denied queue lock; authorized escalated retry accepted. No approval rejection.
- Saved map remains blocked by LadderActor_22 upper landing inset, documented above. Repair probe passes only transient geometry; ordinary roof smoke must remain strict.
- Current designer queue supersedes older paragraphs: 139 -> 121 -> 148 -> 149 -> 147. Reuse existing 121 lane; review Gemini fbad5357 before creating codex branch. No extra lanes, no main push.

## LIMIT checkpoint 2026-11-26 17:30 Asia/Jerusalem

- Последний код **9ce2d0f525b2da1384919ea411cb2d2fdb64216a**, ранее44863738. Код закоммичен, незавершённый отчёт сохраняется отдельным checkpoint-коммитом.
- Финальное правило учитывает capsule-centre контракт на ОБОИХ концах: GoalZ > FeetZ + max(80, CapsuleHalfHeight+20), а полный путь принимается при EndZ >= GoalZ - этого же допуска; partial upper path всё равно отвергается.11 unit-case внутри Movement.NoWayUp, включая достижимый верхний этаж с centre-goal400/nav310. Последние эти11case **ещё не запускались**.
- Третья worker-сборка PASS416.42s соответствовала промежуточному исправлению. После саморевью изменены ещё тело pure rule и один unit-case; затем код сохранён в9ce2d0f5. Именно9ce2d0f5 сейчас собирается в четвёртом remote run.
- Запрос dispatcher139 PID103324 с17:03 отменён только после затяжного ожидания night-loop. Использована прямо разрешённая альтернатива `Scripts/build.ps1 -Remote worker` в той же lane. Штатный SYNC/PAUSE установлен; чужие locks и процессы не удалялись. Сессия exec **59090**, token **DESKTOP-6RLUUV8-101324-20261126-171612**. Реальная сборка на worker началась17:23:53; последний известный этап — компиляция dev-модуля. На LIMIT она ещё выполняется; **финальный build PASS не заявлен**.
- Lead#1421: дизайнер ушёл, MAIN_PC_FREE выставлен; будущий dispatcher может использовать оба ПК. Самостоятельно этот флаг не менялся.
- Smart320/320 и шесть compatibility PASS выше относятся к предыдущей ревизии; LowCoverMoveLake/FormationStair поймали исправляемую регрессию. Повторные runtime-проверки финальной ревизии **не выполнены**. Исходный RoofLadderSmoke на сохранённой карте останется FAIL до map fix от Lead.

Следующий шаг после reset:

1. Проверить уже начатую сборку: session59090, Saved/Logs/RemoteBuilds.jsonl и Saved/Logs/Build.log. При её успехе не пересобирать без причины.
2. `Scripts/test.ps1 -Smart -SkipSmokes -Base origin/main` в lane139. Затем dispatcher smoke полными именами RoofLadderSmoke, RoofLadderRepairProbe, LowCoverMoveLakeSmoke, FormationStairSmoke (и нужные остальные mapped checks по фактической ревизии). Сохранить исходную строгую проверку крыши и не ставить knownFail.
3. Финально локальный audit/отчёт/коммит и FINISH со всеми фактическими PASS/FAIL. Передать Lead map fix125/125 для LadderActor_22; карты/JSON/assets Codex не меняет.
4. Далее срочно121, затем148->149->147->143->146->136->137->138->140 (Lead#1414). Existing lane121 clean at fbad5357, reuse её без второй lane. Read-only review заметки: `C:/Users/Zephyrus15Duo/Documents/Codex/121-readonly-review-checkpoint.md`; исходники121 не редактировались. В121 перед повторным использованием WIP исправить подтверждённые ложные tactics/атрибуцию; основной приоритет — полная запись в Play через Lead до боёв27.11.

До полного сброса только ожидание/AgentChat. FINISH для139 не отправлялся.
