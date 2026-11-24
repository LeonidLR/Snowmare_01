# Отчёт по задаче 108 — Маппинг смоуков merge_gate, daily_smokes и LFS в worker_sync

- **Ветка / worktree:** `gemini/108-gate-smokes` (`C:/Work/Lanes/CodexTactics-108`)
- **База:** `origin/main` (`8ff14f3ed`)

---

## 1. Причина пустого списка `Worker smokes: none mapped` в `103` и `G014`

- В `Scripts/test_map.json` после архивации старых карт (`L_MovementTest`, `L_Gen_Depot_1` и др.) у **114 правил** с путями `Source/` и у `fallback.smokes` список `"smokes"` оказался пустым (`[]`).
- `merge_gate.ps1` передаёт в `Resolve-Impact` только файлы ветки (`$mergePlan.Paths`), а не грязные файлы рабочего каталога — поэтому ветки `codex/103-low-cover-click` (`OperativeCharacterVault.cpp`, `DepotVaultSmokeCommand.cpp`) и `grok/G014-pmc-grenade` (`PmcSquadSubsystem.cpp`, `PmcCommanderRules.*`) попадали в правила с `"smokes": []` и запускали `0` смоуков на воркере (тогда как локальный `test.ps1 -Smart` цеплял смоуки из 5600+ грязных файлов главного чекаута).

---

## 2. Что изменено

1. **`Scripts/test_map.json`:**
   - Восстановлены активные смоуки из `smokeRegistry` (5 сохранённых карт) для всех **114** правил `Source/` с пустым `"smokes": []` и для `fallback.smokes` (`["TurnArenaLakeSmoke", "LowCoverMoveLakeSmoke"]`).
2. **`Scripts/Tools/merge_gate.ps1`:**
   - Добавлен `Assert-GateWorkerSmokePlan` (и флаг `-AllowNoWorkerSmokes` для явного исключения):
     - бросает ошибку, если затронут `Source/` (кроме `Source/CodexTacticsTests/`), а список смоуков пуст (`0` смоуков);
     - бросает ошибку, если выбранный смоук отсутствует в `smokeRegistry` (устаревшая/архивная карта).
3. **`Scripts/Tools/worker_daily_smokes.json`:**
   - Обновлён с 18 архивных смоуков (`L_MovementTest`) на **23 активных смоука** из `smokeRegistry` по всем **5 сохранённым картам** (`L_Demo01_A_FrozenLake`, `L_Gen_StoryShowcase_Kit`, `L_Gen_Depot_1_X6`, `L_VehicleTest`, `L_MainMenu`).
4. **`Scripts/Tools/worker_sync.ps1` (LFS addendum):**
   - При `-ShipHead` / `-ShipRef` для незапушенного коммита перед сбросом на воркере выполняется `git lfs push origin <Ref>` (пушит только LFS-объекты; ветки остаются незапушенными).
   - На воркере при ошибке `git reset --hard` из-за `smudge filter lfs` или при ошибке `git lfs pull` выводится `LFS-FAILED` (вместо `RESET-FAILED`).
5. **`Scripts/Tools/test_merge_gate_smoke_selection.ps1`:**
   - Автотесты на репро `103` и `G014`, guard на `Source/` с 0 смоуков, guard на архивный смоук, валидацию `worker_daily_smokes.json` и AST-проверку PowerShell.

---

## 3. Проверки

- `Scripts/test.ps1 -CheckMap`: **PASS** (`Impact map OK: every smoke command is registered and mapped.`).
- `Scripts/Tools/test_merge_gate_smoke_selection.ps1`: **PASS**:
  - репро `codex/103`: 5 смоуков (`FormationWallHugSmoke, FormationStairSmoke, FormationWindowSmoke, TurnArenaLakeSmoke, LowCoverMoveLakeSmoke`);
  - репро `grok/G014`: 3 смоука (`TutorialAmbushLakeSmoke, TurnArenaLakeSmoke, TurnArenaYardSmoke`);
  - guard на `Source/` с 0 смоуков: **PASS**;
  - guard на архивный смоук (`DepotVaultSmoke`): **PASS**;
  - `worker_daily_smokes.json`: **PASS** (23 смоука на 5 сохранённых картах);
  - PowerShell AST parse (`merge_gate.ps1`, `worker_sync.ps1`): **PASS**.

---

## 4. Что НЕ проверено
- Живой прогон `merge_gate.ps1` (оставлено Lead, т.к. `slot1` занят merge gate Lead).
- Живой `worker_sync.ps1 -ShipHead` с новым LFS-файлом на воркере (`slot2` не трогали после правок `worker_sync.ps1`).
