# Отчёт по задаче 108 / 108b — Маппинг смоуков merge_gate, daily_smokes и LFS в worker_sync

- **Ветка / worktree:** `gemini/108-gate-smokes` (`C:/Work/Lanes/CodexTactics-108`)
- **База:** `origin/main` (`8ff14f3ed`)

---

## 1. Причина пустого списка `Worker smokes: none mapped` в `103` и `G014`

- В `Scripts/test_map.json` после архивации старых карт (`L_MovementTest`, `L_Gen_Depot_1` и др.) у **113 правил** с путями `Source/` и у `fallback.smokes` список `"smokes"` оказался пустым (`[]`).
- `merge_gate.ps1` передаёт в `Resolve-Impact` только файлы ветки (`$mergePlan.Paths`), а не грязные файлы рабочего каталога — поэтому ветки `codex/103-low-cover-click` (`OperativeCharacterVault.cpp`, `DepotVaultSmokeCommand.cpp`) и `grok/G014-pmc-grenade` (`PmcSquadSubsystem.cpp`, `PmcCommanderRules.*`) попадали в правила с `"smokes": []` и запускали `0` смоуков на воркере.

---

## 2. Что изменено

1. **`Scripts/test_map.json` (минимальный построчный diff: `114 insertions(+), 114 deletions(-)`, форматирование и однострочные массивы полностью сохранены):**
   - Обновлены `fallback.smokes` и 113 правил `Source/` с пустым `"smokes": []` — по **2 релевантных активных смоука** на правило (все из `smokeRegistry`, на 5 сохранённых картах):

   | Группа правил | Назначенные смоуки | Сохранённая карта (`smokeOptions`) | Почему |
   |---|---|---|---|
   | `fallback` | `TurnArenaLakeSmoke`, `LowCoverMoveLakeSmoke` | `L_Demo01_A_FrozenLake` | Базовый бой и движение в укрытиях для неразмеченных `Source/` файлов |
   | PMC AI (`pmc-squad-ai-2026-11-03`, `pmc-traversal-2026-11-22`, `tactical-pmc-2026-11-17`, `rpg-ai-pmc`, `ramp-fight-2026-11-18`, `tactical-map-2026-11-17`, `levelgen-bot-runs-2026-11-02`) | `TurnArenaLakeSmoke`, `TutorialAmbushLakeSmoke` | `L_Demo01_A_FrozenLake` | Пошаговый/тактический бой и засада PMC на активной карте озера |
   | Оружие/бой/гранаты (`grenades`, `projectile-explosion`, `rpg7-launcher-pack`, `weapons-arsenal`, `quick-fire`, `real-time-fire`, `suppression-2026-10-26`, `fire-gate-2026-11-16`, `turn-based`, `waves-victory`) | `TurnArenaLakeSmoke`, `LakePauseOrdersSmoke` | `L_Demo01_A_FrozenLake` | Боевой цикл, приказы на тактической паузе и стрельба на карте озера |
   | Укрытия/ваулт/лестницы (`cover`, `cover-wrap`, `group-cover`, `corner-pick-b17-1-2026-11-17`, `corner-peek-standoff-2026-11-09`, `run-corner-peek-2026-11-07`, `squad-corner-peek-anim-2026-11-01`, `peek-next-wall-camera-2026-11-01`, `designer-2026-10-22-vault-back`, `mantling-slice1-2026-11-15`, `ladders-2026-10-22`) | `LowCoverMoveLakeSmoke`, `CoverSwitchLakeSmoke` | `L_Demo01_A_FrozenLake` | Движение и переключение между низкими/высокими укрытиями |
   | Формация и движение отряда (`formation-*`, `combat-formation-2026-10-26`, `wall-leadership-2026-11-01`, `operative-movement-squad`, `fast-regroup-2026-11-10`, `leader-switch-keeps-move-2026-11-08`, `solo-start-2026-11-09`, `autonomy-postures`) | `FormationWallHugSmoke`, `FormationStairSmoke` | `L_Demo01_A_FrozenLake`, `KitPilot/L_Gen_StoryShowcase_Kit` | Движение строя у стен и по лестницам |
   | Двери/рампы/препятствия (`openable-doors-r1-2026-11-22`, `door-breach-r17-5-2026-11-17`, `ramp-file-2026-11-02`, `level-obstacles`) | `InteriorExitSmoke`, `LevelGenBuildingsSmoke` | `L_Demo01_A_FrozenLake`, `L_Gen_Depot_1_X6` | Вход/выход из помещений и проходимость зданий |
   | Окружение/холод/погода (`environment-*`, `interactive-snow`, `landscape-material`, `blizzard-walk`, `warm-idle`, `tactical-heat`, `exposed-zones`) | `ColdIndoorsSmoke`, `WeatherLakeSmoke` | `L_Gen_Depot_1_X6`, `L_Demo01_A_FrozenLake` | Холод в помещениях и погодная система |
   | Анимации/состояния (`anim-operative`, `anim-enemy`, `prone-crawl-2026-11-09`, `injured-locomotion-2026-11-10`, `wound-pace-2026-11-11`, `look-hold-solo-2026-11-15`, `designer-2026-10-22-heat-fear`, `user-2026-10-22-retreat-facing`, `optimized-character`, `death-cinematic`) | `CoverIndoorSmoke`, `LowCoverMoveLakeSmoke` | `KitPilot/L_Gen_StoryShowcase_Kit`, `L_Demo01_A_FrozenLake` | Анимации укрытий и передвижения |
   | Фреймворк/контроллер/инвентарь (`controller-*`, `game-framework`, `event-bus`, `balance-data`, `transfer-inventory`, `loot`) | `SaveCoverageLakeSmoke`, `LakePauseOrdersSmoke` | `L_Demo01_A_FrozenLake` | Состояние игры, сохранения, пауза и приказы |
   | Прочие `Source/` правила (враги, AI, восприятие, мины, урон, безымянное правило и др.) | `TutorialAmbushLakeSmoke`, `TurnArenaLakeSmoke` (безымянное правило: `LakeQuestSmoke`, `DropLinkReachSmoke`) | `L_Demo01_A_FrozenLake` | Бой/восприятие/квесты на активной карте озера |

2. **`Scripts/Tools/merge_gate.ps1`:**
   - Добавлен `Assert-GateWorkerSmokePlan` (и флаг `-AllowNoWorkerSmokes`), падающий при изменении `Source/` (кроме `Source/CodexTacticsTests/`) с 0 смоуков или со смоуком вне `smokeRegistry`.
3. **`Scripts/Tools/worker_daily_smokes.json`:**
   - Обновлён на **23 активных смоука** из `smokeRegistry` по всем **5 сохранённым картам** (`L_Demo01_A_FrozenLake`, `KitPilot/L_Gen_StoryShowcase_Kit`, `Generated/L_Gen_Depot_1_X6`, `L_VehicleTest`, `L_MainMenu`).
4. **`Scripts/Tools/worker_sync.ps1`:**
   - При `-ShipHead` / `-ShipRef` для незапушенного коммита сначала выполняется `git lfs push origin <Ref>`; на воркере при ошибке smudge/LFS выводится `LFS-FAILED` (вместо `RESET-FAILED`).
5. **`Scripts/Tools/test_merge_gate_smoke_selection.ps1`:**
   - Тесты на репро `103`, `G014`, guard-ы `merge_gate`, `worker_daily_smokes.json` и AST-проверку.

---

## 3. Проверки

- `git diff --stat Scripts/test_map.json`: `1 file changed, 114 insertions(+), 114 deletions(-)`.
- `Scripts/test.ps1 -CheckMap`: **PASS**.
- `Scripts/Tools/test_merge_gate_smoke_selection.ps1`: **PASS**:
  - репро `codex/103`: 4 смоука (`FormationWallHugSmoke, FormationStairSmoke, TurnArenaLakeSmoke, LowCoverMoveLakeSmoke`);
  - репро `grok/G014`: 2 смоука (`TutorialAmbushLakeSmoke, TurnArenaLakeSmoke`);
  - guard на `Source/` с 0 смоуков и на архивный смоук (`DepotVaultSmoke`): **PASS**;
  - `worker_daily_smokes.json`: **PASS** (23 смоука на 5 сохранённых картах);
  - PowerShell AST parse (`merge_gate.ps1`, `worker_sync.ps1`): **PASS**.

---

## 4. Что НЕ проверено
- Живой прогон `merge_gate.ps1` (оставлено Lead).
- Живой `worker_sync.ps1 -ShipHead` с новым LFS-файлом на воркере.
