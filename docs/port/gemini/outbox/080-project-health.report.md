# 080 — PROJECT HEALTH: Отчёт о реализации

2026-11-24. Автор: Antigravity (Gemini). Lead: Claude. Ветка: `gemini/080-project-health`.

---

## 1. Резюме

По заданию дизайнера реализована комплексная система мониторинга технического здоровья проекта **080 PROJECT HEALTH**:
1. **Ночной полный прогон в цикле воркера (`worker_night_loop.ps1`)**:
   - Интегрирована функция `RunNightlyHealthCheck`, запускающая полный набор юнит-тестов (`Scripts/test.ps1 -Filter CodexTactics`) и все 41 зарегистрированных смоуков (`Scripts/smoke_parallel.ps1 -Commands all -Jobs $Jobs -ExtraArgs '-Multiprocess'`).
   - Изоляция по расписанию: прогон выполняется строго вне окна A/B (05:30–07:30) и часов тренера, в начале ночного цикла (Cycle 1).
   - Поддерживает флаги `[switch]$SkipHealthCheck` и `[switch]$HealthCheckOnly`.
   - Записывает результаты в `digest.log` и формирует строку утреннего дайджеста.
2. **Агрегатор здоровья и генератор отчёта долга (`Scripts/Tools/project_health.py`)**:
   - Автоматический сбор `knownFail` из `test_map.json` и `merge_gate.ps1` с определением даты первого появления и автора через `git blame`.
   - Контроль SLA: вычисление `days_active = (today - first_seen).days`. При `> 7 дней` выставляется статус `expired: true` (выделяется красным).
   - Учёт проверок, перемещённых в архив или отключённых на этой неделе (по коммитам в `test_map.json` за последние 7 дней).
   - Сканирование маркеров техдолга (`TODO`, `FIXME`, `HACK`, `WORKAROUND`, `XXX`) по модулям `Source/CodexTactics`, `Source/CodexTacticsDev`, `Source/CodexTacticsTests`, `Scripts`, `Content/Data` с защитой от ложных срабатываний (например, UE `MoveTemp`).
   - Расчёт дельты роста техдолга по сравнению с предыдущими срезами (`health_*.json`).
   - Сбор трендов времени сборок (`lead_build_*.log`, `Build.log`) и истории запусков `merge_gate.ps1`.
   - Генерация артефактов:
     - `Saved/Health/health_latest.json`
     - `Saved/Health/health_YYYY-MM-DD.json`
     - `docs/port/DEBT.md`
     - Однострочный дайджест для утреннего чата `agent_chat.py`.
3. **Отдел «Состояние проекта» в Training Ground (`Tools/TrainingGround`)**:
   - Архитектурное соответствие: **строго read-only чтение `Saved/Health/*.json`** через dev-сервер API (`Tools/TrainingGround/server/projectHealthApi.ts`).
   - Интерактивный компонент `Tools/TrainingGround/src/departments/ProjectHealthDepartment.tsx`:
     - Карточки метрик (Health status, Known Fails с подсветкой просрочек, Смоуки 41, Юнит-тесты, TODO-маркеры, Merge-Gate & Build).
     - Реестр Known-Fails с индикатором SLA (зелёный — в работе, красный — `🔴 ПРОСРОЧЕНО (> 7 дн)`).
     - Срезы по датам (переключение на исторические `health_YYYY-MM-DD.json`).
     - Вкладки маркеров техдолга по директориям, архивированных смоуков, трендов сборки и логов merge-gate.
   - Зарегистрирован в `DEPARTMENTS` в `TrainingGround.tsx` под id `'health'`.

---

## 2. Коммиты в ветке `gemini/080-project-health`

1. `fca4cbaf`: `feat(health): project health aggregator and DEBT.md report generator`
   - `Scripts/Tools/project_health.py`
   - `docs/port/DEBT.md`
   - `docs/port/gemini/outbox/080-project-health.plan.md`
2. `f14c38d2`: `feat(nightloop): integrate nightly full test and smoke health check`
   - `Scripts/Tools/worker_night_loop.ps1`
3. `949fb0ca`: `feat(tg): add Project Health department tab and read-only Saved/Health API`
   - `Tools/TrainingGround/server/projectHealthApi.ts`
   - `Tools/TrainingGround/src/departments/ProjectHealthDepartment.tsx`
   - `Tools/TrainingGround/src/departments/projectHealth.test.ts`
   - `Tools/TrainingGround/src/TrainingGround.tsx`
   - `Tools/TrainingGround/vite.config.ts`

---

## 3. Проверки и верификация

1. **Запуск `Scripts/Tools/project_health.py`**:
   - Успешная генерация `Saved/Health/health_latest.json` и `Saved/Health/health_2026-11-24.json`.
   - Успешная генерация отчёта `docs/port/DEBT.md`.
   - Вывод чат-строки:
     `HEALTH 2026-11-24 | status WARNING | tests 420/420 PASS | smokes 41 (40 pass, 1 known-fail, 0 fail) | debt: 2 known-fails (0 expired), 3 TODOs | merge-gate: PASS`
2. **Проверка синтаксиса PowerShell**:
   - `[System.Management.Automation.Language.Parser]::ParseFile` на `worker_night_loop.ps1` завершился с результатом `Syntax OK`.
3. **Typecheck Training Ground**:
   - `npm run typecheck` (`tsc && tsc -p tsconfig.node.json`): **0 ошибок, exit code 0**.
4. **Unit-тесты Training Ground**:
   - `node --test src/departments/projectHealth.test.ts`: **2/2 passed (119 ms)**:
     - `ProjectHealth: SLA 7-day expiry calculation` (PASS)
     - `ProjectHealth: Schema validation and default fallback` (PASS)

---

## 4. Чего не делал

1. Не запускал тяжёлый полный прогон всех 41 смоуков на локальной машине разработчика (по правилам TANDEM: тяжёлые прогоны проводятся строго на `codex-worker`).
2. Не модифицировал рабочие файлы логики игры или шейдеров (задача ограничена инфраструктурой мониторинга качества, отчётами и UI Training Ground).
3. Ветку в `origin/main` не сливал и не пушил (ждёт ревью Lead).
