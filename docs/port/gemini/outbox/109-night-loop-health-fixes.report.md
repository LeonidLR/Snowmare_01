# Отчёт по задаче 109 — Исправления health-шага ночного цикла (по ревью Codex 082)

- **Ветка / worktree:** `gemini/109-night-loop-health` (`C:/Work/Lanes/CodexTactics-109`)
- **База:** `origin/main` (`7249663c3`)

---

## 1. Исправленные пункты Must-fix из отчёта 082

1. **Ложный зелёный health при ошибке запуска или чтения (`Scripts/Tools/project_health.py`, `Scripts/Tools/worker_night_loop.ps1`):**
   - `project_health.py` теперь открывает все JSON и лог-файлы с `encoding="utf-8-sig"`, корректно разбирая UTF-8 BOM от PowerShell 5.1 (`smoke_parallel.ps1`).
   - Удалены фейковые значения по умолчанию (`420/420`, `40 pass / 1 known-fail / 0 fail`): при отсутствии, пустом файле, `No tests matched`, ошибке парсинга, устаревании (`--max-age-minutes 180`) или ненулевом коде возврата (`--test-exit`, `--smoke-exit`) метрика помечается `available=False`, `overall_status = "CRITICAL"`, а `--strict` завершается с кодом `1`.
   - `worker_night_loop.ps1` передаёт `--test-exit $testExit --smoke-exit $smokeExit --max-age-minutes 180 --strict`; режим `-HealthCheckOnly` возвращает `exit 1` при пропуске (окно/бюджет), занятости (`TakeLock`), ошибке сборки/freeze или `CRITICAL`-статусе.

2. **Граница окна 05:30, heartbeat и контролируемое завершение (`Scripts/Tools/worker_night_loop.ps1`, `Scripts/Tools/worker_night_guard.ps1`):**
   - Добавлен резерв бюджета `-HealthReserveMinutes 45` до `$TrainerFrom` (`05:30`), а также **повторная проверка окна и остатка времени (`< 10m`) после `TakeLock`**.
   - Дочерние этапы (`all tests`, `registered smokes`) запускаются через `Invoke-HealthChild` с циклом опроса каждые `30 s`: обновляется `heartbeat.txt` (`Beat`), а при достижении `$stopAt` (`05:30`) дерево процесса прибивается через `taskkill.exe /T /F /PID`, в `digest.log` пишется `PARTIAL`, и `finally` освобождает `agent.lock`.
   - В `worker_night_guard.ps1` при протухании heartbeat добавлен `taskkill.exe /T /F` как страховка после `Kill-Tree` и очистка stale-лока `night-loop|night-trainer`.

3. **Проверка `FROZEN` и координация со слотами (`Scripts/Tools/worker_night_loop.ps1`):**
   - `Get-FreezeState` читает `Saved\Worker\FROZEN` (правило окна как в `worker_sync.ps1`) и извлекает замороженный SHA. При активном freeze `UpdateAndBuild` не делает `git checkout -f origin/main` и не пересобирает DLL (а при несовпадении `HEAD` с frozen SHA отказывает с ошибкой).
   - `Get-OtherSlotActiveCount` проверяет процессы `UnrealEditor*` других слотов на машине (`CodexTactics-slot1`, `slot2` и т.д.) и при их наличии снижает `-Jobs` для health-смоуков до `1`.

4. **Nits из 082:**
   - Health-отчёт `DEBT.md` пишется в `Saved\Health\DEBT.md` (untracked), а не в tracked `docs\port\DEBT.md`.
   - Число смоуков берётся динамически из `Scripts\test_map.json` (`smokeRegistry`), в `digest.log` различаются `HEALTH` и `HEALTH-ONLY`.

---

## 2. Проверки и репро

### Автотесты Python (`Scripts/Tools/test_project_health.py`)
`python Scripts/Tools/test_project_health.py` — **PASS (4/4 за `0.261 s`)**:
1. `test_smoke_json_with_utf8_bom_and_fail`: JSON с UTF-8 BOM и 1 `FAIL` корректно распознаётся (`available=True`, `fail=1`).
2. `test_no_tests_matched_and_engine_exit_1_never_fakes_420`: `No tests matched` + `exit 1` даёт `available=False`, `total=0`, `passed=0` (без фейкового `420/420`).
3. `test_stale_artifacts_are_rejected`: файл старше `--max-age-minutes 30` помечается `available=False`, `fresh=False`, `stale`.
4. `test_strict_cli_exits_nonzero_on_bom_fail_or_unavailable`: CLI с `--strict` возвращает `exit 1`, `status CRITICAL`, `tests UNAVAILABLE`, `1 fail`.

### Dry-run репро `worker_night_loop.ps1` (во временной директории)
1. Нормальное окно + активный `Saved\Worker\FROZEN` (`deadbeef`): `repro_ok_exit=0`, лог `health dry-run: stopAt=... freeze=True(deadbeef) otherSlots=0 smokes=41`.
2. Старт внутри окна `-TrainerFrom .. -TrainerUntil`: `repro_window_exit=1`, лог `health run: inside trainer/AB window - skipped`.
3. Остаток до окна `9.5m < reserve 45m`: `repro_budget_exit=1`, лог `health run: only 9,5m before stop-at ... < reserve 45m - skipped`.

---

## 3. Команда установки обновлённой копии на worker (`C:\Work\NightLoop`)

```powershell
ssh codex-worker "powershell -NoProfile -Command `"Copy-Item C:\Work\CodexTactics\Scripts\Tools\worker_night_loop.ps1,C:\Work\CodexTactics\Scripts\Tools\worker_night_guard.ps1 C:\Work\NightLoop\ -Force; Get-FileHash C:\Work\NightLoop\worker_night_loop.ps1,C:\Work\NightLoop\worker_night_guard.ps1 -Algorithm SHA256 | Format-Table -AutoSize`""
```

Ожидаемые SHA256 после установки:
- `worker_night_loop.ps1`: `6AAA6D95984B4ED5CF899DBD67C2762C6017C01F8AFA659BFC800C4592DFE639`
- `worker_night_guard.ps1`: `BFD71BB325E357B8E9001D5DEF4AB18A539D8E1FF1BF17BA58255E53D89CAE31`

---

## 4. Что НЕ проверено
- Живой полный прогон `RunNightlyHealthCheck` (`all tests + all smokes`) на `codex-worker` и установка в `C:\Work\NightLoop` (оставлено Lead после слияния).
- Устаревшие пути `L_Gen_Depot_1` / `L_Gen_Depot_X4` в `$jobsList` (`worker_night_loop.ps1`, nit из 082) не менялись.
