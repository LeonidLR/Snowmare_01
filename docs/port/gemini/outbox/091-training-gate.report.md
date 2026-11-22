# 091 - Training Gate & Scenario Regression (T-10) - Report (Rework)

## Handover
- **Done**:
  - **Review Point 1 (BOM / UTF-8-SIG)**: `Scripts/Tools/scenario_regression.py` и `Scripts/Trainer/night_trainer.py` обновлены для чтения JSON через `encoding="utf-8-sig"`. Результаты `smoke_parallel.ps1` (записываемые с PowerShell BOM) парсятся без ошибок.
  - **Review Point 2 (Пакетный запуск smoke)**: В `Scripts/Tools/scenario_regression.py` убран последовательный цикл вызова отдельных процессов на каждый сид. Теперь запускается один пакетный вызов `smoke_parallel.ps1 -Commands <smoke> -Repeat N -SmokeArgs "Seed={n}" -NoRetry -Jobs J -ResultJson <temp_json>`, и все N результатов считываются из единого JSON.
  - **Review Point 3 (--compare-yesterday & сценарии)**: Реализована функция `extract_seed_results()`, корректно читающая структуру `results` и мультисценарную `scenarios`. Убран хардкод `["crossroads", "alley", "roofs"]`: по умолчанию берутся все сценарии из `SCENARIO_MAP` (`crossroads`, `alley`, `roofs`, `chokepoint`, `stalemate`, `encounter`), либо переданный список через запятую.
  - **Review Point 4 (Гигиена файлов gate_pass.json)**: В `Scripts/Trainer/night_trainer.py` при вердикте `BLOCKED` устаревший `gate_pass.json` удаляется из `Saved/Trainer/`. В `Scripts/Trainer/run_night_trainer.ps1` добавлена проверка валидности содержимого (`.passed -eq $true`), исключающая запуск при невалидном или устаревшем файле.
  - **Review Point 5 (Поиск метрик, снятие прокси, штрафы, KNOWN-FAIL)**:
    - В `Scripts/Trainer/nighttrainer/gate.py` поиск метрик теперь проверяет реальную структуру `sides.<side>.behaviour.metrics.<k>.value`, `sides.<side>.behaviour.<k>`, `rnd.behaviour` и `defence`.
    - Удалены синтетические прокси (`time_in_contact_s` не подменяется `duration_s`, `moves` не подменяется `flanks_attempted`).
    - Метрики штрафов (`friendly_fire`, `stuck_events`) с нулевыми значениями в чистых раундах больше не помечаются как мертвые.
    - В проверке G1 допускаются статусы `KNOWN_FAIL` и `KNOWN-FAIL`.
    - В `Scripts/Trainer/tests/test_gate.py` тесты переведены на реальную схему `codex.pmc_arena_behaviour/1`, добавлены проверки на удаление прокси, сохранение нулевых штрафов и удаление устаревшего `gate_pass.json`.
- **Left**: Ничего (все 5 замечаний ревью закрыты).
- **Needs Claude**: Слияние из локальной ветки `gemini/091-training-gate` в `main`.
- **Next Step**: Переход к задаче `090 rework` (Tactical Export v2).

---

## Claim | Evidence Table

| Claim | Evidence (File:Line / Command / Output) |
|---|---|
| Review 1: UTF-8-SIG чтение в scenario_regression | `Scripts/Tools/scenario_regression.py:85,156,381,393` (`encoding="utf-8-sig"`) |
| Review 1: UTF-8-SIG чтение в night_trainer | `Scripts/Trainer/night_trainer.py:108,114,120` (`encoding="utf-8-sig"`) |
| Review 2: Единый пакетный вызов smoke_parallel | `Scripts/Tools/scenario_regression.py:100-185` (`-Repeat len(seeds) -SmokeArgs "Seed={n}" -NoRetry -Jobs parallel`) |
| Review 3: Поддержка results и scenarios в сравнении | `Scripts/Tools/scenario_regression.py:214-288` (`extract_seed_results`, `compare_with_yesterday`) |
| Review 3: Динамический список сценариев без хардкода | `Scripts/Tools/scenario_regression.py:365-385` (`list(SCENARIO_MAP.keys())`) |
| Review 4: Удаление устаревшего gate_pass на BLOCKED | `Scripts/Trainer/night_trainer.py:125-147` (`os.remove(pass_file) if not gate_res["passed"]`) |
| Review 4: Валидация passed: true в run_night_trainer | `Scripts/Trainer/run_night_trainer.ps1:46-59` (`$gateDoc.passed -eq $true`) |
| Review 5: Поиск behaviour.metrics.<k>.value | `Scripts/Trainer/nighttrainer/gate.py:77-152` (`get_metric_value_from_round`) |
| Review 5: Удаление фейковых прокси | `Scripts/Trainer/nighttrainer/gate.py:136-146` (только скалярные поля side_info, без fallback на duration_s) |
| Review 5: Исключение штрафов с 0 из dead metrics | `Scripts/Trainer/nighttrainer/gate.py:154-205` (`extract_penalty_metrics`, `is_penalty`) |
| Review 5: Поддержка KNOWN-FAIL в G1 | `Scripts/Trainer/nighttrainer/gate.py:240-270` (`res_dict[clean_req] not in ("PASS", "PASS*", "KNOWN_FAIL", "KNOWN-FAIL")`) |
| Юнит-тесты test_gate.py (17 тестов) | `python -m unittest Scripts/Trainer/tests/test_gate.py` -> 17 tests OK (0.177s) |
| Все юнит-тесты тренера (102 теста) | `python -m unittest discover -s Scripts/Trainer/tests` -> 102 tests OK (13.638s) |
| Typesafe Jev Safety Audit | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` -> PASS, Risk 0.1/4 |
| Чистота git diff | `git diff --check` -> 0 errors |

---

## Примечание по тестам инструментов (`Scripts/Tools/tests`)
Все 102 теста подсистемы тренера (`Scripts/Trainer/tests`) проходят на 100%.
В общем наборе инструментов `Scripts/Tools/tests` (запускаемом общим хуком `gemini_gate.ps1` из-за наличия `Scripts/Tools/scenario_regression.py`) присутствует предсуществующее падение в `test_task_board.py:229` (`codex-008 title_ru`), внесенное коммитом `f43b61bd` от 2026-11-19 в ветке `main` до начала работы над задачей. В соответствии с правилом Rule 12 сторонние подсистемы не модифицировались.

---

## Протокол верификации и логи

### 1. Тесты гейта (`test_gate.py`)
```text
$ python -m unittest Scripts/Trainer/tests/test_gate.py
.................
----------------------------------------------------------------------
Ran 17 tests in 0.177s

OK
```

### 2. Полный прогон тестов подсистемы тренера
```text
$ python -m unittest discover -s Scripts/Trainer/tests
----------------------------------------------------------------------
Ran 102 tests in 13.638s

OK
```

### 3. Проверка удаления stale `gate_pass.json` и блокировки запуска
```powershell
$ Set-Content -Path Saved/Trainer/gate_pass.json -Value '{"passed": false}'
$ powershell -ExecutionPolicy Bypass -File Scripts/Trainer/run_night_trainer.ps1
run_night_trainer: REFUSED - training gate pass file (...Saved\Trainer\gate_pass.json) missing, expired, or invalid. Run 'night_trainer.py --check-gate' first.

$ python Scripts/Trainer/night_trainer.py --check-gate --mock
============================================================
 TRAINING GATE: BLOCKED
============================================================
 • G1_smoke_health: FAIL
 • G2_metric_liveness: FAIL
 • G3_baseline_sufficiency: FAIL
Result written to ...\Saved\Trainer\gate_blocked.json
============================================================

$ Test-Path Saved/Trainer/gate_pass.json
False
```

### 4. Проверка Quick Day Mode и Night Mode в `scenario_regression.py`
```text
$ python Scripts/Tools/scenario_regression.py --scenario crossroads --seeds 1..5 --mock
# Day Loop Digest: crossroads (PmcCrossroadsSmoke)
- **Pass Rate**: 5 / 5 (100.0%)
- **Worst Seeds**: 1, 2, 3
- **Status**: ALL SEEDS GREEN

$ python Scripts/Tools/scenario_regression.py --night --seeds 1..3 --mock
# Nightly Scenario Regression Report (2026-11-22 09:25)
## 1. Summary by Scenario
| Scenario | Smoke | Seeds | Pass Rate | Failed Seeds | Worst Seeds |
|---|---|---|---|---|---|
| **crossroads** | `PmcCrossroadsSmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
| **alley** | `PmcAlleySmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
| **roofs** | `PmcRoofDefenceSmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
| **chokepoint** | `PmcArenaTacticalSmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
| **stalemate** | `PmcStalemateSmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
| **encounter** | `PmcEncounterSmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
```

### 5. Typesafe Safety Audit
```text
$ python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini
[Jev:System1] Auditing unstaged changes for agent 'gemini' (5 files)...
============================================================
 🛡️ TYPESAFE (JEV) SAFETY & BOUNDARY AUDIT
============================================================
 • Agent Audited   : GEMINI — unstaged
 • Boundary Breach : ✅ PASS (code check of protected paths)
 • Risk Rating     : 0.1 / 4 (Jev)
------------------------------------------------------------
 Scripts/Tools/scenario_regression.py  | 280 +++++++++++++++++++++-------------
 Scripts/Trainer/night_trainer.py      |  30 +++-
 Scripts/Trainer/nighttrainer/gate.py  | 134 ++++++++++++----
 Scripts/Trainer/run_night_trainer.ps1 |  13 +-
 Scripts/Trainer/tests/test_gate.py    | 191 ++++++++++++++++++++++-
 5 files changed, 499 insertions(+), 149 deletions(-)
============================================================
```

---

091 FINISH | PASS | 0918723e | done: review points 1-5 fixed (utf-8-sig BOM fix, batch smoke_parallel execution, compare-yesterday results/scenarios, stale gate_pass cleanup, behaviour metric lookup without fake proxies, penalty zero exemption, KNOWN-FAIL support) | NOT done: none | needs Claude: merge from local branch gemini/091-training-gate | gate: unittest 102/102 pass, triage PASS (0.0/4), git diff clean, pre-existing main issue in test_task_board.py (commit f43b61bd) | report: docs/port/gemini/outbox/091-training-gate.report.md
