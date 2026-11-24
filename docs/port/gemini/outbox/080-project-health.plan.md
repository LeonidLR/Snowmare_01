# 080 — PROJECT HEALTH: Архитектурный план и дизайн-документ

2026-11-24. Автор: Antigravity (Gemini). Lead: Claude. Ветка: `gemini/080-project-health`.

---

## 1. Цели и скоуп задачи

Задача **080 PROJECT HEALTH** реализует непрерывный мониторинг технического здоровья и долгового реестра проекта CodexTactics:
1. **Ночной полный прогон на воркере (`worker_night_loop.ps1`)**:
   - Запуск полного набора юнит-тестов (`Scripts/test.ps1 -Filter CodexTactics`).
   - Запуск всех 41 зарегистрированных смоуков (`Scripts/smoke_parallel.ps1 -Commands all`).
   - Изоляция по времени: строго вне окна A/B (05:30–07:30) и часов тренера. Запуск в начале ночного цикла (Cycle 1 / перед циклическими сидами).
2. **Ежедневный отчёт технического долга (`docs/port/DEBT.md` + `Saved/Health/*.json`)**:
   - `knownFail` с датой первого появления, причиной (`_why`), владельцем и контролем 7-дневного срока (просроченные `> 7 дней` выделяются красным `EXPIRED`).
   - Смоуки и тесты, отключённые или перемещённые в архив на этой неделе.
   - Динамика маркеров техдолга (`TODO`, `FIXME`, `TEMP`, `HACK`, `WORKAROUND`) по модулям и директориям.
   - Тренд времени сборки (по логам `lead_build_*.log`, `Build.log`, `merge_gate`).
   - История и результаты прогонов `merge_gate.ps1`.
3. **Вкладка «Состояние проекта» в Training Ground (`Tools/TrainingGround`)**:
   - Новый отдел `health` («Состояние проекта») в навигации Training Ground.
   - Архитектурное ограничение: компонент работает **строго на чтение `Saved/Health/*.json`** (read-only), отображает статус здоровья, карточки метрик, таблицу долгов с подсветкой просрочек и историю merge-gate.
4. **Утреннее оповещение в чате**:
   - Однострочный дайджест в `agent_chat.py` каждое утро:
     `HEALTH <date> | tests X/X | smokes 41 (Y pass, Z known-fail) | debt: N known-fails (M expired), K TODOs | merge-gate: <status>`

---

## 2. Архитектурная схема компонентов

```
                  worker_night_loop.ps1 (Cycle 1, ~22:00 / Early Night)
                                    │
           ┌────────────────────────┴────────────────────────┐
           ▼                                                 ▼
    Scripts/test.ps1                            Scripts/smoke_parallel.ps1
  (all CodexTactics.*)                                (-Commands all)
           │                                                 │
           └────────────────────────┬────────────────────────┘
                                    ▼
                     Scripts/Tools/project_health.py
           (сбор логов, git blame/log, TODO grep, merge-gate)
                                    │
           ┌────────────────────────┼────────────────────────┐
           ▼                        ▼                        ▼
Saved/Health/health_latest.json docs/port/DEBT.md     Saved/Worker/nightloop/
Saved/Health/health_<date>.json                       digest.log / agent_chat
           │
           ▼
Tools/TrainingGround (Tab: «Состояние проекта»)
  - GET /api/project-health -> Saved/Health/*.json
  - UI: ProjectHealthDepartment.tsx
```

---

## 3. Спецификация данных `Saved/Health/health_latest.json`

```json
{
  "$schema": "codex.project_health/1",
  "generated_at": "2026-11-24T06:00:00Z",
  "head": "ba8c544c",
  "summary": {
    "status": "WARNING", // "OK" | "WARNING" | "CRITICAL"
    "unit_tests": { "total": 420, "passed": 420, "failed": 0, "duration_s": 92.4 },
    "smokes": { "total": 41, "pass": 40, "known_fail": 1, "fail": 0, "duration_s": 480.0 },
    "known_fails_count": 1,
    "known_fails_expired": 0,
    "disabled_this_week_count": 0,
    "todo_markers_count": 48,
    "last_build_time_s": 241.5,
    "last_merge_gate_status": "PASS"
  },
  "known_fails": [
    {
      "name": "GeneratedPropsX6Smoke",
      "type": "smoke",
      "reason": "G003: 0 safe crates on X6 cover layout, designer file not regenerated",
      "owner": "gemini",
      "first_seen": "2026-11-24",
      "days_active": 0,
      "expired": false
    }
  ],
  "disabled_this_week": [],
  "todo_markers": {
    "total": 48,
    "by_directory": {
      "Source/CodexTactics": 18,
      "Source/CodexTacticsDev": 14,
      "Source/CodexTacticsTests": 6,
      "Scripts": 10
    },
    "sample": [
      { "file": "Source/CodexTactics/Private/Player/OperativeCharacter.cpp", "line": 142, "text": "TODO: smooth camera blend" }
    ]
  },
  "build_trend": [
    { "date": "2026-11-23 20:31", "target": "CodexTacticsEditor", "duration_s": 396.78, "actions": 59, "status": "SUCCESS" }
  ],
  "merge_gate_runs": [
    { "timestamp": "20261123-203140-096", "branch": "ai/x6-arena", "status": "PASS", "details": "41 smokes, 0 unexpected fails" }
  ]
}
```

---

## 4. План реализации по шагам (малые коммиты)

1. **Коммит 1: Агрегатор здоровья и генератор DEBT.md (`Scripts/Tools/project_health.py`)**:
   - Парсинг `test_map.json` (поиск `knownFail`, извлечение `_why`, определение даты и автора через `git log -S` / blame).
   - Подсчёт дней активности и вычисление флага `expired` (`> 7 дней`).
   - Поиск изменений отключённых смоуков за последние 7 дней через `git log`.
   - Быстрый сканер маркеров `TODO/FIXME/TEMP/HACK/WORKAROUND`.
   - Парсер логов сборок и merge-gate из `Saved/Logs/Worker/` и `Saved/lead_build_*.log`.
   - Генерация `Saved/Health/health_latest.json`, `Saved/Health/health_YYYY-MM-DD.json` и `docs/port/DEBT.md`.
2. **Коммит 2: Интеграция в `worker_night_loop.ps1`**:
   - Добавление вызова `RunNightlyHealthCheck` в начале ночного цикла (Cycle 1).
   - Проверка блокировок `TakeLock`, тайм-аутов и корректное освобождение ресурсов до окна 05:30.
   - Запись результатов в `digest.log` и формирование утренней строки для `agent_chat.py`.
3. **Коммит 3: Интерфейс Training Ground — вкладка «Состояние проекта»**:
   - Backend endpoint в `Tools/TrainingGround/server/projectHealthApi.ts` и `vite.config.ts`: `GET /api/project-health` (чтение файлов из `Saved/Health/`).
   - Frontend компонент `Tools/TrainingGround/src/departments/ProjectHealthDepartment.tsx`.
   - Регистрация в `DEPARTMENTS` в `TrainingGround.tsx` под id `'health'`.
   - Тесты в `Tools/TrainingGround/src/departments/projectHealth.test.ts`.
4. **Коммит 4: Финальная валидация и отчёт**:
   - Тестовый локальный прогон `project_health.py` с проверкой генерации `DEBT.md` и JSON.
   - Прогон `typecheck` и тестов Training Ground.
   - Подготовка отчёта `docs/port/gemini/outbox/080-project-health.report.md`.
