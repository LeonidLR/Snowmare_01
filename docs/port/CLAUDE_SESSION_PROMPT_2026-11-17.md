# Claude (lead) session prompt, 2026-11-17 - paste the block into a NEW Claude Code session (working dir = the main repo)

```text
Ты Claude, ведущий (lead) по CodexTactics. Отвечай мне по-русски; код, коммиты и доки пиши по-английски.
Продолжаем с утра 2026-11-17: прошлая сессия переполнила контекст. Сначала прочитай:
- CLAUDE.md, MEMORY.md (там все правила) и docs/port/CLAUDE_SESSION_PROMPT_2026-11-17.md (этот файл: состояние ниже);
- docs/port/TASK_BOARD.md: раздел «Решения дизайнера 2026-11-17» и строку 42+072;
- чат: python Scripts/Tools/agent_chat.py read --last 15
Потом запусти слушатель чата в фоне: python Scripts/Tools/agent_chat.py wait --as claude --timeout 3600
Запускай его через Bash run_in_background и перезапускай после каждого сообщения. Никогда не перенаправляй его вывод в файл или /dev/null.
```

## Состояние на 2026-11-17 утро (main 76421a8a, всё запушено)

Сегодня в main:
- **F-1 (d6aa830b).** После боя боец через 3 с возвращается к направлению, которое приказал игрок: `ThreatFacingRules::ShouldReturnToOrderedFacing`, RetreatFacingSmoke, этапы 6–7.
- **F-2 (8eb301da).** Лидер сам занимает угол: `FormationLeaderCornerRules`, A/B-переключатель `Codex.Formation.LeaderCorner`.
- **F-3 (0c8c4837).** Клик по верху препятствия: подойти шагом и залезть. Двойной клик: подбежать и залезть. Клик по грани: укрытие. Проверка — MantleClickSmoke, случай a2.
- **Ночь.** Отчёт Codex 035 (e40e5429) и задача 037 (01efefe9).

**Идёт сейчас.**
1. **F-4а — цели генератора в игре.** Сабагент прошлой сессии работает в worktree `CodexTactics-gemcli`, ветка `claude/f4-mission-goals`. С закрытием прошлой сессии он остановится.
   - Проверь `git -C ../CodexTactics-gemcli log main..claude/f4-mission-goals` и `status`.
   - Если есть коммит и отчёт с зелёным гейтом, проверь и влей.
   - Если нет — доделай по брифу.

   **Бриф F-4а:**
   - Файл рантайма `Content/Data/LevelGen/objectives/<Map>.objectives.json` для трёх карт: L_Gen_Depot_X4, L_Gen_Depot_1_X6, L_Gen_Test_X2. Источник — `objectives.goals` из accepted-раскладок.
   - TG при «Принять» пишет такой же файл.
   - C++ world subsystem: цель засчитана, если боец в радиусе 300 см (2D, тот же этаж; цель типа room — внутри комнаты).
   - Все цели взяты → победа через СУЩЕСТВУЮЩИЙ поток (VictoryPanel / MissionSession / GameFlow).
   - Маркеры в мире и на мини-карте, строка «Цели: N/M».
   - Smoke `MissionGoalsSmoke` на L_Gen_Test_X2.
   - Без новых .uasset.

   Решения, которые агент принял сам, передай дизайнеру на подтверждение.
2. **Codex 037** (worktree `CodexTactics-agents`, ветка `codex/037-red-smokes`). Чинит красные smoke по одной.
   - Готово: MilitaryZombie 11b04070, CorpsePersist b3fafd28, OptimizedCharacter 4616ea40.
   - Дальше: Stealth → CoverHug → MantleDescent → FormationJitter → LevelGenRamp.
   - Вливать в main только после проверки, по одному smoke или пакетом.
   - Промпт новой сессии Codex: `docs/port/codex/SESSION_PROMPT_2026-11-17.md`.
3. **Антигравити (Gemini, дневной)**, worktree `CodexTactics-gemday` (создан утром, ещё не собран).
   - Очередь: 077 — жизненный цикл приказов ЧВК, по диагнозу Codex 030; 078 — стоимость трупов 0,92/1,70 мс против 0,5; 079 — короткий приказ заканчивается от толчка напарника (флейк CoverSmoke).
   - Промпт: `docs/port/gemini/SESSION_PROMPT_2026-11-17.md`.
   - Дизайнер просил сегодня перекладывать на Антигравити побольше. Держи его очередь полной и ревьюй каждый план (первый коммит).

**Названия (дизайнер 2026-11-17).** «Антигравити» = Gemini = дневной работник. «CLI» = Gemini CLI, только ночью: `Scripts/Tools/gemini_night.ps1`, worktree `CodexTactics-gemcli`, бюджет маленький, «расходуй экономно».

**Ждёт дизайнера.**
- Проверка перелезания. После неё — задача 42 + 072: клип спрыгивания `Jumping_1__UE` и неудачное приземление.
- 7 значений угроз по умолчанию в `docs/port/LEVELGEN_LAYERS_REPORT.md`.
- Решения F-4а, когда агент закончит.

**Правила, которые чаще всего забывались.**
- Дизайнер играет в main: никогда не собирать и не тестировать в main.
- Свои правки — в worktree. Для Claude это `CodexTactics-gemcli` днём, если он свободен, иначе создай `new_agent_worktree.ps1 -Name claude`.
- Пушить только с `$env:CODEX_AGENT='claude'`. Коммит и пуш в main после гейта; PR и ветки на GitHub не нужны.
- Каждое делегирование — через `python Scripts/Tools/model_router.py --classify "..."` и указанного агента ue-lN.
- Правило 10 % лимита для Codex и Gemini.
- Правка .md через PowerShell `Set-Content -Encoding utf8` добавляет BOM, поэтому пиши через `[IO.File]::WriteAllText` с UTF8 без BOM или через Edit.
- Отчёты и скриншоты — на телефон, через SendUserFile.
