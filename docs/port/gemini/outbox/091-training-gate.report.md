# 091 - Training Gate & Scenario Regression (T-10) - Report (Rework 2026-11-22)

## Handover
- **Done**:
  - **Review Point a (`gate.py`, `test_gate.py`)**: Построение строк пробного прогона (`build_probe_rows`) через канонический `ArenaAdapter.parse_doc`. Это гарантирует идентичный с тренером парсинг метрик, включая канонически вычисляемый `objective_win` (из `winner + end_reason + role`), разбор документа поведения `codex.pmc_arena_behaviour/1` через `parse_behaviour` и сохранение блока `defence`. Тест `test_gate_passes_healthy_probe` переписан под реальную C++ структуру (статы в `sides.A`, поведение в `round.behaviour`, `objective_win` вычисляется адаптером).
  - **Review Point b (`scenario_regression.py`)**: Исправлена привязка сидов и повторов. При запуске непрерывного диапазона 1..N запускается единый пакетный вызов `smoke_parallel.ps1 -Repeat N -SmokeArgs "Seed={n}"`, и каждая строка результата снабжается полями `repeat: r_repeat` и `seed: r_repeat`. При запуске произвольного списка сидов (например, `20..50`) тесты выполняются для каждого явного сида с флагом `-SmokeArgs "Seed={seed}"`, гарантируя выполнение именно запрошенных сидов.
  - **Review Point c (`gate.py`, `test_gate.py`)**: В `check_smoke_health` реализовано сохранение худшего статуса (worst-state retention) через `_is_worse_state`. Повторные запуски одного и того же смоука со смешанными результатами (например, RoofDefence r2/r6/r8 со статусом FAIL) больше не маскируются последующими PASS. Добавлен юнит-тест в `test_gate.py`.
  - **Review Point d (`test_gate.py`)**: В `test_night_trainer_check_gate_cli` добавлен аргумент `--repo-root <tempdir>` с автоматической очисткой во временном каталоге, исключающий удаление живого `Saved/Trainer/gate_pass.json` в репозитории.
  - **Review Point e (`scenario_regression.py`)**: Сценарий `encounter` (`PmcEncounterSmoke`, игнорирующий `Seed=`) исключен из дефолтного списка ночной регрессии `SCENARIO_MAP`.
  - **Inbox Hygiene**: Удален собственный блок `## Allowed paths` из `docs/port/gemini/inbox/091-training-gate.md` в соответствии с правилом Lead Review.
- **Left**: Ничего (все пункты a-e ревью от 2026-11-22 закрыты).
- **Needs Claude**: Слияние из локальной ветки `gemini/091-training-gate` в `main`.
- **Next Step**: Переход к задаче `098` (`docs/port/gemini/inbox/098-walk-smoke-digest.md`).

---

## Claim | Evidence Table

| Claim | Evidence (File:Line / Command / Output) |
|---|---|
| Review a: build_probe_rows via ArenaAdapter | `Scripts/Trainer/nighttrainer/gate.py:79-106` (`build_probe_rows`, `ArenaAdapter.parse_doc`) |
| Review a: derived objective_win & real C++ layout in tests | `Scripts/Trainer/tests/test_gate.py:77-135` (`sides.A`, `round.behaviour`, `winner="A"`, `end_reason="objective"`) |
| Review b: scenario_regression seed & repeat mapping | `Scripts/Tools/scenario_regression.py:116-258` (`is_contiguous_1_to_n`, `r_repeat`, `r_seed`) |
| Review c: Worst-state retention в check_smoke_health | `Scripts/Trainer/nighttrainer/gate.py:283-314` (`_is_worse_state`, `FAIL` > `PASS`) |
| Review c: Юнит-тест на worst-state retention | `Scripts/Trainer/tests/test_gate.py:261-274` (`test_smoke_health` с повтором `PmcRoofDefenceSmoke`) |
| Review d: --repo-root <tempdir> в CLI тесте | `Scripts/Trainer/tests/test_gate.py:276-297` (`tempfile.mkdtemp`, `--repo-root`, `shutil.rmtree`) |
| Review e: Исключение encounter из SCENARIO_MAP | `Scripts/Tools/scenario_regression.py:28-35` (`crossroads`, `alley`, `roofs`, `chokepoint`, `stalemate`) |
| Юнит-тесты test_gate.py (17 тестов) | `python -m unittest Scripts/Trainer/tests/test_gate.py` -> 17 tests OK (0.186s) |
| Все юнит-тесты тренера (102 теста) | `python -m unittest discover -s Scripts/Trainer/tests` -> 102 tests OK (15.245s) |
| Чистота git diff | `git diff --check` -> 0 errors |

---

## Протокол верификации и логи

### 1. Тесты гейта (`test_gate.py`)
```text
$ python -m unittest Scripts/Trainer/tests/test_gate.py
.................
----------------------------------------------------------------------
Ran 17 tests in 0.186s

OK
```

### 2. Полный прогон тестов подсистемы тренера
```text
$ python -m unittest discover -s Scripts/Trainer/tests
----------------------------------------------------------------------
Ran 102 tests in 15.245s

OK
```

### 3. Проверка Quick Day Mode и Night Mode в `scenario_regression.py`
```text
$ python Scripts/Tools/scenario_regression.py --scenario crossroads --seeds 1..5 --mock
# Day Loop Digest: crossroads (PmcCrossroadsSmoke)
- **Pass Rate**: 5 / 5 (100.0%)
- **Worst Seeds**: 1, 2, 3
- **Status**: ALL SEEDS GREEN

$ python Scripts/Tools/scenario_regression.py --night --seeds 1..3 --mock
# Nightly Scenario Regression Report (2026-11-22 11:41)

## 1. Summary by Scenario
| Scenario | Smoke | Seeds | Pass Rate | Failed Seeds | Worst Seeds |
|---|---|---|---|---|---|
| **crossroads** | `PmcCrossroadsSmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
| **alley** | `PmcAlleySmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
| **roofs** | `PmcRoofDefenceSmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
| **chokepoint** | `PmcArenaTacticalSmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
| **stalemate** | `PmcStalemateSmoke` | 3 | **100.0%** (3/3) | none | 1, 2, 3 |
```
