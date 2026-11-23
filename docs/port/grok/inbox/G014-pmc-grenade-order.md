# G014 - PMC grenade order (enemy AI, open-field skills)

From: enemy-AI session (Claude), 2026-11-23. Designer: "Дайте им всё оружие ... гранаты ... не бесконечное" and
"сначала научите бойцов воевать на открытом пространстве" (memory open-field-skills-first). Lead OK: Grok implements.

## Goal
A PMC fighter throws a hand grenade at enemies when it pays: a target behind cover the rifle cannot hit, or a group of
2+ enemies within the blast radius, in throw range, no mate near the impact. Finite: `pmc_operators.json` stats.grenades = 2
(already on branch ai/x6-arena 67cd040d; today the PMC grenade is only the autonomous monster-cluster throw).

## Where
- Order: add `EPmcOrder::Grenade` (Source/CodexTactics/Public/AI/PmcCommanderRules.h) + its score in PmcCommanderRules.cpp
  (ScoreOrder) with JSON weights in Content/Data/AI/pmc_commander.json (new keys, loaded like the others; defaults = no change
  when the weight is 0). Execute it in UPmcSquadSubsystem::ExecuteOrder (PmcSquadSubsystem.cpp).
- Reuse the operative's throw - do NOT write a new projectile: OperativeCharacterCombat.cpp (the grenade throw the player and the
  playtest bot use: PlaytestBotRules / PlaytestBotSubsystem show how the bot picks and throws), GrenadesCount on AOperativeCharacter.
- Target pick: like LauncherTacticsRules (Content/Data/Combat/launcher_tactics.json cluster / cover / friendly_clear logic) - a
  pure function `PmcCommanderRules::ScoreGrenadeTarget(...)` so it is unit-testable.
- Arena key `Grenades=0|1` (default 1 once it passes; parse it in PmcArenaCommand.cpp Parse, unknown keys FAIL the arena).
- Learning Agents probe (ai/la-probe): leave its 9-order action set alone; note the new order in your report.

## Tests / checks
- Unit: CodexTactics.AI.PmcCommander.Grenade* (score 0 without grenades / mate near impact / out of range; > 0 for a covered
  target or a 2+ cluster).
- Build + `Scripts/test.ps1 -Filter CodexTactics.AI` on the worker (Scripts/Tools/worker_sync.ps1 -Build, -Jobs 4 by day).
- Smokes on the worker: the 19 Pmc smokes (list in Scripts/test_map.json "pmc-squad-ai") - no new FAIL.
- Paired A/B on X6 (L_Gen_Depot_1_X6 + L_PmcArenaX6.json, seeds 1060-1359, same machine, Grenades=0 vs 1) against the X6 base
  (det/x6base-r1, measured 2026-11-24 08:00). Accept only if wins >= base AND captures <= base (McNemar not worse).

## Report (docs/port/grok/outbox/G014-...report.md)
Wins / captures / kills vs the base + the open-field skill card: grenades thrown per round, kills by grenade, enemies flushed
out of cover (target left cover within 3 s of the throw), team-kills (must be 0), time in cover, retreats ending in cover.
